#include "pattern_window.h"
#include "arming_gate.h"
#include "hover_policy.h"
#include "pattern_layout.h"
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <climits>
#include <strsafe.h>
#include <objbase.h>
#include <oleauto.h>
#include <uiautomation.h>
#include <memory>
#include <atomic>
#include <string>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uiautomationcore.lib")

namespace patlock {
namespace {

// ── Timer IDs (named constants to prevent collision) ─────────────────────────
enum TimerId : UINT_PTR {
    kTimerInactivity = 1,    // 2-minute session timeout
    kTimerFinish     = 2,    // hover-stroke finish deadline
    kTimerShort      = 3,    // short-stroke minimum wait
    kTimerSuccess    = 4,    // close after success flash (120ms)
    kTimerTooShort   = 5,    // clear 'too short' message (1000ms)
    kTimerRetry      = 6,    // wrong-pattern / lockout clear
    kTimerStart      = 7,    // hover-to-start arm delay
    kTimerPoll       = 8     // cursor position poll (30ms)
};

// ── Timing Constants ─────────────────────────────────────────────────────────
constexpr UINT kSuccessFlashMs = 120;
constexpr UINT kWrongFlashMs = 800;
constexpr UINT kTooShortFlashMs = 1000;
constexpr UINT kInactivityTimeoutMs = 120000;
constexpr UINT kCursorPollMs = 30;

// ── Color Key for Transparent Window (non-high-contrast mode) ────────────────
constexpr COLORREF TransparentKey = RGB(1, 1, 1);

/// Returns the Windows system accent color for Windows 11 Fluent Design.
/// Falls back to Windows 11 default blue if retrieval fails.
Gdiplus::Color GetSystemAccentColor() noexcept {
    DWORD color = 0;
    BOOL opaque = FALSE;
    // DwmGetColorizationColor requires dwmapi.dll
    HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
    if (dwmapi) {
        typedef HRESULT (WINAPI *DwmGetColorizationColorFunc)(DWORD*, BOOL*);
        auto func = reinterpret_cast<DwmGetColorizationColorFunc>(GetProcAddress(dwmapi, "DwmGetColorizationColor"));
        if (func && SUCCEEDED(func(&color, &opaque))) {
            FreeLibrary(dwmapi);
            // DwmGetColorizationColor returns AARRGGBB, unlike COLORREF's BGR
            // byte order. Do not use GetRValue/GetGValue/GetBValue here.
            return Gdiplus::Color(255,
                static_cast<BYTE>((color >> 16) & 0xff),
                static_cast<BYTE>((color >> 8) & 0xff),
                static_cast<BYTE>(color & 0xff));
        }
        FreeLibrary(dwmapi);
    }
    // Fallback: Windows 11 default blue
    return Gdiplus::Color(255, 0, 120, 212);
}

// Layout constants
constexpr int WindowW     = PatternLayout::Width;
constexpr int WindowH     = PatternLayout::Height;
constexpr int GridSpacing = PatternLayout::Spacing;
constexpr int GridTopY    = PatternLayout::Top;
constexpr int HitRadius   = PatternLayout::HitRadius;

// ── State ────────────────────────────────────────────────────────────────────

enum class VisualState {
    Drawing,
    Checking,
    Success,
    ErrorWrong,
    ErrorLockout,
    ErrorTooShort
};

struct State {
    Pattern pattern;
    bool dragging = false;
    bool hoverStroke = false;
    int pendingStartDot = -1;
    ArmingGate gate;
    ULONGLONG startDue = 0;
    ULONGLONG finishDue = 0;
    ULONGLONG shortDue = 0;
    bool hide = false;
    VisualState visual = VisualState::Drawing;
    POINT previous{};
    POINT finishAnchor{};
    POINT observedCursor{};
    PatternVerifier verifier = nullptr;
    RetryDelay retryDelay = nullptr;
    DWORD retrySeconds = 0;
    bool passwordRequired = false;
    int hoverDot = -1;
    HWND fallback = nullptr;
    HWND status = nullptr;
    HFONT font = nullptr;
    HBRUSH background = nullptr;
    bool highContrast = false;
    double scale = 1.0;
    RECT anchor{};          // Native username bounds in screen pixels (UI Automation).
    bool hasAnchor = false;
    PatternDone onDone = nullptr;
    ULONG_PTR gdiplusToken = 0;
    bool finished = false;
    INT_PTR resultCode = IDCANCEL;
};

// The window is modeless: it must never run its own message loop inside LogonUI,
// or LogonUI cannot process clicks on "Sign-in options" / the user list. Every
// close request is posted here and handled on the window's own thread.
constexpr UINT kMsgEnd = WM_APP + 1;
void EndPattern(HWND window, INT_PTR code) noexcept {
    if (window) PostMessageW(window, kMsgEnd, static_cast<WPARAM>(code), 0);
}
// The currently open pattern dialog, so the provider can close it when LogonUI
// deselects the tile (user clicked "Sign-in options" or another user).
std::atomic<HWND> g_activeWindow{nullptr};

// ── Native username anchor (UI Automation) ───────────────────────────────────
// LogonUI draws the avatar/username/"Sign-in options" itself and exposes no
// layout API. To sit exactly where the PIN box sits we ask UI Automation for the
// username element. The query runs on a worker thread while this thread pumps
// messages (UIA may need this thread to answer), with a hard timeout, and any
// failure falls back to the old centered estimate. It never blocks LogonUI.
struct AnchorQuery {
    HWND root = nullptr;
    std::wstring name;
    RECT rect{};
    bool found = false;
};

DWORD WINAPI AnchorThread(LPVOID param) noexcept {
    std::unique_ptr<std::shared_ptr<AnchorQuery>> holder(static_cast<std::shared_ptr<AnchorQuery>*>(param));
    AnchorQuery& query = **holder;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init)) return 0;
    IUIAutomation* uia = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))) && uia) {
        IUIAutomationElement* root = nullptr;
        if (SUCCEEDED(uia->ElementFromHandle(query.root, &root)) && root) {
            VARIANT value;
            VariantInit(&value);
            value.vt = VT_BSTR;
            value.bstrVal = SysAllocString(query.name.c_str());
            IUIAutomationCondition* condition = nullptr;
            if (value.bstrVal && SUCCEEDED(uia->CreatePropertyCondition(UIA_NamePropertyId, value, &condition)) && condition) {
                // Several elements can share the username as their Name (the whole
                // tile container, the avatar group, the text). The text is the
                // smallest one, so take that and ignore anything tall.
                IUIAutomationElementArray* items = nullptr;
                if (SUCCEEDED(root->FindAll(TreeScope_Descendants, condition, &items)) && items) {
                    int count = 0;
                    items->get_Length(&count);
                    LONG bestArea = LONG_MAX;
                    for (int i = 0; i < count; ++i) {
                        IUIAutomationElement* element = nullptr;
                        if (FAILED(items->GetElement(i, &element)) || !element) continue;
                        BOOL offscreen = FALSE;
                        RECT r{};
                        element->get_CurrentIsOffscreen(&offscreen);
                        if (!offscreen && SUCCEEDED(element->get_CurrentBoundingRectangle(&r)) &&
                            r.right > r.left && r.bottom > r.top && (r.bottom - r.top) <= 140) {
                            const LONG area = (r.right - r.left) * (r.bottom - r.top);
                            if (area < bestArea) { bestArea = area; query.rect = r; query.found = true; }
                        }
                        element->Release();
                    }
                    items->Release();
                }
                condition->Release();
            }
            VariantClear(&value);
            root->Release();
        }
        uia->Release();
    }
    CoUninitialize();
    return 0;
}

