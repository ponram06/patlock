# Current record and submission protocol (format 6)

This supersedes historical storage implementation descriptions. The approved
Option A permits Pattern after reboot and defaults password-age expiry off.
Admin/SYSTEM compromise and offline extraction of an unencrypted baseline store
are outside these protections. BitLocker/device encryption is a prerequisite.
An extracted verifier permits offline pattern guessing; TPM wrapping does not
remove that verifier attack.

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
submission token. Matching successful Windows ReportResult clears both. Failure,
cancellation after verification, missing result or crash leaves it disabled;
re-enrollment recovers. Serialized output goes to Windows only after reservation.
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
