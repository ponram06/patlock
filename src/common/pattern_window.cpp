#include "pattern_window.h"
#include "arming_gate.h"
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

RECT SigninLinkRect(int windowWidth) noexcept {
    const int linkWidth = 180;
    const int left = (windowWidth - linkWidth) / 2;
    return {left, 246, left + linkWidth, 274};
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
    int hitRadius;
};

Layout Grid(HWND window) noexcept {
    RECT r{};
    GetClientRect(window, &r);
    const int cw = static_cast<int>(r.right);
    const int startX = cw / 2 - GridSpacing;
    return {startX, GridTopY, GridSpacing, HitRadius};
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
        SetLayeredWindowAttributes(window, TransparentKey, 0, LWA_COLORKEY);

        // Position: centered horizontally, placed 35px below screen center
        // Sits cleanly below 'testuser' without any overlap!
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int posX = (screenWidth - WindowW) / 2;
        int posY = screenHeight / 2 + 35;
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

    // ── GDI+ Anti-Aliased Fluent Surface Rendering ───────────────────────────
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC hdc = BeginPaint(window, &paint);
        RECT rect{};
        GetClientRect(window, &rect);

        // Double buffer
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

        // 1) Fill outside with TransparentKey (outer margin)
        HBRUSH keyBrush = CreateSolidBrush(TransparentKey);
        FillRect(memDC, &rect, keyBrush);
        DeleteObject(keyBrush);

        {
            Gdiplus::Graphics g(memDC);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            // 2) Draw solid midnight-slate card with rounded corners:
            // This completely COVERS and HIDES any background LogonUI icons!
            Gdiplus::GraphicsPath cardPath;
            AddRoundedRect(cardPath, Gdiplus::RectF(6.0f, 6.0f, rect.right - 12.0f, rect.bottom - 12.0f), 20.0f);

            Gdiplus::SolidBrush cardBgBrush(Gdiplus::Color(255, 18, 22, 30));
            g.FillPath(&cardBgBrush, &cardPath);

            Gdiplus::Pen cardBorderPen(Gdiplus::Color(160, 56, 64, 82), 1.0f);
            g.DrawPath(&cardBorderPen, &cardPath);

            // ── Colors by state ──────────────────────────────────────────────
            Gdiplus::Color lineColor(255, 0, 170, 255);
            Gdiplus::Color ringColor(255, 0, 170, 255);
            Gdiplus::Color discBg(255, 12, 48, 96);
            Gdiplus::Color coreColor(255, 255, 255, 255);

            wchar_t statusMsg[64] = L"Draw pattern to unlock";
            Gdiplus::Color statusColor(255, 160, 172, 195);

            if (state->visual == VisualState::Drawing) {
                if (state->pattern.count > 0) {
                    if (state->pattern.count < 5) {
                        StringCchPrintfW(statusMsg, ARRAYSIZE(statusMsg), L"%zu / 5 dots connected", state->pattern.count);
                        statusColor = Gdiplus::Color(255, 90, 205, 255);
                    } else {
                        StringCchPrintfW(statusMsg, ARRAYSIZE(statusMsg), L"%zu dots connected", state->pattern.count);
                        statusColor = Gdiplus::Color(255, 50, 220, 125);
                    }
                } else if (!state->gate.Armed()) {
                    StringCchCopyW(statusMsg, ARRAYSIZE(statusMsg), L"Move pointer away to start");
                    statusColor = Gdiplus::Color(255, 255, 195, 80);
                }
            } else if (state->visual == VisualState::Success) {
                StringCchCopyW(statusMsg, ARRAYSIZE(statusMsg), L"Signing in...");
                statusColor = Gdiplus::Color(255, 46, 213, 115);
                lineColor   = Gdiplus::Color(255, 46, 213, 115);
                ringColor   = Gdiplus::Color(255, 46, 213, 115);
                discBg      = Gdiplus::Color(255, 18, 65, 40);
                coreColor   = Gdiplus::Color(255, 240, 255, 245);
            } else if (state->visual == VisualState::ErrorWrong) {
                StringCchCopyW(statusMsg, ARRAYSIZE(statusMsg), L"Incorrect pattern");
                statusColor = Gdiplus::Color(255, 255, 75, 75);
                lineColor   = Gdiplus::Color(255, 255, 75, 75);
                ringColor   = Gdiplus::Color(255, 255, 75, 75);
                discBg      = Gdiplus::Color(255, 70, 20, 25);
                coreColor   = Gdiplus::Color(255, 255, 225, 230);
            } else if (state->visual == VisualState::ErrorTooShort) {
                StringCchCopyW(statusMsg, ARRAYSIZE(statusMsg), L"Connect at least 5 dots");
                statusColor = Gdiplus::Color(255, 255, 75, 75);
                lineColor   = Gdiplus::Color(255, 255, 75, 75);
                ringColor   = Gdiplus::Color(255, 255, 75, 75);
                discBg      = Gdiplus::Color(255, 70, 20, 25);
                coreColor   = Gdiplus::Color(255, 255, 225, 230);
            } else if (state->visual == VisualState::ErrorLockout) {
                StringCchCopyW(statusMsg, ARRAYSIZE(statusMsg), L"Locked out. Wait a moment.");
                statusColor = Gdiplus::Color(255, 255, 75, 75);
                lineColor   = Gdiplus::Color(255, 255, 75, 75);
                ringColor   = Gdiplus::Color(255, 255, 75, 75);
                discBg      = Gdiplus::Color(255, 70, 20, 25);
                coreColor   = Gdiplus::Color(255, 255, 225, 230);
            }

            // ── Top Subtitle Status ──────────────────────────────────────────
            Gdiplus::Font statusFont(L"Segoe UI", 10.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush statusBrush(statusColor);
            Gdiplus::StringFormat centerFmt;
            centerFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            centerFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            Gdiplus::RectF statusRect(0.0f, 16.0f, static_cast<float>(rect.right), 26.0f);
            g.DrawString(statusMsg, -1, &statusFont, statusRect, &centerFmt, &statusBrush);

            const Layout grid = Grid(window);

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
                    g.DrawLine(&linePen, last.x, last.y, state->previous.x, state->previous.y);
                }
            }

            // ── 9 Smooth Anti-Aliased Floating Dots (Mockup Match!) ──────────
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
                    Gdiplus::SolidBrush db(Gdiplus::Color(255, 18, 48, 88));
                    g.FillEllipse(&db, p.x - rOut, p.y - rOut, 2.0f * rOut, 2.0f * rOut);
                    Gdiplus::Pen hp(Gdiplus::Color(255, 0, 160, 255), 2.0f);
                    g.DrawEllipse(&hp, p.x - rOut, p.y - rOut, 2.0f * rOut, 2.0f * rOut);

                    float rIn = 4.5f;
                    Gdiplus::SolidBrush cb(Gdiplus::Color(255, 210, 235, 255));
                    g.FillEllipse(&cb, p.x - rIn, p.y - rIn, 2.0f * rIn, 2.0f * rIn);
                } else {
                    // Idle dot: clean, smooth solid dark circle
                    float r = 10.5f;
                    Gdiplus::SolidBrush dotBrush(Gdiplus::Color(255, 34, 38, 50));
                    g.FillEllipse(&dotBrush, p.x - r, p.y - r, 2.0f * r, 2.0f * r);

                    Gdiplus::Pen dotRim(Gdiplus::Color(255, 58, 66, 84), 1.0f);
                    g.DrawEllipse(&dotRim, p.x - r, p.y - r, 2.0f * r, 2.0f * r);

                    // Small subtle pearl center pip
                    float rPip = 2.5f;
                    Gdiplus::SolidBrush pipBrush(Gdiplus::Color(255, 95, 105, 125));
                    g.FillEllipse(&pipBrush, p.x - rPip, p.y - rPip, 2.0f * rPip, 2.0f * rPip);
                }
            }

            // ── "Other sign-in options" Link at Bottom ───────────────────────
            Gdiplus::Font linkFont(L"Segoe UI", 10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
            Gdiplus::Color linkColor = state->hoverLink
                ? Gdiplus::Color(255, 255, 255, 255)
                : Gdiplus::Color(255, 210, 218, 235);
            Gdiplus::SolidBrush linkBrush(linkColor);

            Gdiplus::RectF linkRect(0.0f, 244.0f, static_cast<float>(rect.right), 26.0f);
            g.DrawString(L"Other sign-in options", -1, &linkFont, linkRect, &centerFmt, &linkBrush);
        }

        // Blit double buffer to screen
        BitBlt(hdc, 0, 0, rect.right, rect.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
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

    // Initialize GDI+ for anti-aliasing
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    alignas(DWORD) BYTE buffer[sizeof(DLGTEMPLATE) + 3 * sizeof(WORD)]{};
    auto* dialog = reinterpret_cast<DLGTEMPLATE*>(buffer);
    dialog->style = WS_POPUP;
    dialog->cx = 200;
    dialog->cy = 200;
    State state;
    state.hide = hideTrail;
    state.verifier = verifier;
    const INT_PTR result = DialogBoxIndirectParamW(instance, dialog, owner, Dialog, reinterpret_cast<LPARAM>(&state));

    Gdiplus::GdiplusShutdown(gdiplusToken);

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
