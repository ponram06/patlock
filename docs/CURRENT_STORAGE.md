# Current record and submission protocol (format 7)

This supersedes historical storage implementation descriptions. The approved
Option A permits Pattern after reboot and defaults password-age expiry off.
Admin/SYSTEM compromise and offline extraction of an unencrypted baseline store
are outside these protections. BitLocker/device encryption is a prerequisite.

## Format 7 changes (2026-11-15)

Format 7 keeps the format 6 layout and size; only the meaning of two fields changes.
Format 5/6 records are still read and verified the old way. Re-enroll to upgrade.

- **TPM-mode pattern verifier is bound to the TPM-wrapped key.** Format 6 stored
  PBKDF2(pattern) next to the TPM-wrapped key. Anyone who copied the record could
  test all ~389,000 patterns offline, then use the TPM once with the right one. That
  skipped both the attempt counter and TPM anti-hammering. Format 7 stores
  HMAC(AES key, PBKDF2(pattern)) instead. The AES key comes only from the TPM with the
  right pattern-derived authorization, so a copied record no longer lets anyone test
  patterns offline. The HMAC is still checked after the TPM releases the key. A
  wrong pattern is therefore rejected even if a TPM ever released the key without
  enforcing authorization.
- **Consequence:** in format 7 TPM mode, every wrong pattern reaches the TPM and
  counts toward its dictionary-attack counter. That counter is shared with Windows
  Hello PIN. Patlock's own cap (10 failures, then password refresh) limits this to
  10 TPM failures per refresh. Each enrollment adds one more deliberate failure
  (the authorization self-test).
- **Password verifier is PBKDF2** (600,000 iterations, record salt, separate
  domain) instead of one HMAC keyed by a value stored in the same record. Used only
  by `--refresh` to confirm that the enrolled password copy is still current.
- **Baseline (no TPM) mode is off by default.** `Enroll` fails with
  ERROR_NOT_SUPPORTED, and existing baseline records are refused at unlock, unless
  `HKLM\SOFTWARE\Policies\Patlock` `AllowSoftwareKey` is 1. Baseline
  still keeps its key beside the ciphertext, and its verifier still permits offline
  pattern guessing. Only disk encryption and the registry DACL protect it.

Each SID has a protected SYSTEM/Administrators-only HKLM record. The fixed-size
versioned representation is HMAC/identity/bounds checked. AES-GCM uses a random
nonce and SID associated data. PBKDF2 verification and TPM authorization use
separate domains. Temporary pattern/password/authorization/key buffers are wiped.

Updates enlist the account key in a KTM registry transaction, replace its value
and commit. Closing an uncommitted transaction rolls it back. There is no visible
nontransactional write followed by a separately fallible flush. Unavailable KTM
or failed commit declines authentication. The VM regression test uses its own
temporary HKCU key for commit, rollback and removal.

A restricted global mutex serializes operations. Verification commits an attempt
before hashing. Wrong patterns keep it and delays escalate; valid patterns reset
consecutive failures. Ten failures require full-password refresh through the
elevated tool. Backward clock movement relative to stored activity declines.

Before releasing plaintext, verification commits disabled state and a unique
submission token. Matching successful Windows ReportResult clears both. Windows
failure leaves it disabled. If the provider knows the password never reached Windows
(tile deselected, window closed after success, serialization failed), it clears the
reservation itself. A missing result or a crash after serialization leaves the record
disabled. `--refresh` with the unchanged enrolled password re-enables it; a changed
password needs re-enrollment. Serialized output goes to Windows only after reservation.
Plaintext is wiped after packing; wrong tokens cannot complete a new enrollment.

TPM enrollment creates a unique generation, requests policy/ACL restrictions and
checks an authorized wrapping roundtrip. Only explicit missing hardware permits
baseline selection; operational errors abort. Failed new enrollment cleans its
new key. Old keys retire only after replacement commit. Retirement failure warns
without undoing the saved credential. Crashes may leave unused historical keys;
do not delete unknown machine keys to clean them up.

`patlock_enroll --remove` verifies the account password and confirmation, disables
the record, removes its active TPM generation and transactionally deletes the
record. Provider uninstall otherwise preserves enrollment. Forensic erasure of
registry remnants/backups is not claimed; keep disk encryption active.

Official persistence sources:

- https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeytransactedw
- https://learn.microsoft.com/en-us/windows/win32/api/ktmw32/nf-ktmw32-createtransaction
- https://learn.microsoft.com/en-us/windows/win32/api/ktmw32/nf-ktmw32-committransaction

Current-candidate runtime behavior remains pending VM acceptance.
