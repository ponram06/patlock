#include "pattern_window.h"
#include "arming_gate.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <strsafe.h>

namespace patlock {
namespace {

// ── Visual Design Constants ──────────────────────────────────────────────────
// Dark frosted-glass card floating over the lock-screen / desktop wallpaper.
// The outer margins use a color-key for 100% transparency; the inner card is
// opaque dark glass so dots and text are always perfectly readable.

constexpr COLORREF TransparentKey = RGB(1, 1, 1);

// Card palette
constexpr COLORREF CardBg        = RGB(16, 18, 26);
constexpr COLORREF CardBorder    = RGB(50, 56, 72);
constexpr int      CardMargin    = 12;
constexpr int      CardRadius    = 20;

// Dot palette (idle)
constexpr COLORREF DotDiscBg     = RGB(36, 40, 56);
constexpr COLORREF DotDiscRim    = RGB(62, 68, 88);
constexpr COLORREF DotCenter     = RGB(170, 180, 200);
constexpr int      DotOuter      = 17;
constexpr int      DotInner      = 5;

// Dot palette (hovered)
constexpr COLORREF HoverDiscBg   = RGB(22, 52, 95);
constexpr COLORREF HoverDiscRim  = RGB(55, 155, 255);
constexpr COLORREF HoverCenter   = RGB(200, 230, 255);
constexpr int      HoverOuter    = 19;
constexpr int      HoverInner    = 6;

// Dot palette (visited / active)
constexpr COLORREF ActiveDiscBg  = RGB(18, 55, 105);
constexpr COLORREF ActiveDiscRim = RGB(50, 170, 255);
constexpr COLORREF ActiveCenter  = RGB(255, 255, 255);
constexpr int      ActiveOuter   = 19;
constexpr int      ActiveInner   = 7;

// Line
constexpr COLORREF LineDraw      = RGB(50, 170, 255);
constexpr int      LineWidth     = 5;

// Success
constexpr COLORREF SuccessLine   = RGB(40, 200, 100);
constexpr COLORREF SuccessRim    = RGB(40, 200, 100);
constexpr COLORREF SuccessBg     = RGB(18, 60, 38);
constexpr COLORREF SuccessCore   = RGB(230, 255, 240);

// Error
constexpr COLORREF ErrorLine     = RGB(235, 65, 65);
constexpr COLORREF ErrorRim      = RGB(235, 65, 65);
constexpr COLORREF ErrorBg       = RGB(65, 20, 25);
constexpr COLORREF ErrorCore     = RGB(255, 220, 225);

// Text
constexpr COLORREF TextIdle      = RGB(155, 165, 185);
constexpr COLORREF TextCount     = RGB(90, 200, 255);
constexpr COLORREF TextReady     = RGB(40, 200, 100);
constexpr COLORREF TextWarn      = RGB(255, 195, 80);
constexpr COLORREF TextError     = RGB(235, 75, 75);
constexpr COLORREF TextHint      = RGB(110, 118, 140);

// Layout
constexpr int WindowW = 360;
constexpr int WindowH = 350;
constexpr int GridSpacing = 82;
constexpr int GridTop = 65;       // Y of first row of dots inside the window
constexpr int HitRadius = 30;

// ── State ────────────────────────────────────────────────────────────────────

enum class VisualState {
    Drawing,
    Success,
    ErrorWrong,
    ErrorLockout,
    ErrorTooShort
};

struct State {
    Pattern pattern;
    bool dragging = false;
    bool hide = false;
    VisualState visual = VisualState::Drawing;
    POINT previous{};
    PatternVerifier verifier = nullptr;
    int hoverDot = -1;
    ArmingGate gate;
    int pendingStartDot = -1;
};

struct Layout {
    int x;
    int y;
    int spacing;
    int radius;
    int hitRadius;
};

// ── Helpers ──────────────────────────────────────────────────────────────────

Layout Grid(HWND window) noexcept {
    RECT r{};
    GetClientRect(window, &r);
    const int cw = static_cast<int>(r.right);
    const int startX = cw / 2 - GridSpacing;
    return {startX, GridTop, GridSpacing, DotOuter, HitRadius};
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

POINT CursorClient(HWND window) noexcept {
    POINT cur{-10000, -10000};
    if (GetCursorPos(&cur)) ScreenToClient(window, &cur);
    return cur;
}

// ── Stroke Reset ─────────────────────────────────────────────────────────────

void ResetStroke(HWND window, State& state) noexcept {
    KillTimer(window, 2);
    KillTimer(window, 3);
    KillTimer(window, 5);
    KillTimer(window, 6);
    KillTimer(window, 7);
    state.pattern.Clear();
    state.dragging = false;
    state.visual = VisualState::Drawing;
    state.hoverDot = -1;
    state.pendingStartDot = -1;
    ReleaseCapture();
    const POINT cur = CursorClient(window);
    const Layout grid = Grid(window);
    state.gate.Disarm(cur.x, cur.y, DotAt(window, cur, grid.hitRadius) >= 0);
    InvalidateRect(window, nullptr, FALSE);
}

// ── Tracking ─────────────────────────────────────────────────────────────────

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
    InvalidateRect(window, nullptr, FALSE);
}

// ── Finish ───────────────────────────────────────────────────────────────────

void FinishPattern(HWND window, State& state) noexcept {
    if (state.visual != VisualState::Drawing) return;
    KillTimer(window, 2);
    KillTimer(window, 3);
    KillTimer(window, 7);

    if (!state.pattern.Valid()) {
        if (state.pattern.count > 0) {
            state.pattern.Clear();
            state.dragging = false;
            state.visual = VisualState::ErrorTooShort;
            ReleaseCapture();
            InvalidateRect(window, nullptr, FALSE);
            SetTimer(window, 5, 1200, nullptr);
        } else {
            ResetStroke(window, state);
        }
        return;
    }

    state.dragging = false;
    ReleaseCapture();

    if (state.verifier) {
        VerifyResult result = state.verifier(state.pattern);
        if (result == VerifyResult::Success) {
            state.visual = VisualState::Success;
            InvalidateRect(window, nullptr, FALSE);
            SetTimer(window, 4, 140, nullptr);
        } else if (result == VerifyResult::Lockout) {
            state.visual = VisualState::ErrorLockout;
            InvalidateRect(window, nullptr, FALSE);
            SetTimer(window, 1, 1800, nullptr);
        } else {
            state.visual = VisualState::ErrorWrong;
            InvalidateRect(window, nullptr, FALSE);
            SetTimer(window, 6, 800, nullptr);
        }
    } else {
        state.visual = VisualState::Success;
        InvalidateRect(window, nullptr, FALSE);
        EndDialog(window, IDOK);
    }
}

// ── GDI Helpers ──────────────────────────────────────────────────────────────

void FillCircle(HDC dc, int cx, int cy, int r, COLORREF fill, COLORREF rim) noexcept {
    HPEN pen = CreatePen(PS_SOLID, 1, rim);
    HBRUSH brush = CreateSolidBrush(fill);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void FillCircleThick(HDC dc, int cx, int cy, int r, COLORREF fill, COLORREF rim, int penW) noexcept {
    HPEN pen = CreatePen(PS_SOLID, penW, rim);
    HBRUSH brush = CreateSolidBrush(fill);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

// ── Dialog Proc ──────────────────────────────────────────────────────────────

INT_PTR CALLBACK Dialog(HWND window, UINT message, WPARAM wp, LPARAM lp) noexcept {
    State* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<State*>(lp);
        SetWindowLongPtrW(window, DWLP_USER, lp);
        SetWindowTextW(window, L"Patlock");

        // Layered window: color-key transparency so lock-screen wallpaper shows around the card
        LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        SetWindowLongPtrW(window, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
        SetLayeredWindowAttributes(window, TransparentKey, 0, LWA_COLORKEY);

        // Position: centered horizontally, below username on lock screen
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int posX = (screenWidth - WindowW) / 2;
        int posY = screenHeight / 2 + 20;
        SetWindowPos(window, HWND_TOP, posX, posY, WindowW, WindowH, SWP_SHOWWINDOW);

        // Disarm gate so cursor already resting on a dot doesn't start a stroke
        const POINT cur = CursorClient(window);
        const Layout grid = Grid(window);
        state->gate.Disarm(cur.x, cur.y, DotAt(window, cur, grid.hitRadius) >= 0);
        state->pendingStartDot = -1;

        if (!SetTimer(window, 1, 120000, nullptr)) EndDialog(window, -1);
        return TRUE;
    }
    if (!state) return FALSE;

    if (message == WM_LBUTTONDOWN || message == WM_MOUSEMOVE || message == WM_LBUTTONUP) {
        if (!SetTimer(window, 1, 120000, nullptr)) { state->pattern.Clear(); EndDialog(window, -1); return TRUE; }
    }

    switch (message) {
    case WM_TIMER:
        if (wp == 1) {
            state->pattern.Clear();
            ReleaseCapture();
            EndDialog(window, IDCANCEL);
            return TRUE;
        }
        if (wp == 2) {
            KillTimer(window, 2);
            FinishPattern(window, *state);
            return TRUE;
        }
        if (wp == 3) {
            KillTimer(window, 3);
            FinishPattern(window, *state);
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
            if (state->visual == VisualState::ErrorWrong) ResetStroke(window, *state);
            return TRUE;
        }
        if (wp == 7) {
            KillTimer(window, 7);
            if (state->pendingStartDot >= 0 && !state->dragging && state->visual == VisualState::Drawing &&
                state->gate.Armed()) {
                state->dragging = true;
                state->pattern.Clear();
                state->pattern.Add(static_cast<BYTE>(state->pendingStartDot));
                state->pendingStartDot = -1;
                state->previous = CursorClient(window);
                SetCapture(window);
                InvalidateRect(window, nullptr, FALSE);
            }
            return TRUE;
        }
        return TRUE;

    case WM_DESTROY:
        for (UINT_PTR id = 1; id <= 7; ++id) KillTimer(window, id);
        return TRUE;

    case WM_ERASEBKGND:
        return TRUE;

    case WM_LBUTTONDOWN: {
        const POINT current = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (state->visual != VisualState::Drawing) return TRUE;

        if (state->dragging && state->pattern.Valid()) {
            FinishPattern(window, *state);
            return TRUE;
        }

        const Layout grid = Grid(window);
        const int dot = DotAt(window, current, grid.hitRadius);
        if (dot < 0) return TRUE;

        state->pendingStartDot = -1;
        KillTimer(window, 7);
        state->pattern.Clear();
        state->visual = VisualState::Drawing;
        state->dragging = true;
        state->previous = current;
        SetCapture(window);
        state->pattern.Add(static_cast<BYTE>(dot));
        InvalidateRect(window, nullptr, FALSE);
        return TRUE;
    }

    case WM_MOUSEMOVE: {
        const POINT current = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (state->visual != VisualState::Drawing) return TRUE;

        const Layout grid = Grid(window);
        const int dot = DotAt(window, current, grid.hitRadius);
        state->gate.Observe(current.x, current.y);

        if (!state->dragging) {
            const bool armed = state->gate.Armed();
            state->hoverDot = armed ? dot : -1;
            if (armed && dot >= 0) {
                if (state->pendingStartDot != dot) {
                    state->pendingStartDot = dot;
                    SetTimer(window, 7, 150, nullptr);
                }
            } else {
                state->pendingStartDot = -1;
                KillTimer(window, 7);
            }
            InvalidateRect(window, nullptr, FALSE);
        } else {
            state->hoverDot = dot;
            Track(window, *state, current);
            KillTimer(window, 2);
            KillTimer(window, 3);
            if (state->pattern.Valid()) {
                SetTimer(window, 2, 350, nullptr);
            } else if (state->pattern.count > 0) {
                SetTimer(window, 3, 1800, nullptr);
            }
        }
        return TRUE;
    }

    case WM_LBUTTONUP: {
        if (state->visual != VisualState::Drawing) return TRUE;
        if (state->dragging) {
            Track(window, *state, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            FinishPattern(window, *state);
        }
        return TRUE;
    }

    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
        if (state->dragging && !state->pattern.Valid()) ResetStroke(window, *state);
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL) {
            if (state->pattern.count > 0 || state->dragging) {
                ResetStroke(window, *state);
                return TRUE;
            }
            state->pattern.Clear();
            ReleaseCapture();
            EndDialog(window, IDCANCEL);
            return TRUE;
        }
        return FALSE;

    case WM_CLOSE:
        state->pattern.Clear();
        ReleaseCapture();
        EndDialog(window, IDCANCEL);
        return TRUE;

    // ── Rendering ────────────────────────────────────────────────────────────
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC hdc = BeginPaint(window, &paint);
        RECT rect{};
        GetClientRect(window, &rect);

        // Double buffer
        HDC dc = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
        HGDIOBJ oldBmp = SelectObject(dc, memBmp);

        // 1) Fill entire window with transparent color key
        HBRUSH keyBrush = CreateSolidBrush(TransparentKey);
        FillRect(dc, &rect, keyBrush);
        DeleteObject(keyBrush);

        // 2) Draw dark floating card with rounded corners
        RECT cardRect = {CardMargin, CardMargin, rect.right - CardMargin, rect.bottom - CardMargin};
        HPEN cardPen = CreatePen(PS_SOLID, 1, CardBorder);
        HBRUSH cardBrush = CreateSolidBrush(CardBg);
        HGDIOBJ oPen = SelectObject(dc, cardPen);
        HGDIOBJ oBrush = SelectObject(dc, cardBrush);
        RoundRect(dc, cardRect.left, cardRect.top, cardRect.right, cardRect.bottom, CardRadius, CardRadius);
        SelectObject(dc, oPen);
        SelectObject(dc, oBrush);
        DeleteObject(cardPen);
        DeleteObject(cardBrush);

        SetBkMode(dc, TRANSPARENT);

        // ── Determine colors by state ────────────────────────────────────────
        COLORREF lineColor = LineDraw;
        COLORREF rimColor = ActiveDiscRim;
        COLORREF discBg = ActiveDiscBg;
        COLORREF coreColor = ActiveCenter;

        wchar_t statusText[64] = L"Glide across dots to draw";
        COLORREF statusColor = TextIdle;

        if (state->visual == VisualState::Drawing) {
            if (state->pattern.count > 0) {
                if (state->pattern.count < 5) {
                    StringCchPrintfW(statusText, ARRAYSIZE(statusText), L"%zu / 5 dots connected", state->pattern.count);
                    statusColor = TextCount;
                } else {
                    StringCchPrintfW(statusText, ARRAYSIZE(statusText), L"%zu dots \xB7 pause to sign in", state->pattern.count);
                    statusColor = TextReady;
                }
            } else if (!state->gate.Armed()) {
                StringCchCopyW(statusText, ARRAYSIZE(statusText), L"Move pointer away, then start");
                statusColor = TextWarn;
            }
        } else if (state->visual == VisualState::Success) {
            StringCchCopyW(statusText, ARRAYSIZE(statusText), L"\x2713  Signing in...");
            statusColor = TextReady;
            lineColor = SuccessLine;
            rimColor = SuccessRim;
            discBg = SuccessBg;
            coreColor = SuccessCore;
        } else if (state->visual == VisualState::ErrorWrong) {
            StringCchCopyW(statusText, ARRAYSIZE(statusText), L"\x2717  Wrong pattern");
            statusColor = TextError;
            lineColor = ErrorLine;
            rimColor = ErrorRim;
            discBg = ErrorBg;
            coreColor = ErrorCore;
        } else if (state->visual == VisualState::ErrorTooShort) {
            StringCchCopyW(statusText, ARRAYSIZE(statusText), L"Connect at least 5 dots");
            statusColor = TextError;
            lineColor = ErrorLine;
            rimColor = ErrorRim;
            discBg = ErrorBg;
            coreColor = ErrorCore;
        } else if (state->visual == VisualState::ErrorLockout) {
            StringCchCopyW(statusText, ARRAYSIZE(statusText), L"\x26A0  Locked out \xB7 use Password");
            statusColor = TextError;
            lineColor = ErrorLine;
            rimColor = ErrorRim;
            discBg = ErrorBg;
            coreColor = ErrorCore;
        }

        // ── Fonts ────────────────────────────────────────────────────────────
        HFONT statusFont = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT hintFont = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        // ── Status text ──────────────────────────────────────────────────────
        RECT statusRect = {CardMargin + 10, CardMargin + 12, rect.right - CardMargin - 10, CardMargin + 40};
        HGDIOBJ oldFont = SelectObject(dc, statusFont);
        SetTextColor(dc, statusColor);
        DrawTextW(dc, statusText, -1, &statusRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        // ── Connecting lines ─────────────────────────────────────────────────
        const Layout grid = Grid(window);
        LOGBRUSH lb{BS_SOLID, lineColor, 0};
        HPEN linePen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
            LineWidth, &lb, 0, nullptr);
        HGDIOBJ prevPen = linePen ? SelectObject(dc, linePen) : nullptr;

        if (!state->hide && state->pattern.count) {
            POINT p = Dot(grid, state->pattern.dots[0]);
            MoveToEx(dc, p.x, p.y, nullptr);
            for (size_t i = 1; i < state->pattern.count; ++i) {
                p = Dot(grid, state->pattern.dots[i]);
                LineTo(dc, p.x, p.y);
            }
            // Live rubber-band trail
            if (state->dragging && state->visual == VisualState::Drawing) {
                LineTo(dc, state->previous.x, state->previous.y);
            }
        }

        if (prevPen) SelectObject(dc, prevPen);
        if (linePen) DeleteObject(linePen);

        // ── Dots ─────────────────────────────────────────────────────────────
        for (BYTE i = 0; i < 9; ++i) {
            const POINT p = Dot(grid, i);
            const bool visited = !state->hide && state->pattern.Contains(i);
            const bool hovered = (!visited && state->hoverDot == i && state->visual == VisualState::Drawing);

            if (visited) {
                // Outer glow ring
                FillCircleThick(dc, p.x, p.y, ActiveOuter, discBg, rimColor, 2);
                // Bright center core
                FillCircle(dc, p.x, p.y, ActiveInner, coreColor, coreColor);
            } else if (hovered) {
                // Highlight ring
                FillCircleThick(dc, p.x, p.y, HoverOuter, HoverDiscBg, HoverDiscRim, 2);
                FillCircle(dc, p.x, p.y, HoverInner, HoverCenter, HoverCenter);
            } else {
                // Idle dot: subtle disc with small center
                FillCircle(dc, p.x, p.y, DotOuter, DotDiscBg, DotDiscRim);
                FillCircle(dc, p.x, p.y, DotInner, DotCenter, DotCenter);
            }
        }

        // ── Hint text ────────────────────────────────────────────────────────
        RECT hintRect = {CardMargin + 10, rect.bottom - CardMargin - 28, rect.right - CardMargin - 10, rect.bottom - CardMargin - 8};
        SelectObject(dc, hintFont);
        SetTextColor(dc, TextHint);
        DrawTextW(dc, L"Esc to reset  \xB7  draw your unlock pattern", -1, &hintRect, DT_CENTER | DT_SINGLELINE);

        SelectObject(dc, oldFont);
        DeleteObject(statusFont);
        DeleteObject(hintFont);

        // Blit
        BitBlt(hdc, 0, 0, rect.right, rect.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(dc);
        EndPaint(window, &paint);
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
    Pattern& output) noexcept
{
    output.Clear();
    alignas(DWORD) BYTE buffer[sizeof(DLGTEMPLATE) + 3 * sizeof(WORD)]{};
    auto* dialog = reinterpret_cast<DLGTEMPLATE*>(buffer);
    dialog->style = WS_POPUP;
    dialog->cx = 200;
    dialog->cy = 200;
    State state;
    state.hide = hideTrail;
    state.verifier = verifier;
    const INT_PTR result = DialogBoxIndirectParamW(instance, dialog, owner, Dialog, reinterpret_cast<LPARAM>(&state));
    if (result == IDOK && state.pattern.Valid()) {
        output = state.pattern;
        return S_OK;
    }
    if (result == IDCANCEL) return S_FALSE;
    return E_FAIL;
}

HRESULT DrawPattern(HINSTANCE instance, HWND owner, bool hideTrail, Pattern& output) noexcept {
    return DrawPatternInteractive(instance, owner, hideTrail, nullptr, output);
}

} // namespace patlock
