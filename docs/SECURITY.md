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
Protection comes from: BitLocker/Device Encryption, access-locked storage, TPM binding, lockout, and requiring the full password regularly.

## Storage design
Record contents: format version, account name, salt, PBKDF2 hash of the pattern, encrypted password (AES-GCM, with nonce and tag),
attempt counter, lockout-until time, last full-password time.

Baseline:
- Random 256-bit key held in a store readable only by SYSTEM and Administrators (LSA secret or locked-down HKLM key).
- AES-GCM encryption of the password; a fresh random nonce per encryption.
- Pattern verified with PBKDF2-HMAC-SHA256, per-user random 16-byte salt, high iteration count, constant-time compare.

Hardened:
- TPM-backed key requiring an authorization value derived from the pattern, so guesses are rate-limited by hardware.
- Fall back to baseline if no TPM. Document which mode is active.

## Rules for code
- Approved password freshness flow: the elevated desktop enrollment tool
  explicitly verifies the local account's password to refresh eligibility.
  Built-in Password, PIN and Hello sign-ins alone do not reset Patlock state.
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
