# Phase 3 enrollment and baseline storage checks

Build inside the VM using PHASE2_VM.md, then run both CTest executables. No tests
have been executed by the agent. Use a throwaway local account, a snapshot,
working Password fallback and BitLocker/device encryption.

1. Run `build\Release\patlock_enroll.exe`. Accept elevation. Enter an unqualified
   local username and its password. Draw the same pattern twice, minimum five dots.
2. Cancel at each dialog and verify no enrollment is created/replaced. Try a wrong
   password once, mismatched patterns, a short pattern and a domain/Microsoft
   account. They must be rejected. Avoid repeated wrong Windows passwords because
   the real Windows account-lockout policy applies to LogonUserW.
3. Install the current DLL with the guarded script. At this checkpoint the right
   pattern reports verification success; it does not yet unlock Windows.
4. Wrong patterns must decline. Five wrong patterns cause a 30-second delay.
   Continue only after each delay: 1, 5, 15, 15 minutes; ten failures require refresh.
5. Verify the persistent attempt count survives a restart. Restart additionally
   invalidates freshness. Sign in with Password and run `patlock_enroll.exe --refresh`
   with the full account password; then Pattern can verify again.
6. As a non-admin test user, attempting to open HKLM\SOFTWARE\Patlock must be denied.
   Inspect ACLs as admin without exporting, printing or sharing the Record value.
7. Restore a snapshot between fault cases: corrupt/delete a record; deny write
   access; hold the named mutex past its timeout; roll the VM clock backward.
   Each must decline Pattern while built-in Password keeps working.
8. Change the account password. Refresh with the new password must require
   re-enrollment rather than silently preserving stale ciphertext. Re-enroll.
9. With a fault-injection/debugger build in the VM, force RegFlushKey failure
   after a successful record write during enrollment and refresh. No lease may
   be activated; a later Pattern attempt must decline until successful refresh.

These are human acceptance steps, not recorded passes. Store operations mutate
the registry, so do not run the enrollment tool or integration checks on the host.
See STORAGE.md for plain-language security behavior and known limitations.
