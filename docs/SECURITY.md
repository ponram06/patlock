# Security Notes

## Threat model
In scope:
- Someone guessing the pattern at the lock screen (physical access, online guessing).
- Someone watching the user draw it (shoulder surfing).
- A normal local user or malware without admin rights trying to read the stored secret.
- A stolen laptop with an offline disk.

Out of scope (be honest about this in the final README):
- An attacker with admin/SYSTEM rights on the running, unlocked machine.
- Hardware and firmware attacks.
- Anything that makes this stronger than a long password. It is not.

## Core trade-off
The provider must be able to produce the Windows password after a pattern match. So the password is stored in recoverable (encrypted) form.
Protection comes from BitLocker/device encryption, access-locked storage, lockout
and TPM-backed wrapping where supported. TPM authorization and anti-hammering
require TPM_VALIDATION.md checks. Option A has no periodic password requirement.

## Storage design
Record contents: format version, account name, salt, PBKDF2 hash of the pattern, encrypted password (AES-GCM, with nonce and tag),
attempt counter, lockout-until time, last full-password time.

Baseline:
- Random 256-bit key held in a store readable only by SYSTEM and Administrators (LSA secret or locked-down HKLM key).
- AES-GCM encryption of the password; a fresh random nonce per encryption.
- Pattern verified with PBKDF2-HMAC-SHA256, per-user random 16-byte salt, high iteration count, constant-time compare.

Hardened:
- TPM-backed key supplied a separate pattern authorization. Hardware rate limiting
  is not asserted until verified on the target TPM/KSP.
- Fall back to baseline if no TPM. Document which mode is active.

## Rules for code
- Availability across reboots & lockout (Option A): Pattern unlock remains active across reboots, matching Windows Hello PIN convenience. Durably persisted escalating lockouts (30s, 1m, 5m, 15m; disabled at 10) protect against brute-force guessing. Password freshness expiration is configurable via `kMaxPasswordAge` (default disabled). Stale passwords resulting from password changes are handled by one-shot failure detection and fallback to the built-in Password tile.
- Never log the pattern, password, keys, salts-with-secrets, or decrypted data.
- SecureZeroMemory all sensitive buffers right after use; avoid copying them into std::string where it can be avoided.
- Constant-time comparison for hashes and tags.
- Validate every field read from the record; a corrupt record means Pattern is disabled, not a crash.
- Fail closed: any exception or error leads to hiding/declining the tile, never unlocking.
- No hardcoded keys or passwords. No debug backdoors in release builds.
- Lockout counters are persisted before the result of an attempt is shown.
- After one failed submission of the stored password (for example, it was changed), stop auto-submitting and require re-enrollment to avoid Windows account lockout.

## Known flaws and mitigations
See `PLAN.md` section 7. Keep that list updated as real tests reveal more.

## Hardening policy (software keys and audit trail)
- **TPM required by default.** A software-key (baseline) record stores its AES key beside the
  ciphertext, so the pattern is only a gate: anyone able to read `HKLM\SOFTWARE\Patlock`
  can decrypt the stored password. Enrollment now refuses baseline mode, and existing baseline
  records are treated as unavailable, unless an administrator sets
  `HKLM\SOFTWARE\Policies\Patlock\AllowSoftwareKey` (DWORD 1). Use that only on throwaway
  test machines.
- **Audit trail.** Enrollment, accepted and incorrect patterns, and disabling are written to the
  Application log (source `Patlock`, event IDs 2000-2002 and 2010). Events carry only the account
  SID and counters.
- **Known limits (not fixed here).** The PBKDF2 verifier is stored in the record, so an
  administrator-level reader can guess the roughly 380,000 possible patterns offline; the
  password MAC is likewise a fast offline oracle for the Windows password. Removing the stored
  verifier in TPM mode depends on proven TPM authorization enforcement (TPM_VALIDATION.md).
  An independent security review is still required.
