# Phase 7: Real-Trackpad Tuning and Real-Hardware Deployment

## Purpose
The VMware VM provides only a generic virtual mouse pointer. It proves that Windows message routing, clipping, and LogonUI window parenting succeed on the lock screen, but cannot evaluate how drawing feels under real trackpad ergonomics (acceleration curves, tap-and-drag mechanics, palm rejection, dot-hit target tolerance).

---

## Step 7a: Desktop Tuning (Safe - No Provider Installed)

Run `pattern_window_demo.exe` directly on the host laptop as a standard desktop application:
```powershell
.\build\Release\pattern_window_demo.exe
```
(or extract from `dist\Patlock-Release.zip`).

### Tuning Matrix:
1. **Tap-and-Drag**:
   - Double-tap and hold to drag across the grid.
   - Verify that dot registration remains unbroken between dot transitions.
2. **Press-and-Hold**:
   - Physical trackpad click-and-drag.
   - Verify that continuous line drawing tracks smoothly without dropped hitpoints.
3. **Corner-to-Corner Diagonal Crossing**:
   - Draw `0 -> 8` (top-left to bottom-right).
   - Verify that intermediate dot `4` is automatically captured by the skipped-dot rule.
4. **Fast Drawing vs Overshoot**:
   - Swipe rapidly across dots.
   - The line-segment collision algorithm (`Track` in `pattern_window.cpp`) samples the full segment between consecutive mousemove events, guaranteeing that fast swipes do not jump over unvisited dots.
5. **Hit Radius**:
   - Grid spacing is `(min(width - 100, height - 150)) / 2`.
   - Hit radius is `spacing / 4` (~25% of cell distance).
   - This provides sufficient forgiveness for trackpad inertia while preventing accidental adjacent-dot triggers.
6. **Trail Visibility**:
   - Toggle "Hide trail" to verify visual feedback vs shoulder-surfing privacy.

---

## Step 7b: Real-Hardware Provider Install (Optional & Guarded)

> [!CAUTION]
> Installing a Credential Provider on physical hardware modifies your primary sign-in screen.
> Execute ONLY after all tests in `docs/TESTING.md` have passed in the VM.

### Mandatory Safeguards:
1. **Second Administrator Account**: Verify a secondary local administrator account exists with a known, tested password.
2. **BitLocker Recovery Key**: Back up your BitLocker recovery key to a separate physical device or printed paper.
3. **System Restore Point**: Create a fresh Windows System Restore point before installation.
4. **Safe Mode Rehearsal**: Ensure you know how to access Windows Safe Mode (Shift + Restart -> Troubleshoot -> Advanced options -> Startup Settings -> Safe Mode), which bypasses custom credential providers.

### Installation Command:
Run in elevated PowerShell:
```powershell
# Copy PatlockProvider.dll to System32
Copy-Item .\PatlockProvider.dll -Destination "$env:SystemRoot\System32\PatlockProvider.dll"

# Register the provider
reg import .\install\register.reg
```

### Emergency Removal (if lock screen fails):
Boot to Safe Mode or Windows Recovery Environment Command Prompt:
```cmd
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\{21d42e91-a8c0-4e63-b5d7-372d8585982c}" /f
reg delete "HKLM\SOFTWARE\Classes\CLSID\{21d42e91-a8c0-4e63-b5d7-372d8585982c}" /f
del "%SystemRoot%\System32\PatlockProvider.dll"
```
Your standard Windows Password, PIN, and Windows Hello providers will remain 100% operational.
