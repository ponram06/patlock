# Phase 5: Hardening - VM Acceptance Guide

## What was built
1. **TPM-backed key protection (`src/common/tpm.cpp`, `src/common/tpm.h`)**:
   - Uses Microsoft Platform Crypto Provider (`MS_PLATFORM_CRYPTO_PROVIDER`) with TPM 2.0.
   - Manages a persisted RSA 2048 key bound to the user's SID (`Patlock_<SID>`).
   - The key's usage authorization (`PCP_USAGEAUTH` / `NCRYPT_PIN_PROPERTY`) is bound to the pattern hash (PBKDF2-HMAC-SHA256).
   - The AES-256-GCM password encryption key is wrapped with RSA-OAEP (SHA-256).
   - If a TPM is present, enrollment creates the TPM-wrapped key (`tpmMode = 1`).
   - If TPM is absent or unavailable, Patlock gracefully falls back to baseline mode (`tpmMode = 0`) where the key is stored in the ACL-protected registry hive.
   - `patlock_enroll.exe` displays whether enrollment succeeded in TPM-hardened mode or baseline mode.

2. **Escalating Lockout Policy**:
   - 0-4 failures: 0s delay
   - 5 failures: 30s delay
   - 6 failures: 60s (1 min) delay
   - 7 failures: 300s (5 min) delay
   - 8-9 failures: 900s (15 min) delay
   - 10 failures: Pattern disabled until explicit Patlock password verification via `patlock_enroll.exe --refresh`.
   - Attempt count and timestamps are persisted to HKLM before testing guesses (crash consumes the attempt).

3. **Full-Password Freshness Rule**:
   - Freshness is bound to a volatile registry key (`HKLM\SOFTWARE\Patlock\Boot\<SID>`).
   - A reboot clears volatile registry keys, rendering Pattern unavailable until `patlock_enroll.exe --refresh` is run.
   - 24-hour expiration (`passwordTime` compared to `Now()`) revokes Pattern eligibility even without reboot.
   - Password changes: `Refresh` computes `passwordMac` (HMAC of supplied password) and compares in constant time; if the Windows password changed, refresh fails and requires full re-enrollment.

4. **Fail-Closed and Zero-Secret Logging**:
   - Provider DLL log (`PatlockSample.log`) contains only fixed string `"sample: enumeration requested\r\n"`. Zero secrets, patterns, hashes, or passwords are ever logged.
   - Any corruption, invalid version, bad SID, invalid MAC, clock rollback, or TPM failure immediately declines the tile without unlocking or crashing.
   - Sensitive buffers are managed with `patlock::Secret` RAII wrappers and explicit `SecureZeroMemory`.

---

## VM Test Procedure

### 1. Build and Run Automated Tests
Inside the VM (after snapshot):
```powershell
cmake -B build -S . -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
Verify that `pattern_rules` and `crypto_policy` tests pass.
Note the console output from `crypto_policy` indicating whether the VM's virtual TPM is detected.

### 2. Enrollment Mode Test
1. Run `.\build\Release\patlock_enroll.exe`.
2. Enter local account credentials and draw a 5+ dot pattern twice.
3. Observe the confirmation dialog:
   - If the VM has a virtual TPM enabled: "Saved in TPM-hardened mode (Platform Crypto Provider)."
   - If the VM has no TPM: "Saved in baseline mode (software store)."
4. Verify registry at `HKLM\SOFTWARE\Patlock\<SID>\Record`:
   - Inspect permissions: only SYSTEM and Administrators have access.

### 3. Lockout Escalation Test
1. Lock screen (`Win+L`).
2. Click "Pattern" tile, click "Draw pattern".
3. Enter wrong pattern 5 times:
   - On 5th failure: tile warns to wait 30 seconds.
   - Verify pattern window rejects immediate retry until 30s elapses.
4. Continue entering wrong patterns to verify escalation (60s, 300s).
5. At 10 wrong attempts: verify Pattern tile shows disabled message.
6. Sign in with standard Password tile.
7. Run `.\build\Release\patlock_enroll.exe --refresh`.
8. Lock screen: verify Pattern tile is re-enabled.

### 4. Reboot and Freshness Test
1. Enroll pattern, verify it unlocks.
2. Restart the VM.
3. On sign-in screen, click "Pattern" tile:
   - Click "Draw pattern": verify message says Pattern is unavailable and prompts to use Password and run refresh.
4. Sign in with Password.
5. Run `.\build\Release\patlock_enroll.exe --refresh`.
6. Lock screen: verify Pattern works again.

### 5. Stale Password Test
1. Change local VM account password via Windows Settings or `net user`.
2. Run `.\build\Release\patlock_enroll.exe --refresh`:
   - Enter old password: rejected by Windows validation.
   - Enter new password: rejected by Patlock MAC check (password changed, requires re-enrollment).
3. Verify that re-enrolling (without `--refresh`) successfully updates the stored credential.
