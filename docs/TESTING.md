# Test Checklist

Run in the Windows 11 VM first (take a snapshot before each install). Tick each item and record results in `docs/PROGRESS.md`.

## Install / uninstall
- [ ] Register script makes the Pattern tile appear on the lock screen
- [ ] Unregister script removes it completely
- [ ] Password and PIN options unchanged and working after install

## Enrollment
- [ ] Wrong Windows password rejected
- [ ] Mismatched confirmation rejected
- [ ] Pattern under the minimum dots rejected
- [ ] Record unreadable by a normal (non-admin) user

## Pattern input
- [ ] Press-and-hold drag and tap-and-drag both work
- [ ] Skipped-dot rule matches the prototype (e.g. 0 to 8 adds 4; 0 to 2 adds 1)
- [ ] Hit targets feel forgiving on at least two different trackpads
- [ ] Works on lock screen AND sign-in screen after restart

## Unlock
- [ ] Correct pattern unlocks
- [ ] Wrong pattern does not unlock and increments the counter
- [ ] Reversed pattern does not unlock

## Lockout
- [ ] 5 wrong attempts triggers a 30 s delay; delays escalate
- [ ] Lockout state survives reboot
- [ ] After about 10 failures Pattern is disabled until explicit Patlock password verification
- [ ] Counter clears after explicit Patlock password verification

## Boot & Availability policy (Option A)
- [ ] Pattern available immediately on lock screen AND sign-in screen after restart
- [ ] Lockout counter and backoff timer persist durably across restarts
- [ ] Stale password (changed in Windows) drops cleanly to built-in Password tile

## Failure handling (Password option must still work in every case)
- [ ] Windows password changed after enrollment
- [ ] TPM missing or unavailable
- [ ] Record corrupted or deleted
- [ ] Pattern window fails to create
- [ ] Provider DLL missing or bad
- [ ] Safe Mode boots normally

## Scenarios
- [ ] Win+L lock screen
- [ ] Sign-in screen after restart
- [ ] Fast user switching
- [ ] Local account; then separately a Microsoft account (document results)

## Security checks
- [ ] No secrets in any log file
- [ ] Plaintext buffers wiped (code review)
- [ ] Offline encrypted volume remains locked without recovery/unlock material;
      record BitLocker configuration and test conditions without absolute claims
- [ ] Code review of storage, serialization and lockout code done by the human

## Real hardware (Phase 7)
- [ ] Standalone pattern window tested on the real trackpad with tap-and-drag on and off
- [ ] Hit radius and spacing tuned; values recorded
- [ ] (Optional 7b) provider installed on a real machine only after safeguards in RECOVERY.md; lock, unlock and restart tests pass
