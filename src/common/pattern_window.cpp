#include "pattern_window.h"
#include "arming_gate.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <strsafe.h>

namespace patlock {
namespace {

constexpr COLORREF TransparentKey = RGB(2, 4, 6);

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
    ArmingGate gate;            // a hover-start is allowed only after deliberate pointer movement
    int pendingStartDot = -1;   // Dot waiting for 150ms hover confirmation
};

struct Layout {
    int x;
    int y;
    int spacing;
    int radius;
    int hitRadius;
};

RECT SigninButtonRect(int windowWidth) noexcept {
    const int btnWidth = 176;
    const int left = (windowWidth - btnWidth) / 2;
    return {left, 222, left + btnWidth, 248};
}

Layout Grid(HWND window) noexcept {
    RECT r{};
    GetClientRect(window, &r);
    const int availableWidth = static_cast<int>(r.right);
    const int spacing = 68;
    const int startX = availableWidth / 2 - spacing;
    const int startY = 44;
    return {startX, startY, spacing, 14, 28};
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

// Cursor position in client coordinates
POINT CursorClient(HWND window) noexcept {
    POINT cur{-10000, -10000};
    if (GetCursorPos(&cur)) ScreenToClient(window, &cur);
    return cur;
}

// The ONE place that returns the window to "waiting for a deliberate start".
// Used on open, Esc, wrong pattern, too-short stroke, lost capture and empty strokes.
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
    const double hitRadius = static_cast<double>(grid.hitRadius);

    for (BYTE i = 0; i < 9; ++i) {
        const POINT p = Dot(grid, i);
        double t = length > 0 ? ((p.x - state.previous.x) * dx + (p.y - state.previous.y) * dy) / length : 0;
        t = (std::max)(0.0, (std::min)(1.0, t));
        const double ex = state.previous.x + t * dx - p.x;
        const double ey = state.previous.y + t * dy - p.y;
        if (ex * ex + ey * ey <= hitRadius * hitRadius) hits[count++] = {t, i};
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
            SetTimer(window, 5, 1200, nullptr);
        } else {
            // A click or movement that connected no dots must not leave a ghost stroke or mouse capture behind.
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
            // Wrong pattern: flash red for 800ms, then reset pattern and disarm gate until deliberate movement
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

INT_PTR CALLBACK Dialog(HWND window, UINT message, WPARAM wp, LPARAM lp) noexcept {
    State* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<State*>(lp);
        SetWindowLongPtrW(window, DWLP_USER, lp);
        SetWindowTextW(window, L"Patlock - Pattern Sign-in");

        // Transparent layered window: TransparentKey is 100% see-through so the lock-screen wallpaper shows through
        LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        SetWindowLongPtrW(window, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
        SetLayeredWindowAttributes(window, TransparentKey, 0, LWA_COLORKEY);

        // Position window: centered horizontally, placed cleanly below username 'testuser' without any overlap!
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int width = 280;
        int height = 280;
        int posX = (screenWidth - width) / 2;
        // Positioned 30px below screen center to avoid covering avatar or username
        int posY = screenHeight / 2 + 30;
        SetWindowPos(window, HWND_TOP, posX, posY, width, height, SWP_SHOWWINDOW);

        // A dot that happens to be under the cursor must not start a stroke: pointer has to move away first
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
            // Security timeout or lockout close
            state->pattern.Clear();
            ReleaseCapture();
            EndDialog(window, IDCANCEL);
            return TRUE;
        }
        if (wp == 2) {
            // Auto-finish on pause
            KillTimer(window, 2);
            FinishPattern(window, *state);
            return TRUE;
        }
        if (wp == 3) {
            // Inactivity timeout
            KillTimer(window, 3);
            FinishPattern(window, *state);
            return TRUE;
        }
        if (wp == 4) {
            // Success animation complete -> close dialog with IDOK
            KillTimer(window, 4);
            EndDialog(window, IDOK);
            return TRUE;
        }
        if (wp == 5) {
            // Reset "too short" error state back to normal
            KillTimer(window, 5);
            if (state->visual == VisualState::ErrorTooShort) ResetStroke(window, *state);
            return TRUE;
        }
        if (wp == 6) {
            // Red flash error display finished -> reset pattern and allow instant redraw
            KillTimer(window, 6);
            if (state->visual == VisualState::ErrorWrong) ResetStroke(window, *state);
            return TRUE;
        }
        if (wp == 7) {
            // 150ms intentional hover confirmed on starting dot
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
        return TRUE; // Handled cleanly in WM_PAINT

    case WM_LBUTTONDOWN: {
        const POINT current = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};

        // Check if user clicked the "Other sign-in options" button
        RECT btnRect = SigninButtonRect(280);
        if (PtInRect(&btnRect, current)) {
            state->pattern.Clear();
            ReleaseCapture();
            EndDialog(window, IDCANCEL);
            return TRUE;
        }

        if (state->visual != VisualState::Drawing) return TRUE;

        // Tapping on last dot finishes pattern if valid
        if (state->dragging && state->pattern.Valid()) {
            FinishPattern(window, *state);
            return TRUE;
        }

        // A click starts a stroke only when it lands ON a dot. Clicking empty space does nothing.
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
            // Only a deliberate approach (armed gate) can start a stroke by hovering
            if (armed && dot >= 0) {
                if (state->pendingStartDot != dot) {
                    state->pendingStartDot = dot;
                    // Require 150ms intentional pause on the starting dot to arm hover drawing
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
                // Pause for 350ms on final dot auto-completes and verifies
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
            // When Esc is pressed:
            // If user has drawn any dots or is dragging: RESET current pattern and stay in pattern mode!
            if (state->pattern.count > 0 || state->dragging) {
                ResetStroke(window, *state);
                return TRUE;
            }
            // If pattern is already empty: close the grid and return to tile so user can click other sign-in options
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

    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC hdc = BeginPaint(window, &paint);
        RECT rect{};
        GetClientRect(window, &rect);

        // Double buffer for silky smooth 60fps rendering without any flicker
        HDC dc = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
        HGDIOBJ oldBmp = SelectObject(dc, memBmp);

        // 100% transparent color key background: allows the blurred lock-screen wallpaper to show through completely!
        HBRUSH bgBrush = CreateSolidBrush(TransparentKey);
        FillRect(dc, &rect, bgBrush);
        DeleteObject(bgBrush);
        SetBkMode(dc, TRANSPARENT);

        // Clean typography (Segoe UI)
        HFONT subFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT btnFont = CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT hintFont = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        wchar_t dynamicSub[64] = L"Glide across dots to unlock";
        COLORREF subColor = RGB(175, 185, 200);
        COLORREF lineColor = RGB(0, 180, 255);       // Electric cyan-blue
        COLORREF ringColor = RGB(0, 180, 255);
        COLORREF ringBgColor = RGB(16, 45, 80);
        COLORREF coreColor = RGB(235, 248, 255);

        if (state->visual == VisualState::Drawing) {
            if (state->pattern.count > 0) {
                StringCchPrintfW(dynamicSub, ARRAYSIZE(dynamicSub),
                    state->pattern.count < 5 ? L"%zu / 5 dots connected" : L"%zu dots connected - pause to sign in",
                    state->pattern.count);
                subColor = state->pattern.count < 5 ? RGB(100, 215, 255) : RGB(46, 213, 115);
            } else if (!state->gate.Armed()) {
                StringCchCopyW(dynamicSub, ARRAYSIZE(dynamicSub), L"Move the pointer away, then start");
                subColor = RGB(255, 200, 90);
            }
        } else if (state->visual == VisualState::Success) {
            StringCchCopyW(dynamicSub, ARRAYSIZE(dynamicSub), L"Signing in...");
            subColor = RGB(46, 213, 115);            // Emerald green
            lineColor = RGB(46, 213, 115);
            ringColor = RGB(46, 213, 115);
            ringBgColor = RGB(20, 60, 35);
            coreColor = RGB(230, 255, 235);
        } else if (state->visual == VisualState::ErrorWrong) {
            StringCchCopyW(dynamicSub, ARRAYSIZE(dynamicSub), L"Incorrect pattern");
            subColor = RGB(255, 85, 85);             // Vivid coral-red
            lineColor = RGB(255, 75, 75);
            ringColor = RGB(255, 75, 75);
            ringBgColor = RGB(70, 22, 28);
            coreColor = RGB(255, 225, 230);
        } else if (state->visual == VisualState::ErrorTooShort) {
            StringCchCopyW(dynamicSub, ARRAYSIZE(dynamicSub), L"Connect at least 5 dots");
            subColor = RGB(255, 85, 85);
            lineColor = RGB(255, 75, 75);
            ringColor = RGB(255, 75, 75);
            ringBgColor = RGB(70, 22, 28);
            coreColor = RGB(255, 225, 230);
        } else if (state->visual == VisualState::ErrorLockout) {
            StringCchCopyW(dynamicSub, ARRAYSIZE(dynamicSub), L"Locked out. Use Password.");
            subColor = RGB(255, 85, 85);
            lineColor = RGB(255, 75, 75);
            ringColor = RGB(255, 75, 75);
            ringBgColor = RGB(70, 22, 28);
            coreColor = RGB(255, 225, 230);
        }

        // Render Subtitle cleanly right above dots
        RECT subRect{0, 10, rect.right, 32};
        HGDIOBJ oldFont = SelectObject(dc, subFont);
        SetTextColor(dc, subColor);
        DrawTextW(dc, dynamicSub, -1, &subRect, DT_CENTER | DT_SINGLELINE);

        const Layout grid = Grid(window);

        // Render Connecting Lines with rounded caps and rounded joins
        LOGBRUSH lb{BS_SOLID, lineColor, 0};
        HPEN linePen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND, 6, &lb, 0, nullptr);
        HGDIOBJ prevPen = linePen ? SelectObject(dc, linePen) : nullptr;

        if (!state->hide && state->pattern.count) {
            POINT p = Dot(grid, state->pattern.dots[0]);
            MoveToEx(dc, p.x, p.y, nullptr);
            for (size_t i = 1; i < state->pattern.count; ++i) {
                p = Dot(grid, state->pattern.dots[i]);
                LineTo(dc, p.x, p.y);
            }
            // LIVE RUBBER-BAND LINE following cursor tip
            if (state->dragging && state->visual == VisualState::Drawing) {
                LineTo(dc, state->previous.x, state->previous.y);
            }
        }

        // Render 9 Grid Dots: dark circular discs matching user's reference mockup
        for (BYTE i = 0; i < 9; ++i) {
            const POINT p = Dot(grid, i);
            const bool visited = !state->hide && state->pattern.Contains(i);
            const bool hovered = (!visited && state->hoverDot == i && state->visual == VisualState::Drawing);

            if (visited) {
                // Active Visited Dot: Outer glow halo + solid bright core
                HPEN vPen = CreatePen(PS_SOLID, 2, ringColor);
                HBRUSH vBrush = CreateSolidBrush(ringBgColor);
                HGDIOBJ oP = SelectObject(dc, vPen);
                HGDIOBJ oB = SelectObject(dc, vBrush);
                Ellipse(dc, p.x - 18, p.y - 18, p.x + 18, p.y + 18);

                HBRUSH cBrush = CreateSolidBrush(coreColor);
                SelectObject(dc, cBrush);
                Ellipse(dc, p.x - 6, p.y - 6, p.x + 6, p.y + 6);

                SelectObject(dc, oP);
                SelectObject(dc, oB);
                DeleteObject(vPen);
                DeleteObject(vBrush);
                DeleteObject(cBrush);
            } else if (hovered) {
                // Hovered Dot: subtle cyan highlight ring
                HPEN hPen = CreatePen(PS_SOLID, 2, RGB(0, 180, 255));
                HBRUSH hBrush = CreateSolidBrush(RGB(18, 48, 82));
                HGDIOBJ oP = SelectObject(dc, hPen);
                HGDIOBJ oB = SelectObject(dc, hBrush);
                Ellipse(dc, p.x - 16, p.y - 16, p.x + 16, p.y + 16);

                HBRUSH cBrush = CreateSolidBrush(RGB(220, 240, 255));
                SelectObject(dc, cBrush);
                Ellipse(dc, p.x - 5, p.y - 5, p.x + 5, p.y + 5);

                SelectObject(dc, oP);
                SelectObject(dc, oB);
                DeleteObject(hPen);
                DeleteObject(hBrush);
                DeleteObject(cBrush);
            } else {
                // Inactive Dot: dark circular backing disc (stands out crisply against desktop lock screen wallpaper)
                HPEN uPen = CreatePen(PS_SOLID, 1, RGB(70, 78, 96));
                HBRUSH uBrush = CreateSolidBrush(RGB(44, 48, 60));
                HGDIOBJ oP = SelectObject(dc, uPen);
                HGDIOBJ oB = SelectObject(dc, uBrush);
                Ellipse(dc, p.x - 14, p.y - 14, p.x + 14, p.y + 14);

                HBRUSH cBrush = CreateSolidBrush(RGB(210, 218, 235));
                SelectObject(dc, cBrush);
                Ellipse(dc, p.x - 4, p.y - 4, p.x + 4, p.y + 4);

                SelectObject(dc, oP);
                SelectObject(dc, oB);
                DeleteObject(uPen);
                DeleteObject(uBrush);
                DeleteObject(cBrush);
            }
        }

        // Render "Other sign-in options" button pill at bottom
        RECT btnRect = SigninButtonRect(rect.right);
        HPEN btnPen = CreatePen(PS_SOLID, 1, RGB(72, 80, 100));
        HBRUSH btnBrush = CreateSolidBrush(RGB(28, 32, 44));
        HGDIOBJ oP = SelectObject(dc, btnPen);
        HGDIOBJ oB = SelectObject(dc, btnBrush);
        RoundRect(dc, btnRect.left, btnRect.top, btnRect.right, btnRect.bottom, 14, 14);
        SelectObject(dc, oP);
        SelectObject(dc, oB);
        DeleteObject(btnPen);
        DeleteObject(btnBrush);

        SelectObject(dc, btnFont);
        SetTextColor(dc, RGB(235, 240, 252));
        DrawTextW(dc, L"Other sign-in options", -1, &btnRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Render Escape hint at bottom
        RECT footerRect{0, rect.bottom - 22, rect.right, rect.bottom - 4};
        SelectObject(dc, hintFont);
        SetTextColor(dc, RGB(155, 162, 178));
        DrawTextW(dc, L"Press Esc to reset pattern", -1, &footerRect, DT_CENTER | DT_SINGLELINE);

        if (prevPen) SelectObject(dc, prevPen);
        if (linePen) DeleteObject(linePen);
        SelectObject(dc, oldFont);
        DeleteObject(subFont);
        DeleteObject(btnFont);
        DeleteObject(hintFont);

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
    // Frameless popup window
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