bool FindUsernameAnchor(HWND owner, const wchar_t* name, RECT* out) noexcept {
    if (!owner || !name || !*name || !out) return false;
    try {
        auto query = std::make_shared<AnchorQuery>();
        query->root = GetAncestor(owner, GA_ROOT);
        if (!query->root) query->root = owner;
        query->name = name;
        auto* holder = new std::shared_ptr<AnchorQuery>(query);
        HANDLE thread = CreateThread(nullptr, 0, AnchorThread, holder, 0, nullptr);
        if (!thread) { delete holder; return false; }
        const ULONGLONG deadline = GetTickCount64() + 500;
        for (;;) {
            const ULONGLONG now = GetTickCount64();
            if (now >= deadline) break;
            const DWORD wait = MsgWaitForMultipleObjects(1, &thread, FALSE, static_cast<DWORD>(deadline - now), QS_ALLINPUT);
            if (wait == WAIT_OBJECT_0) break;          // worker finished
            if (wait != WAIT_OBJECT_0 + 1) break;      // timeout or failure
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        const bool finished = WaitForSingleObject(thread, 0) == WAIT_OBJECT_0;
        CloseHandle(thread); // On timeout the worker finishes alone; it owns its own shared_ptr.
        wchar_t log[160];
        StringCchPrintfW(log, ARRAYSIZE(log), L"patlock: username anchor finished=%d found=%d rect=%ld,%ld,%ld,%ld\n",
            finished ? 1 : 0, query->found ? 1 : 0, query->rect.left, query->rect.top, query->rect.right, query->rect.bottom);
        OutputDebugStringW(log);
        if (!finished || !query->found) return false;
        *out = query->rect;
        return true;
    } catch (...) {
        return false;
    }
}

Gdiplus::Color Palette(const State& state, Gdiplus::Color normal, int systemColor) noexcept {
    if (!state.highContrast) return normal;
    const COLORREF color = GetSysColor(systemColor);
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

bool Configure(HWND window, State& state, UINT dpi, const RECT* suggested) noexcept {
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    const HMONITOR target = suggested ? MonitorFromRect(suggested, MONITOR_DEFAULTTONEAREST)
        : MonitorFromWindow(GetWindow(window, GW_OWNER), MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(target, &monitor) || dpi == 0) return false;
    const RECT area = monitor.rcWork;
    const int width = area.right - area.left;
    const int height = area.bottom - area.top;
    state.scale = (std::min)(static_cast<double>(dpi) / 96.0,
        (std::min)(static_cast<double>(width - 16) / WindowW, static_cast<double>(height - 16) / WindowH));
    if (state.scale < 0.5) return false; // Decline when controls cannot remain usable.
    const int w = static_cast<int>(WindowW * state.scale);
    const int h = static_cast<int>(WindowH * state.scale);
    int preferredX = area.left + (width - w) / 2;
    int preferredY = area.top + height / 2 + static_cast<int>(PatternLayout::FallbackCenterOffset * state.scale);
    if (state.hasAnchor) {
        // Center under the native username and start just below it, exactly
        // where LogonUI places its own PIN box.
        preferredX = (state.anchor.left + state.anchor.right) / 2 - w / 2;
        preferredY = state.anchor.bottom + static_cast<int>(PatternLayout::AnchorGap * state.scale);
    }
    if (suggested) { preferredX = suggested->left; preferredY = suggested->top; }
    const int x = (std::max)(static_cast<int>(area.left), (std::min)(preferredX, static_cast<int>(area.right) - w));
    const int y = (std::max)(static_cast<int>(area.top), (std::min)(preferredY, static_cast<int>(area.bottom) - h));
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)) return false;
    state.highContrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    HBRUSH brush = CreateSolidBrush(state.highContrast ? GetSysColor(COLOR_WINDOW) : TransparentKey);
    HFONT font = CreateFontW(-static_cast<int>(16 * state.scale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (!brush || !font) { if (brush) DeleteObject(brush); if (font) DeleteObject(font); return false; }
    SendMessageW(state.fallback, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(state.status, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (state.font) DeleteObject(state.font);
    if (state.background) DeleteObject(state.background);
    state.font = font;
    state.background = brush;
    auto scaled = [&state](int value) { return static_cast<int>(value * state.scale); };
    if (!SetWindowPos(window, HWND_TOP, x, y, w, h, SWP_NOACTIVATE) ||
        !MoveWindow(state.status, scaled(20), scaled(PatternLayout::StatusTop), scaled(WindowW - 40), scaled(PatternLayout::StatusHeight), TRUE) ||
        !MoveWindow(state.fallback, 0, 0, 0, 0, FALSE)) return false; // Hidden: native "Sign-in options" replaces it.
    // Only the margin is color-key transparent. An opaque panel keeps text
    // readable and prevents LogonUI account text showing through the controls.
    if (!SetLayeredWindowAttributes(window, TransparentKey, state.highContrast ? 255 : 0,
        state.highContrast ? LWA_ALPHA : LWA_COLORKEY)) return false;
    InvalidateRect(window, nullptr, TRUE);
    return true;
}

struct Layout {
    int x;
    int y;
    int spacing;
    int hitRadius;
};

Layout Grid(HWND window) noexcept {
    RECT r{};
    GetClientRect(window, &r);
    const int cw = static_cast<int>(r.right);
    const double scale = static_cast<double>(cw) / WindowW;
    const int spacing = static_cast<int>(GridSpacing * scale);
    return {cw / 2 - spacing, static_cast<int>(GridTopY * scale), spacing, static_cast<int>(HitRadius * scale)};
}

POINT Dot(Layout grid, int dot) noexcept {
    return {grid.x + (dot % 3) * grid.spacing, grid.y + (dot / 3) * grid.spacing};
}

int DotAt(HWND window, POINT pt, int threshold) noexcept {
    const Layout grid = Grid(window);
    for (BYTE i = 0; i < 9; ++i) {
        const POINT p = Dot(grid, i);
        const int dx = pt.x - p.x;
        const int dy = pt.y - p.y;
        if (dx * dx + dy * dy <= threshold * threshold) return i;
    }
    return -1;
}

void UpdateStatus(HWND window, const State& state) noexcept {
    // LogonUI owns the account name. Keep Patlock's ready state visually quiet;
    // show status only when recovery guidance is needed.
    wchar_t text[96] = L"";
    if (state.visual == VisualState::Success) StringCchCopyW(text, ARRAYSIZE(text), L"Pattern accepted");
    else if (state.visual == VisualState::Checking) StringCchCopyW(text, ARRAYSIZE(text), L"Checking pattern...");
    else if (state.visual == VisualState::ErrorTooShort) StringCchPrintfW(text, ARRAYSIZE(text), L"Connect at least %zu dots", MinimumDots);
    else if (state.visual == VisualState::ErrorWrong) StringCchCopyW(text, ARRAYSIZE(text), L"Incorrect pattern");
    else if (state.visual == VisualState::ErrorLockout) {
        if (state.passwordRequired) StringCchCopyW(text, ARRAYSIZE(text), L"Pattern unavailable. Use Password.");
        else StringCchPrintfW(text, ARRAYSIZE(text), L"Try again in %lu seconds", state.retrySeconds);
    }
    wchar_t old[96]{};
    GetWindowTextW(state.status, old, ARRAYSIZE(old));
    if (lstrcmpW(old, text) != 0) {
        SetWindowTextW(state.status, text);
        NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, state.status, OBJID_CLIENT, 0);
    }
    const bool show = state.visual == VisualState::Success || state.visual == VisualState::Checking ||
        state.visual == VisualState::ErrorTooShort || state.visual == VisualState::ErrorWrong ||
        state.visual == VisualState::ErrorLockout;
    ShowWindow(state.status, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    InvalidateRect(window, nullptr, FALSE);
}

bool RefreshRetry(HWND window, State& state) noexcept {
    state.passwordRequired = true;
    state.retrySeconds = 0;
    if (state.retryDelay) {
        HRESULT hr = E_FAIL;
        try { hr = state.retryDelay(&state.retrySeconds); }
        catch (...) { state.pattern.Clear(); EndPattern(window, -1); return false; }
        if (hr == S_OK) state.passwordRequired = false;
        else if (hr != E_ACCESSDENIED) { state.pattern.Clear(); EndPattern(window, -1); return false; }
    }
    return true;
}

void ResetStroke(HWND window, State& state) noexcept {
    KillTimer(window, kTimerFinish);
    KillTimer(window, kTimerShort);
    KillTimer(window, kTimerTooShort);
    KillTimer(window, kTimerRetry);
    KillTimer(window, kTimerStart);
    state.pattern.Clear();
    state.dragging = false;
    state.hoverStroke = false;
    state.pendingStartDot = -1;
    state.startDue = state.finishDue = state.shortDue = 0;
    state.visual = VisualState::Drawing;
    state.hoverDot = -1;
    if (GetCapture() == window) ReleaseCapture();
    POINT cursor{};
    if (!GetCursorPos(&cursor) || !ScreenToClient(window, &cursor)) { EndPattern(window, -1); return; }
    const Layout grid = Grid(window);
    state.observedCursor = cursor;
    state.gate.Disarm(static_cast<int>(cursor.x / state.scale), static_cast<int>(cursor.y / state.scale),
        DotAt(window, cursor, grid.hitRadius) >= 0);
    UpdateStatus(window, state);
}

void Track(HWND window, State& state, POINT to) noexcept {
    const Layout grid = Grid(window);
    struct Hit { double t; BYTE dot; } hits[9]{};
    size_t count = 0;
    const double dx = static_cast<double>(to.x) - state.previous.x;
    const double dy = static_cast<double>(to.y) - state.previous.y;
    const double length = dx * dx + dy * dy;
    const double hitR = static_cast<double>(grid.hitRadius);

    for (BYTE i = 0; i < 9; ++i) {
        const POINT p = Dot(grid, i);
        double t = length > 0 ? ((p.x - state.previous.x) * dx + (p.y - state.previous.y) * dy) / length : 0;
        t = (std::max)(0.0, (std::min)(1.0, t));
        const double ex = state.previous.x + t * dx - p.x;
        const double ey = state.previous.y + t * dy - p.y;
        if (ex * ex + ey * ey <= hitR * hitR) hits[count++] = {t, i};
    }
    std::sort(hits, hits + count, [](const Hit& a, const Hit& b) { return a.t < b.t; });
    for (size_t i = 0; i < count; ++i) state.pattern.Add(hits[i].dot);
    state.previous = to;
    UpdateStatus(window, state);
}

void FinishPattern(HWND window, State& state) noexcept {
    if (state.visual != VisualState::Drawing) return;
    KillTimer(window, kTimerFinish);
    KillTimer(window, kTimerShort);
    KillTimer(window, kTimerStart);
    state.startDue = state.finishDue = state.shortDue = 0;

    if (!state.pattern.Valid()) {
        if (state.pattern.count > 0) {
            state.pattern.Clear();
            state.dragging = false;
            state.visual = VisualState::ErrorTooShort;
            if (GetCapture() == window) ReleaseCapture();
            UpdateStatus(window, state);
            if (!SetTimer(window, kTimerTooShort, kTooShortFlashMs, nullptr)) EndPattern(window, -1);
        } else {
            ResetStroke(window, state);
        }
        return;
    }

    state.dragging = false;
    if (GetCapture() == window) ReleaseCapture();

    if (state.verifier) {
        state.visual = VisualState::Checking;
        UpdateStatus(window, state);
        UpdateWindow(state.status);
        UpdateWindow(window);
        VerifyResult result = VerifyResult::Error;
        try { result = state.verifier(state.pattern); }
        catch (...) { state.pattern.Clear(); EndPattern(window, -1); return; }
        if (result == VerifyResult::Error) { state.pattern.Clear(); EndPattern(window, -1); return; }
        if (result == VerifyResult::Success) {
            state.visual = VisualState::Success;
            UpdateStatus(window, state);
            if (!SetTimer(window, kTimerSuccess, kSuccessFlashMs, nullptr)) { state.pattern.Clear(); EndPattern(window, -1); }
        } else if (result == VerifyResult::Lockout) {
            state.visual = VisualState::ErrorLockout;
            state.pattern.Clear();
            if (!RefreshRetry(window, state)) return;
            UpdateStatus(window, state);
            if (!state.passwordRequired && !SetTimer(window, kTimerRetry, 1000, nullptr)) EndPattern(window, -1);
        } else {
            // Wrong pattern: flash red for 800ms, then reset stroke for instant retry
            state.visual = VisualState::ErrorWrong;
            UpdateStatus(window, state);
            if (!SetTimer(window, kTimerRetry, kWrongFlashMs, nullptr)) { state.pattern.Clear(); EndPattern(window, -1); }
        }
    } else {
        state.visual = VisualState::Success;
        UpdateStatus(window, state);
        EndPattern(window, IDOK);
    }
}

void AddRoundedRect(Gdiplus::GraphicsPath& path, Gdiplus::RectF rect, float radius) noexcept {
    float d = 2.0f * radius;
    path.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

// ── Dialog Procedure ─────────────────────────────────────────────────────────

INT_PTR CALLBACK Dialog(HWND window, UINT message, WPARAM wp, LPARAM lp) noexcept {
    State* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<State*>(lp);
        SetWindowLongPtrW(window, DWLP_USER, lp);
        SetWindowTextW(window, L"Patlock");
        g_activeWindow.store(window);
        state->finished = false;

        // Transparent layered window for floating rounded corners
        LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        SetWindowLongPtrW(window, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
        if (!SetLayeredWindowAttributes(window, TransparentKey, 0, LWA_COLORKEY)) { EndPattern(window, -1); return TRUE; }

        // Zero-size, invisible, not a tab stop: the native "Sign-in options" row
        // below the grid is the fallback now. Esc still closes the dialog.
        state->fallback = CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
        state->status = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(1001), nullptr, nullptr);
        if (!state->fallback || !state->status || !Configure(window, *state, GetDpiForWindow(window), nullptr)) {
            EndPattern(window, -1); return TRUE;
        }
        SetFocus(window);
        ResetStroke(window, *state);
        if (state->retryDelay) {
            if (!RefreshRetry(window, *state)) return FALSE;
            if (state->passwordRequired || state->retrySeconds) {
                state->visual = VisualState::ErrorLockout;
                UpdateStatus(window, *state);
                if (!state->passwordRequired && !SetTimer(window, kTimerRetry, 1000, nullptr)) { EndPattern(window, -1); return FALSE; }
            }
        }

        if (!SetTimer(window, kTimerInactivity, kInactivityTimeoutMs, nullptr)) EndPattern(window, -1);
        if (!SetTimer(window, kTimerPoll, kCursorPollMs, nullptr)) EndPattern(window, -1);
        return FALSE; // Focus was explicitly assigned to the dialog itself.
    }
    if (message == WM_NCDESTROY) {
        HWND expected = window;
        g_activeWindow.compare_exchange_strong(expected, nullptr);
        if (state) {
            SetWindowLongPtrW(window, DWLP_USER, 0);
            const HRESULT hr = (state->resultCode == IDOK && state->pattern.Valid()) ? S_OK
                : (state->resultCode == IDCANCEL ? S_FALSE : E_FAIL);
            PatternDone done = std::move(state->onDone);
            Pattern pattern = state->pattern;
            const ULONG_PTR token = state->gdiplusToken;
            state->pattern.Clear();
            delete state;
            Gdiplus::GdiplusShutdown(token);
            if (done) done(hr, pattern);
            pattern.Clear();
        }
        return FALSE;
    }
    if (!state) return FALSE;
    if (message == kMsgEnd) {
        if (!state->finished) {
            state->finished = true;
            state->resultCode = static_cast<INT_PTR>(wp);
            if (GetCapture() == window) ReleaseCapture();
            DestroyWindow(window);
        }
        return TRUE;
    }

    if (message == WM_LBUTTONDOWN || message == WM_MOUSEMOVE || message == WM_LBUTTONUP) {
        if (!SetTimer(window, kTimerInactivity, kInactivityTimeoutMs, nullptr)) { state->pattern.Clear(); EndPattern(window, -1); return TRUE; }
    }

    switch (message) {
    case WM_TIMER:
        if (wp == kTimerPoll) {
            // Color-keyed transparent areas pass mouse messages through. Sample
            // actual cursor movement so generous hit targets still work there.
            // Stationary samples do not reset inactivity or hover deadlines.
            if (state->visual != VisualState::Drawing || !IsWindowVisible(window)) return TRUE;
            POINT current{}; RECT bounds{};
            if (!GetCursorPos(&current) || !ScreenToClient(window, &current) || !GetClientRect(window, &bounds)) {
                EndPattern(window, -1); return TRUE;
            }
            if (current.x != state->observedCursor.x || current.y != state->observedCursor.y) {
                state->observedCursor = current;
                if (state->dragging || PtInRect(&bounds, current)) {
                    const WPARAM buttons = (GetKeyState(VK_LBUTTON) & 0x8000) ? MK_LBUTTON : 0;
                    SendMessageW(window, WM_MOUSEMOVE, buttons, MAKELPARAM(current.x, current.y));
                }
            }
            return TRUE;
        }
        if (wp == kTimerStart) {
            if (!state->startDue) return TRUE;
            const UINT remaining = HoverPolicy::Remaining(GetTickCount64(), state->startDue);
            if (remaining) {
                if (!SetTimer(window, kTimerStart, remaining, nullptr)) EndPattern(window, -1);
                return TRUE;
            }
            KillTimer(window, kTimerStart);
            state->startDue = 0;
            POINT current{};
            if (!GetCursorPos(&current) || !ScreenToClient(window, &current)) { EndPattern(window, -1); return TRUE; }
            const int actualDot = DotAt(window, current, Grid(window).hitRadius);
            if (!IsWindowVisible(window)) { ResetStroke(window, *state); return TRUE; }
            if (HoverPolicy::CanStart(state->pendingStartDot, actualDot, state->gate.Armed(),
                state->dragging, state->visual == VisualState::Drawing)) {
                state->pendingStartDot = -1;
                state->pattern.Clear();
                state->dragging = true; state->hoverStroke = true;
                state->previous = current;
                state->finishAnchor = current;
                SetCapture(window);
                if (GetCapture() != window) { ResetStroke(window, *state); return TRUE; }
                state->pattern.Add(static_cast<BYTE>(actualDot));
                state->shortDue = GetTickCount64() + HoverPolicy::ShortStrokeDelayMs;
                if (!SetTimer(window, kTimerShort, HoverPolicy::ShortStrokeDelayMs, nullptr)) { EndPattern(window, -1); return TRUE; }
                UpdateStatus(window, *state);
            } else state->pendingStartDot = -1;
            return TRUE;
        }
        if (wp == kTimerFinish || wp == kTimerShort) {
            ULONGLONG& deadline = wp == kTimerFinish ? state->finishDue : state->shortDue;
            if (!deadline) return TRUE;
            const UINT remaining = HoverPolicy::Remaining(GetTickCount64(), deadline);
            if (remaining) {
                if (!SetTimer(window, wp, remaining, nullptr)) EndPattern(window, -1);
                return TRUE;
            }
            KillTimer(window, wp); deadline = 0;
            if (!state->hoverStroke || !state->dragging || state->visual != VisualState::Drawing) return TRUE;
            POINT current{}; RECT bounds{};
            if (!GetCursorPos(&current) || !ScreenToClient(window, &current) || !GetClientRect(window, &bounds)) {
                EndPattern(window, -1); return TRUE;
            }
            if (!IsWindowVisible(window) || GetCapture() != window || !PtInRect(&bounds, current)) {
                ResetStroke(window, *state); return TRUE;
            }
            RECT fallback{};
            GetWindowRect(state->fallback, &fallback);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&fallback), 2);
            if (PtInRect(&fallback, current)) { ResetStroke(window, *state); return TRUE; }
            const double dx = current.x - state->finishAnchor.x;
            const double dy = current.y - state->finishAnchor.y;
            const double tolerance = 3 * state->scale;
            if (dx * dx + dy * dy > tolerance * tolerance) {
                Track(window, *state, current);
                state->finishAnchor = current;
                const UINT_PTR timer = state->pattern.Valid() ? kTimerFinish : kTimerShort;
                const UINT delay = state->pattern.Valid() ? HoverPolicy::FinishDelayMs : HoverPolicy::ShortStrokeDelayMs;
                state->finishDue = state->shortDue = 0;
                (timer == kTimerFinish ? state->finishDue : state->shortDue) = GetTickCount64() + delay;
                if (!SetTimer(window, timer, delay, nullptr)) EndPattern(window, -1);
                return TRUE;
            }
            if ((wp == kTimerFinish && HoverPolicy::CanFinish(state->hoverStroke, state->dragging, state->pattern.Valid(), true)) ||
                (wp == kTimerShort && !state->pattern.Valid())) FinishPattern(window, *state);
            return TRUE;
        }
        if (wp == kTimerInactivity) {
            state->pattern.Clear();
            if (GetCapture() == window) ReleaseCapture();
            EndPattern(window, IDCANCEL);
            return TRUE;
        }
        if (wp == kTimerSuccess) {
            KillTimer(window, kTimerSuccess);
            EndPattern(window, IDOK);
            return TRUE;
        }
        if (wp == kTimerTooShort) {
            KillTimer(window, kTimerTooShort);
            if (state->visual == VisualState::ErrorTooShort) ResetStroke(window, *state);
            return TRUE;
        }
        if (wp == kTimerRetry) {
            KillTimer(window, kTimerRetry);
            if (state->visual == VisualState::ErrorLockout) {
                if (!RefreshRetry(window, *state)) return TRUE;
                if (!state->passwordRequired && state->retrySeconds == 0) ResetStroke(window, *state);
                else {
                    UpdateStatus(window, *state);
                    if (!state->passwordRequired && !SetTimer(window, kTimerRetry, 1000, nullptr)) EndPattern(window, -1);
                }
            } else if (state->visual == VisualState::ErrorWrong) ResetStroke(window, *state);
            return TRUE;
        }
        return TRUE;

    case WM_DESTROY:
        if (state->font) { DeleteObject(state->font); state->font = nullptr; }
        if (state->background) { DeleteObject(state->background); state->background = nullptr; }
        for (UINT_PTR id = 1; id <= 8; ++id) KillTimer(window, id);
        return TRUE;

    case WM_ERASEBKGND:
        return TRUE;

    case WM_LBUTTONDOWN: {
        const POINT current = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        // Capture routes a click on the native fallback to this window.
        RECT fallback{};
        GetWindowRect(state->fallback, &fallback);
        MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&fallback), 2);
        if (PtInRect(&fallback, current)) {
            state->pattern.Clear(); state->dragging = false;
            if (GetCapture() == window) ReleaseCapture();
            EndPattern(window, IDCANCEL); return TRUE;
        }

        if (state->visual != VisualState::Drawing || state->dragging) return TRUE;

        const Layout grid = Grid(window);
        const int dot = DotAt(window, current, grid.hitRadius);
        if (dot < 0) return TRUE;

        KillTimer(window, kTimerStart);
        state->startDue = 0;
        state->pattern.Clear();
        state->visual = VisualState::Drawing;
        state->dragging = true;
        state->hoverStroke = false;
        state->previous = current;
        SetCapture(window);
        if (GetCapture() != window) { ResetStroke(window, *state); return TRUE; }
        state->pattern.Add(static_cast<BYTE>(dot));
        UpdateStatus(window, *state);
        return TRUE;
    }

    case WM_MOUSEMOVE: {
        const POINT current = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        state->observedCursor = current;

        if (state->visual != VisualState::Drawing) return TRUE;
        const Layout grid = Grid(window);
        state->hoverDot = DotAt(window, current, grid.hitRadius);
        state->gate.Observe(static_cast<int>(current.x / state->scale), static_cast<int>(current.y / state->scale));
        if (state->dragging) {
            RECT bounds{};
            GetClientRect(window, &bounds);
            if (state->hoverStroke && !PtInRect(&bounds, current)) { ResetStroke(window, *state); return TRUE; }
            if ((!state->hoverStroke && !(wp & MK_LBUTTON)) || GetCapture() != window) {
                ResetStroke(window, *state);
                return TRUE;
            }
            const size_t oldCount = state->pattern.count;
            Track(window, *state, current);
            if (state->hoverStroke) {
                const double dx = current.x - state->finishAnchor.x;
                const double dy = current.y - state->finishAnchor.y;
                const double tolerance = 3 * state->scale;
                if (state->pattern.count != oldCount || dx * dx + dy * dy > tolerance * tolerance) {
                    state->finishAnchor = current;
                    KillTimer(window, kTimerFinish); KillTimer(window, kTimerShort);
                    state->finishDue = state->shortDue = 0;
                    const UINT_PTR timer = state->pattern.Valid() ? kTimerFinish : kTimerShort;
                    const UINT delay = state->pattern.Valid() ? HoverPolicy::FinishDelayMs : HoverPolicy::ShortStrokeDelayMs;
                    (timer == kTimerFinish ? state->finishDue : state->shortDue) = GetTickCount64() + delay;
                    if (!SetTimer(window, timer, delay, nullptr)) { state->pattern.Clear(); EndPattern(window, -1); return TRUE; }
                }
            }
        } else if (state->gate.Armed() && state->hoverDot >= 0) {
            if (state->pendingStartDot != state->hoverDot) {
                state->pendingStartDot = state->hoverDot;
                state->startDue = GetTickCount64() + HoverPolicy::StartDelayMs;
                if (!SetTimer(window, kTimerStart, HoverPolicy::StartDelayMs, nullptr)) { EndPattern(window, -1); return TRUE; }
            }
        } else {
            state->pendingStartDot = -1;
            state->startDue = 0;
            KillTimer(window, kTimerStart);
        }
        UpdateStatus(window, *state);
        return TRUE;
    }

    case WM_LBUTTONUP: {
        if (state->visual != VisualState::Drawing) return TRUE;
        if (state->dragging && GetCapture() == window) {
            Track(window, *state, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (!state->hoverStroke || state->pattern.Valid()) {
                FinishPattern(window, *state);
            }
        }
        return TRUE;
    }

    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
        if (state->dragging) ResetStroke(window, *state);
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL) {
            state->pattern.Clear();
            if (GetCapture() == window) ReleaseCapture();
            EndPattern(window, IDCANCEL);
            return TRUE;
        }
        return FALSE;

    case WM_DPICHANGED:
        if (state->visual == VisualState::Drawing) ResetStroke(window, *state);
        if (!Configure(window, *state, LOWORD(wp), reinterpret_cast<const RECT*>(lp))) EndPattern(window, -1);
        else if (state->visual == VisualState::Drawing) ResetStroke(window, *state);
        return TRUE;

    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
        if (state->visual == VisualState::Drawing) ResetStroke(window, *state);
        if (!Configure(window, *state, GetDpiForWindow(window), nullptr)) EndPattern(window, -1);
        else if (state->visual == VisualState::Drawing) ResetStroke(window, *state);
        return TRUE;

    case WM_CTLCOLORSTATIC:
        if (reinterpret_cast<HWND>(lp) == state->status) {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, state->highContrast ? GetSysColor(COLOR_WINDOWTEXT) : RGB(240, 243, 250));
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<INT_PTR>(state->background);
        }
        break;

    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (!item || item->hwndItem != state->fallback) return FALSE;
        FillRect(item->hDC, &item->rcItem, state->background);
        if (!state->highContrast) {
            // A quiet pill preserves a real pointer hit area between glyphs.
            // Fully color-keyed text padding would pass clicks through.
            HGDIOBJ brush = SelectObject(item->hDC, GetStockObject(DC_BRUSH));
            HGDIOBJ pen = SelectObject(item->hDC, GetStockObject(NULL_PEN));
            const COLORREF previous = SetDCBrushColor(item->hDC,
                (item->itemState & ODS_SELECTED) ? RGB(35, 48, 65) : RGB(22, 28, 38));
            const int radius = static_cast<int>(14 * state->scale);
            RoundRect(item->hDC, item->rcItem.left, item->rcItem.top, item->rcItem.right, item->rcItem.bottom, radius, radius);
            SetDCBrushColor(item->hDC, previous);
            if (pen) SelectObject(item->hDC, pen);
            if (brush) SelectObject(item->hDC, brush);
        }
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, state->highContrast ? GetSysColor(COLOR_WINDOWTEXT) :
            ((item->itemState & ODS_SELECTED) ? RGB(180, 205, 235) : RGB(250, 250, 252)));
        HGDIOBJ old = SelectObject(item->hDC, state->font);
        RECT text = item->rcItem;
        DrawTextW(item->hDC, L"&Other sign-in options", -1, &text,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | ((item->itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0));
        if ((item->itemState & ODS_FOCUS) && !(item->itemState & ODS_NOFOCUSRECT)) {
            RECT focus = item->rcItem; InflateRect(&focus, -3, -3); DrawFocusRect(item->hDC, &focus);
        }
        if (old) SelectObject(item->hDC, old);
        return TRUE;
    }

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && state->visual == VisualState::Drawing) ResetStroke(window, *state);
        break;

    case WM_CLOSE:
        state->pattern.Clear();
        if (GetCapture() == window) ReleaseCapture();
        EndPattern(window, IDCANCEL);
        return TRUE;

    // ── GDI+ Anti-Aliased Fluent Surface Rendering ───────────────────────────
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC hdc = BeginPaint(window, &paint);
        RECT physical{};
        GetClientRect(window, &physical);
        RECT rect{0, 0, WindowW, WindowH};

        // Double buffer
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, physical.right, physical.bottom);
        if (!memDC || !memBmp) {
            if (memBmp) DeleteObject(memBmp);
            if (memDC) DeleteDC(memDC);
            EndPaint(window, &paint);
            state->pattern.Clear(); EndPattern(window, -1); return TRUE;
        }
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
        if (!oldBmp || oldBmp == HGDI_ERROR) {
            DeleteObject(memBmp); DeleteDC(memDC); EndPaint(window, &paint);
            state->pattern.Clear(); EndPattern(window, -1); return TRUE;
        }

        // 1) Fill outside with TransparentKey (outer margin)
        HBRUSH keyBrush = CreateSolidBrush(state->highContrast ? GetSysColor(COLOR_WINDOW) : TransparentKey);
        const bool filled = keyBrush && FillRect(memDC, &physical, keyBrush);
        if (keyBrush) DeleteObject(keyBrush);
        if (!filled) {
            SelectObject(memDC, oldBmp); DeleteObject(memBmp); DeleteDC(memDC); EndPaint(window, &paint);
            state->pattern.Clear(); EndPattern(window, -1); return TRUE;
        }

        {
            Gdiplus::Graphics g(memDC);
            g.ScaleTransform(static_cast<float>(state->scale), static_cast<float>(state->scale));
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            // Normal mode leaves the native account header and wallpaper visible.
            // Patlock renders only the grid and fallback action.
            if (state->highContrast) {
                Gdiplus::GraphicsPath cardPath;
                AddRoundedRect(cardPath, Gdiplus::RectF(6.0f, 6.0f, rect.right - 12.0f, rect.bottom - 12.0f), 20.0f);
                Gdiplus::SolidBrush background(Palette(*state, Gdiplus::Color(), COLOR_WINDOW));
                g.FillPath(&background, &cardPath);
                Gdiplus::Pen border(Palette(*state, Gdiplus::Color(), COLOR_WINDOWTEXT), 1.0f);
                g.DrawPath(&border, &cardPath);
            }

            // ── Colors by state ──────────────────────────────────────────────
            const Gdiplus::Color accent = GetSystemAccentColor();
            Gdiplus::Color lineColor = accent;
            Gdiplus::Color ringColor = accent;
            Gdiplus::Color discBg(255, 12, 48, 96);
            Gdiplus::Color coreColor(255, 255, 255, 255);

            if (state->visual == VisualState::Success) {
                lineColor = ringColor = Gdiplus::Color(255, 46, 213, 115);
                discBg = Gdiplus::Color(255, 18, 65, 40);
            } else if (state->visual == VisualState::ErrorWrong || state->visual == VisualState::ErrorTooShort || state->visual == VisualState::ErrorLockout) {
                lineColor = ringColor = Gdiplus::Color(255, 255, 75, 75);
                discBg = Gdiplus::Color(255, 70, 20, 25);
            }
            if (state->highContrast) {
                lineColor = ringColor = Palette(*state, lineColor, COLOR_HIGHLIGHT);
                coreColor = Palette(*state, coreColor, COLOR_HIGHLIGHTTEXT);
                discBg = Palette(*state, discBg, COLOR_HIGHLIGHT);
            }
            const Layout grid{WindowW / 2 - GridSpacing, GridTopY, GridSpacing, HitRadius};

            // ── Anti-Aliased Connecting Lines with Rounded Caps ──────────────
            Gdiplus::Pen linePen(lineColor, 3.0f);
            linePen.SetStartCap(Gdiplus::LineCapRound);
            linePen.SetEndCap(Gdiplus::LineCapRound);
            linePen.SetLineJoin(Gdiplus::LineJoinRound);

            if (!state->hide && state->pattern.count) {
                for (size_t i = 1; i < state->pattern.count; ++i) {
                    POINT p1 = Dot(grid, state->pattern.dots[i - 1]);
                    POINT p2 = Dot(grid, state->pattern.dots[i]);
                    g.DrawLine(&linePen, p1.x, p1.y, p2.x, p2.y);
                }
                if (state->dragging && state->visual == VisualState::Drawing) {
                    POINT last = Dot(grid, state->pattern.dots[state->pattern.count - 1]);
                    g.DrawLine(&linePen, last.x, last.y, static_cast<int>(state->previous.x / state->scale), static_cast<int>(state->previous.y / state->scale));
                }
            }

            // Draw the nine dots.
            for (BYTE i = 0; i < 9; ++i) {
                const POINT p = Dot(grid, i);
                const bool visited = !state->hide && state->pattern.Contains(i);
                const bool hovered = (!visited && state->hoverDot == i && state->visual == VisualState::Drawing);

                if (visited) {
                    // Connected dot: outer ring + inner core
                    float rOut = 11.0f;
                    Gdiplus::SolidBrush db(discBg);
                    g.FillEllipse(&db, p.x - rOut, p.y - rOut, 2.0f * rOut, 2.0f * rOut);
                    Gdiplus::Pen rp(ringColor, 2.0f);
                    g.DrawEllipse(&rp, p.x - rOut, p.y - rOut, 2.0f * rOut, 2.0f * rOut);

                    float rIn = 4.5f;
                    Gdiplus::SolidBrush cb(coreColor);
                    g.FillEllipse(&cb, p.x - rIn, p.y - rIn, 2.0f * rIn, 2.0f * rIn);
                } else if (hovered) {
                    // Hovered dot: cyan accent ring
                    float rOut = 11.0f;
                    Gdiplus::SolidBrush db(Palette(*state, Gdiplus::Color(255, 18, 48, 88), COLOR_WINDOW));
                    g.FillEllipse(&db, p.x - rOut, p.y - rOut, 2.0f * rOut, 2.0f * rOut);
                    Gdiplus::Pen hp(Palette(*state, Gdiplus::Color(255, 0, 160, 255), COLOR_HIGHLIGHT), 1.75f);
                    g.DrawEllipse(&hp, p.x - rOut, p.y - rOut, 2.0f * rOut, 2.0f * rOut);

                    float rIn = 4.0f;
                    Gdiplus::SolidBrush cb(Palette(*state, Gdiplus::Color(255, 210, 235, 255), COLOR_WINDOWTEXT));
                    g.FillEllipse(&cb, p.x - rIn, p.y - rIn, 2.0f * rIn, 2.0f * rIn);
                } else {
                    // Quiet solid dots, with a fine light edge for dark wallpapers.
                    float r = 11.0f;
                    Gdiplus::SolidBrush dotBrush(Palette(*state, Gdiplus::Color(255, 10, 14, 20), COLOR_WINDOW));
                    g.FillEllipse(&dotBrush, p.x - r, p.y - r, 2.0f * r, 2.0f * r);

                    Gdiplus::Pen dotRim(Palette(*state, Gdiplus::Color(160, 150, 165, 185), COLOR_WINDOWTEXT), 1.0f);
                    g.DrawEllipse(&dotRim, p.x - r, p.y - r, 2.0f * r, 2.0f * r);

                }
            }

        }

        // Blit double buffer to screen
        const BOOL copied = BitBlt(hdc, 0, 0, physical.right, physical.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        EndPaint(window, &paint);
        if (!copied) { state->pattern.Clear(); EndPattern(window, -1); }
        return TRUE;
    }
    }
    return FALSE;
}

} // anon namespace

