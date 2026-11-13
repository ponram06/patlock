# Recovery Plan (read before installing anything)

Before every install in the VM:
1. Take a VM snapshot (name it with the date and phase).
2. Make sure a second administrator account exists and you know its password.
3. Back up the BitLocker recovery key if BitLocker is on.

If sign-in breaks:
1. **Revert the VM snapshot.** This is the fastest fix.
2. Or boot into Safe Mode (Safe Mode normally loads only the default providers) and run `install/unregister.reg`, or delete the provider's
   key under `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\{our GUID}`.
3. Or sign in with the second admin account and run the uninstall script.
4. Or use Windows Recovery Environment, open Command Prompt, and remove the registry key from the offline hive.

Never:
- Install an untested build on your main PC.
- Delete or modify built-in providers.
- Skip the snapshot.

## Extra rules for a real (non-VM) machine
- Do this only after the whole VM test pass and only in Phase 7b.
- Before installing: a second working admin account, a restore point or full backup, the BitLocker recovery key saved offline, the Safe Mode removal steps rehearsed in the VM.
- Safe Mode: on the sign-in screen hold Shift, click Power > Restart, then Troubleshoot > Advanced options > Startup Settings > Restart > press 4.
- If locked out completely: boot the Windows Recovery Environment, open Command Prompt, and remove the provider key from the offline registry (needs the BitLocker key if encrypted).
