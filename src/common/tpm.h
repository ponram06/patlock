#pragma once
#include <windows.h>

namespace patlock {
namespace tpm {
/// Distinguishes TPM hardware availability from operational failures.
/// @return S_OK: TPM hardware present and PCP provider accessible (may still fail operations).
///         S_FALSE: TPM hardware definitively absent (permits baseline fallback).
///         E_FAIL or NTE_*: Provider present but TPM unavailable/locked (abort enrollment).
HRESULT Probe() noexcept;

/// Returns true if TPM hardware is available for key operations.
/// Compatibility helper; do not use to decide whether fallback is permissible.
/// Use Probe() for enrollment decisions.
bool Available() noexcept;

/// Validates that TPM authorization enforcement is functional.
/// Creates a temporary test key, wraps plaintext with authA, attempts decrypt with
/// wrong authB (must fail), then decrypt with correct authA (must succeed).
/// @return S_OK if authorization enforcement works correctly.
///         E_ACCESSDENIED or E_FAIL if TPM does not enforce authorization.
///         Other HRESULT on operational errors.
HRESULT ValidateAuthorization() noexcept;

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
// Deletes Patlock machine keys whose identity starts with identityPrefix, except
// keepIdentity (may be null). Removes keys orphaned by earlier failed deletes.
// Best effort; returns the first failure.
HRESULT PurgeKeys(PCWSTR identityPrefix, PCWSTR keepIdentity) noexcept;
} // namespace tpm
} // namespace patlock
