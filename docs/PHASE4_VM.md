# Phase 4 Windows credential submission

Build/test in the VM, take a snapshot, enroll a throwaway local account and
install the x64 DLL. The current flow is **Draw pattern, then click the submit
arrow**. Pattern input alone does not authenticate; Windows still checks the
serialized username/password.

Acceptance cases (all pending):

1. Win+L, select Pattern, draw the enrolled pattern and submit. Windows unlocks.
   Lock again and repeat, proving the success callback clears the one-shot guard.
2. Wrong/reversed pattern declines before Windows receives a stored password;
   repeat the persistent lockout cases in PHASE3_VM.md.
3. Restart, sign in through Password, run the elevated `patlock_enroll --refresh`
   flow, lock, and verify Pattern works. Password sign-in alone is insufficient.
4. Sign out and test the sign-in screen; test fast user switching separately.
5. Change the Windows password after enrollment. The next Pattern submission
   may fail once at Windows; the record must then stay disabled across LogonUI
   restart/reboot. Only re-enrollment installs the new password.
6. Interrupt/kill the test LogonUI instance in the VM after reservation and before
   its result callback. Pattern must decline afterward; re-enrollment recovers.
7. Cancel drawing, deselect the tile, submit twice and switch users mid-flow.
   Never submit a credential without a newly completed valid pattern.
8. Corrupt ciphertext/tag or deny record flush. No serialized credential may be
   returned; built-in Password must still work.

The provider stores a unique submission token and disabled flag *before* releasing
the decrypted password for serialization. Only a matching successful ReportResult
clears that guard. No callback, failure, or crash leaves it disabled. Whether the
current Windows version supplies the expected success callback is specifically
unverified; if it does not, repeated Pattern unlock will remain disabled. Do not
weaken the guard to work around that without a reviewed design.

Plaintext is copied into the sample's protected-password/packing helpers, then
wiped immediately after packing. The serialized output is handed to Windows and
cannot be wiped before Windows consumes it. Failed-output allocations are wiped
before freeing. Local accounts only; Microsoft/domain account support is excluded.

Sources: Microsoft's sample packing helpers and
https://learn.microsoft.com/en-us/windows/win32/api/credentialprovider/nf-credentialprovider-icredentialprovidercredential-reportresult

This is written code, not a verified Windows unlock or a passed VM phase.