HRESULT ShowPatternWindow(
    HINSTANCE instance,
    HWND owner,
    bool hideTrail,
    PatternVerifier verifier,
    RetryDelay retryDelay,
    const wchar_t* anchorName,
    PatternDone onDone) noexcept
{
    if (g_activeWindow.load()) return E_UNEXPECTED; // One pattern window at a time.

    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok) return E_FAIL;

    State* state = nullptr;
    try {
        state = new State();
        state->hide = hideTrail;
        state->verifier = std::move(verifier);
        state->retryDelay = std::move(retryDelay);
        state->onDone = std::move(onDone);
        state->gdiplusToken = gdiplusToken;
    } catch (...) {
        delete state;
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return E_OUTOFMEMORY;
    }
    state->hasAnchor = FindUsernameAnchor(owner, anchorName, &state->anchor);

    alignas(DWORD) BYTE buffer[sizeof(DLGTEMPLATE) + 3 * sizeof(WORD)]{};
    auto* dialog = reinterpret_cast<DLGTEMPLATE*>(buffer);
    dialog->style = WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE;
    dialog->cx = 200;
    dialog->cy = 200;
    HWND window = CreateDialogIndirectParamW(instance, dialog, owner, Dialog, reinterpret_cast<LPARAM>(state));
    if (!window) {
        // WM_INITDIALOG never ran, so nothing owns the state yet.
        state->onDone = nullptr;
        delete state;
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return E_FAIL;
    }
    return S_OK; // From here the window owns the state and calls onDone exactly once.
}

