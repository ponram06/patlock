#include "pattern_window.h"
#include "arming_gate.h"
#include "hover_policy.h"
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <strsafe.h>

#pragma comment(lib, "gdiplus.lib")

namespace patlock {
namespace {

// ── Color Key for Outer Transparent Margins ──────────────────────────────────
constexpr COLORREF TransparentKey = RGB(1, 1, 1);

// Layout constants
constexpr int WindowW     = 280;
constexpr int WindowH     = 292;
constexpr int GridSpacing = 62;
constexpr int GridTopY    = 80;    // Center Y of row 0 inside the card
constexpr int HitRadius   = 28;

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
};

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
    const int preferredX = suggested ? suggested->left : area.left + (width - w) / 2;
    const int preferredY = suggested ? suggested->top : area.top + height / 2 + static_cast<int>(35 * state.scale);
    const int x = (std::max)(static_cast<int>(area.left), (std::min)(preferredX, static_cast<int>(area.right) - w));
    const int y = (std::max)(static_cast<int>(area.top), (std::min)(preferredY, static_cast<int>(area.bottom) - h));
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)) return false;
    state.highContrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    HBRUSH brush = CreateSolidBrush(state.highContrast ? GetSysColor(COLOR_WINDOW) : RGB(18, 22, 30));
    HFONT font = CreateFontW(-static_cast<int>(14 * state.scale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
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
        !MoveWindow(state.status, scaled(12), scaled(16), scaled(256), scaled(34), TRUE) ||
        !MoveWindow(state.fallback, scaled(32), scaled(244), scaled(216), scaled(30), TRUE)) return false;
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
    wchar_t text[96] = L"Hover on a dot, then draw";
    if (state.visual == VisualState::Success) StringCchCopyW(text, ARRAYSIZE(text), L"Pattern accepted");
    else if (state.visual == VisualState::Checking) StringCchCopyW(text, ARRAYSIZE(text), L"Checking pattern...");
    else if (state.visual == VisualState::ErrorTooShort) StringCchPrintfW(text, ARRAYSIZE(text), L"Connect at least %zu dots", MinimumDots);
    else if (state.visual == VisualState::ErrorWrong) StringCchCopyW(text, ARRAYSIZE(text), L"Incorrect pattern");
    else if (state.visual == VisualState::ErrorLockout) {
        if (state.passwordRequired) StringCchCopyW(text, ARRAYSIZE(text), L"Pattern unavailable. Use Password.");
        else StringCchPrintfW(text, ARRAYSIZE(text), L"Try again in %lu seconds", state.retrySeconds);
    } else if (state.pattern.count) {
        StringCchPrintfW(text, ARRAYSIZE(text), state.hoverStroke
            ? L"%zu dots connected; pause to submit" : L"%zu dots connected; release to submit", state.pattern.count);
    } else if (!state.gate.Armed()) {
        StringCchCopyW(text, ARRAYSIZE(text), L"Move away, then hover on a dot");
    }
    wchar_t old[96]{};
    GetWindowTextW(state.status, old, ARRAYSIZE(old));
    if (lstrcmpW(old, text) != 0) {
        SetWindowTextW(state.status, text);
        NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, state.status, OBJID_CLIENT, 0);
    }
    InvalidateRect(window, nullptr, FALSE);
}

bool RefreshRetry(HWND window, State& state) noexcept {
    state.passwordRequired = true;
    state.retrySeconds = 0;
    if (state.retryDelay) {
        HRESULT hr = E_FAIL;
        try { hr = state.retryDelay(&state.retrySeconds); }
        catch (...) { state.pattern.Clear(); EndDialog(window, -1); return false; }
        if (hr == S_OK) state.passwordRequired = false;
        else if (hr != E_ACCESSDENIED) { state.pattern.Clear(); EndDialog(window, -1); return false; }
    }
    return true;
}

