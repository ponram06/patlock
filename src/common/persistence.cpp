#include "persistence.h"
#include <ktmw32.h>
namespace patlock {
namespace {
HRESULT ChangeValue(HKEY key, PCWSTR name, const void* input, DWORD length, bool erase) noexcept {
    if (!key || !name || (!erase && (!input || !length))) return E_INVALIDARG;
    HANDLE transaction = CreateTransaction(nullptr, nullptr, 0, 0, 0, 3000, nullptr);
    if (transaction == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    HKEY enlisted = nullptr;
    LSTATUS error = RegOpenKeyTransactedW(key, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY,
        &enlisted, transaction, nullptr);
    HRESULT hr = HRESULT_FROM_WIN32(error);
    if (error == ERROR_SUCCESS) {
        error = erase ? RegDeleteValueW(enlisted, name)
            : RegSetValueExW(enlisted, name, 0, REG_BINARY, static_cast<const BYTE*>(input), length);
        hr = HRESULT_FROM_WIN32(error);
        if (error == ERROR_SUCCESS && !CommitTransaction(transaction)) hr = HRESULT_FROM_WIN32(GetLastError());
    }
    // Closing an uncommitted transaction rolls it back. Do not misreport a
    // subsequent nontransactional flush failure as an aborted committed write.
    if (enlisted) RegCloseKey(enlisted);
    CloseHandle(transaction);
    return hr;
}
}
HRESULT WriteRegistryBlob(HKEY key, PCWSTR name, const void* input, DWORD length) noexcept {
    return ChangeValue(key, name, input, length, false);
}
HRESULT DeleteRegistryValue(HKEY key, PCWSTR name) noexcept {
    return ChangeValue(key, name, nullptr, 0, true);
}
}
