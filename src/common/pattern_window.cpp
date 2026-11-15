#include "pattern_window.h"
#include "arming_gate.h"
#include "hover_policy.h"
#include "pattern_layout.h"
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
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
    kTimerPoll       = 8,    // cursor position poll (30ms)
    kTimerAnim       = 9,    // dot grow / shake / trail fade animation (16ms, only while animating)
    kTimerAnchor     = 10    // follow the native username when LogonUI re-lays out
};
constexpr UINT_PTR kLastTimer = kTimerAnchor;

// ── Timing Constants ─────────────────────────────────────────────────────────
constexpr UINT kSuccessFlashMs = 160;
constexpr UINT kConfirmFlashMs = 320;   // enrollment: show the finished stroke before closing
constexpr UINT kDesktopFinishDelayMs = 1000; // enrollment has no time pressure; avoid early submits
constexpr UINT kWrongFlashMs = 800;
constexpr UINT kTooShortFlashMs = 1000;
constexpr UINT kInactivityTimeoutMs = 120000;
constexpr UINT kCursorPollMs = 30;
constexpr ULONGLONG kShakeMs = 380;   // wrong-pattern shake
constexpr ULONGLONG kFadeMs = 260;    // trail fade-out after the finger lifts
constexpr UINT kAnchorPollMs = 400;   // username position re-check

// The window is per-pixel alpha (UpdateLayeredWindow): anti-aliased edges blend
// with the wallpaper, and fully transparent pixels pass clicks to LogonUI.

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

// The credential-provider surface must keep the compact native PIN geometry.
// Desktop enrollment gets its own roomy, self-contained card.
constexpr int DesktopCardWidth = 360;
constexpr int DesktopCardHeight = 410;
constexpr int DesktopGridSpacing = 82;
constexpr int DesktopGridTop = 134;
constexpr int DesktopStatusTop = 350;
constexpr int DesktopStatusHeight = 24;
constexpr int DesktopHitRadius = 34;   // Same share of the spacing as the lock screen (30 of 72).
constexpr COLORREF DesktopCardBackground = RGB(248, 250, 252);

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
    HWND owner = nullptr;
    std::wstring anchorName;
    bool desktopCard = false;       // Enrollment on the desktop: opaque rounded card, not the see-through lock-screen overlay.
    // Animation state (all drawn in WM_PAINT; the 16 ms timer only runs while needed).
    float grow[9]{};                // 0 = idle, 1 = fully "touched" per dot
    ULONGLONG shakeStart = 0;       // wrong-pattern shake start tick (0 = off)
    Pattern fadePattern;            // trail that is currently fading out
    ULONGLONG fadeStart = 0;
    bool fadeRed = false;
    wchar_t statusText[96]{};       // Rendered by Patlock; the STATIC child carries it for accessibility.
    bool renderPending = false;
    bool anchorPending = false;      // An asynchronous username lookup is in flight.
    ULONGLONG anchorSince = 0;
    int anchorMisses = 0;            // Consecutive lookups that did not find the username.
    bool escapeDown = false;         // Edge detection for Esc read by the cursor poll.
};

int ContentWidth(const State& state) noexcept {
    return state.desktopCard ? DesktopCardWidth : WindowW;
}

int ContentHeight(const State& state) noexcept {
    return state.desktopCard ? DesktopCardHeight : WindowH;
}

int ContentGridSpacing(const State& state) noexcept {
    return state.desktopCard ? DesktopGridSpacing : GridSpacing;
}

int ContentGridTop(const State& state) noexcept {
    return state.desktopCard ? DesktopGridTop : GridTopY;
}

int ContentStatusTop(const State& state) noexcept {
    return state.desktopCard ? DesktopStatusTop : PatternLayout::StatusTop;
}

int ContentHitRadius(const State& state) noexcept {
    return state.desktopCard ? DesktopHitRadius : HitRadius;
}

UINT FinishDelay(const State& state) noexcept {
    return state.desktopCard ? kDesktopFinishDelayMs : HoverPolicy::FinishDelayMs;
}

int ContentStatusHeight(const State& state) noexcept {
    return state.desktopCard ? DesktopStatusHeight : PatternLayout::StatusHeight;
}

