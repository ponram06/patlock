#include "security.h"
#include "tpm.h"
#include "persistence.h"
#include <bcrypt.h>
#include <sddl.h>
#include <strsafe.h>
#include <cstddef>

namespace patlock {
namespace {
HRESULT g_lastRetireError = S_OK;
constexpr PCWSTR RootPath = L"SOFTWARE\\Patlock";
constexpr PCWSTR Access = L"D:P(A;;KA;;;SY)(A;;KA;;;BA)";
constexpr ULONGLONG Second = 10000000ULL;
// Wall-clock drift tolerated before the pattern is refused. VM time sync, NTP and
// snapshot restores move the clock backwards by seconds or minutes; without a
// tolerance the "clock went backwards" guard bricked the pattern until refresh.
constexpr ULONGLONG kClockSkewTolerance = 10ULL * 60 * Second;
// A verified submission that never reached Windows (tile deselected, LogonUI
// restarted, crash) expires after this long instead of blocking the pattern forever.
constexpr ULONGLONG kPendingSubmission = 120ULL * Second;

struct Record {
    DWORD magic = 0x5041544c;
    DWORD version = 6;
    DWORD iterations = HashIterations;
    DWORD attempts = 0;
    DWORD disabled = 0;
    DWORD cipherBytes = 0;
    DWORD tpmMode = 0;       // 1 = TPM-backed, 0 = baseline random key
    DWORD reserved = 0;
    ULONGLONG until = 0;
    ULONGLONG passwordTime = 0;
    ULONGLONG lastSeen = 0;
    BYTE boot[16]{};
    BYTE submission[16]{};
    BYTE salt[16]{};
    BYTE hash[32]{};
    wchar_t user[256]{};
    wchar_t sid[184]{};
    BYTE nonce[12]{};
    BYTE tag[16]{};
    BYTE passwordMac[32]{};
    BYTE cipher[1026]{};
    BYTE tpmWrappedKey[256]{};
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
    bool Init(PCWSTR text) noexcept {
        return ConvertStringSecurityDescriptorToSecurityDescriptorW(text, SDDL_REVISION_1, &value, nullptr) != FALSE;
    }
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
    return WriteRegistryBlob(key, name, input, length);
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

HRESULT ComputePasswordMac(const BYTE key[32], PCWSTR password, BYTE output[32]) noexcept {
    if (!key || !password) return E_INVALIDARG;

    // Fixed-size buffer to prevent length-based timing attacks
    // Pad password to 512 characters (1024 bytes including null terminator)
    constexpr size_t kMaxPasswordChars = 512;
    wchar_t padded[kMaxPasswordChars]{};
    SecureZeroMemory(padded, sizeof(padded));

    const size_t passwordLen = wcsnlen_s(password, kMaxPasswordChars);
    if (passwordLen >= kMaxPasswordChars) return E_INVALIDARG;

    wcscpy_s(padded, kMaxPasswordChars, password);
    // Remaining bytes are already zero-padded

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0) return E_FAIL;

    NTSTATUS result = BCryptCreateHash(algorithm, &hash, nullptr, 0, const_cast<BYTE*>(key), 32, 0);

    // Always hash the full fixed-size buffer (including null terminator and padding)
    if (result >= 0) {
        result = BCryptHashData(hash, reinterpret_cast<BYTE*>(padded), static_cast<DWORD>(sizeof(padded)), 0);
    }

    if (result >= 0) result = BCryptFinishHash(hash, output, 32, 0);

