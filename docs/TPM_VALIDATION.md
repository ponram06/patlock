# TPM implementation and validation boundary

Source review only on the development host: no build, executable, TPM operation,
registry write, or installation was performed. The implementation below is not
proof of wrong-authorization rejection or TPM dictionary-attack protection.

## API decision

The previous code sent arbitrary 32-byte input to a custom `PCP_USAGEAUTH`
property. Its byte format was not justified. Microsoft's official
[PCPTool SDKSample.cpp](https://github.com/microsoft/TSS.MSR/blob/main/PCPTool.v11/exe/SDKSample.cpp)
uses `NCRYPT_PIN_PROPERTY` with a terminating UTF-16 string in both
`PcpToolCreateKey` and `PcpToolDecrypt`. Patlock now follows that input path:
the separately derived 32-byte authorization becomes 64 hexadecimal UTF-16
characters plus terminator. The temporary text is wiped. It is never requested
as a persisted application property. This changes the old TPM authorization
encoding: re-enroll legacy TPM records using the full throwaway password in the
VM; do not silently retry the old raw property or change protection mode.

PCPTool is an API usage reference, not evidence of target Windows 11 TPM behavior.
The Platform KSP translates its PIN into TPM authorization. This code makes no
claim that its text is passed unchanged as a TPM2 authValue, that the TPM2 name
algorithm is SHA-256, or that the application's 32-byte input determines the
hardware hash algorithm. OAEP SHA-256 is the wrapping padding algorithm only.
No TPM dictionary-attack parameters or global lockout settings are changed.

Other official references consulted:

- [Tbsi_GetDeviceInfo](https://learn.microsoft.com/en-us/windows/win32/api/tbs/nf-tbs-tbsi_getdeviceinfo): only `TBS_E_TPM_NOT_FOUND` enables baseline selection. KSP failure, TBS service failure, disabled TPM, or unsupported operation is an error, not permission to downgrade.
- [Key storage properties](https://learn.microsoft.com/en-us/windows/win32/seccng/key-storage-property-identifiers): zero export policy, decrypt-only usage, security descriptor property.
- [NCryptSetProperty](https://learn.microsoft.com/en-us/windows/win32/api/ncrypt/nf-ncrypt-ncryptsetproperty): DACL security-information flag and silent requests.
- [NCryptCreatePersistedKey](https://learn.microsoft.com/en-us/windows/win32/api/ncrypt/nf-ncrypt-ncryptcreatepersistedkey): machine key and refusal to overwrite existing names.
- [NCryptFinalizeKey](https://learn.microsoft.com/en-us/windows/win32/api/ncrypt/nf-ncrypt-ncryptfinalizekey): silent finalization.
- [NCryptDeleteKey](https://learn.microsoft.com/en-us/windows/win32/api/ncrypt/nf-ncrypt-ncryptdeletekey): deletion consumes the handle only on success.

## Code behavior

`Probe` returns S_OK only for device-info success plus an opened Platform KSP.
This is not a promise that key creation, authorization, ACLs or OAEP are supported.
Only S_FALSE explicitly means no compatible TPM; all subsequent errors fail
closed. The compatibility `Available` helper must not drive baseline fallback.

New keys use a unique enrollment-generation identity without overwrite. The
requested DACL has only SYSTEM and Administrators full-access entries and is
protected against inheritance. Existing owner/group are retained. Unsupported
ACL, PIN or export-policy requests abort enrollment. All UI-capable key
operations request silent behavior; provider open and key allocation have no
silent flag in their documented generic flag lists.

Enrollment wraps the AES key, reopens the persisted machine key, supplies the
same authorization, decrypts and compares in constant time. Temporary recovered
plaintext is wiped. Failed creation attempts delete only the newly-created key;
if deletion fails its status is returned and an orphan key may remain. A process
crash can also leave an orphan. Successful record commit/deletion of superseded
keys remains the store's responsibility. Output buffers are cleared on errors.
NTE/TPM HRESULTs retain their original facility and code.

## Required VM checks (not executed)

Use a disposable Windows 11 VM snapshot, virtual TPM and throwaway account.
Never perform these checks on the development host. Record Windows build, SDK,
TPM manufacturer/version, status codes, and pass/fail only; never secret inputs.

1. Build x64 /W4 with static runtime in the VM; resolve any compiler diagnostics.
2. Enroll and reopen after process exit and reboot; correct authorization must
   recover the exact generated AES key and unlock through Windows authentication.
3. In a separate disposable test process, open a fresh key handle and attempt
   decryption with no PIN and with a different derived authorization. Both must
   fail with cleared plaintext. Repeat after process exit/reboot to rule out
   process/provider authorization caching. A correct-PIN roundtrip alone does not
   establish authorization enforcement. If any negative check succeeds, stop
   using TPM enrollment and report the actual status and platform details.
4. Confirm a standard user cannot open/use/delete the machine key. Inspect the
   stored key DACL as an administrator; confirm only SYSTEM and Administrators
   have access and inheritance is disabled. Confirm SYSTEM can reopen/decrypt.
5. Attempt private export; it must fail. Public export is not a secret export.
6. Force failures after each creation stage (property, finalize, ACL, encrypt,
   reopen, decrypt) and check newly-created key cleanup. Simulate deletion
   failure and confirm enrollment fails, records remain unchanged, and no
   baseline mode is selected. Existing enrollment generation must survive.
7. Test no virtual TPM separately from stopped TBS service, disabled/unready TPM,
   access denied, reset TPM, missing key and unsupported padding/property. Only
   the explicit no-compatible-TPM probe result may select baseline for a new
   enrollment; a TPM-mode record must never be reinterpreted as baseline.
8. Confirm silent operations show no KSP PIN prompts in normal desktop and
   LogonUI sessions; any NTE_SILENT_CONTEXT error must decline safely.
9. Anti-hammering needs a deliberate isolated-VM investigation. Wrong-PIN tests
   can affect other TPM-protected credentials because hardware lockout can be
   shared. Use fresh disposable snapshots and observe TPM DA counters/lockout
   through official tooling. Determine whether these specific keys use DA
   protection and whether failures increment counters across reboot. Do not
   change global TPM lockout configuration for the app. Until evidence is
   recorded, describe this as TPM wrapping with unverified anti-hammering;
   continue enforcing Patlock's durable attempt limits separately.

Physical TPM behavior and real lock-screen compatibility remain unverified.