// The window is modeless: it must never run its own message loop inside LogonUI,
// or LogonUI cannot process clicks on "Sign-in options" / the user list. Every
// close request is posted here and handled on the window's own thread.
constexpr UINT kMsgEnd = WM_APP + 1;
constexpr UINT kMsgRender = WM_APP + 2;   // Coalesced redraw; posted messages run before queued input.
constexpr UINT kMsgAnchor = WM_APP + 3;   // Username rectangle from the async lookup.
void EndPattern(HWND window, INT_PTR code) noexcept {
    if (window) PostMessageW(window, kMsgEnd, static_cast<WPARAM>(code), 0);
}
void Render(HWND window, State& state) noexcept;
void RequestRender(HWND window, State& state) noexcept {
    if (state.renderPending) return;
    state.renderPending = PostMessageW(window, kMsgRender, 0, 0) != FALSE;
}
void StartAnim(HWND window) noexcept {
    SetTimer(window, kTimerAnim, 16, nullptr); // Re-arming an existing timer is harmless.
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
    HWND notify = nullptr;   // When set, the result is posted as kMsgAnchor.
    std::wstring name;
    RECT rect{};
    bool found = false;
};

void QueryAnchor(AnchorQuery& query) noexcept {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init)) return;
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
}

struct AnchorStart {
    std::shared_ptr<AnchorQuery> query;
    HMODULE module = nullptr;
};

void RunAnchorQuery(AnchorStart* start) noexcept {
    std::unique_ptr<AnchorStart> holder(start);
    AnchorQuery& query = *holder->query;
    QueryAnchor(query);
    if (query.notify) {
        // Pack the rectangle into the message itself so nothing leaks if the
        // window is already gone. right <= left means "not found".
        const RECT r = query.found ? query.rect : RECT{};
        PostMessageW(query.notify, kMsgAnchor,
            static_cast<WPARAM>(static_cast<ULONGLONG>(static_cast<ULONG>(r.left)) | (static_cast<ULONGLONG>(static_cast<ULONG>(r.top)) << 32)),
            static_cast<LPARAM>(static_cast<ULONGLONG>(static_cast<ULONG>(r.right)) | (static_cast<ULONGLONG>(static_cast<ULONG>(r.bottom)) << 32)));
    }
}

DWORD WINAPI AnchorThread(LPVOID param) noexcept {
    auto* start = static_cast<AnchorStart*>(param);
    const HMODULE module = start->module;
    RunAnchorQuery(start);
    // The thread holds its own reference to this module, so LogonUI unloading the
    // provider while a lookup is still running cannot pull code out from under it.
    FreeLibraryAndExitThread(module, 0);
}

HANDLE StartAnchorQuery(const std::shared_ptr<AnchorQuery>& query) noexcept {
    try {
        auto start = std::make_unique<AnchorStart>();
        start->query = query;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(&AnchorThread), &start->module)) return nullptr;
        const HMODULE module = start->module;
        HANDLE thread = CreateThread(nullptr, 0, AnchorThread, start.get(), 0, nullptr);
        if (!thread) { FreeLibrary(module); return nullptr; }
        start.release(); // The thread owns it now.
        return thread;
    } catch (...) {
        return nullptr;
    }
}