    SecureZeroMemory(padded, sizeof(padded));

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
    if (FAILED(hr) || !Equal(mac.data(), bundle.mac, 32) || record.magic != 0x5041544c || (record.version != 5 && record.version != 6) ||
        record.iterations != HashIterations || record.attempts > 10 || record.disabled > 1 ||
        record.cipherBytes < 4 || record.cipherBytes > sizeof(record.cipher) || record.cipherBytes % 2 ||
        record.tpmMode > 1 ||
        wcsnlen_s(record.user, ARRAYSIZE(record.user)) >= ARRAYSIZE(record.user) || !record.user[0] ||
        wcsnlen_s(record.sid, ARRAYSIZE(record.sid)) >= ARRAYSIZE(record.sid) || wcscmp(sid, record.sid)) return E_FAIL;
    // Pending = disabled with a live one-shot token. A permanent disable (Windows
    // declined the stored credential) zeroes the token and never expires here.
    const BYTE none[16]{};
    const ULONGLONG now = Now();
    if (record.disabled && !Equal(record.submission, none, 16) && now >= record.lastSeen &&
        now - record.lastSeen >= kPendingSubmission) {
        record.disabled = 0;
        SecureZeroMemory(record.submission, sizeof(record.submission));
    }
    return S_OK;
}

HRESULT Save(Key& user, Bundle& bundle) noexcept {
    HRESULT hr = Mac(bundle, bundle.mac);
    return FAILED(hr) ? hr : WriteBlob(user.value, L"Record", &bundle, sizeof(bundle));
}

// Option A: Drop restart requirement, retain escalating lockout and attempt limits.
// Set kMaxPasswordAge to 0 to disable password age expiration (e.g. 30ULL * 86400 * Second).
constexpr ULONGLONG kMaxPasswordAge = 0;

// A software-key ("baseline") record keeps its AES key next to the ciphertext, so
// the pattern gives no cryptographic protection against anyone who can read the
// registry. A TPM-backed key is therefore required unless an administrator opts
// out on a throwaway test machine:
//   reg add HKLM\SOFTWARE\Policies\Patlock /v AllowSoftwareKey /t REG_DWORD /d 1 /f
bool SoftwareKeyAllowed() noexcept {
    DWORD value = 0, size = sizeof(value);
    const LSTATUS result = RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Patlock", L"AllowSoftwareKey",
        RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, nullptr, &value, &size);
    return result == ERROR_SUCCESS && value == 1;
}

// Security audit trail in the Application log (source "Patlock"). Text carries only
// the account SID and counters, never the pattern, password, keys or hashes.
void Audit(WORD type, DWORD id, PCWSTR text) noexcept {
    HANDLE source = RegisterEventSourceW(nullptr, L"Patlock");
    if (!source) return;
    ReportEventW(source, type, 0, id, nullptr, 1, 0, &text, nullptr);
    DeregisterEventSource(source);
}

HRESULT Check(Key& /*root*/, const Record& record) noexcept {
    const ULONGLONG now = Now();
    // Lockout is judged against the later of "now" and the last recorded time, so
    // tolerating small clock drift never shortens a lockout.
    const ULONGLONG effective = now > record.lastSeen ? now : record.lastSeen;
    if (record.disabled || record.attempts >= 10 || !record.passwordTime ||
        (record.tpmMode != 1 && !SoftwareKeyAllowed()) ||
        now + kClockSkewTolerance < record.passwordTime || now + kClockSkewTolerance < record.lastSeen ||
        (kMaxPasswordAge > 0 && now - record.passwordTime >= kMaxPasswordAge) ||
        effective < record.until) {
        return E_ACCESSDENIED;
    }
    return S_OK;
}

HRESULT CipherWithKey(const BYTE key[32], Record& record, Password& password, bool encrypt) noexcept {
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_KEY_HANDLE hKey = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0) return E_FAIL;
    NTSTATUS result = BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE, reinterpret_cast<BYTE*>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)), sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    if (result >= 0) result = BCryptGenerateSymmetricKey(algorithm, &hKey, nullptr, 0, const_cast<BYTE*>(key), 32, 0);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info; BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = record.nonce; info.cbNonce = sizeof(record.nonce);
    info.pbTag = record.tag; info.cbTag = sizeof(record.tag);
    info.pbAuthData = reinterpret_cast<BYTE*>(record.sid); info.cbAuthData = sizeof(record.sid);
    ULONG written = 0;
    if (result >= 0) {
        if (encrypt) result = BCryptEncrypt(hKey, reinterpret_cast<BYTE*>(password.data()), record.cipherBytes,
            &info, nullptr, 0, record.cipher, sizeof(record.cipher), &written, 0);
        else result = BCryptDecrypt(hKey, record.cipher, record.cipherBytes, &info, nullptr, 0,
            reinterpret_cast<BYTE*>(password.data()), sizeof(password.value), &written, 0);
    }
    if (hKey) BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (result < 0 || written != record.cipherBytes) { password.Clear(); return E_FAIL; }
    if (!encrypt && (password.value[written / sizeof(wchar_t) - 1] != L'\0' ||
        wcsnlen_s(password.data(), password.value.size()) + 1 != written / sizeof(wchar_t))) { password.Clear(); return E_FAIL; }
    return S_OK;
}

