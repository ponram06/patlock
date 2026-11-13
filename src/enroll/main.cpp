#define SECURITY_WIN32
#include <windows.h>
#include <wincred.h>
#include <strsafe.h>
#include "pattern_window.h"
#include "security.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int) {
    const bool refresh = command && wcscmp(command, L"--refresh") == 0;
    if (command && *command && !refresh) {
        MessageBoxW(nullptr, L"Use no arguments to enroll, or --refresh to verify your password again.", L"Patlock", MB_OK | MB_ICONERROR); return 1;
    }
    if (MessageBoxW(nullptr, L"VM testing only. Baseline mode stores a recoverable Windows password.\n"
        L"BitLocker/device encryption is a prerequisite; administrators can recover the stored password.\n"
        L"Use only a throwaway local VM account. Continue?", L"Patlock baseline enrollment", MB_YESNO | MB_ICONWARNING) != IDYES) return 0;
    wchar_t user[256]{};
    patlock::Password password;
    CREDUI_INFOW ui{}; ui.cbSize = sizeof(ui);
    ui.pszCaptionText = refresh ? L"Patlock: verify password again" : L"Patlock: enroll local account";
    ui.pszMessageText = L"Enter an unqualified local username and its full Windows password. No domain or Microsoft accounts.";
    BOOL save = FALSE;
    const DWORD prompt = CredUIPromptForCredentialsW(&ui, L"Patlock local account", nullptr, 0,
        user, ARRAYSIZE(user), password.data(), static_cast<ULONG>(password.value.size()), &save,
        CREDUI_FLAGS_GENERIC_CREDENTIALS | CREDUI_FLAGS_ALWAYS_SHOW_UI | CREDUI_FLAGS_DO_NOT_PERSIST | CREDUI_FLAGS_EXCLUDE_CERTIFICATES);
    if (prompt == ERROR_CANCELLED) return 0;
    HRESULT hr = HRESULT_FROM_WIN32(prompt);
    wchar_t sid[184]{};
    if (SUCCEEDED(hr)) hr = patlock::ValidatePassword(user, password.data(), sid);
    if (SUCCEEDED(hr) && refresh) hr = patlock::Refresh(user, sid, password.data());
    if (SUCCEEDED(hr) && !refresh) {
        patlock::Pattern first, second;
        hr = patlock::DrawPattern(instance, nullptr, false, first);
        if (hr != S_OK) return FAILED(hr) ? 1 : 0;
        MessageBoxW(nullptr, L"Draw the same pattern again to confirm it.", L"Patlock", MB_OK);
        hr = patlock::DrawPattern(instance, nullptr, false, second);
        if (hr != S_OK) return FAILED(hr) ? 1 : 0;
        if (first.count != second.count || !patlock::Equal(first.dots.data(), second.dots.data(), first.count)) hr = E_INVALIDARG;
        if (SUCCEEDED(hr)) hr = patlock::Enroll(user, sid, password.data(), first);
    }
    password.Clear();
    if (SUCCEEDED(hr)) {
        const bool isTpm = (patlock::QueryTpmMode(sid) == S_OK);
        const wchar_t* modeMsg = isTpm
            ? L"Saved in TPM-hardened mode (Platform Crypto Provider).\nPassword verification is valid for this boot and up to 24 hours."
            : L"Saved in baseline mode (software store).\nPassword verification is valid for this boot and up to 24 hours.";
        MessageBoxW(nullptr, modeMsg, L"Patlock", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(nullptr,
            L"No usable change was saved. Check the local account/password, pattern confirmation and elevation. A stale or corrupt record requires re-enrollment.",
            L"Patlock", MB_OK | MB_ICONERROR);
    }
    return FAILED(hr) ? 1 : 0;
}