bool FindUsernameAnchor(HWND owner, const wchar_t* name, RECT* out) noexcept {
    if (!owner || !name || !*name || !out) return false;
    try {
        auto query = std::make_shared<AnchorQuery>();
        query->root = GetAncestor(owner, GA_ROOT);
        if (!query->root) query->root = owner;
        query->name = name;
        HANDLE thread = StartAnchorQuery(query);
        if (!thread) return false;
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
        CloseHandle(thread); // On timeout the worker finishes alone; it shares ownership of the query.
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

void BeginAnchorRefresh(HWND window, State& state, HWND owner, const wchar_t* name) noexcept {
    if (!owner || !name || !*name) return;
    const ULONGLONG now = GetTickCount64();
    if (state.anchorPending && now - state.anchorSince < 3000) return;
    try {
        auto query = std::make_shared<AnchorQuery>();
        query->root = GetAncestor(owner, GA_ROOT);
        if (!query->root) query->root = owner;
        query->notify = window;
        query->name = name;
        HANDLE thread = StartAnchorQuery(query);
        if (!thread) return;
        CloseHandle(thread);
        state.anchorPending = true;
        state.anchorSince = now;
    } catch (...) {
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
    const int contentWidth = ContentWidth(state);
    const int contentHeight = ContentHeight(state);
    state.scale = (std::min)(static_cast<double>(dpi) / 96.0,
        (std::min)(static_cast<double>(width - 16) / contentWidth, static_cast<double>(height - 16) / contentHeight));
    if (state.scale < 0.5) return false; // Decline when controls cannot remain usable.
    const int w = static_cast<int>(contentWidth * state.scale);
    const int h = static_cast<int>(contentHeight * state.scale);
    int preferredX = area.left + (width - w) / 2;
    int preferredY = state.desktopCard ? area.top + (height - h) / 2
        : area.top + height / 2 + static_cast<int>(PatternLayout::FallbackCenterOffset * state.scale);
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
    HBRUSH brush = CreateSolidBrush(state.highContrast ? GetSysColor(COLOR_WINDOW) : DesktopCardBackground);
    HFONT font = CreateFontW(-static_cast<int>(15 * state.scale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
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
        !MoveWindow(state.status, scaled(20), scaled(ContentStatusTop(state)), scaled(contentWidth - 40), scaled(ContentStatusHeight(state)), TRUE) ||
        !MoveWindow(state.fallback, 0, 0, 0, 0, FALSE)) return false; // Hidden: native "Sign-in options" replaces it.
    RequestRender(window, state);
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
    const auto* state = reinterpret_cast<const State*>(GetWindowLongPtrW(window, DWLP_USER));
    const int contentWidth = state ? ContentWidth(*state) : WindowW;
    const int spacingLogical = state ? ContentGridSpacing(*state) : GridSpacing;
    const int topLogical = state ? ContentGridTop(*state) : GridTopY;
    const int cw = static_cast<int>(r.right);
    const double scale = static_cast<double>(cw) / contentWidth;
    const int spacing = static_cast<int>(spacingLogical * scale);
    const int hitLogical = state ? ContentHitRadius(*state) : HitRadius;
    return {cw / 2 - spacing, static_cast<int>(topLogical * scale), spacing, static_cast<int>(hitLogical * scale)};
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

void UpdateStatus(HWND window, State& state) noexcept {
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
    if (lstrcmpW(state.statusText, text) != 0) {
        StringCchCopyW(state.statusText, ARRAYSIZE(state.statusText), text);
        // A layered window does not draw child controls; the STATIC child stays
        // as the accessible name source for screen readers.
        SetWindowTextW(state.status, text);
        NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, state.status, OBJID_CLIENT, 0);
    }
    RequestRender(window, state);
}

bool RefreshRetry(HWND window, State& state) noexcept {
    state.passwordRequired = true;
    state.retrySeconds = 0;
    if (state.retryDelay) {
        HRESULT hr = E_FAIL;
        try { hr = state.retryDelay(&state.retrySeconds); }
        catch (...) { state.pattern.Clear(); EndPattern(window, -1); return false; }
        if (hr == S_OK) state.passwordRequired = false;
        // Any other failure (not enrolled, pattern disabled, stale password) keeps
        // passwordRequired = true: the grid stays up and shows a quiet status line.
    }
    return true;
}

void ResetStroke(HWND window, State& state) noexcept {
    KillTimer(window, kTimerFinish);
    KillTimer(window, kTimerShort);
    KillTimer(window, kTimerTooShort);
    KillTimer(window, kTimerRetry);
    KillTimer(window, kTimerStart);
    if (!state.hide && state.pattern.count > 1 && state.visual != VisualState::Success) {
        state.fadePattern = state.pattern;
        state.fadeStart = GetTickCount64();
        state.fadeRed = state.visual == VisualState::ErrorWrong || state.visual == VisualState::ErrorTooShort;
        StartAnim(window);
    }
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

// (Re)starts the pause timer that submits a hover stroke, or times out a short one.
void ArmFinish(HWND window, State& state) noexcept {
    KillTimer(window, kTimerFinish);
    KillTimer(window, kTimerShort);
    state.finishDue = state.shortDue = 0;
    const bool valid = state.pattern.Valid();
    const UINT delay = valid ? FinishDelay(state) : HoverPolicy::ShortStrokeDelayMs;
    (valid ? state.finishDue : state.shortDue) = GetTickCount64() + delay;
    if (!SetTimer(window, valid ? kTimerFinish : kTimerShort, delay, nullptr)) EndPattern(window, -1);
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
        Render(window, state); // Show "Checking" now: verification below blocks this thread.
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
            state.shakeStart = GetTickCount64();
            StartAnim(window);
            if (!RefreshRetry(window, state)) return;
            UpdateStatus(window, state);
            if (!state.passwordRequired && !SetTimer(window, kTimerRetry, 1000, nullptr)) EndPattern(window, -1);
        } else {
            // Wrong pattern: flash red for 800ms, then reset stroke for instant retry
            state.visual = VisualState::ErrorWrong;
            state.shakeStart = GetTickCount64();
            StartAnim(window);
            UpdateStatus(window, state);
            if (!SetTimer(window, kTimerRetry, kWrongFlashMs, nullptr)) { state.pattern.Clear(); EndPattern(window, -1); }
        }
    } else {
        // No verifier (enrollment): briefly show the completed stroke in green
        // instead of the window vanishing the instant the last dot is reached.
        state.visual = VisualState::Success;
        UpdateStatus(window, state);
        if (!SetTimer(window, kTimerSuccess, kConfirmFlashMs, nullptr)) EndPattern(window, IDOK);
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

// ── Rendering (per-pixel alpha) ──────────────────────────────────────────────

Gdiplus::Color WithAlpha(Gdiplus::Color color, float alpha) noexcept {
    const float a = (std::max)(0.0f, (std::min)(1.0f, alpha));
    return Gdiplus::Color(static_cast<BYTE>(color.GetA() * a), color.GetR(), color.GetG(), color.GetB());
}

Gdiplus::PointF DotPoint(const State& state, int dot) noexcept {
    const float spacing = static_cast<float>(ContentGridSpacing(state));
    const float centerX = ContentWidth(state) / 2.0f;
    return Gdiplus::PointF(centerX + (dot % 3 - 1) * spacing, static_cast<float>(ContentGridTop(state)) + (dot / 3) * spacing);
}

void FillCircle(Gdiplus::Graphics& g, Gdiplus::Color color, Gdiplus::PointF center, float radius) noexcept {
    if (radius <= 0.0f || color.GetA() == 0) return;
    Gdiplus::SolidBrush brush(color);
    g.FillEllipse(&brush, center.X - radius, center.Y - radius, 2.0f * radius, 2.0f * radius);
}

void StrokeLines(Gdiplus::Graphics& g, const Gdiplus::PointF* points, int count, Gdiplus::Color color, float width) noexcept {
    if (count < 2 || width <= 0.0f || color.GetA() == 0) return;
    Gdiplus::Pen pen(color, width);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    g.DrawLines(&pen, points, count);
}

// Draws the whole surface in logical (96 DPI) units. Returns true while a dot
// is still easing toward its target size.
bool PaintSurface(Gdiplus::Graphics& g, const State& state) noexcept {
    const float width = static_cast<float>(ContentWidth(state));
    const float height = static_cast<float>(ContentHeight(state));
    const bool hc = state.highContrast;
    const bool card = state.desktopCard && !hc;
    const bool error = state.visual == VisualState::ErrorWrong || state.visual == VisualState::ErrorTooShort ||
        state.visual == VisualState::ErrorLockout;
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::StringFormat center;
    center.SetAlignment(Gdiplus::StringAlignmentCenter);
    center.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    // ── Background ───────────────────────────────────────────────────────────
    if (hc) {
        Gdiplus::SolidBrush background(Palette(state, Gdiplus::Color(), COLOR_WINDOW));
        g.FillRectangle(&background, 0.0f, 0.0f, width, height);
        Gdiplus::Pen border(Palette(state, Gdiplus::Color(), COLOR_WINDOWTEXT), 1.0f);
        g.DrawRectangle(&border, 0.5f, 0.5f, width - 1.0f, height - 1.0f);
    } else if (card) {
        const Gdiplus::RectF body(10.0f, 10.0f, width - 20.0f, height - 22.0f);
        for (int i = 8; i >= 1; --i) { // Soft drop shadow.
            const float spread = static_cast<float>(i);
            Gdiplus::GraphicsPath shadow;
            AddRoundedRect(shadow, Gdiplus::RectF(body.X - spread, body.Y - spread + 3.0f,
                body.Width + 2.0f * spread, body.Height + 2.0f * spread), 24.0f + spread);
            Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(5, 15, 23, 42));
            g.FillPath(&shadowBrush, &shadow);
        }
        Gdiplus::GraphicsPath cardPath;
        AddRoundedRect(cardPath, body, 24.0f);
        Gdiplus::SolidBrush cardFill(Gdiplus::Color(255, 255, 255, 255));
        g.FillPath(&cardFill, &cardPath);
        Gdiplus::Pen cardBorder(Gdiplus::Color(255, 226, 232, 240), 1.0f);
        g.DrawPath(&cardBorder, &cardPath);
        Gdiplus::Font titleFont(&family, 19.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font detailFont(&family, 13.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 15, 23, 42));
        Gdiplus::SolidBrush detailBrush(Gdiplus::Color(255, 100, 116, 139));
        wchar_t detail[64]{};
        StringCchPrintfW(detail, ARRAYSIZE(detail), L"Connect at least %zu dots", MinimumDots);
        g.DrawString(L"Draw your pattern", -1, &titleFont, Gdiplus::RectF(0.0f, 36.0f, width, 28.0f), &center, &titleBrush);
        g.DrawString(detail, -1, &detailFont, Gdiplus::RectF(0.0f, 66.0f, width, 20.0f), &center, &detailBrush);
    } else {
        // A nearly invisible plate (alpha 1) under the grid: fully transparent
        // pixels pass the pointer through to LogonUI, so this keeps real mouse
        // messages flowing over the dots. Nothing outside the grid is covered.
        const float spacing = static_cast<float>(ContentGridSpacing(state));
        const float pad = static_cast<float>(HitRadius) + 8.0f;
        const float top = static_cast<float>(ContentGridTop(state));
        Gdiplus::GraphicsPath plate;
        AddRoundedRect(plate, Gdiplus::RectF(width / 2.0f - spacing - pad, top - pad,
            2.0f * (spacing + pad), 2.0f * (spacing + pad)), 28.0f);
        Gdiplus::SolidBrush hitBrush(Gdiplus::Color(1, 0, 0, 0));
        g.FillPath(&hitBrush, &plate);
    }

    // Wrong-pattern shake moves the grid and the status line together.
    if (state.shakeStart) {
        const double t = static_cast<double>(GetTickCount64() - state.shakeStart) / kShakeMs;
        if (t >= 0.0 && t < 1.0) {
            g.TranslateTransform(static_cast<float>(9.0 * (1.0 - t) * std::sin(t * 6.0 * 3.141592653589793)), 0.0f);
        }
    }

    // ── Colours ──────────────────────────────────────────────────────────────
    // Lock screen: white strokes read on any wallpaper (with a soft shadow).
    // Desktop card: the Windows accent colour on white.
    Gdiplus::Color tint = card ? GetSystemAccentColor() : Gdiplus::Color(255, 255, 255, 255);
    if (state.visual == VisualState::Success) tint = card ? Gdiplus::Color(255, 22, 163, 74) : Gdiplus::Color(255, 120, 220, 120);
    else if (error) tint = card ? Gdiplus::Color(255, 220, 38, 38) : Gdiplus::Color(255, 255, 112, 112);
    if (hc) tint = Palette(state, tint, COLOR_HIGHLIGHT);
    const Gdiplus::Color shadow = (card || hc) ? Gdiplus::Color(0, 0, 0, 0) : Gdiplus::Color(64, 0, 0, 0);
    const float lineWidth = card ? 4.0f : 4.5f;

    // ── Trail ────────────────────────────────────────────────────────────────
    if (state.fadeStart && state.fadePattern.count > 1) {
        const float t = static_cast<float>(std::min<ULONGLONG>(kFadeMs, GetTickCount64() - state.fadeStart)) / kFadeMs;
        Gdiplus::PointF faded[9];
        int count = 0;
        for (size_t i = 0; i < state.fadePattern.count && count < 9; ++i) faded[count++] = DotPoint(state, state.fadePattern.dots[i]);
        Gdiplus::Color fadeColor = state.fadeRed ? (card ? Gdiplus::Color(255, 220, 38, 38) : Gdiplus::Color(255, 255, 112, 112))
                                                 : (card ? GetSystemAccentColor() : Gdiplus::Color(255, 255, 255, 255));
        if (hc) fadeColor = Palette(state, fadeColor, COLOR_HIGHLIGHT);
        StrokeLines(g, faded, count, WithAlpha(fadeColor, 0.85f * (1.0f - t)), lineWidth);
    }
    if (!state.hide && state.pattern.count) {
        Gdiplus::PointF points[10];
        int count = 0;
        for (size_t i = 0; i < state.pattern.count && count < 9; ++i) points[count++] = DotPoint(state, state.pattern.dots[i]);
        if (state.dragging && state.visual == VisualState::Drawing) {
            points[count++] = Gdiplus::PointF(static_cast<float>(state.previous.x / state.scale),
                static_cast<float>(state.previous.y / state.scale));
        }
        StrokeLines(g, points, count, shadow, lineWidth + 3.0f);
        StrokeLines(g, points, count, WithAlpha(tint, card || hc ? 1.0f : 0.9f), lineWidth);
    }

    // ── Dots ─────────────────────────────────────────────────────────────────
    bool animating = false;
    for (BYTE i = 0; i < 9; ++i) {
        const Gdiplus::PointF p = DotPoint(state, i);
        const bool visited = !state.hide && state.pattern.Contains(i);
        const bool hovered = !visited && state.hoverDot == i && state.visual == VisualState::Drawing;
        const float target = visited ? 1.0f : (hovered ? 0.5f : 0.0f);
        if (std::fabs(target - state.grow[i]) > 0.005f) animating = true;
        const float grow = state.grow[i];
        if (hc) {
            FillCircle(g, visited ? Palette(state, tint, COLOR_HIGHLIGHT) : Palette(state, Gdiplus::Color(), COLOR_WINDOWTEXT),
                p, (visited ? 9.0f : 6.0f) + 2.0f * grow);
            continue;
        }
        // Soft touch target that swells when the dot is reached.
        Gdiplus::Color halo = card ? Gdiplus::Color(255, 241, 245, 249) : Gdiplus::Color(34, 255, 255, 255);
        if (visited) halo = WithAlpha(tint, card ? 0.16f : 0.28f);
        else if (grow > 0.02f) halo = card ? Gdiplus::Color(255, 226, 232, 240) : Gdiplus::Color(64, 255, 255, 255);
        FillCircle(g, halo, p, 15.0f + 9.0f * grow);
        const float coreRadius = (visited ? 7.0f : 5.5f) + 1.5f * grow;
        FillCircle(g, shadow, p, coreRadius + 1.5f); // Concentric: an even outline, not an offset shadow.
        const Gdiplus::Color core = visited ? tint
            : (card ? Gdiplus::Color(255, 100, 116, 139) : Gdiplus::Color(240, 255, 255, 255));
        FillCircle(g, core, p, coreRadius);
    }

    // ── Status line ──────────────────────────────────────────────────────────
    const wchar_t* text = state.statusText;
    bool hint = false;
    if (!*text && !state.desktopCard && state.visual == VisualState::Drawing && !state.dragging && state.pattern.count == 0) {
        text = L"Draw your pattern";
        hint = true;
    }
    if (*text) {
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        Gdiplus::Font font(&family, card ? 13.0f : 14.0f, hint ? Gdiplus::FontStyleRegular : Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        const Gdiplus::RectF box(16.0f, static_cast<float>(ContentStatusTop(state)), width - 32.0f,
            static_cast<float>(ContentStatusHeight(state)));
        if (!card && !hc) {
            Gdiplus::SolidBrush textShadow(Gdiplus::Color(hint ? 90 : 140, 0, 0, 0));
            g.DrawString(text, -1, &font, Gdiplus::RectF(box.X, box.Y + 1.0f, box.Width, box.Height), &center, &textShadow);
        }
        Gdiplus::Color color = hc ? Palette(state, Gdiplus::Color(), COLOR_WINDOWTEXT)
            : card ? (error ? tint : Gdiplus::Color(255, 51, 65, 85))
            : (error ? Gdiplus::Color(255, 255, 190, 190) : Gdiplus::Color(hint ? 200 : 255, 255, 255, 255));
        Gdiplus::SolidBrush textBrush(color);
        g.DrawString(text, -1, &font, box, &center, &textBrush);
    }
    return animating;
}

// Draws into a 32-bit premultiplied DIB and hands it to UpdateLayeredWindow.
void Render(HWND window, State& state) noexcept {
    RECT client{};
    if (!GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0) return;
    const int w = client.right;
    const int h = client.bottom;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h; // Top-down rows, matching the GDI+ stride below.
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    HDC memory = screen ? CreateCompatibleDC(screen) : nullptr;
    void* bits = nullptr;
    HBITMAP dib = memory ? CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    bool ok = false;
    bool animating = false;
    if (dib && bits) {
        ZeroMemory(bits, static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
        {
            Gdiplus::Bitmap canvas(w, h, w * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
            Gdiplus::Graphics g(&canvas);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            g.ScaleTransform(static_cast<float>(state.scale), static_cast<float>(state.scale));
            animating = PaintSurface(g, state);
            g.Flush(Gdiplus::FlushIntentionSync);
        }
        const HGDIOBJ old = SelectObject(memory, dib);
        if (old && old != HGDI_ERROR) {
            POINT source{0, 0};
            SIZE size{w, h};
            BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            ok = UpdateLayeredWindow(window, screen, nullptr, &size, memory, &source, 0, &blend, ULW_ALPHA) != FALSE;
            SelectObject(memory, old);
        }
    }
    if (dib) DeleteObject(dib);
    if (memory) DeleteDC(memory);
    if (screen) ReleaseDC(nullptr, screen);
    if (!ok) { state.pattern.Clear(); EndPattern(window, -1); return; } // Fail closed: no half-drawn grid.
    if (animating) StartAnim(window);
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

        // Per-pixel alpha layered window. SetLayeredWindowAttributes must never be
        // called on it, or UpdateLayeredWindow stops working.
        LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        SetWindowLongPtrW(window, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);

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
        if (state->hasAnchor) SetTimer(window, kTimerAnchor, kAnchorPollMs, nullptr); // Optional: no anchor tracking if it fails.
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
    if (message == kMsgRender) {
        state->renderPending = false;
        Render(window, *state);
        return TRUE;
    }
    if (message == kMsgAnchor) {
        state->anchorPending = false;
        const ULONGLONG a = static_cast<ULONGLONG>(wp), b = static_cast<ULONGLONG>(lp);
        const RECT r{static_cast<LONG>(static_cast<ULONG>(a & 0xffffffffULL)), static_cast<LONG>(static_cast<ULONG>(a >> 32)),
                     static_cast<LONG>(static_cast<ULONG>(b & 0xffffffffULL)), static_cast<LONG>(static_cast<ULONG>(b >> 32))};
        if (r.right <= r.left || r.bottom <= r.top) {
            // The username left the screen (back to the clock cover, or another
            // view). Hide instead of floating a grid over unrelated UI.
            if (++state->anchorMisses >= 2 && IsWindowVisible(window) && !state->dragging) {
                ResetStroke(window, *state);
                ShowWindow(window, SW_HIDE);
            }
            return TRUE;
        }
        state->anchorMisses = 0;
        const bool moved = std::abs(r.left - state->anchor.left) > 1 || std::abs(r.top - state->anchor.top) > 1 ||
            std::abs(r.right - state->anchor.right) > 1 || std::abs(r.bottom - state->anchor.bottom) > 1;
        if (moved && !state->dragging) {
            state->anchor = r;
            if (!Configure(window, *state, GetDpiForWindow(window), nullptr)) { EndPattern(window, -1); return TRUE; }
        }
        if (!IsWindowVisible(window) && !state->dragging) {
            ShowWindow(window, SW_SHOWNOACTIVATE);
            ResetStroke(window, *state);
        }
        return TRUE;
    }
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
        if (wp == kTimerAnim) {
            const ULONGLONG now = GetTickCount64();
            bool active = false;
            for (int i = 0; i < 9; ++i) {
                const bool visited = !state->hide && state->pattern.Contains(static_cast<BYTE>(i));
                const bool hovered = !visited && state->hoverDot == i && state->visual == VisualState::Drawing;
                const float target = visited ? 1.0f : (hovered ? 0.5f : 0.0f);
                float& grow = state->grow[i];
                grow += (target - grow) * 0.35f;
                if (std::fabs(target - grow) < 0.01f) grow = target; else active = true;
            }
            if (state->shakeStart) { if (now - state->shakeStart < kShakeMs) active = true; else state->shakeStart = 0; }
            if (state->fadeStart) {
                if (now - state->fadeStart < kFadeMs) active = true;
                else { state->fadeStart = 0; state->fadePattern.Clear(); }
            }
            if (!active) KillTimer(window, kTimerAnim);
            RequestRender(window, *state);
            return TRUE;
        }
        if (wp == kTimerAnchor) {
            if (!state->dragging) BeginAnchorRefresh(window, *state, state->owner, state->anchorName.c_str());
            return TRUE;
        }
        if (wp == kTimerPoll) {
            // Color-keyed transparent areas pass mouse messages through. Sample
            // actual cursor movement so generous hit targets still work there.
            // Stationary samples do not reset inactivity or hover deadlines.
            if (state->visual != VisualState::Drawing || !IsWindowVisible(window)) return TRUE;
            // On the lock screen LogonUI keeps keyboard focus, so Esc never reaches
            // this window as a message. Read the key state while a stroke is in
            // progress and restart the stroke on a fresh press.
            const bool escapeDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
            const bool escapePressed = escapeDown && !state->escapeDown;
            state->escapeDown = escapeDown;
            // The desktop card gets Esc as a dialog command instead (see WM_COMMAND);
            // handling it here too could turn one press into reset-then-close.
            if (escapePressed && !state->desktopCard && (state->dragging || state->pattern.count)) {
                ResetStroke(window, *state);
                return TRUE;
            }
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
            if (!IsWindowVisible(window)) { ResetStroke(window, *state); return TRUE; }
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
                const UINT delay = state->pattern.Valid() ? FinishDelay(*state) : HoverPolicy::ShortStrokeDelayMs;
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
        for (UINT_PTR id = 1; id <= kLastTimer; ++id) KillTimer(window, id);
        state->fadePattern.Clear();
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
            // Casual strokes must survive: leaving the window, a trackpad dropping
            // the button mid-drag, or losing capture no longer cancel the stroke.
            // Off-grid points simply hit no dot; a drag that lost its button
            // continues as a hover stroke and is submitted by a pause.
            if (!state->hoverStroke && !(wp & MK_LBUTTON)) state->hoverStroke = true;
            const size_t oldCount = state->pattern.count;
            Track(window, *state, current);
            // All nine dots used: nothing more can be added, so submit now instead
            // of waiting for a pause (seen in the VM: the user moved away and the
            // complete stroke was discarded).
            if (state->pattern.count >= 9) { FinishPattern(window, *state); return TRUE; }
            if (state->hoverStroke) {
                const double dx = current.x - state->finishAnchor.x;
                const double dy = current.y - state->finishAnchor.y;
                const double tolerance = 3 * state->scale;
                if (state->pattern.count != oldCount || dx * dx + dy * dy > tolerance * tolerance) {
                    state->finishAnchor = current;
                    KillTimer(window, kTimerFinish); KillTimer(window, kTimerShort);
                    state->finishDue = state->shortDue = 0;
                    const UINT_PTR timer = state->pattern.Valid() ? kTimerFinish : kTimerShort;
                    const UINT delay = state->pattern.Valid() ? FinishDelay(*state) : HoverPolicy::ShortStrokeDelayMs;
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
        if (state->dragging) {
            Track(window, *state, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            // Releasing a complete pattern submits it, as on a phone. Releasing a
            // short one is usually a trackpad finger lift: keep drawing instead of
            // showing "too short"; a pause submits or times out the stroke.
            if (state->pattern.Valid()) FinishPattern(window, *state);
            else { state->hoverStroke = true; ArmFinish(window, *state); }
        }
        return TRUE;
    }

    case WM_CAPTURECHANGED:
        // Another window took the pointer, or a trackpad ended its drag. Keep the
        // stroke: the cursor poll keeps tracking it and a pause submits it.
        // (FinishPattern/ResetStroke clear `dragging` before releasing capture.)
        if (state->dragging && state->visual == VisualState::Drawing) {
            state->hoverStroke = true;
            ArmFinish(window, *state);
        }
        return TRUE;

    case WM_CANCELMODE:
        if (state->dragging) ResetStroke(window, *state);
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL) {
            // Esc during a stroke restarts it (started on the wrong dot); it is not
            // submitted, so no attempt is used. Esc with nothing drawn closes.
            if (state->visual == VisualState::Drawing && (state->dragging || state->pattern.count)) {
                ResetStroke(window, *state);
                return TRUE;
            }
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

    case WM_PAINT: {
        // UpdateLayeredWindow owns the pixels; just validate and redraw.
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        RequestRender(window, *state);
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
    state->desktopCard = (owner == nullptr);
    state->owner = owner;
    try { if (anchorName) state->anchorName = anchorName; } catch (...) { state->onDone = nullptr; delete state; Gdiplus::GdiplusShutdown(gdiplusToken); return E_OUTOFMEMORY; }
    state->hasAnchor = FindUsernameAnchor(owner, anchorName, &state->anchor);
    if (anchorName && *anchorName && !state->hasAnchor) {
        // The username is not on screen: LogonUI is still showing the lock cover
        // (clock screen) or is mid-transition. Do not float a grid over it.
        state->onDone = nullptr;
        delete state;
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    }

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

bool IsUsernameVisible(HWND owner, const wchar_t* name) noexcept {
    RECT rect{};
    return FindUsernameAnchor(owner, name, &rect);
}

HRESULT DrawPattern(HINSTANCE instance, HWND owner, bool hideTrail, Pattern& output) noexcept {
    return DrawPatternInteractive(instance, owner, hideTrail, nullptr, output);
}

} // namespace patlock