HRESULT KeyIdentity(const Record& record, wchar_t (&identity)[240]) noexcept {
    HRESULT hr = StringCchCopyW(identity, ARRAYSIZE(identity), record.sid);
    if (SUCCEEDED(hr)) hr = StringCchCatW(identity, ARRAYSIZE(identity), L"_");
    for (BYTE value : record.nonce) {
        wchar_t hex[3]{};
        if (SUCCEEDED(hr)) hr = StringCchPrintfW(hex, ARRAYSIZE(hex), L"%02x", static_cast<unsigned>(value));
        if (SUCCEEDED(hr)) hr = StringCchCatW(identity, ARRAYSIZE(identity), hex);
    }
    return hr;
}
HRESULT AuthenticatedIdentity(PCWSTR user, PCWSTR sid, PCWSTR password) noexcept {
    wchar_t verified[184]{};
    HRESULT hr = ValidatePassword(user, password, verified);
    if (FAILED(hr) || !sid || wcscmp(verified, sid)) return E_ACCESSDENIED;
    return S_OK;
}
} // anon namespace

HRESULT Enroll(PCWSTR name, PCWSTR sid, PCWSTR supplied, const Pattern& pattern) noexcept {
    if (!pattern.Valid() || !supplied || wcsnlen_s(supplied, 513) >= 513) return E_INVALIDARG;
    HRESULT hr = AuthenticatedIdentity(name, sid, supplied); if (FAILED(hr)) return hr;
    Lock lock; if (!lock.Acquire()) return E_ACCESSDENIED;
    Key root, user; hr = OpenUser(sid, root, user, true); if (FAILED(hr)) return hr;

    // Retire the previous generation only after the replacement record commits.
    Bundle previous;
    wchar_t previousIdentity[240]{};
    const bool retirePrevious = SUCCEEDED(Read(user, sid, previous)) && previous.record.tpmMode == 1 &&
        SUCCEEDED(KeyIdentity(previous.record, previousIdentity));

    wchar_t createdIdentity[240]{};
    bool createdTpm = false;
    Bundle bundle;
    hr = StringCchCopyW(bundle.record.user, ARRAYSIZE(bundle.record.user), name);
    if (SUCCEEDED(hr)) hr = StringCchCopyW(bundle.record.sid, ARRAYSIZE(bundle.record.sid), sid);
    if (SUCCEEDED(hr)) hr = Random(bundle.key, sizeof(bundle.key));
    if (SUCCEEDED(hr)) hr = Random(bundle.record.salt, sizeof(bundle.record.salt));
    if (SUCCEEDED(hr)) hr = Random(bundle.record.nonce, sizeof(bundle.record.nonce));
    if (SUCCEEDED(hr)) hr = HashPattern(pattern, bundle.record.salt, bundle.record.hash);
    if (FAILED(hr)) return hr;
    hr = ComputePasswordMac(bundle.key, supplied, bundle.record.passwordMac);
    if (FAILED(hr)) return hr;

    // Key management: validate TPM, then try TPM-backed protection; only fall back to baseline on S_FALSE.
    Secret<BYTE, 32> aesKey;
    hr = Random(aesKey.data(), 32);
    if (FAILED(hr)) return hr;

    const HRESULT probe = tpm::Probe();
    if (FAILED(probe)) {
        // TPM provider exists but hardware is unavailable/locked - abort enrollment
        return probe;
    }

    if (probe == S_OK) {
        // TPM hardware available - validate authorization enforcement before using it
        HRESULT validationHr = tpm::ValidateAuthorization();
        if (FAILED(validationHr)) {
            // TPM exists but authorization enforcement is broken - abort enrollment
            return validationHr;
        }

        Secret<BYTE, 32> authorization;
        HRESULT tpmHr = DeriveTpmAuthorization(pattern, bundle.record.salt, authorization.data());
        wchar_t identity[240]{};
        if (SUCCEEDED(tpmHr)) tpmHr = KeyIdentity(bundle.record, identity);
        if (SUCCEEDED(tpmHr)) tpmHr = tpm::ProtectKey(identity, authorization.data(), aesKey.data(), bundle.record.tpmWrappedKey);


        if (SUCCEEDED(tpmHr)) {
            bundle.record.tpmMode = 1;
            CopyMemory(createdIdentity, identity, sizeof(identity));
            createdTpm = true;
        } else {
            // TPM operation failed after validation passed - abort enrollment, don't fall back
            return tpmHr;
        }
    } else {
        // probe == S_FALSE: TPM definitively absent. A software key does not protect
        // the stored password from anyone who can read the registry, so refuse
        // unless an administrator explicitly allowed it (test machines only).
        if (!SoftwareKeyAllowed()) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        bundle.record.tpmMode = 0;
        SecureZeroMemory(bundle.record.tpmWrappedKey, sizeof(bundle.record.tpmWrappedKey));
        CopyMemory(aesKey.data(), bundle.key, 32);
    }

    hr = Random(bundle.record.boot, sizeof(bundle.record.boot));
    if (FAILED(hr)) return hr;

    Password password;
    hr = StringCchCopyW(password.data(), password.value.size(), supplied);
    if (FAILED(hr)) return hr;

    bundle.record.cipherBytes = static_cast<DWORD>((wcslen(password.data()) + 1) * sizeof(wchar_t));
    hr = CipherWithKey(aesKey.data(), bundle.record, password, true);
    password.Clear();
    aesKey.Clear();
    if (FAILED(hr)) {
        if (createdTpm) tpm::DeleteKey(createdIdentity);
        return hr;
    }

    bundle.record.passwordTime = bundle.record.lastSeen = Now();
    hr = Save(user, bundle);

    if (FAILED(hr)) {
        // Record write failed - clean up the new TPM key
        if (createdTpm) {
            const HRESULT cleanup = tpm::DeleteKey(createdIdentity);
            if (FAILED(cleanup)) return cleanup;  // Propagate cleanup failure
        }
        return hr;
    }

    {
        wchar_t text[160]{};
        StringCchPrintfW(text, ARRAYSIZE(text), L"Pattern enrolled for %s (%s key).", sid, bundle.record.tpmMode == 1 ? L"TPM-backed" : L"software");
        Audit(EVENTLOG_INFORMATION_TYPE, 2010, text);
    }

    // Record committed successfully - now safe to retire the old TPM key
    if (retirePrevious) {
        HRESULT retireHr = tpm::DeleteKey(previousIdentity);
        if (FAILED(retireHr)) {
            // Log the orphaned key but don't fail the enrollment
            // TODO: Add event log entry for manual cleanup: previousIdentity
            g_lastRetireError = retireHr;
            return S_FALSE;  // Partial success: new key works, old key orphaned
        }
    }

    return S_OK;
}

