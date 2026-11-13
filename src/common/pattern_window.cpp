#include "pattern_window.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace patlock {
namespace {
struct State {
    Pattern pattern;
    bool dragging = false;
    bool hide = false;
    bool tooShort = false;
    POINT previous{};
};

struct Layout { int x; int y; int spacing; int radius; };

Layout Grid(HWND window) noexcept {
    RECT r{}; GetClientRect(window, &r);
    const int spacing = (std::max)(30, (std::min)(static_cast<int>(r.right) - 100, static_cast<int>(r.bottom) - 150) / 2);
    return {static_cast<int>(r.right) / 2 - spacing, 70, spacing, spacing / 4};
}

POINT Dot(Layout grid, int dot) noexcept {
    return {grid.x + dot % 3 * grid.spacing, grid.y + dot / 3 * grid.spacing};
}

int DotAt(HWND window, POINT pt) noexcept {
    const Layout grid = Grid(window);
    for (BYTE i = 0; i < 9; ++i) {
        const POINT p = Dot(grid, i);
        const int dx = pt.x - p.x;
        const int dy = pt.y - p.y;
        if (dx * dx + dy * dy <= grid.radius * grid.radius) return i;
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
    for (BYTE i = 0; i < 9; ++i) {
        const POINT p = Dot(grid, i);
        double t = length > 0 ? ((p.x - state.previous.x) * dx + (p.y - state.previous.y) * dy) / length : 0;
        t = (std::max)(0.0, (std::min)(1.0, t));
        const double ex = state.previous.x + t * dx - p.x;
        const double ey = state.previous.y + t * dy - p.y;
        if (ex * ex + ey * ey <= grid.radius * grid.radius) hits[count++] = {t, i};
    }
    std::sort(hits, hits + count, [](const Hit& a, const Hit& b) { return a.t < b.t; });
    for (size_t i = 0; i < count; ++i) state.pattern.Add(hits[i].dot);
    state.previous = to;
    InvalidateRect(window, nullptr, FALSE);
}

INT_PTR CALLBACK Dialog(HWND window, UINT message, WPARAM wp, LPARAM lp) noexcept {
    State* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<State*>(lp);
        SetWindowLongPtrW(window, DWLP_USER, lp);
        SetWindowTextW(window, L"Patlock - draw your pattern");
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
            EndDialog(window, IDCANCEL);
            return TRUE;
        }
        if (wp == 2) {
            KillTimer(window, 2);
            if (state->pattern.Valid()) {
                state->dragging = false;
                EndDialog(window, IDOK);
            }
            return TRUE;
        }
        if (wp == 3) {
            KillTimer(window, 3);
            if (state->dragging && !state->pattern.Valid() && state->pattern.count > 0) {
                state->pattern.Clear();
                state->dragging = false;
                state->tooShort = true;
                InvalidateRect(window, nullptr, FALSE);
            }
            return TRUE;
        }
        return TRUE;
    case WM_DESTROY:
        KillTimer(window, 1);
        KillTimer(window, 2);
        KillTimer(window, 3);
        return TRUE;
    case WM_LBUTTONDOWN: {
        KillTimer(window, 2);
        KillTimer(window, 3);
        const POINT current = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (state->dragging && state->pattern.Valid()) {
            state->dragging = false;
            EndDialog(window, IDOK);
            return TRUE;
        }
        state->pattern.Clear();
        state->tooShort = false;
        state->dragging = true;
        state->previous = current;
        SetCapture(window);
        Track(window, *state, state->previous);
        return TRUE;
    }
    case WM_MOUSEMOVE: {
        const POINT current = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (!state->dragging) {
            // Glide-to-start: as soon as pointer moves over any dot, drawing begins!
            // No double-tap or button holding required on trackpads.
            const int dot = DotAt(window, current);
            if (dot >= 0) {
                state->dragging = true;
                state->tooShort = false;
                state->pattern.Clear();
                state->pattern.Add(static_cast<BYTE>(dot));
                state->previous = current;
                InvalidateRect(window, nullptr, FALSE);
            }
        } else {
            Track(window, *state, current);
            KillTimer(window, 2);
            KillTimer(window, 3);
            if (state->pattern.Valid()) {
                // Pause for 450ms on the final dot auto-completes and submits!
                SetTimer(window, 2, 450, nullptr);
            } else if (state->pattern.count > 0) {
                SetTimer(window, 3, 1200, nullptr);
            }
        }
        return TRUE;
    }
    case WM_LBUTTONUP: {
        KillTimer(window, 2);
        KillTimer(window, 3);
        if (state->dragging) {
            Track(window, *state, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            ReleaseCapture();
            if (state->pattern.Valid()) {
                state->dragging = false;
                EndDialog(window, IDOK);
            } else if (state->pattern.count > 0) {
                state->pattern.Clear();
                state->dragging = false;
                state->tooShort = true;
                InvalidateRect(window, nullptr, FALSE);
            }
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
            state->pattern.Clear();
            EndDialog(window, IDCANCEL);
            return TRUE;
        }
        return FALSE;
    case WM_CLOSE:
        state->pattern.Clear();
        EndDialog(window, IDCANCEL);
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT rect{};
        GetClientRect(window, &rect);
        FillRect(dc, &rect, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        SetBkMode(dc, TRANSPARENT);
        RECT hint = rect;
        hint.top = 12;
        hint.bottom = 55;
        DrawTextW(dc,
            state->tooShort ? L"Pattern too short (minimum 5 dots). Try again." :
            L"Glide across at least 5 dots to draw.\nPause on last dot to unlock. Esc to cancel.",
            -1, &hint, DT_CENTER | DT_WORDBREAK);

        const Layout grid = Grid(window);
        HPEN pen = CreatePen(PS_SOLID, 4, RGB(0, 120, 215));
        HGDIOBJ oldPen = pen ? SelectObject(dc, pen) : nullptr;
        if (!state->hide && state->pattern.count) {
            POINT p = Dot(grid, state->pattern.dots[0]);
            MoveToEx(dc, p.x, p.y, nullptr);
            for (size_t i = 1; i < state->pattern.count; ++i) {
                p = Dot(grid, state->pattern.dots[i]);
                LineTo(dc, p.x, p.y);
            }
        }
        HBRUSH active = CreateSolidBrush(RGB(0, 120, 215));
        for (BYTE i = 0; i < 9; ++i) {
            const POINT p = Dot(grid, i);
            HGDIOBJ brush = GetStockObject(LTGRAY_BRUSH);
            if (!state->hide && state->pattern.Contains(i) && active) brush = active;
            HGDIOBJ old = SelectObject(dc, brush);
            Ellipse(dc, p.x - 12, p.y - 12, p.x + 12, p.y + 12);
            SelectObject(dc, old);
        }
        if (active) DeleteObject(active);
        if (oldPen) SelectObject(dc, oldPen);
        if (pen) DeleteObject(pen);
        EndPaint(window, &paint);
        return TRUE;
    }
    }
    return FALSE;
}
} // anon namespace

HRESULT DrawPattern(HINSTANCE instance, HWND owner, bool hideTrail, Pattern& output) noexcept {
    output.Clear();
    alignas(DWORD) BYTE buffer[sizeof(DLGTEMPLATE) + 3 * sizeof(WORD)]{};
    auto* dialog = reinterpret_cast<DLGTEMPLATE*>(buffer);
    dialog->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    dialog->cx = 260;
    dialog->cy = 260;
    State state;
    state.hide = hideTrail;
    const INT_PTR result = DialogBoxIndirectParamW(instance, dialog, owner, Dialog, reinterpret_cast<LPARAM>(&state));
    if (result == IDOK && state.pattern.Valid()) {
        output = state.pattern;
        return S_OK;
    }
    if (result == IDCANCEL) return S_FALSE;
    return E_FAIL;
}
} // namespace patlock
