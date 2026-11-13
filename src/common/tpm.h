#pragma once
#include <windows.h>

namespace patlock {
namespace tpm {

// Check whether the Microsoft Platform Crypto Provider (TPM KSP) is available.
bool Available() noexcept;

// Creates an RSA 2048 key in the TPM for this SID, with separately derived authValue
// as its usage authorization. Encrypts plainKey (32 bytes) with OAEP SHA-256.
// Returns the 256-byte wrappedKey.
HRESULT ProtectKey(PCWSTR sid, const BYTE authValue[32], const BYTE plainKey[32],
                   BYTE wrappedKey[256]) noexcept;

// Opens the RSA 2048 key in the TPM for this SID, supplies authValue,
// and decrypts wrappedKey (256 bytes) with OAEP SHA-256 to recover plainKey (32 bytes).
HRESULT UnprotectKey(PCWSTR sid, const BYTE authValue[32], const BYTE wrappedKey[256],
                     BYTE plainKey[32]) noexcept;

// Deletes the persisted TPM key for this SID.
HRESULT DeleteKey(PCWSTR sid) noexcept;

} // namespace tpm
} // namespace patlock