HRESULT Refresh(PCWSTR name, PCWSTR sid, PCWSTR supplied) noexcept {
    HRESULT hr = AuthenticatedIdentity(name, sid, supplied); if (FAILED(hr)) return hr;
    Lock lock; if (!lock.Acquire()) return E_ACCESSDENIED;
    Key root, user; hr = OpenUser(sid, root, user); if (FAILED(hr)) return hr;
    Bundle bundle; hr = Read(user, sid, bundle); if (FAILED(hr)) return hr;
    if (_wcsicmp(bundle.record.user, name)) return E_ACCESSDENIED;

    // Verify supplied password matches the enrolled password using constant-time MAC comparison.
    // This allows refresh without requiring pattern entry, while ensuring changed passwords are rejected.
    Secret<BYTE, 32> expectedMac;
    hr = ComputePasswordMac(bundle.key, supplied, expectedMac.data());
    if (FAILED(hr) || !Equal(expectedMac.data(), bundle.record.passwordMac, 32)) return E_ACCESSDENIED;

    // The password was just proven twice (Windows + enrolled MAC), so refresh may
    // also clear a disabled record (a stale pending submission or a transient
    // Windows failure). A changed password fails the MAC check above: re-enroll.
    bundle.record.passwordTime = bundle.record.lastSeen = Now();
    bundle.record.attempts = 0; bundle.record.until = 0;
    bundle.record.disabled = 0;
    SecureZeroMemory(bundle.record.submission, sizeof(bundle.record.submission));
    return Save(user, bundle);
}

