# Record format, TPM hardening, and password-refresh implementation

Current modes:
- **TPM-hardened (primary when TPM available)**: AES-256 key is wrapped with RSA-2048 OAEP via the Microsoft Platform Crypto Provider (`MS_PLATFORM_CRYPTO_PROVIDER`). The TPM key's usage authorization is bound to the pattern hash (`PCP_USAGEAUTH`). Guessing is rate-limited by the TPM's hardware anti-hammering logic.
- **Baseline (fallback)**: When no TPM is detected, random 256-bit AES key is stored in the SYSTEM/Administrators-only registry store. BitLocker/device encryption is a prerequisite.

The elevated native C++ tool uses a nonpersistent credential prompt, validates
the unqualified local account password with LogonUserW, and obtains the SID from
the returned token. Enrollment draws twice and requires matching sequences of at
least five dots. Enrollment/refresh verify the password again before writing.
No account name, password or pattern is accepted on the command line.

Each SID has one REG_BINARY record under the 64-bit HKLM\SOFTWARE\Patlock key.
Root and account keys use a protected DACL granting only SYSTEM and Administrators
full access. Opening a key reapplies that DACL before reading a secret. The
version-3 record contains a random 256-bit HMAC key, account identity, PBKDF2 salt and
hash, AES-GCM ciphertext/nonce/tag, `tpmMode` flag, `tpmWrappedKey` (256 bytes for RSA-OAEP),
`passwordMac` (HMAC-SHA256 of enrolled password), attempt state, freshness and an HMAC over the
record. It is a fixed C++ layout for this build, not a general interchange format.
Unknown size/version, invalid bounds, changed SID or bad MAC decline the tile.

PBKDF2-HMAC-SHA256 uses 600,000 iterations and a random 16-byte salt. Input is
the prototype's ASCII digit encoding. Hash comparison examines every byte.
AES-GCM uses a fresh random 12-byte nonce at enrollment, a 16-byte tag and the
record SID as associated data. Credential/key/derived-hash buffers are wiped by
RAII and on explicit completion; Windows/crypto-library internal copies are
outside this code's direct control.

A global ACL-restricted mutex serializes record changes. Timeout or abandonment
declines. Before testing a guess, the provider reserves an attempt and flushes
the record to the registry. Wrong patterns keep it; correct patterns restore
the previous count. A crash can consume an attempt. Success does not reset the
failure history; only explicit password verification does. Delays are 30 seconds,
1 minute, 5 minutes, 15 minutes, then 15 minutes; ten failures disable pattern
eligibility until password refresh. Clock rollback relative to stored activity
declines. Registry write/flush failure declines.

Freshness is at most 24 hours and bound to a random identity in a volatile registry
key, with a separate lease for each SID. Enrollment/refresh revoke that lease
before changing the record, then reactivate only after the durable record flush
succeeds. Thus a failed flush cannot leave a newly refreshed account eligible.
A Windows restart removes that key. Only enrollment or successful explicit
password refresh can create a fresh identity. Normal Password/PIN/Hello sign-in
events and pattern success never refresh it. Fast Startup/hibernate semantics
must be tested separately; these are not asserted to be equivalent to Restart.

After signing in with the built-in Password tile, run in the VM:

```powershell
.\build\Release\patlock_enroll.exe --refresh
```

Password changes, disabled stale credentials and corrupt records require full
re-enrollment (run without --refresh). Uninstalling the provider does not delete
records. Never export the store or share its contents.

## Sources checked

- [PBKDF2](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptderivekeypbkdf2)
- [AES encryption](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptencrypt)
- [Registry creation and volatile keys](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeyexw)
- [Local password validation](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-logonuserw)
- [Credential prompt](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-creduipromptforcredentialsw)

All runtime behavior, ACL enforcement, reboot behavior and cryptography remain
UNVERIFIED pending VM builds/tests. No live host secret store was created.
