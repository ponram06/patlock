// Standalone Pattern Window Demo and Trackpad Tuning Tool (Phase 7)
// Runs on the desktop without installing or registering any Credential Provider.
// Used to test and tune trackpad feel (tap-and-drag, sensitivity, overshoot).

#include "pattern_window.h"
#include <strsafe.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    bool hideTrail = false;

    while (true) {
        patlock::Pattern pattern;
        const HRESULT hr = patlock::DrawPattern(instance, nullptr, hideTrail, pattern);

        if (hr == S_OK) {
            wchar_t seqText[128] = L"";
            for (size_t i = 0; i < pattern.count; ++i) {
                wchar_t step[16];
                StringCchPrintfW(step, ARRAYSIZE(step), i == 0 ? L"%d" : L" -> %d", pattern.dots[i]);
                StringCchCatW(seqText, ARRAYSIZE(seqText), step);
            }

            wchar_t message[512];
            StringCchPrintfW(message, ARRAYSIZE(message),
                L"Pattern captured successfully!\n\n"
                L"Dot sequence: %s\n"
                L"Dot count: %zu (minimum 5)\n"
                L"Valid: %s\n"
                L"Hide trail mode: %s\n\n"
                L"Click YES to draw again.\n"
                L"Click NO to toggle trail visibility (current: %s).\n"
                L"Click CANCEL to exit.",
                seqText,
                pattern.count,
                pattern.Valid() ? L"YES" : L"NO",
                hideTrail ? L"ON" : L"OFF",
                hideTrail ? L"HIDDEN" : L"VISIBLE");

            pattern.Clear();

            const int choice = MessageBoxW(nullptr, message, L"Patlock Trackpad Tuning Demo", MB_YESNOCANCEL | MB_ICONINFORMATION);
            if (choice == IDYES) {
                continue;
            } else if (choice == IDNO) {
                hideTrail = !hideTrail;
                continue;
            } else {
                break;
            }
        } else if (hr == S_FALSE) {
            // User pressed Esc or cancelled
            const int choice = MessageBoxW(nullptr,
                L"Drawing cancelled.\n\nClick RETRY to try again, or CANCEL to exit.",
                L"Patlock Demo", MB_RETRYCANCEL | MB_ICONQUESTION);
            if (choice == IDRETRY) continue;
            break;
        } else {
            MessageBoxW(nullptr, L"Failed to create pattern window.", L"Patlock Demo", MB_OK | MB_ICONERROR);
            return 1;
        }
    }

    return 0;
}
