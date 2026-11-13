#include "pattern_window.h"
#include "arming_gate.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <strsafe.h>

namespace patlock {
namespace {

// ── Visual Palette (Exact Match to User Mockup) ──────────────────────────────
// 100% transparent color key: the blurred lock-screen wallpaper shows through everywhere.
// No rectangular container, no card, no borders. Just clean floating dots.

constexpr COLORREF TransparentKey = RGB(1, 1, 1);

// Idle dots: dark slate circular discs floating directly on the wallpaper
constexpr COLORREF DotDiscBg     = RGB(22, 26, 35);
constexpr COLORREF DotDiscRim    = RGB(42, 48, 62);
constexpr COLORREF DotCenter     = RGB(72, 80, 100);
constexpr int      DotOuterR     = 13;
constexpr int      DotInnerR     = 4;

// Hovered dot
constexpr COLORREF HoverDiscBg   = RGB(18, 48, 88);
constexpr COLORREF HoverDiscRim  = RGB(0, 160, 255);
constexpr COLORREF HoverCenter   = RGB(200, 230, 255);
constexpr int      HoverOuterR   = 15;
constexpr int      HoverInnerR   = 5;

// Visited / Connected dot
constexpr COLORREF ActiveDiscBg  = RGB(12, 48, 95);
constexpr COLORREF ActiveDiscRim = RGB(0, 170, 255);
constexpr COLORREF ActiveCenter  = RGB(255, 255, 255);
constexpr int      ActiveOuterR  = 15;
constexpr int      ActiveInnerR  = 6;

// Line
constexpr COLORREF LineDraw      = RGB(0, 170, 255);
constexpr int      LineWidth     = 5;

// Success
constexpr COLORREF SuccessLine   = RGB(46, 213, 115);
constexpr COLORREF SuccessRim    = RGB(46, 213, 115);
constexpr COLORREF SuccessBg     = RGB(18, 65, 40);
constexpr COLORREF SuccessCore   = RGB(235, 255, 240);

// Error
constexpr COLORREF ErrorLine     = RGB(255, 75, 75);
constexpr COLORREF ErrorRim      = RGB(255, 75, 75);
constexpr COLORREF ErrorBg       = RGB(70, 20, 25);
constexpr COLORREF ErrorCore     = RGB(255, 225, 230);

// Layout & Dimensions
constexpr int WindowW     = 280;
constexpr int WindowH     = 280;
constexpr int GridSpacing = 64;
constexpr int GridTopY    = 58;    // Center Y of row 0
constexpr int HitRadius   = 28;

RECT SigninLinkRect(int windowWidth) noexcept {
    const int linkWidth = 180;
    const int left = (windowWidth - linkWidth) / 2;
    return {left, 228, left + linkWidth, 256};
}

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
    bool hoverLink = false;
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
    return {startX, GridTopY, GridSpacing, DotOuterR, HitRadius};
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
            SetTimer(window, 5, 1000, nullptr);
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
            SetTimer(window, 4, 120, nullptr);
        } else if (result == VerifyResult::Lockout) {
            state.visual = VisualState::ErrorLockout;
            InvalidateRect(window, nullptr, FALSE);
            SetTimer(window, 6, 1200, nullptr);
        } else {
            // Wrong pattern: flash red for 800ms, then reset stroke for instant retry
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

void DrawDisc(HDC dc, int cx, int cy, int r, COLORREF fill, COLORREF rim, int rimWidth = 1) noexcept {
    HPEN pen = CreatePen(PS_SOLID, rimWidth, rim);
    HBRUSH brush = CreateSolidBrush(fill);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

// ── Dialog Procedure ─────────────────────────────────────────────────────────

INT_PTR CALLBACK Dialog(HWND window, UINT message, WPARAM wp, LPARAM lp) noexcept {
    State* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<State*>(lp);
        SetWindowLongPtrW(window, DWLP_USER, lp);
        SetWindowTextW(window, L"Patlock");

        // 100% Transparent layered window
        LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        SetWindowLongPtrW(window, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
        SetLayeredWindowAttributes(window, TransparentKey, 0, LWA_COLORKEY);

        // Position: centered horizontally, placed 45px below center to clear avatar & username
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int posX = (screenWidth - WindowW) / 2;
        int posY = screenHeight / 2 + 45;
        SetWindowPos(window, HWND_TOP, posX, posY, WindowW, WindowH, SWP_SHOWWINDOW);

        const POINT cur = CursorClient(window);
        const Layout grid = Grid(window);
        state->gate.Disarm(cur.x, cur.y, DotAt(window, cur, grid.hitRadius) >= 0);
        state->pendingStartDot = -1;
        state->hoverLink = false;

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
            if (state->visual == VisualState::ErrorWrong || state->visual == VisualState::ErrorLockout) {
                ResetStroke(window, *state);
            }
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

    case WM_SETCURSOR: {
        const POINT cur = CursorClient(window);
        const RECT linkRect = SigninLinkRect(WindowW);
        if (PtInRect(&linkRect, cur)) {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        const POINT current = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};

        // Click on "Other sign-in options" text link -> close grid immediately
        const RECT linkRect = SigninLinkRect(WindowW);
        if (PtInRect(&linkRect, current)) {
            state->pattern.Clear();
            ReleaseCapture();
            EndDialog(window, IDCANCEL);
            return TRUE;
        }

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

        // Hover tracking for "Other sign-in options" link
        const RECT linkRect = SigninLinkRect(WindowW);
        const bool onLink = PtInRect(&linkRect, current) != FALSE;
        if (onLink != state->hoverLink) {
            state->hoverLink = onLink;
            InvalidateRect(window, &linkRect, FALSE);
        }

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
                    SetTimer(window, 7, 120, nullptr);
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

    // ── Painting (100% Transparent Wallpaper Integration) ───────────────────
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC hdc = BeginPaint(window, &paint);
        RECT rect{};
        GetClientRect(window, &rect);

        // Double buffer for 60fps flicker-free rendering
        HDC dc = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
        HGDIOBJ oldBmp = SelectObject(dc, memBmp);

        // 1) Fill entire canvas with transparent color-key (wallpaper shows through cleanly!)
        HBRUSH keyBrush = CreateSolidBrush(TransparentKey);
        FillRect(dc, &rect, keyBrush);
        DeleteObject(keyBrush);
        SetBkMode(dc, TRANSPARENT);

        // ── Status Text & Colors ─────────────────────────────────────────────
        COLORREF lineColor = LineDraw;
        COLORREF ringColor = ActiveDiscRim;
        COLORREF discBg    = ActiveDiscBg;
        COLORREF coreColor = ActiveCenter;

        wchar_t statusMsg[64] = L"";
        COLORREF statusColor = RGB(170, 185, 210);
        bool showStatus = false;

        if (state->visual == VisualState::Drawing) {
            if (state->pattern.count > 0) {
                if (state->pattern.count < 5) {
                    StringCchPrintfW(statusMsg, ARRAYSIZE(statusMsg), L"%zu / 5 dots", state->pattern.count);
                    statusColor = RGB(120, 205, 255);
                } else {
                    StringCchPrintfW(statusMsg, ARRAYSIZE(statusMsg), L"%zu dots connected", state->pattern.count);
                    statusColor = RGB(70, 225, 130);
                }
                showStatus = true;
            } else if (!state->gate.Armed()) {
                StringCchCopyW(statusMsg, ARRAYSIZE(statusMsg), L"Move pointer away to start");
                statusColor = RGB(255, 195, 80);
                showStatus = true;
            }
        } else if (state->visual == VisualState::Success) {
            StringCchCopyW(statusMsg, ARRAYSIZE(statusMsg), L"Signing in...");
            statusColor = SuccessLine;
            lineColor = SuccessLine;
            ringColor = SuccessRim;
            discBg = SuccessBg;
            coreColor = SuccessCore;
            showStatus = true;
        } else if (state->visual == VisualState::ErrorWrong) {
            StringCchCopyW(statusMsg, ARRAYSIZE(statusMsg), L"Incorrect pattern");
            statusColor = ErrorLine;
            lineColor = ErrorLine;
            ringColor = ErrorRim;
            discBg = ErrorBg;
            coreColor = ErrorCore;
            showStatus = true;
        } else if (state->visual == VisualState::ErrorTooShort) {
            StringCchCopyW(statusMsg, ARRAYSIZE(statusMsg), L"Connect at least 5 dots");
            statusColor = ErrorLine;
            lineColor = ErrorLine;
            ringColor = ErrorRim;
            discBg = ErrorBg;
            coreColor = ErrorCore;
            showStatus = true;
        } else if (state->visual == VisualState::ErrorLockout) {
            StringCchCopyW(statusMsg, ARRAYSIZE(statusMsg), L"Locked out. Wait a moment.");
            statusColor = ErrorLine;
            lineColor = ErrorLine;
            ringColor = ErrorRim;
            discBg = ErrorBg;
            coreColor = ErrorCore;
            showStatus = true;
        }

        // ── Render Top Status Text ───────────────────────────────────────────
        if (showStatus) {
            HFONT statusFont = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            HGDIOBJ oldF = SelectObject(dc, statusFont);
            SetTextColor(dc, statusColor);
            RECT statusRect = {0, 14, rect.right, 38};
            DrawTextW(dc, statusMsg, -1, &statusRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            SelectObject(dc, oldF);
            DeleteObject(statusFont);
        }

        const Layout grid = Grid(window);

        // ── Connecting Lines with Live Rubber-Band ────────────────────────────
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
            if (state->dragging && state->visual == VisualState::Drawing) {
                LineTo(dc, state->previous.x, state->previous.y);
            }
        }

        if (prevPen) SelectObject(dc, prevPen);
        if (linePen) DeleteObject(linePen);

        // ── Render 9 Floating Dark Discs ─────────────────────────────────────
        for (BYTE i = 0; i < 9; ++i) {
            const POINT p = Dot(grid, i);
            const bool visited = !state->hide && state->pattern.Contains(i);
            const bool hovered = (!visited && state->hoverDot == i && state->visual == VisualState::Drawing);

            if (visited) {
                // Connected dot: glowing accent ring + white center
                DrawDisc(dc, p.x, p.y, ActiveOuterR, discBg, ringColor, 2);
                DrawDisc(dc, p.x, p.y, ActiveInnerR, coreColor, coreColor);
            } else if (hovered) {
                // Hovered dot: highlight ring
                DrawDisc(dc, p.x, p.y, HoverOuterR, HoverDiscBg, HoverDiscRim, 2);
                DrawDisc(dc, p.x, p.y, HoverInnerR, HoverCenter, HoverCenter);
            } else {
                // Idle dot: clean solid dark disc floating on wallpaper (Mockup match!)
                DrawDisc(dc, p.x, p.y, DotOuterR, DotDiscBg, DotDiscRim, 1);
                DrawDisc(dc, p.x, p.y, DotInnerR, DotCenter, DotCenter);
            }
        }

        // ── Render "Other sign-in options" Link ───────────────────────────────
        RECT linkRect = SigninLinkRect(rect.right);
        HFONT linkFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HGDIOBJ oldF = SelectObject(dc, linkFont);
        SetTextColor(dc, state->hoverLink ? RGB(255, 255, 255) : RGB(220, 226, 238));
        DrawTextW(dc, L"Other sign-in options", -1, &linkRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        SelectObject(dc, oldF);
        DeleteObject(linkFont);

        // Blit to screen
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
