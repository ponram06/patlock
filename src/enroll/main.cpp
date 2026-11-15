#define SECURITY_WIN32
#include <windows.h>
#include <wincred.h>
#include <strsafe.h>
#include "pattern_window.h"
#include "security.h"
#include "tpm.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int) {
    const bool refresh = command && wcscmp(command, L"--refresh") == 0;
    const bool remove = command && wcscmp(command, L"--remove") == 0;
    if (command && *command && !refresh && !remove) {
        MessageBoxW(nullptr, L"Use no arguments to enroll, --refresh to verify your password again, or --remove to remove enrollment.", L"Patlock", MB_OK | MB_ICONERROR); return 1;
    }
    if (MessageBoxW(nullptr, L"VM testing only. Patlock stores a recoverable Windows password.\n"
        L"BitLocker/device encryption is required. Baseline storage is used only when no TPM is present.\n"
        L"Use only a throwaway local VM account. Continue?", L"Set up Patlock", MB_YESNO | MB_ICONWARNING) != IDYES) return 0;
    wchar_t user[256]{};
    patlock::Password password;
    CREDUI_INFOW ui{}; ui.cbSize = sizeof(ui);
    ui.pszCaptionText = remove ? L"Patlock: remove enrollment" : refresh ? L"Patlock: verify password again" : L"Patlock: enroll local account";
    ui.pszMessageText = L"Enter an unqualified local username and its full Windows password. No domain or Microsoft accounts.";
    BOOL save = FALSE;
    const DWORD prompt = CredUIPromptForCredentialsW(&ui, L"Patlock local account", nullptr, 0,
        user, ARRAYSIZE(user), password.data(), static_cast<ULONG>(password.value.size()), &save,
        CREDUI_FLAGS_GENERIC_CREDENTIALS | CREDUI_FLAGS_ALWAYS_SHOW_UI | CREDUI_FLAGS_DO_NOT_PERSIST | CREDUI_FLAGS_EXCLUDE_CERTIFICATES);
    if (prompt == ERROR_CANCELLED) return 0;
    HRESULT hr = HRESULT_FROM_WIN32(prompt);
    PCWSTR cleanUser = wcsrchr(user, L'\\') ? wcsrchr(user, L'\\') + 1 : user;
    wchar_t sid[184]{};
    if (SUCCEEDED(hr)) hr = patlock::ValidatePassword(cleanUser, password.data(), sid);
    if (SUCCEEDED(hr) && refresh) hr = patlock::Refresh(cleanUser, sid, password.data());
    if (SUCCEEDED(hr) && remove) {
        if (MessageBoxW(nullptr, L"Remove this account's Pattern enrollment and active TPM key?\nWindows Password sign-in remains available.", L"Remove Patlock enrollment", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
        hr = patlock::RemoveEnrollment(cleanUser, sid, password.data());
    }
    bool allowSoftwareKey = false;
    if (SUCCEEDED(hr) && !refresh && !remove) {
        // Decide on storage before the pattern is drawn, so the user can back out.
        const HRESULT probe = patlock::tpm::Probe();
        if (FAILED(probe)) {
            password.Clear();
            MessageBoxW(nullptr, L"A TPM is present but could not be used, so nothing was saved.\n"
                L"Check that the TPM is ready in tpm.msc and try again.", L"Patlock", MB_OK | MB_ICONERROR);
            return 1;
        }
        if (probe == S_FALSE) {
            if (MessageBoxW(nullptr, L"No TPM was found. Without one, the encryption key for your Windows password is "
                L"stored next to it in the registry.\n\nAny administrator, and anyone who can read this disk while "
                L"BitLocker/device encryption is off, can recover your Windows password.\n\n"
                L"Use software-only storage anyway?", L"Patlock: no TPM", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
                password.Clear();
                return 0;
            }
            allowSoftwareKey = true;
        }
    }
    if (SUCCEEDED(hr) && !refresh && !remove) {
        patlock::Pattern first, second;
        hr = patlock::DrawPattern(instance, nullptr, false, first);
        if (hr != S_OK) return FAILED(hr) ? 1 : 0;
        MessageBoxW(nullptr, L"Draw the same pattern again to confirm it.", L"Patlock", MB_OK);
        hr = patlock::DrawPattern(instance, nullptr, false, second);
        if (hr != S_OK) return FAILED(hr) ? 1 : 0;
        if (first.count != second.count || !patlock::Equal(first.dots.data(), second.dots.data(), first.count)) hr = E_INVALIDARG;
        if (SUCCEEDED(hr)) hr = patlock::Enroll(cleanUser, sid, password.data(), first, allowSoftwareKey);
    }
    password.Clear();
    if (SUCCEEDED(hr) && remove) {
        MessageBoxW(nullptr, L"Pattern enrollment removed. Windows Password sign-in is available.", L"Patlock", MB_OK | MB_ICONINFORMATION); return 0;
    }
    if (SUCCEEDED(hr)) {
        const bool isTpm = (patlock::QueryTpmMode(sid) == S_OK);
        const wchar_t* modeMsg = isTpm
            ? L"Enrolled with a TPM-backed key (experimental; protection requires validation).\nPattern unlock is active and persists across reboots."
            : L"Enrolled successfully in baseline mode (software store).\nPattern unlock is active and persists across reboots.";
        MessageBoxW(nullptr, modeMsg, L"Patlock", MB_OK | MB_ICONINFORMATION);
        if (hr == S_FALSE) MessageBoxW(nullptr, L"The new enrollment was saved, but an old TPM key could not be retired.\nThe active credential is usable; see the recovery notes for cleanup.", L"Patlock cleanup", MB_OK | MB_ICONWARNING);
    } else {
        wchar_t errorMsg[512];
        if (hr == HRESULT_FROM_WIN32(ERROR_LOGON_FAILURE)) {
            StringCchPrintfW(errorMsg, ARRAYSIZE(errorMsg),
                L"Windows rejected the password for '%s'.\n\n"
                L"Enter the account's Windows password, not its Windows Hello PIN.\n"
                L"If needed, configure a password in Windows Settings, then try again.",
                cleanUser);
        } else if (refresh && hr == E_ACCESSDENIED) {
            StringCchPrintfW(errorMsg, ARRAYSIZE(errorMsg),
                L"This password does not match the one Patlock enrolled.\n\n"
                L"If the Windows password was changed, enroll again (run without --refresh).");
        } else if (hr == E_INVALIDARG) {
            StringCchPrintfW(errorMsg, ARRAYSIZE(errorMsg),
                L"Invalid input or mismatched pattern confirmation.\n"
                L"Pattern must connect at least 5 dots and match exactly on both draws.");
        } else {
            StringCchPrintfW(errorMsg, ARRAYSIZE(errorMsg),
                L"No usable change was saved (error 0x%08X).\n"
                L"Check the account name, password, pattern confirmation, and elevation.",
                hr);
        }
        MessageBoxW(nullptr, errorMsg, L"Patlock", MB_OK | MB_ICONERROR);
    }
    return FAILED(hr) ? 1 : 0;
}