HRESULT Eligible(PCWSTR sid) noexcept {
    Lock lock; if (!lock.Acquire()) return E_ACCESSDENIED;
    Key root, user; HRESULT hr = OpenUser(sid, root, user); if (FAILED(hr)) return hr;
    Bundle bundle; hr = Read(user, sid, bundle);
    return FAILED(hr) ? hr : Check(root, bundle.record);
}

HRESULT RemoveEnrollment(PCWSTR name, PCWSTR sid, PCWSTR password) noexcept {
    HRESULT hr = AuthenticatedIdentity(name, sid, password); if (FAILED(hr)) return hr;
    Lock lock; if (!lock.Acquire()) return E_ACCESSDENIED;
    Key root, user; hr = OpenUser(sid, root, user); if (FAILED(hr)) return hr;
    Bundle bundle; hr = Read(user, sid, bundle); if (FAILED(hr)) return hr;
    bundle.record.disabled = 1;
    hr = Save(user, bundle); if (FAILED(hr)) return hr;
    if (bundle.record.tpmMode) {
        wchar_t identity[240]{};
        hr = KeyIdentity(bundle.record, identity);
        if (SUCCEEDED(hr)) hr = tpm::DeleteKey(identity);
        if (FAILED(hr)) return hr;
    }
    return DeleteRegistryValue(user.value, L"Record");
}

HRESULT QueryRetryAfter(PCWSTR sid, DWORD* seconds) noexcept {
    if (!seconds) return E_POINTER;
    *seconds = 0;
    Lock lock; if (!lock.Acquire()) return E_ACCESSDENIED;
    Key root, user; HRESULT hr = OpenUser(sid, root, user); if (FAILED(hr)) return hr;
    Bundle bundle; hr = Read(user, sid, bundle); if (FAILED(hr)) return hr;
    const Record& record = bundle.record;
    const ULONGLONG now = Now();
    const ULONGLONG effective = now > record.lastSeen ? now : record.lastSeen;
    if (record.disabled || record.attempts >= 10 || (record.tpmMode != 1 && !SoftwareKeyAllowed()) ||
        now + kClockSkewTolerance < record.lastSeen ||
        now + kClockSkewTolerance < record.passwordTime ||
        !record.passwordTime || (kMaxPasswordAge && now - record.passwordTime >= kMaxPasswordAge)) return E_ACCESSDENIED;
    if (record.until > effective) *seconds = static_cast<DWORD>((record.until - effective + Second - 1) / Second);
    return S_OK;
}

