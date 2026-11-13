#include "persistence.h"
#include <ktmw32.h>
#include <cstdio>

// VM ONLY: touches a temporary HKCU key, never the credential store.
int main() {
    wchar_t path[128]{};
    swprintf_s(path, L"Software\\PatlockTransactionTests_%lu_%llu", GetCurrentProcessId(), GetTickCount64());
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY,
        nullptr, &key, nullptr) != ERROR_SUCCESS) return 1;
    int failed = 0;
    auto check = [&](bool ok, const char* label) { if (!ok) { ++failed; std::printf("FAIL: %s\n", label); } };
    const DWORD original = 41, replacement = 42;
    check(SUCCEEDED(patlock::WriteRegistryBlob(key, L"Value", &original, sizeof(original))), "initial atomic write");
    HANDLE transaction = CreateTransaction(nullptr, nullptr, 0, 0, 0, 3000, nullptr);
    HKEY enlisted = nullptr;
    if (transaction != INVALID_HANDLE_VALUE && RegOpenKeyTransactedW(key, nullptr, 0,
        KEY_SET_VALUE | KEY_WOW64_64KEY, &enlisted, transaction, nullptr) == ERROR_SUCCESS) {
        check(RegSetValueExW(enlisted, L"Value", 0, REG_BINARY,
            reinterpret_cast<const BYTE*>(&replacement), sizeof(replacement)) == ERROR_SUCCESS, "stage replacement");
        check(RollbackTransaction(transaction) != FALSE, "explicit rollback");
    } else check(false, "open rollback transaction");
    if (enlisted) RegCloseKey(enlisted);
    if (transaction != INVALID_HANDLE_VALUE) CloseHandle(transaction);
    DWORD actual = 0, size = sizeof(actual), type = 0;
    check(RegQueryValueExW(key, L"Value", nullptr, &type, reinterpret_cast<BYTE*>(&actual), &size) == ERROR_SUCCESS &&
        actual == original, "rollback preserves original");
    check(SUCCEEDED(patlock::WriteRegistryBlob(key, L"Value", &replacement, sizeof(replacement))), "commit replacement");
    size = sizeof(actual);
    check(RegQueryValueExW(key, L"Value", nullptr, &type, reinterpret_cast<BYTE*>(&actual), &size) == ERROR_SUCCESS &&
        actual == replacement, "committed value visible");
    check(FAILED(patlock::WriteRegistryBlob(key, nullptr, &original, sizeof(original))), "invalid input declines");
    check(SUCCEEDED(patlock::DeleteRegistryValue(key, L"Value")), "transactional removal");
    size = sizeof(actual);
    check(RegQueryValueExW(key, L"Value", nullptr, &type, reinterpret_cast<BYTE*>(&actual), &size) == ERROR_FILE_NOT_FOUND,
        "removed value absent");
    RegCloseKey(key);
    check(RegDeleteKeyExW(HKEY_CURRENT_USER, path, KEY_WOW64_64KEY, 0) == ERROR_SUCCESS, "temporary key cleanup");
    std::printf("Registry transaction checks: %s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
