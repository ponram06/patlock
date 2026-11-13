#include "pattern_window.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <strsafe.h>

namespace patlock {
namespace {

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
    POINT initialCursor{};
    bool armed = false;
};

struct Layout {
    int x;
    int y;
    int spacing;
    int radius;
    int hitRadius;
};

Layout Grid(HWND window) noexcept {
    RECT r{};
    GetClientRect(window, &r);
    const int availableWidth = static_cast<int>(r.right);
    const int spacing = 84;
    const int startX = availableWidth / 2 - spacing;
    const int startY = 100;
    return {startX, startY, spacing, 14, 32};
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

    if (!state.pattern.Valid()) {
        if (state.pattern.count > 0) {
            state.pattern.Clear();
            state.dragging = false;
            state.visual = VisualState::ErrorTooShort;
            ReleaseCapture();
            InvalidateRect(window, nullptr, FALSE);
            SetTimer(window, 5, 1200, nullptr);
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
            // Allow user to see emerald confirmation for 140ms then close
            SetTimer(window, 4, 140, nullptr);
        } else if (result == VerifyResult::Lockout) {
            state.visual = VisualState::ErrorLockout;
            InvalidateRect(window, nullptr, FALSE);
            SetTimer(window, 1, 1800, nullptr);
        } else {
            // WrongPattern: flash red for 800ms then auto-reset pattern for instant retry
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

        // Enable layered window style for translucent acrylic lock-screen integration
        LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        SetWindowLongPtrW(window, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);

        // ~80% opacity: dark frosted glass letting blurred lock-screen wallpaper show through
        SetLayeredWindowAttributes(window, 0, 205, LWA_ALPHA);

        // Enable Windows 11 Rounded Corners and Dark Mode on window frame if supported
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) {
            auto pfnDwmSetWindowAttribute = reinterpret_cast<HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD)>(
                GetProcAddress(dwm, "DwmSetWindowAttribute"));
            if (pfnDwmSetWindowAttribute) {
                BOOL darkMode = TRUE;
                pfnDwmSetWindowAttribute(window, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkMode, sizeof(darkMode));
                DWORD corner = 2 /* DWMWCP_ROUND */;
                pfnDwmSetWindowAttribute(window, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));
            }
            FreeLibrary(dwm);
        }

        // Center window horizontally and place it naturally below the user avatar
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int width = 340;
        int height = 380;
        int posX = (screenWidth - width) / 2;
        int posY = (screenHeight - height) / 2 + 25;
        SetWindowPos(window, HWND_TOP, posX, posY, width, height, SWP_SHOWWINDOW);

