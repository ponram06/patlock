#include "pattern_window.h"
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    patlock::Pattern pattern;
    const HRESULT hr = patlock::DrawPattern(instance, nullptr, false, pattern);
    pattern.Clear();
    if (hr == S_OK) MessageBoxW(nullptr, L"Pattern captured. No authentication performed and no pattern stored.", L"Patlock demo", MB_OK);
    else if (FAILED(hr)) MessageBoxW(nullptr, L"Could not open the pattern window.", L"Patlock demo", MB_OK | MB_ICONERROR);
    return FAILED(hr) ? 1 : 0;
}
