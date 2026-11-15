# Recovery Plan (read before installing anything)

Before every install in the VM:
1. Take a VM snapshot (name it with the date and phase).
2. Make sure a second administrator account exists and you know its password.
3. Back up the BitLocker recovery key if BitLocker is on.

If sign-in breaks:
1. **Revert the VM snapshot.** This is the fastest fix.
2. Or boot into Safe Mode (Safe Mode normally loads only the default providers) and
   run `Uninstall-Patlock.ps1` as administrator. It removes only registrations
   marked `Patlock Pattern Provider` and never removes Windows Password.
3. Or sign in with the second admin account and run the installed uninstaller at
   `Program Files\Patlock\Uninstall-Patlock.ps1`.
4. Or use Windows Recovery Environment, open Command Prompt, and remove the registry key from the offline hive.

## Remove the registration without any file (works from Safe Mode)
Safe Mode can open a **32-bit** Command Prompt (its title contains `wow64`). A 32-bit
program is silently redirected away from `C:\Windows\System32`, so `reg import` of a file
there fails with "Error opening the file" (seen in the VM rehearsal, 2026-11-15). These two
commands need no file, and `/reg:64` targets the right registry view from either prompt:
```
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\{21d42e91-a8c0-4e63-b5d7-372d8585982c}" /f /reg:64
reg delete "HKLM\SOFTWARE\Classes\CLSID\{21d42e91-a8c0-4e63-b5d7-372d8585982c}" /f /reg:64
```
Or switch to the 64-bit prompt first by typing `C:\Windows\Sysnative\cmd.exe`.
Copy/paste into a VM does not work in Safe Mode (VMware Tools user services do not run).

## Kill switch (hide the Pattern tile without uninstalling)
Setting this value makes the provider decline every sign-in scenario, so LogonUI shows no
Pattern tile; Password and other providers are unaffected. Delete it (or set 0) to re-enable.

- From any admin prompt (another admin account, or Safe Mode):
  ```
  reg add HKLM\SOFTWARE\Policies\Patlock /v DisableProvider /t REG_DWORD /d 1 /f
  ```
- Offline, from Windows Recovery Environment > Troubleshoot > Command Prompt. The Windows
  drive is often not `C:` there; find it with `dir D:\Windows` (or E:, F:). If BitLocker is on,
  unlock it first with `manage-bde -unlock D: -RecoveryPassword <key>`.
  ```
  reg load HKLM\OFFSOFT D:\Windows\System32\config\SOFTWARE
  reg add "HKLM\OFFSOFT\Policies\Patlock" /v DisableProvider /t REG_DWORD /d 1 /f
  reg unload HKLM\OFFSOFT
  ```
  Then restart. Rehearse this once in the VM before any real-hardware install.

Never:
- Install an untested build on your main PC.
- Delete or modify built-in providers.
- Skip the snapshot.

## Extra rules for a real (non-VM) machine
- Do this only after the whole VM test pass and only in Phase 7b.
- Before installing: a second working admin account, a restore point or full backup, the BitLocker recovery key saved offline, the Safe Mode removal steps rehearsed in the VM.
- Safe Mode: on the sign-in screen hold Shift, click Power > Restart, then Troubleshoot > Advanced options > Startup Settings > Restart > press 4.
- If locked out completely: boot the Windows Recovery Environment, open Command Prompt, and remove the provider key from the offline registry (needs the BitLocker key if encrypted).
- After every recovery or uninstall, reboot and verify the built-in Password tile
  before attempting another Patlock installation.
