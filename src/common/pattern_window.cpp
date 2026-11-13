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
POINT Dot(Layout grid, int dot) noexcept { return {grid.x + dot % 3 * grid.spacing, grid.y + dot / 3 * grid.spacing}; }
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
        if (wp == 1) { state->pattern.Clear(); EndDialog(window, IDCANCEL); }
        return TRUE;
    case WM_DESTROY:
        KillTimer(window, 1); return TRUE;
    case WM_LBUTTONDOWN:
        state->pattern.Clear(); state->tooShort = false; state->dragging = true;
        state->previous = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        SetCapture(window); Track(window, *state, state->previous); return TRUE;
    case WM_MOUSEMOVE:
        if (state->dragging) Track(window, *state, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        return TRUE;
    case WM_LBUTTONUP:
        if (state->dragging) {
            Track(window, *state, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            state->dragging = false; ReleaseCapture();
            if (state->pattern.Valid()) EndDialog(window, IDOK);
            else { state->pattern.Clear(); state->tooShort = true; InvalidateRect(window, nullptr, FALSE); }
        }
        return TRUE;
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
        if (state->dragging) { state->dragging = false; state->pattern.Clear(); InvalidateRect(window, nullptr, FALSE); }
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL) { state->pattern.Clear(); EndDialog(window, IDCANCEL); return TRUE; }
        return FALSE;
    case WM_CLOSE:
        state->pattern.Clear(); EndDialog(window, IDCANCEL); return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint);
        RECT rect{}; GetClientRect(window, &rect);
        FillRect(dc, &rect, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        SetBkMode(dc, TRANSPARENT);
        RECT hint = rect; hint.top = 12; hint.bottom = 55;
        DrawTextW(dc, state->tooShort ? L"Use at least 5 dots. Try again." : L"Hold and drag across at least 5 dots.\nRelease to finish. Esc to cancel.", -1, &hint, DT_CENTER | DT_WORDBREAK);
        const Layout grid = Grid(window);
        HPEN pen = CreatePen(PS_SOLID, 3, RGB(40, 100, 210));
        HGDIOBJ oldPen = pen ? SelectObject(dc, pen) : nullptr;
        if (!state->hide && state->pattern.count) {
            POINT p = Dot(grid, state->pattern.dots[0]); MoveToEx(dc, p.x, p.y, nullptr);
            for (size_t i = 1; i < state->pattern.count; ++i) { p = Dot(grid, state->pattern.dots[i]); LineTo(dc, p.x, p.y); }
        }
        HBRUSH active = CreateSolidBrush(RGB(40, 100, 210));
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
        EndPaint(window, &paint); return TRUE;
    }
    }
    return FALSE;
}
}
HRESULT DrawPattern(HINSTANCE instance, HWND owner, bool hideTrail, Pattern& output) noexcept {
    output.Clear();
    // No controls: DLGTEMPLATE immediately followed by menu/class/title zero WORDs.
    alignas(DWORD) BYTE buffer[sizeof(DLGTEMPLATE) + 3 * sizeof(WORD)]{};
    auto* dialog = reinterpret_cast<DLGTEMPLATE*>(buffer);
    dialog->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    dialog->cx = 260; dialog->cy = 260;
    State state; state.hide = hideTrail;
    const INT_PTR result = DialogBoxIndirectParamW(instance, dialog, owner, Dialog, reinterpret_cast<LPARAM>(&state));
    if (result == IDOK && state.pattern.Valid()) { output = state.pattern; return S_OK; }
    if (result == IDCANCEL) return S_FALSE;
    return E_FAIL;
}
}
