# Phase 6: Packaging, Full Testing, and Documentation Guide

## Overview
Phase 6 validates the end-to-end functionality across all scenarios, produces clean deployable packages, and establishes documentation with honest security boundaries.

## 1. Automated Test Pass
Inside the VM:
```powershell
cmake -B build -S . -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
Expected output:
- `pattern_rules`: PASS (all 9-dot patterns, skipped-dot rules, minimum length)
- `crypto_policy`: PASS (PBKDF2 hashing, constant-time compare, escalating lockout, secret wipe)

## 2. Packaging Build Outputs
Run the packaging script inside the VM:
```powershell
.\install\package.ps1 -Configuration Release
```
This generates:
- `dist\Patlock\`: Uncompressed standalone distribution folder.
- `dist\Patlock-Release.zip`: Compressed release archive containing DLL, enrollment tool, demo tool, install scripts, and recovery instructions.

## 3. End-to-End Test Matrix (`docs/TESTING.md`)

Execute the following sequence inside the VM:

### Installation & Coexistence
1. Run:
   ```powershell
   .\install\install.ps1 -DllPath .\build\Release\PatlockProvider.dll -AcknowledgeVm -SnapshotAndPasswordFallbackVerified
   ```
2. Press `Win+L`:
   - Both "Password" and "Pattern" tiles must be present.
   - Built-in Password sign-in must work normally.

### Enrollment
1. Run `.\build\Release\patlock_enroll.exe`.
2. Draw pattern twice; test mismatched pattern rejection.
3. Test pattern with under 5 dots (rejected).
4. Successfully enroll with valid 5+ dot pattern.
5. Confirm whether "TPM-hardened" or "Baseline" mode is reported.

### Lock and Unlock
1. Press `Win+L`.
2. Select "Pattern" tile, click "Draw pattern".
3. Draw the enrolled pattern and release:
   - Window closes.
   - Click "Submit" (or hit Enter).
   - Windows unlocks immediately.

### Incorrect Pattern & Escalating Lockout
1. Lock screen (`Win+L`).
2. Enter wrong pattern 1-4 times: increments failure counter, no delay.
3. Enter 5th wrong pattern: 30-second lockout delay enforced.
4. Attempt immediate input during delay: dialog reports lockout delay.
5. Test 6th wrong pattern (60s), 7th (300s), 8th-9th (900s).
6. 10th wrong pattern: Pattern tile disabled.

### Full-Password Freshness and Restart
1. Restart the VM.
2. At logon screen, select "Pattern" tile:
   - Clicking "Draw pattern" displays warning that explicit password verification is required.
3. Sign in using the built-in Password tile.
4. Run elevated:
   ```powershell
   .\build\Release\patlock_enroll.exe --refresh
   ```
5. Lock screen: verify Pattern tile is restored and working.

### Stale Password Handling
1. Change Windows local account password.
2. Lock screen: attempt Pattern unlock.
3. Provider reports credential declined by Windows; automatic retry disabled to prevent account lockout.
4. Re-enroll with the new password.

### Clean Uninstallation
1. Run:
   ```powershell
   .\install\uninstall.ps1 -AcknowledgeVm
   ```
2. Reboot VM.
3. Press `Win+L`: verify Pattern tile is completely gone; Password sign-in unaffected.