        // Record initial cursor position so accidental mouse clicks when opening the tile do not start a dot
        POINT cur{};
        GetCursorPos(&cur);
        ScreenToClient(window, &cur);
        state->initialCursor = cur;
        state->armed = false;

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
            // Idle timeout with incomplete pattern
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
            if (state->visual == VisualState::ErrorTooShort) {
                state->visual = VisualState::Drawing;
                InvalidateRect(window, nullptr, FALSE);
            }
            return TRUE;
        }
        if (wp == 6) {
            // Red flash error display finished -> reset pattern and allow instant redraw
            KillTimer(window, 6);
            if (state->visual == VisualState::ErrorWrong) {
                state->pattern.Clear();
                state->visual = VisualState::Drawing;
                state->dragging = false;
                InvalidateRect(window, nullptr, FALSE);
            }
            return TRUE;
        }
        return TRUE;

    case WM_DESTROY:
        for (UINT_PTR id = 1; id <= 6; ++id) KillTimer(window, id);
        return TRUE;

    case WM_ERASEBKGND:
        return TRUE; // Double buffer handles background painting cleanly

    case WM_LBUTTONDOWN: {
        const POINT current = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (state->visual != VisualState::Drawing) return TRUE;

        // Arm hover immediately on click
        state->armed = true;

        // Tapping on last dot finishes pattern if valid
        if (state->dragging && state->pattern.Valid()) {
            FinishPattern(window, *state);
            return TRUE;
        }

        // Start drawing on click or tap
        state->pattern.Clear();
        state->visual = VisualState::Drawing;
        state->dragging = true;
        state->previous = current;
        SetCapture(window);
        const Layout grid = Grid(window);
        const int dot = DotAt(window, current, grid.hitRadius);
        if (dot >= 0) state->pattern.Add(static_cast<BYTE>(dot));
        InvalidateRect(window, nullptr, FALSE);
        return TRUE;
    }

    case WM_MOUSEMOVE: {
        const POINT current = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (state->visual != VisualState::Drawing) return TRUE;

        // Check if cursor has moved away from initial position before arming hover
        if (!state->armed) {
            const int dx = current.x - state->initialCursor.x;
            const int dy = current.y - state->initialCursor.y;
            if (dx * dx + dy * dy >= 144) { // moved at least 12 pixels intentionally
                state->armed = true;
            } else {
                return TRUE;
            }
        }

        const Layout grid = Grid(window);
        if (!state->dragging) {
            // Glide-to-start: hovering near any dot begins drawing immediately without clicks
            const int dot = DotAt(window, current, grid.hitRadius);
            state->hoverDot = dot;
            if (dot >= 0) {
                state->dragging = true;
                state->visual = VisualState::Drawing;
                state->pattern.Clear();
                state->pattern.Add(static_cast<BYTE>(dot));
                state->previous = current;
                SetCapture(window);
                InvalidateRect(window, nullptr, FALSE);
            } else {
                InvalidateRect(window, nullptr, FALSE);
            }
        } else {
            state->hoverDot = DotAt(window, current, grid.hitRadius);
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
        if (state->dragging && !state->pattern.Valid()) {
            state->dragging = false;
            state->pattern.Clear();
            InvalidateRect(window, nullptr, FALSE);
        }
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL) {
            // When Esc is pressed:
            // If user has drawn dots or is dragging: RESET current pattern, free cursor, remain in pattern mode!
            if (state->pattern.count > 0 || state->dragging) {
                state->pattern.Clear();
                state->dragging = false;
                state->visual = VisualState::Drawing;
                state->hoverDot = -1;
                ReleaseCapture();
                InvalidateRect(window, nullptr, FALSE);
                return TRUE;
            }
            // If pattern is already empty: close the grid and return to tile
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

        // Dark frosted acrylic background surface
        HBRUSH bgBrush = CreateSolidBrush(RGB(18, 20, 26));
        FillRect(dc, &rect, bgBrush);
        DeleteObject(bgBrush);
        SetBkMode(dc, TRANSPARENT);

        // Soft rounded acrylic border
        HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(55, 60, 75));
        HGDIOBJ prevBorderPen = SelectObject(dc, borderPen);
        HGDIOBJ prevBorderBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, 0, 0, rect.right, rect.bottom, 22, 22);
        SelectObject(dc, prevBorderPen);
        SelectObject(dc, prevBorderBrush);
        DeleteObject(borderPen);

        // Header typography (Segoe UI)
        HFONT titleFont = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT subFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT hintFont = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        PCWSTR titleText = L"Pattern Sign-in";
        wchar_t dynamicSub[64] = L"Glide across 5+ dots to unlock";
        COLORREF titleColor = RGB(245, 246, 250);
        COLORREF subColor = RGB(165, 172, 188);
        COLORREF lineColor = RGB(0, 180, 255);       // Electric cyan-blue
        COLORREF ringColor = RGB(0, 180, 255);
        COLORREF ringBgColor = RGB(16, 45, 80);
        COLORREF coreColor = RGB(235, 248, 255);

        if (state->visual == VisualState::Drawing) {
            if (state->pattern.count > 0) {
                StringCchPrintfW(dynamicSub, ARRAYSIZE(dynamicSub),
                    state->pattern.count < 5 ? L"%zu dots connected (min 5)" : L"%zu dots connected - pause to sign in",
                    state->pattern.count);
                subColor = state->pattern.count < 5 ? RGB(100, 200, 255) : RGB(46, 213, 115);
            }
        } else if (state->visual == VisualState::Success) {
            titleText = L"Pattern Verified";
            StringCchCopyW(dynamicSub, ARRAYSIZE(dynamicSub), L"Signing in...");
            titleColor = RGB(46, 213, 115);        // Emerald green
            subColor = RGB(160, 235, 190);
            lineColor = RGB(46, 213, 115);
            ringColor = RGB(46, 213, 115);
            ringBgColor = RGB(20, 60, 35);
            coreColor = RGB(230, 255, 235);
        } else if (state->visual == VisualState::ErrorWrong) {
            titleText = L"Incorrect Pattern";
            StringCchCopyW(dynamicSub, ARRAYSIZE(dynamicSub), L"Please try again");
            titleColor = RGB(255, 75, 75);         // Vivid coral-red
            subColor = RGB(255, 175, 175);
            lineColor = RGB(255, 75, 75);
            ringColor = RGB(255, 75, 75);
            ringBgColor = RGB(70, 22, 28);
            coreColor = RGB(255, 225, 230);
        } else if (state->visual == VisualState::ErrorTooShort) {
            titleText = L"Pattern Too Short";
            StringCchCopyW(dynamicSub, ARRAYSIZE(dynamicSub), L"Connect at least 5 dots");
            titleColor = RGB(255, 75, 75);
            subColor = RGB(255, 175, 175);
            lineColor = RGB(255, 75, 75);
            ringColor = RGB(255, 75, 75);
            ringBgColor = RGB(70, 22, 28);
            coreColor = RGB(255, 225, 230);
        } else if (state->visual == VisualState::ErrorLockout) {
            titleText = L"Pattern Locked Out";
            StringCchCopyW(dynamicSub, ARRAYSIZE(dynamicSub), L"Too many attempts. Use Password.");
            titleColor = RGB(255, 75, 75);
            subColor = RGB(255, 175, 175);
            lineColor = RGB(255, 75, 75);
            ringColor = RGB(255, 75, 75);
            ringBgColor = RGB(70, 22, 28);
            coreColor = RGB(255, 225, 230);
        }

        // Render Title
        RECT titleRect{0, 18, rect.right, 45};
        HGDIOBJ oldFont = SelectObject(dc, titleFont);
        SetTextColor(dc, titleColor);
        DrawTextW(dc, titleText, -1, &titleRect, DT_CENTER | DT_SINGLELINE);

        // Render Subtitle
        RECT subRect{0, 48, rect.right, 72};
        SelectObject(dc, subFont);
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

        // Render 9 Grid Dots with modern layered halos
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
                Ellipse(dc, p.x - 20, p.y - 20, p.x + 20, p.y + 20);

                HBRUSH cBrush = CreateSolidBrush(coreColor);
                SelectObject(dc, cBrush);
                Ellipse(dc, p.x - 7, p.y - 7, p.x + 7, p.y + 7);

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
                Ellipse(dc, p.x - 18, p.y - 18, p.x + 18, p.y + 18);

                HBRUSH cBrush = CreateSolidBrush(RGB(220, 240, 255));
                SelectObject(dc, cBrush);
                Ellipse(dc, p.x - 6, p.y - 6, p.x + 6, p.y + 6);

                SelectObject(dc, oP);
                SelectObject(dc, oB);
                DeleteObject(hPen);
                DeleteObject(hBrush);
                DeleteObject(cBrush);
            } else {
                // Unvisited Dot: dark halo + pearl center
                HPEN uPen = CreatePen(PS_SOLID, 1, RGB(58, 64, 80));
                HBRUSH uBrush = CreateSolidBrush(RGB(26, 30, 38));
                HGDIOBJ oP = SelectObject(dc, uPen);
                HGDIOBJ oB = SelectObject(dc, uBrush);
                Ellipse(dc, p.x - 15, p.y - 15, p.x + 15, p.y + 15);

                HBRUSH cBrush = CreateSolidBrush(RGB(185, 192, 208));
                SelectObject(dc, cBrush);
                Ellipse(dc, p.x - 5, p.y - 5, p.x + 5, p.y + 5);

                SelectObject(dc, oP);
                SelectObject(dc, oB);
                DeleteObject(uPen);
                DeleteObject(uBrush);
                DeleteObject(cBrush);
            }
        }

        // Render Escape hint at bottom
        RECT footerRect{0, rect.bottom - 28, rect.right, rect.bottom - 8};
        SelectObject(dc, hintFont);
        SetTextColor(dc, RGB(115, 122, 140));
        DrawTextW(dc, L"Press Esc to reset pattern", -1, &footerRect, DT_CENTER | DT_SINGLELINE);

        if (prevPen) SelectObject(dc, prevPen);
        if (linePen) DeleteObject(linePen);
        SelectObject(dc, oldFont);
        DeleteObject(titleFont);
        DeleteObject(subFont);
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
    dialog->cx = 240;
    dialog->cy = 265;
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
