#pragma once
#include <windows.h>

namespace patlock {
namespace tpm {
// S_OK: TPM reported and Platform KSP opened (not a key-operation guarantee).
// S_FALSE: TBS explicitly reports no compatible TPM. Failure: operational error.
// Only S_FALSE permits enrollment to select baseline protection.
HRESULT Probe() noexcept;
// Compatibility helper; do not use to decide whether fallback is permissible.
bool Available() noexcept;

// identity is a unique per-enrollment SID/generation name supplied by the store.
// Uses a wiped hex UTF-16 encoding of separately derived 32-byte authorization
// with the Platform KSP PIN property. Hardware anti-hammering is not yet verified.
// Creates RSA-2048 machine key, restricts ACL, disables private export, wraps the
// AES key using OAEP-SHA256, and checks correct-auth reopen/decrypt roundtrip.
// On failure clears output and attempts deletion of the newly created key.
HRESULT ProtectKey(PCWSTR identity, const BYTE authValue[32], const BYTE plainKey[32],
                   BYTE wrappedKey[256]) noexcept;
HRESULT UnprotectKey(PCWSTR identity, const BYTE authValue[32], const BYTE wrappedKey[256],
                     BYTE plainKey[32]) noexcept;
HRESULT DeleteKey(PCWSTR identity) noexcept;
} // namespace tpm
} // namespace patlock