void ResetStroke(HWND window, State& state) noexcept {
    KillTimer(window, 2);
    KillTimer(window, 3);
    KillTimer(window, 5);
    KillTimer(window, 6);
    KillTimer(window, 7);
    state.pattern.Clear();
    state.dragging = false;
    state.hoverStroke = false;
    state.pendingStartDot = -1;
    state.startDue = state.finishDue = state.shortDue = 0;
    state.visual = VisualState::Drawing;
    state.hoverDot = -1;
    if (GetCapture() == window) ReleaseCapture();
    POINT cursor{};
    if (!GetCursorPos(&cursor) || !ScreenToClient(window, &cursor)) { EndDialog(window, -1); return; }
    const Layout grid = Grid(window);
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
    KillTimer(window, 2);
    KillTimer(window, 3);
    KillTimer(window, 7);
    state.startDue = state.finishDue = state.shortDue = 0;

    if (!state.pattern.Valid()) {
        if (state.pattern.count > 0) {
            state.pattern.Clear();
            state.dragging = false;
            state.visual = VisualState::ErrorTooShort;
            if (GetCapture() == window) ReleaseCapture();
            UpdateStatus(window, state);
            if (!SetTimer(window, 5, 1000, nullptr)) EndDialog(window, -1);
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
        catch (...) { state.pattern.Clear(); EndDialog(window, -1); return; }
        if (result == VerifyResult::Error) { state.pattern.Clear(); EndDialog(window, -1); return; }
        if (result == VerifyResult::Success) {
            state.visual = VisualState::Success;
            UpdateStatus(window, state);
            if (!SetTimer(window, 4, 120, nullptr)) { state.pattern.Clear(); EndDialog(window, -1); }
        } else if (result == VerifyResult::Lockout) {
            state.visual = VisualState::ErrorLockout;
            state.pattern.Clear();
            if (!RefreshRetry(window, state)) return;
            UpdateStatus(window, state);
            if (!state.passwordRequired && !SetTimer(window, 6, 1000, nullptr)) EndDialog(window, -1);
        } else {
            // Wrong pattern: flash red for 800ms, then reset stroke for instant retry
            state.visual = VisualState::ErrorWrong;
            UpdateStatus(window, state);
            if (!SetTimer(window, 6, 800, nullptr)) { state.pattern.Clear(); EndDialog(window, -1); }
        }
    } else {
        state.visual = VisualState::Success;
        UpdateStatus(window, state);
        EndDialog(window, IDOK);
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

        // Transparent layered window for floating rounded corners
        LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        SetWindowLongPtrW(window, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
        if (!SetLayeredWindowAttributes(window, TransparentKey, 0, LWA_COLORKEY)) { EndDialog(window, -1); return TRUE; }

        state->fallback = CreateWindowExW(0, L"BUTTON", L"&Other sign-in options",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
        state->status = CreateWindowExW(0, L"STATIC", L"Hover on a dot, then draw",
            WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(1001), nullptr, nullptr);
        if (!state->fallback || !state->status || !Configure(window, *state, GetDpiForWindow(window), nullptr)) {
            EndDialog(window, -1); return TRUE;
        }
        SetFocus(state->fallback);
        ResetStroke(window, *state);
        if (state->retryDelay) {
            if (!RefreshRetry(window, *state)) return FALSE;
            if (state->passwordRequired || state->retrySeconds) {
                state->visual = VisualState::ErrorLockout;
                UpdateStatus(window, *state);
                if (!state->passwordRequired && !SetTimer(window, 6, 1000, nullptr)) { EndDialog(window, -1); return FALSE; }
            }
        }

        if (!SetTimer(window, 1, 120000, nullptr)) EndDialog(window, -1);
        return FALSE; // Focus was explicitly assigned to the native fallback button.
    }
    if (!state) return FALSE;

    if (message == WM_LBUTTONDOWN || message == WM_MOUSEMOVE || message == WM_LBUTTONUP) {
        if (!SetTimer(window, 1, 120000, nullptr)) { state->pattern.Clear(); EndDialog(window, -1); return TRUE; }
    }

    switch (message) {
    case WM_TIMER:
        if (wp == 7) {
            if (!state->startDue) return TRUE;
            const UINT remaining = HoverPolicy::Remaining(GetTickCount64(), state->startDue);
            if (remaining) {
                if (!SetTimer(window, 7, remaining, nullptr)) EndDialog(window, -1);
                return TRUE;
            }
            KillTimer(window, 7);
            state->startDue = 0;
            POINT current{};
            if (!GetCursorPos(&current) || !ScreenToClient(window, &current)) { EndDialog(window, -1); return TRUE; }
            const int actualDot = DotAt(window, current, Grid(window).hitRadius);
            if (GetActiveWindow() != window) { ResetStroke(window, *state); return TRUE; }
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
                if (!SetTimer(window, 3, HoverPolicy::ShortStrokeDelayMs, nullptr)) { EndDialog(window, -1); return TRUE; }
                UpdateStatus(window, *state);
            } else state->pendingStartDot = -1;
            return TRUE;
        }
        if (wp == 2 || wp == 3) {
            ULONGLONG& deadline = wp == 2 ? state->finishDue : state->shortDue;
            if (!deadline) return TRUE;
            const UINT remaining = HoverPolicy::Remaining(GetTickCount64(), deadline);
            if (remaining) {
                if (!SetTimer(window, wp, remaining, nullptr)) EndDialog(window, -1);
                return TRUE;
            }
            KillTimer(window, wp); deadline = 0;
            if (!state->hoverStroke || !state->dragging || state->visual != VisualState::Drawing) return TRUE;
            POINT current{}; RECT bounds{};
            if (!GetCursorPos(&current) || !ScreenToClient(window, &current) || !GetClientRect(window, &bounds)) {
                EndDialog(window, -1); return TRUE;
            }
            if (GetActiveWindow() != window || GetCapture() != window || !PtInRect(&bounds, current)) {
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
                const UINT_PTR timer = state->pattern.Valid() ? 2 : 3;
                const UINT delay = state->pattern.Valid() ? HoverPolicy::FinishDelayMs : HoverPolicy::ShortStrokeDelayMs;
                state->finishDue = state->shortDue = 0;
                (timer == 2 ? state->finishDue : state->shortDue) = GetTickCount64() + delay;
                if (!SetTimer(window, timer, delay, nullptr)) EndDialog(window, -1);
                return TRUE;
            }
            if ((wp == 2 && HoverPolicy::CanFinish(state->hoverStroke, state->dragging, state->pattern.Valid(), true)) ||
                (wp == 3 && !state->pattern.Valid())) FinishPattern(window, *state);
            return TRUE;
        }
        if (wp == 1) {
            state->pattern.Clear();
            if (GetCapture() == window) ReleaseCapture();
            EndDialog(window, IDCANCEL);
            return TRUE;
        }
        if (wp == 4) {
            KillTimer(window, 4);
            EndDialog(window, IDOK);
            return TRUE;
        }
        if (wp == 5) {
            KillTimer(window, 5);
            if (state->visual == VisualState::ErrorTooShort) ResetStroke(window, *state);
            return TRUE;
        }
        if (wp == 6) {
            KillTimer(window, 6);
            if (state->visual == VisualState::ErrorLockout) {
                if (!RefreshRetry(window, *state)) return TRUE;
                if (!state->passwordRequired && state->retrySeconds == 0) ResetStroke(window, *state);
                else {
                    UpdateStatus(window, *state);
                    if (!state->passwordRequired && !SetTimer(window, 6, 1000, nullptr)) EndDialog(window, -1);
                }
            } else if (state->visual == VisualState::ErrorWrong) ResetStroke(window, *state);
            return TRUE;
        }
        return TRUE;

    case WM_DESTROY:
        if (state->font) { DeleteObject(state->font); state->font = nullptr; }
        if (state->background) { DeleteObject(state->background); state->background = nullptr; }
        for (UINT_PTR id = 1; id <= 7; ++id) KillTimer(window, id);
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
            EndDialog(window, IDCANCEL); return TRUE;
        }

        if (state->visual != VisualState::Drawing || state->dragging) return TRUE;

        const Layout grid = Grid(window);
        const int dot = DotAt(window, current, grid.hitRadius);
        if (dot < 0) return TRUE;

        KillTimer(window, 7);
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
                    KillTimer(window, 2); KillTimer(window, 3);
                    state->finishDue = state->shortDue = 0;
                    const UINT_PTR timer = state->pattern.Valid() ? 2 : 3;
                    const UINT delay = state->pattern.Valid() ? HoverPolicy::FinishDelayMs : HoverPolicy::ShortStrokeDelayMs;
                    (timer == 2 ? state->finishDue : state->shortDue) = GetTickCount64() + delay;
                    if (!SetTimer(window, timer, delay, nullptr)) { state->pattern.Clear(); EndDialog(window, -1); return TRUE; }
                }
            }
        } else if (state->gate.Armed() && state->hoverDot >= 0) {
            if (state->pendingStartDot != state->hoverDot) {
                state->pendingStartDot = state->hoverDot;
                state->startDue = GetTickCount64() + HoverPolicy::StartDelayMs;
                if (!SetTimer(window, 7, HoverPolicy::StartDelayMs, nullptr)) { EndDialog(window, -1); return TRUE; }
            }
        } else {
            state->pendingStartDot = -1;
            state->startDue = 0;
            KillTimer(window, 7);
        }
        UpdateStatus(window, *state);
        return TRUE;
    }

    case WM_LBUTTONUP: {
        if (state->visual != VisualState::Drawing) return TRUE;
        if (state->dragging && !state->hoverStroke && GetCapture() == window) {
            Track(window, *state, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            FinishPattern(window, *state);
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
            EndDialog(window, IDCANCEL);
            return TRUE;
        }
        return FALSE;

    case WM_DPICHANGED:
        if (state->visual == VisualState::Drawing) ResetStroke(window, *state);
        if (!Configure(window, *state, LOWORD(wp), reinterpret_cast<const RECT*>(lp))) EndDialog(window, -1);
        else if (state->visual == VisualState::Drawing) ResetStroke(window, *state);
        return TRUE;

    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
        if (state->visual == VisualState::Drawing) ResetStroke(window, *state);
        if (!Configure(window, *state, GetDpiForWindow(window), nullptr)) EndDialog(window, -1);
        else if (state->visual == VisualState::Drawing) ResetStroke(window, *state);
        return TRUE;

    case WM_CTLCOLORSTATIC:
        if (reinterpret_cast<HWND>(lp) == state->status) {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, state->highContrast ? GetSysColor(COLOR_WINDOWTEXT) : RGB(210, 218, 235));
            SetBkColor(dc, state->highContrast ? GetSysColor(COLOR_WINDOW) : RGB(18, 22, 30));
            return reinterpret_cast<INT_PTR>(state->background);
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && state->visual == VisualState::Drawing) ResetStroke(window, *state);
        break;

    case WM_CLOSE:
        state->pattern.Clear();
        if (GetCapture() == window) ReleaseCapture();
        EndDialog(window, IDCANCEL);
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
            state->pattern.Clear(); EndDialog(window, -1); return TRUE;
        }
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
        if (!oldBmp || oldBmp == HGDI_ERROR) {
            DeleteObject(memBmp); DeleteDC(memDC); EndPaint(window, &paint);
            state->pattern.Clear(); EndDialog(window, -1); return TRUE;
        }

        // 1) Fill outside with TransparentKey (outer margin)
        HBRUSH keyBrush = CreateSolidBrush(TransparentKey);
        const bool filled = keyBrush && FillRect(memDC, &physical, keyBrush);
        if (keyBrush) DeleteObject(keyBrush);
        if (!filled) {
            SelectObject(memDC, oldBmp); DeleteObject(memBmp); DeleteDC(memDC); EndPaint(window, &paint);
            state->pattern.Clear(); EndDialog(window, -1); return TRUE;
        }

        {
            Gdiplus::Graphics g(memDC);
            g.ScaleTransform(static_cast<float>(state->scale), static_cast<float>(state->scale));
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            // Draw the existing rounded card, using system colors in high contrast.
            Gdiplus::GraphicsPath cardPath;
            AddRoundedRect(cardPath, Gdiplus::RectF(6.0f, 6.0f, rect.right - 12.0f, rect.bottom - 12.0f), 20.0f);

            Gdiplus::SolidBrush cardBgBrush(Palette(*state, Gdiplus::Color(255, 18, 22, 30), COLOR_WINDOW));
            g.FillPath(&cardBgBrush, &cardPath);

            Gdiplus::Pen cardBorderPen(Palette(*state, Gdiplus::Color(160, 56, 64, 82), COLOR_WINDOWTEXT), 1.0f);
            g.DrawPath(&cardBorderPen, &cardPath);

            // ── Colors by state ──────────────────────────────────────────────
            Gdiplus::Color lineColor(255, 0, 170, 255);
            Gdiplus::Color ringColor(255, 0, 170, 255);
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
            Gdiplus::Pen linePen(lineColor, 5.0f);
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
                    float rOut = 14.0f;
                    Gdiplus::SolidBrush db(discBg);
                    g.FillEllipse(&db, p.x - rOut, p.y - rOut, 2.0f * rOut, 2.0f * rOut);
                    Gdiplus::Pen rp(ringColor, 2.5f);
                    g.DrawEllipse(&rp, p.x - rOut, p.y - rOut, 2.0f * rOut, 2.0f * rOut);

                    float rIn = 5.5f;
                    Gdiplus::SolidBrush cb(coreColor);
                    g.FillEllipse(&cb, p.x - rIn, p.y - rIn, 2.0f * rIn, 2.0f * rIn);
                } else if (hovered) {
                    // Hovered dot: cyan accent ring
                    float rOut = 13.5f;
                    Gdiplus::SolidBrush db(Palette(*state, Gdiplus::Color(255, 18, 48, 88), COLOR_WINDOW));
                    g.FillEllipse(&db, p.x - rOut, p.y - rOut, 2.0f * rOut, 2.0f * rOut);
                    Gdiplus::Pen hp(Palette(*state, Gdiplus::Color(255, 0, 160, 255), COLOR_HIGHLIGHT), 2.0f);
                    g.DrawEllipse(&hp, p.x - rOut, p.y - rOut, 2.0f * rOut, 2.0f * rOut);

                    float rIn = 4.5f;
                    Gdiplus::SolidBrush cb(Palette(*state, Gdiplus::Color(255, 210, 235, 255), COLOR_WINDOWTEXT));
                    g.FillEllipse(&cb, p.x - rIn, p.y - rIn, 2.0f * rIn, 2.0f * rIn);
                } else {
                    // Idle dot: clean, smooth solid dark circle
                    float r = 12.0f;
                    Gdiplus::SolidBrush dotBrush(Palette(*state, Gdiplus::Color(255, 34, 38, 50), COLOR_WINDOW));
                    g.FillEllipse(&dotBrush, p.x - r, p.y - r, 2.0f * r, 2.0f * r);

                    Gdiplus::Pen dotRim(Palette(*state, Gdiplus::Color(255, 108, 122, 148), COLOR_WINDOWTEXT), 1.0f);
                    g.DrawEllipse(&dotRim, p.x - r, p.y - r, 2.0f * r, 2.0f * r);

                    // Small subtle pearl center pip
                    float rPip = 2.5f;
                    Gdiplus::SolidBrush pipBrush(Palette(*state, Gdiplus::Color(255, 175, 188, 211), COLOR_WINDOWTEXT));
                    g.FillEllipse(&pipBrush, p.x - rPip, p.y - rPip, 2.0f * rPip, 2.0f * rPip);
                }
            }

        }

        // Blit double buffer to screen
        const BOOL copied = BitBlt(hdc, 0, 0, physical.right, physical.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        EndPaint(window, &paint);
        if (!copied) { state->pattern.Clear(); EndDialog(window, -1); }
        return TRUE;
    }
    }
    return FALSE;
}

} // anon namespace

