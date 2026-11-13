#include "security.h"
#include <bcrypt.h>
#include <sddl.h>
#include <strsafe.h>
#include <cstddef>

namespace patlock {
namespace {
constexpr PCWSTR RootPath = L"SOFTWARE\\Patlock";
constexpr PCWSTR Access = L"D:P(A;;KA;;;SY)(A;;KA;;;BA)";
constexpr ULONGLONG Second = 10000000ULL;
struct Record {
    DWORD magic = 0x5041544c;
    DWORD version = 1;
    DWORD iterations = HashIterations;
    DWORD attempts = 0;
    DWORD disabled = 0;
    DWORD cipherBytes = 0;
    ULONGLONG until = 0;
    ULONGLONG passwordTime = 0;
    ULONGLONG lastSeen = 0;
    BYTE boot[16]{};
    BYTE salt[16]{};
    BYTE hash[32]{};
    wchar_t user[256]{};
    wchar_t sid[184]{};
    BYTE nonce[12]{};
    BYTE tag[16]{};
    BYTE cipher[1026]{};
};
struct Bundle {
    BYTE key[32]{};
    Record record{};
    BYTE mac[32]{};
    ~Bundle() { SecureZeroMemory(this, sizeof(*this)); }
};
struct Key {
    HKEY value = nullptr;
    ~Key() { if (value) RegCloseKey(value); }
};
struct Descriptor {
    PSECURITY_DESCRIPTOR value = nullptr;
    ~Descriptor() { if (value) LocalFree(value); }
    bool Init(PCWSTR text) noexcept { return ConvertStringSecurityDescriptorToSecurityDescriptorW(text, SDDL_REVISION_1, &value, nullptr) != FALSE; }
};
struct Lock {
    HANDLE handle = nullptr;
    bool owned = false;
    ~Lock() { if (owned) ReleaseMutex(handle); if (handle) CloseHandle(handle); }
    bool Acquire() noexcept {
        Descriptor sd;
        if (!sd.Init(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)")) return false;
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), sd.value, FALSE};
        handle = CreateMutexW(&attributes, FALSE, L"Global\\PatlockStoreV1");
        if (!handle) return false;
        const DWORD result = WaitForSingleObject(handle, 3000);
        owned = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        return result == WAIT_OBJECT_0;
    }
};
ULONGLONG Now() noexcept {
    FILETIME time{}; GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER value{}; value.LowPart = time.dwLowDateTime; value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}
HRESULT OpenProtected(HKEY parent, PCWSTR name, Key& key, bool create = false, bool temporary = false) noexcept {
    Descriptor sd; if (!sd.Init(Access)) return E_FAIL;
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), sd.value, FALSE};
    const LSTATUS result = create
        ? RegCreateKeyExW(parent, name, 0, nullptr, temporary ? REG_OPTION_VOLATILE : REG_OPTION_NON_VOLATILE,
            KEY_ALL_ACCESS | KEY_WOW64_64KEY, &attributes, &key.value, nullptr)
        : RegOpenKeyExW(parent, name, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, &key.value);
    if (result != ERROR_SUCCESS) return HRESULT_FROM_WIN32(result);
    // Apply the protected DACL before reading any secret, including existing keys.
    return HRESULT_FROM_WIN32(RegSetKeySecurity(key.value, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, sd.value));
}
HRESULT ReadBlob(HKEY key, PCWSTR name, void* output, DWORD length) noexcept {
    DWORD type = 0, size = length;
    const LSTATUS result = RegQueryValueExW(key, name, nullptr, &type, static_cast<BYTE*>(output), &size);
    return result == ERROR_SUCCESS && type == REG_BINARY && size == length ? S_OK : E_FAIL;
}
HRESULT WriteBlob(HKEY key, PCWSTR name, const void* input, DWORD length) noexcept {
    LSTATUS result = RegSetValueExW(key, name, 0, REG_BINARY, static_cast<const BYTE*>(input), length);
    if (result == ERROR_SUCCESS) result = RegFlushKey(key);
    return HRESULT_FROM_WIN32(result);
}
HRESULT Mac(Bundle& bundle, BYTE output[32]) noexcept {
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0) return E_FAIL;
    NTSTATUS result = BCryptCreateHash(algorithm, &hash, nullptr, 0, bundle.key, 32, 0);
    if (result >= 0) result = BCryptHashData(hash, reinterpret_cast<BYTE*>(&bundle.record), sizeof(Record), 0);
    if (result >= 0) result = BCryptFinishHash(hash, output, 32, 0);
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return result >= 0 ? S_OK : E_FAIL;
}
bool ValidSid(PCWSTR sid) noexcept {
    if (!sid || wcsnlen_s(sid, 184) >= 184) return false;
    PSID binary = nullptr;
    const bool ok = ConvertStringSidToSidW(sid, &binary) != FALSE;
    if (binary) LocalFree(binary);
    return ok && !wcschr(sid, L'\\');
}
HRESULT OpenUser(PCWSTR sid, Key& root, Key& user, bool create = false) noexcept {
    if (!ValidSid(sid)) return E_INVALIDARG;
    HRESULT hr = OpenProtected(HKEY_LOCAL_MACHINE, RootPath, root, create);
    if (FAILED(hr)) return hr;
    return OpenProtected(root.value, sid, user, create);
}
HRESULT Read(Key& user, PCWSTR sid, Bundle& bundle) noexcept {
    HRESULT hr = ReadBlob(user.value, L"Record", &bundle, sizeof(bundle));
    if (FAILED(hr)) return hr;
    Secret<BYTE, 32> mac;
    hr = Mac(bundle, mac.data());
    Record& record = bundle.record;
    if (FAILED(hr) || !Equal(mac.data(), bundle.mac, 32) || record.magic != 0x5041544c || record.version != 1 ||
        record.iterations != HashIterations || record.attempts > 10 || record.disabled > 1 ||
        record.cipherBytes < 4 || record.cipherBytes > sizeof(record.cipher) || record.cipherBytes % 2 ||
        wcsnlen_s(record.user, ARRAYSIZE(record.user)) >= ARRAYSIZE(record.user) || !record.user[0] ||
        wcsnlen_s(record.sid, ARRAYSIZE(record.sid)) >= ARRAYSIZE(record.sid) || wcscmp(sid, record.sid)) return E_FAIL;
    return S_OK;
}
HRESULT Save(Key& user, Bundle& bundle) noexcept {
    HRESULT hr = Mac(bundle, bundle.mac);
    return FAILED(hr) ? hr : WriteBlob(user.value, L"Record", &bundle, sizeof(bundle));
}
HRESULT Boot(Key& root, PCWSTR sid, BYTE output[16]) noexcept {
    Key boot;
    HRESULT hr = OpenProtected(root.value, L"Boot", boot);
    if (FAILED(hr)) return hr;
    return ReadBlob(boot.value, sid, output, 16);
}
HRESULT PrepareBoot(Key& root, PCWSTR sid, BYTE output[16]) noexcept {
    HRESULT hr = Random(output, 16); if (FAILED(hr)) return hr;
    Key boot; hr = OpenProtected(root.value, L"Boot", boot, true, true); if (FAILED(hr)) return hr;
    // Revoke eligibility BEFORE changing a durable record. A failed flush leaves
    // no active lease, even though RegSetValueEx made the changed record visible.
    const LSTATUS result = RegDeleteValueW(boot.value, sid);
    return result == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(result);
}
HRESULT ActivateBoot(Key& root, PCWSTR sid, const BYTE value[16]) noexcept {
    Key boot; HRESULT hr = OpenProtected(root.value, L"Boot", boot); if (FAILED(hr)) return hr;
    // This lease is deliberately volatile and is created only AFTER record flush.
    return HRESULT_FROM_WIN32(RegSetValueExW(boot.value, sid, 0, REG_BINARY, value, 16));
}
HRESULT Check(Key& root, const Record& record) noexcept {
    BYTE boot[16]{};
    HRESULT hr = Boot(root, record.sid, boot);
    const ULONGLONG now = Now();
    if (FAILED(hr) || !Equal(boot, record.boot, 16) || record.disabled || record.attempts >= 10 ||
        !record.passwordTime || now < record.passwordTime || now < record.lastSeen ||
        now - record.passwordTime >= 86400 * Second || now < record.until) return E_ACCESSDENIED;
    return S_OK;
}
HRESULT Cipher(Bundle& bundle, Password& password, bool encrypt) noexcept {
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_KEY_HANDLE key = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0) return E_FAIL;
    NTSTATUS result = BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE, reinterpret_cast<BYTE*>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)), sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    if (result >= 0) result = BCryptGenerateSymmetricKey(algorithm, &key, nullptr, 0, bundle.key, 32, 0);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info; BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = bundle.record.nonce; info.cbNonce = sizeof(bundle.record.nonce);
    info.pbTag = bundle.record.tag; info.cbTag = sizeof(bundle.record.tag);
    info.pbAuthData = reinterpret_cast<BYTE*>(bundle.record.sid); info.cbAuthData = sizeof(bundle.record.sid);
    ULONG written = 0;
    if (result >= 0) {
        if (encrypt) result = BCryptEncrypt(key, reinterpret_cast<BYTE*>(password.data()), bundle.record.cipherBytes,
            &info, nullptr, 0, bundle.record.cipher, sizeof(bundle.record.cipher), &written, 0);
        else result = BCryptDecrypt(key, bundle.record.cipher, bundle.record.cipherBytes, &info, nullptr, 0,
            reinterpret_cast<BYTE*>(password.data()), sizeof(password.value), &written, 0);
    }
    if (key) BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (result < 0 || written != bundle.record.cipherBytes) { password.Clear(); return E_FAIL; }
    if (!encrypt && (password.value[written / sizeof(wchar_t) - 1] != L'\0' ||
        wcsnlen_s(password.data(), password.value.size()) + 1 != written / sizeof(wchar_t))) { password.Clear(); return E_FAIL; }
    return S_OK;
}
HRESULT AuthenticatedIdentity(PCWSTR user, PCWSTR sid, PCWSTR password) noexcept {
    wchar_t verified[184]{};
    HRESULT hr = ValidatePassword(user, password, verified);
    if (FAILED(hr) || !sid || wcscmp(verified, sid)) return E_ACCESSDENIED;
    return S_OK;
}
}
HRESULT Enroll(PCWSTR name, PCWSTR sid, PCWSTR supplied, const Pattern& pattern) noexcept {
    if (!pattern.Valid() || !supplied || wcsnlen_s(supplied, 513) >= 513) return E_INVALIDARG;
    HRESULT hr = AuthenticatedIdentity(name, sid, supplied); if (FAILED(hr)) return hr;
    Lock lock; if (!lock.Acquire()) return E_ACCESSDENIED;
    Key root, user; hr = OpenUser(sid, root, user, true); if (FAILED(hr)) return hr;
    Bundle bundle;
    hr = StringCchCopyW(bundle.record.user, ARRAYSIZE(bundle.record.user), name);
    if (SUCCEEDED(hr)) hr = StringCchCopyW(bundle.record.sid, ARRAYSIZE(bundle.record.sid), sid);
    if (SUCCEEDED(hr)) hr = Random(bundle.key, sizeof(bundle.key));
    if (SUCCEEDED(hr)) hr = Random(bundle.record.salt, sizeof(bundle.record.salt));
    if (SUCCEEDED(hr)) hr = Random(bundle.record.nonce, sizeof(bundle.record.nonce));
    if (SUCCEEDED(hr)) hr = HashPattern(pattern, bundle.record.salt, bundle.record.hash);
    if (SUCCEEDED(hr)) hr = PrepareBoot(root, sid, bundle.record.boot);
    Password password;
    if (SUCCEEDED(hr)) hr = StringCchCopyW(password.data(), password.value.size(), supplied);
    bundle.record.cipherBytes = static_cast<DWORD>((wcslen(password.data()) + 1) * sizeof(wchar_t));
    if (SUCCEEDED(hr)) hr = Cipher(bundle, password, true);
    password.Clear();
    bundle.record.passwordTime = bundle.record.lastSeen = Now();
    if (SUCCEEDED(hr)) hr = Save(user, bundle);
    return FAILED(hr) ? hr : ActivateBoot(root, sid, bundle.record.boot);
}
HRESULT Refresh(PCWSTR name, PCWSTR sid, PCWSTR supplied) noexcept {
    HRESULT hr = AuthenticatedIdentity(name, sid, supplied); if (FAILED(hr)) return hr;
    Lock lock; if (!lock.Acquire()) return E_ACCESSDENIED;
    Key root, user; hr = OpenUser(sid, root, user); if (FAILED(hr)) return hr;
    Bundle bundle; hr = Read(user, sid, bundle); if (FAILED(hr)) return hr;
    if (_wcsicmp(bundle.record.user, name) || bundle.record.disabled) return E_ACCESSDENIED;
    // Refresh is not re-enrollment: stale/disabled stored credentials require replacement.
    Password current; hr = Cipher(bundle, current, false);
    if (FAILED(hr) || wcslen(current.data()) != wcslen(supplied) ||
        !Equal(reinterpret_cast<BYTE*>(current.data()), reinterpret_cast<const BYTE*>(supplied), (wcslen(supplied) + 1) * sizeof(wchar_t))) return E_ACCESSDENIED;
    current.Clear();
    hr = PrepareBoot(root, sid, bundle.record.boot); if (FAILED(hr)) return hr;
    bundle.record.passwordTime = bundle.record.lastSeen = Now();
    bundle.record.attempts = 0; bundle.record.until = 0;
    hr = Save(user, bundle);
    return FAILED(hr) ? hr : ActivateBoot(root, sid, bundle.record.boot);
}
HRESULT Eligible(PCWSTR sid) noexcept {
    Lock lock; if (!lock.Acquire()) return E_ACCESSDENIED;
    Key root, user; HRESULT hr = OpenUser(sid, root, user); if (FAILED(hr)) return hr;
    Bundle bundle; hr = Read(user, sid, bundle);
    return FAILED(hr) ? hr : Check(root, bundle.record);
}
HRESULT Verify(PCWSTR sid, const Pattern& pattern, Password* password) noexcept {
    if (password) password->Clear();
    if (!pattern.Valid()) return E_INVALIDARG;
    Lock lock; if (!lock.Acquire()) return E_ACCESSDENIED;
    Key root, user; HRESULT hr = OpenUser(sid, root, user); if (FAILED(hr)) return hr;
    Bundle bundle; hr = Read(user, sid, bundle); if (FAILED(hr)) return hr;
    hr = Check(root, bundle.record); if (FAILED(hr)) return hr;
    const DWORD attempts = bundle.record.attempts;
    const ULONGLONG until = bundle.record.until;
    // Reserve an attempt durably before testing a guess. A crash consumes it.
    ++bundle.record.attempts; bundle.record.lastSeen = Now();
    bundle.record.until = bundle.record.lastSeen + LockoutSeconds(bundle.record.attempts) * Second;
    hr = Save(user, bundle); if (FAILED(hr)) return hr;
    Secret<BYTE, 32> hash;
    hr = HashPattern(pattern, bundle.record.salt, hash.data());
    if (FAILED(hr) || !Equal(hash.data(), bundle.record.hash, 32)) return E_ACCESSDENIED;
    bundle.record.attempts = attempts; bundle.record.until = until;
    hr = Save(user, bundle); if (FAILED(hr)) return hr;
    if (password) hr = Cipher(bundle, *password, false);
    return hr;
}
HRESULT Disable(PCWSTR sid) noexcept {
    Lock lock; if (!lock.Acquire()) return E_ACCESSDENIED;
    Key root, user; HRESULT hr = OpenUser(sid, root, user); if (FAILED(hr)) return hr;
    Bundle bundle; hr = Read(user, sid, bundle); if (FAILED(hr)) return hr;
    bundle.record.disabled = 1;
    return Save(user, bundle);
}
}