// Blocking wrapper for the standalone demo only. The lock screen must use
// ShowPatternWindow so LogonUI keeps pumping its own messages.
HRESULT DrawPatternInteractive(
    HINSTANCE instance,
    HWND owner,
    bool hideTrail,
    const PatternVerifier& verifier,
    Pattern& output,
    const RetryDelay& retryDelay,
    const wchar_t* anchorName) noexcept
{
    output.Clear();
    struct Wait { bool done = false; HRESULT hr = E_FAIL; Pattern pattern; } wait;
    const HRESULT started = ShowPatternWindow(instance, owner, hideTrail, verifier, retryDelay, anchorName,
        [&wait](HRESULT hr, const Pattern& pattern) { wait.hr = hr; wait.pattern = pattern; wait.done = true; });
    if (FAILED(started)) return started;
    MSG msg;
    while (!wait.done) {
        const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) { if (got == 0) PostQuitMessage(static_cast<int>(msg.wParam)); break; }
        HWND dialog = g_activeWindow.load();
        if (dialog && IsDialogMessageW(dialog, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (wait.done && wait.hr == S_OK) { output = wait.pattern; wait.pattern.Clear(); return S_OK; }
    wait.pattern.Clear();
    return wait.done ? wait.hr : E_FAIL;
}

void CloseActivePatternWindow() noexcept {
    if (HWND window = g_activeWindow.load()) PostMessageW(window, WM_CLOSE, 0, 0);
}

HRESULT DrawPattern(HINSTANCE instance, HWND owner, bool hideTrail, Pattern& output) noexcept {
    return DrawPatternInteractive(instance, owner, hideTrail, nullptr, output);
}

} // namespace patlock
