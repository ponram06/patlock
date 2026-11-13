# Patlock - Trackpad Pattern Sign-In for Windows

Patlock is an Android-style 3x3 pattern sign-in for the Windows lock and sign-in screens, drawn with the laptop trackpad or mouse.

Built as an official Windows **Credential Provider** (64-bit C++ DLL loaded by `LogonUI.exe`), Patlock integrates directly into the Windows logon architecture. When the user successfully draws the enrolled pattern, the provider retrieves the encrypted local account credential, packages a standard `KERB_INTERACTIVE_UNLOCK_LOGON` credential serialization, and hands it to Windows Local Security Authority (LSA) for real authentication.

---

## Architecture

Patlock comprises four core components:

1. **Credential Provider DLL (`PatlockProvider.dll`)**:
   - Implements `ICredentialProvider` and `ICredentialProviderCredential2`.
   - Adds a distinct "Pattern" tile to LogonUI beside the built-in Password and PIN tiles.
   - Invokes the modal pattern-drawing dialog via LogonUI's `OnCreatingWindow` callback.
   - Verifies pattern hashes, manages attempt counters, checks lockout delays, and packages serialized credentials.
   - Never filters, replaces, or alters built-in Windows credential providers.

2. **Pattern Window (`src/common/pattern_window.cpp`)**:
   - Custom modal GDI-based 3x3 pattern grid dialog.
   - Supports continuous mouse/trackpad dragging, tap-and-drag, and diagonal crossings.
   - Implements the prototype's skipped-dot rule (e.g. crossing between unvisited opposite corner dots automatically captures the center dot).
   - Enforces a minimum length of 5 dots.
   - Includes optional "Hide trail" mode to resist shoulder surfing.

3. **Enrollment & Password Refresh Tool (`patlock_enroll.exe`)**:
   - Elevated desktop application run by the administrator.
   - Validates the local account password with `LogonUserW` and extracts the account SID.
   - Records and confirms the 3x3 pattern twice.
   - Supports `--refresh` mode to re-verify password freshness after reboot or expiry without needing to redraw the pattern.

4. **Standalone Tuning Demo (`pattern_window_demo.exe`)**:
   - Portable desktop application that runs with zero provider installation or registry changes.
   - Used in Phase 7 to tune trackpad feel, gesture sensitivity, and hit targets on real laptop hardware.

---

## Security Model and Honest Boundaries

> [!IMPORTANT]
> Patlock is a **convenience sign-in mechanism**, similar to an Android lock pattern or Windows PIN. It is **not stronger than a complex Windows password**. BitLocker or Device Encryption is a mandatory prerequisite for offline disk security.

### Threat Model & Protections:
- **Keyspace Limitation**: 3x3 pattern keyspace is relatively small (~389,000 combinations for 5-9 dots). Patlock mitigates online brute-forcing via:
  - Minimum 5 dots required.
  - Escalating lockout delays: 5 wrong attempts -> 30s; 6 -> 1 min; 7 -> 5 min; 8-9 -> 15 min; 10 wrong attempts permanently disables Pattern sign-in until full password re-verification.
  - Attempt reservations are durably flushed to disk before pattern verification (a crash consumes the attempt).
- **Dual-Mode Storage (TPM 2.0 Hardened + Baseline Fallback)**:
  - **TPM Mode**: When a TPM is present, Patlock uses the **Microsoft Platform Crypto Provider** (`MS_PLATFORM_CRYPTO_PROVIDER`) to generate a persisted RSA-2048 key. The key's usage authorization (`PCP_USAGEAUTH` / `NCRYPT_PIN_PROPERTY`) is bound to the PBKDF2 pattern hash. The AES-256 password key is wrapped with RSA-OAEP (SHA-256). Physical TPM hardware enforces anti-hammering dictionary attack throttling.
  - **Baseline Mode**: If no TPM is available (or in virtual machines without virtual TPM), Patlock generates a random 256-bit AES key and stores it in `HKLM\SOFTWARE\Patlock\<SID>\Record` protected by SYSTEM- and Administrators-only DACLs.
- **Boot Availability and Escalating Lockout (Option A)**:
  - Pattern unlock remains available across reboots, matching Windows Hello PIN convenience.
  - Brute-force attacks are constrained by durable escalating lockout (30s at 5 attempts, escalating to 1m, 5m, 15m; disabled after 10 attempts).
  - Password expiration age is configurable via `kMaxPasswordAge` (default disabled; configurable to 30 days). Password changes are handled by one-shot failure detection and fallback to the built-in Password tile.
- **Fail-Closed Execution**:
  - Any error, corrupt record, clock rollback, or TPM fault immediately declines the tile.
  - The built-in Password tile remains 100% available at all times.
- **No Secrets in Logs**:
  - Provider logging is restricted to a fixed lifecycle status string (`sample: enumeration requested`). No passwords, patterns, hashes, or user identities are ever written to disk logs.
- **Memory Hygiene**:
  - Sensitive buffers are wrapped in `patlock::Secret` RAII containers and wiped with `SecureZeroMemory` immediately after use.
  - Hash and MAC comparisons are performed in constant time (`patlock::Equal`).

---

## Building

### Prerequisites
- Windows 11 (or Windows 10 x64)
- Visual Studio 2022 with **Desktop development with C++** workload
- CMake 3.24 or newer

### Build with CMake
Open an elevated **x64 Native Tools Command Prompt for VS 2022**:
```powershell
cd patlock
cmake -B build -S . -A x64
cmake --build build --config Release
```

### Run Automated Unit Tests
```powershell
ctest --test-dir build -C Release --output-on-failure
```
Verifies:
- `pattern_rules`: 3x3 skipped-dot calculations, bounds, duplicates, and minimum length rules.
- `crypto_policy`: PBKDF2 hashing, constant-time equality, escalating lockout intervals, memory wiping, and TPM detection.

---

## Installation & Testing in a Windows 11 VM

All lock-screen testing should be performed inside a Windows 11 VMware Workstation Pro virtual machine.

1. **Take a VM Snapshot** before installing anything.
2. Ensure a second administrator account exists with a known password.
3. In elevated PowerShell inside the VM:
   ```powershell
   .\install\install.ps1 -DllPath .\build\Release\PatlockProvider.dll -AcknowledgeVm -SnapshotAndPasswordFallbackVerified
   ```
4. **Enroll your local account**:
   ```powershell
   .\build\Release\patlock_enroll.exe
   ```
   Enter your local account credentials and draw your 5+ dot pattern twice.
5. **Lock the workstation**:
   Press `Win+L`. Select "Pattern", click "Draw pattern", draw your pattern, and submit to unlock.
6. **Uninstall cleanly**:
   ```powershell
   .\install\uninstall.ps1 -AcknowledgeVm
   ```

---

## Packaging

To produce a self-contained distribution package:
```powershell
.\install\package.ps1 -Configuration Release
```
Outputs:
- `dist\Patlock\`: Standalone deployment folder.
- `dist\Patlock-Release.zip`: Compressed distribution archive.

---

## Recovery

If sign-in is ever interrupted:
1. Revert your VM snapshot.
2. Or boot into Windows Safe Mode (hold Shift while clicking Restart -> Troubleshoot -> Advanced options -> Startup Settings -> Restart -> 4) and run `install\unregister.reg`.
3. Or sign in with the backup administrator account using the standard Password tile.
See `docs/RECOVERY.md` for complete recovery procedures.