HRESULT Verify(PCWSTR sid, const Pattern& pattern, Password* password, BYTE* submission) noexcept {
    if (password) password->Clear();
    if (submission) SecureZeroMemory(submission, 16);
    if (password && !submission) return E_INVALIDARG;
    if (!pattern.Valid()) return E_INVALIDARG;
    Lock lock; if (!lock.Acquire()) return E_ACCESSDENIED;
    Key root, user; HRESULT hr = OpenUser(sid, root, user); if (FAILED(hr)) return hr;
    Bundle bundle; hr = Read(user, sid, bundle); if (FAILED(hr)) return hr;
    hr = Check(root, bundle.record); if (FAILED(hr)) return hr;

    // Reserve an attempt durably before testing a guess. A crash consumes it.
    ++bundle.record.attempts; bundle.record.lastSeen = Now();
    bundle.record.until = bundle.record.lastSeen + LockoutSeconds(bundle.record.attempts) * Second;
    hr = Save(user, bundle); if (FAILED(hr)) return hr;

    Secret<BYTE, 32> hash;
    hr = HashPattern(pattern, bundle.record.salt, hash.data());
    if (FAILED(hr) || !Equal(hash.data(), bundle.record.hash, 32)) {
        wchar_t text[200]{};
        StringCchPrintfW(text, ARRAYSIZE(text), L"Incorrect pattern for %s (failed attempt %lu of 10).", sid, bundle.record.attempts);
        Audit(EVENTLOG_WARNING_TYPE, 2001, text);
        return E_ACCESSDENIED;
    }

    // Resolve the AES decryption key.
    Secret<BYTE, 32> aesKey;
    if (bundle.record.tpmMode == 1) {
        // Use a separate derived authorization, not the persisted verifier.
        Secret<BYTE, 32> authorization;
        hr = DeriveTpmAuthorization(pattern, bundle.record.salt, authorization.data());
        wchar_t identity[240]{};
        if (SUCCEEDED(hr)) hr = KeyIdentity(bundle.record, identity);
        if (SUCCEEDED(hr)) hr = tpm::UnprotectKey(identity, authorization.data(), bundle.record.tpmWrappedKey, aesKey.data());
        if (FAILED(hr)) return E_ACCESSDENIED;
    } else {
        CopyMemory(aesKey.data(), bundle.key, 32);
    }

    // Pattern matched successfully! Reset failure counter and lockout timer.
    bundle.record.attempts = 0; bundle.record.until = 0;
    {
        wchar_t text[160]{};
        StringCchPrintfW(text, ARRAYSIZE(text), L"Pattern accepted for %s.", sid);
        Audit(EVENTLOG_INFORMATION_TYPE, 2000, text);
    }

    if (password) {
        // Generate one-shot submission token BEFORE decrypting password
        hr = Random(bundle.record.submission, sizeof(bundle.record.submission));
        if (FAILED(hr)) { aesKey.Clear(); return hr; }

        // Mark as disabled and persist the submission token BEFORE returning plaintext
        // This ensures crash between decrypt and ReportResult is recoverable
        bundle.record.disabled = 1;
        hr = Save(user, bundle);
        if (FAILED(hr)) {
            aesKey.Clear();
            SecureZeroMemory(bundle.record.submission, sizeof(bundle.record.submission));
            return hr;
        }

        // Token is now durably persisted - safe to decrypt password
        hr = CipherWithKey(aesKey.data(), bundle.record, *password, false);
        aesKey.Clear();

        if (FAILED(hr)) {
            password->Clear();
            return hr;
        }

        // Copy submission token to output
        CopyMemory(submission, bundle.record.submission, 16);
    } else {
        aesKey.Clear();
        hr = Save(user, bundle);
    }

    return hr;
}

HRESULT FinishSubmission(PCWSTR sid, const BYTE submission[16]) noexcept {
    Lock lock; if (!lock.Acquire()) return E_ACCESSDENIED;
    Key root, user; HRESULT hr = OpenUser(sid, root, user); if (FAILED(hr)) return hr;
    Bundle bundle; hr = Read(user, sid, bundle); if (FAILED(hr)) return hr;
    BYTE empty[16]{};
    if (!bundle.record.disabled || !submission || Equal(submission, empty, 16) || !Equal(submission, bundle.record.submission, 16)) return E_ACCESSDENIED;
    bundle.record.disabled = 0;
    bundle.record.attempts = 0;
    bundle.record.until = 0;
    SecureZeroMemory(bundle.record.submission, sizeof(bundle.record.submission));
    return Save(user, bundle);
}

HRESULT Disable(PCWSTR sid) noexcept {
    Lock lock; if (!lock.Acquire()) return E_ACCESSDENIED;
    Key root, user; HRESULT hr = OpenUser(sid, root, user); if (FAILED(hr)) return hr;
    Bundle bundle; hr = Read(user, sid, bundle); if (FAILED(hr)) return hr;
    bundle.record.disabled = 1;
    SecureZeroMemory(bundle.record.submission, sizeof(bundle.record.submission)); // Permanent: never auto-expires.
    {
        wchar_t text[200]{};
        StringCchPrintfW(text, ARRAYSIZE(text), L"Pattern disabled for %s (Windows declined the stored credential, or an administrator request). Full-password refresh required.", sid);
        Audit(EVENTLOG_WARNING_TYPE, 2002, text);
    }
    return Save(user, bundle);
}

HRESULT LastRetireError() noexcept { return g_lastRetireError; }

HRESULT QueryTpmMode(PCWSTR sid) noexcept {
    Lock lock; if (!lock.Acquire()) return E_ACCESSDENIED;
    Key root, user; HRESULT hr = OpenUser(sid, root, user); if (FAILED(hr)) return hr;
    Bundle bundle; hr = Read(user, sid, bundle); if (FAILED(hr)) return hr;
    return bundle.record.tpmMode == 1 ? S_OK : S_FALSE;
}

} // namespace patlock