HRESULT DrawPatternInteractive(
    HINSTANCE instance,
    HWND owner,
    bool hideTrail,
    const PatternVerifier& verifier,
    Pattern& output,
    const RetryDelay& retryDelay) noexcept
{
    output.Clear();

    // Initialize GDI+ for anti-aliasing
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok) return E_FAIL;

    alignas(DWORD) BYTE buffer[sizeof(DLGTEMPLATE) + 3 * sizeof(WORD)]{};
    auto* dialog = reinterpret_cast<DLGTEMPLATE*>(buffer);
    dialog->style = WS_POPUP | WS_CLIPCHILDREN;
    dialog->cx = 200;
    dialog->cy = 200;
    State state;
    state.hide = hideTrail;
    try { state.verifier = verifier; state.retryDelay = retryDelay; }
    catch (...) { Gdiplus::GdiplusShutdown(gdiplusToken); return E_OUTOFMEMORY; }
    const INT_PTR result = DialogBoxIndirectParamW(instance, dialog, owner, Dialog, reinterpret_cast<LPARAM>(&state));

    Gdiplus::GdiplusShutdown(gdiplusToken);

    if (result == IDOK && state.pattern.Valid()) {
        output = state.pattern;
        state.pattern.Clear();
        return S_OK;
    }
    state.pattern.Clear();
    if (result == IDCANCEL) return S_FALSE;
    return E_FAIL;
}

HRESULT DrawPattern(HINSTANCE instance, HWND owner, bool hideTrail, Pattern& output) noexcept {
    return DrawPatternInteractive(instance, owner, hideTrail, nullptr, output);
}

} // namespace patlock
