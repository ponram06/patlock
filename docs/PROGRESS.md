# Progress Log

Update after every session. Keep it honest: separate "written" from "verified on a real lock screen".

## Template
### YYYY-MM-DD: Phase N
- Built:
- Tested by human in VM (results):
- Not verified yet:
- Problems / error messages:
- Decisions made:
- Next step:

## Log
### 2026-11-15: Release/install documentation and guarded packaging
- Written: `Build-Release.ps1`, guarded installer/uninstaller, SHA-256 and
  Authenticode verification, versioned DLL upgrades, Event Log source lifecycle,
  and production/recovery/test documentation.
- Written: Event Log audit metadata for enrollment failures, lockouts and TPM
  retirement failures; no patterns, passwords, keys or hashes are logged.
- Verification: PowerShell parsing and source/whitespace review only. No host
  build, CTest, signing, registry/TPM operation, package install, uninstall or
  lock-screen test was run.
- VM action: run `Build-Release.ps1`, then the full matrix in PRODUCTION.md and
  record results before describing any build as production-ready.

### 2026-11-15: Security audit fixes (record format 7)
- Written (not built, not run; agent works on the host and does not build):
  - Record format 7: the TPM-mode pattern verifier is bound to the TPM-wrapped AES
    key, closing the offline pattern-guessing gap that format 6 documented. The
    password verifier is now PBKDF2. Format 5/6 records keep their old behaviour.
    See CURRENT_STORAGE.md.
  - (Merged with the GitHub commits of the same day: software-only enrollment is
    governed by their AllowSoftwareKey policy, not a consent dialog. Their audit
    events now also cover wrong patterns caught by the format 7 TPM check.)
  - Provider releases the one-shot submission reservation when a verified password
    never reached Windows: tile deselected, window closed after success, or
    GetSerialization failed. This prevents "Pattern unavailable" after a
    correct pattern.
  - `--refresh` with the unchanged enrolled password now clears disabled/pending state.
    It also gives a clear message when the password was changed.
  - update.ps1 no longer kills LogonUI and registers with full path plus markers like
    install.ps1. Previously, uninstall.ps1 refused to remove an update.ps1
    registration. Machines registered by the old update.ps1 need unregister.reg.
  - CMake: /CETCOMPAT for the provider; /W4 /sdl /guard:cf /CETCOMPAT for the
    elevated enrollment tool.
- How to test in the VM (snapshot first):
  1. Build Release x64, check for new warnings, run CTest.
  2. TPM VM: re-enroll, unlock several times, and lock/unlock repeatedly.
  3. Draw a wrong pattern 3 times, then the right one: unlock works and the counter resets.
  4. Draw the correct pattern, then click "Sign-in options" before it signs in.
     Re-select Pattern: it must still be available, not "Pattern unavailable".
  5. Change the password, draw the pattern, and confirm Windows declines once.
     Then `--refresh` with the new password must be refused with the "enroll again"
     message, and re-enrollment must work.
  6. VM without a TPM: enrollment is refused with the AllowSoftwareKey message;
     with the policy set to 1 it enrolls.
  7. An existing format 6 record must still unlock before re-enrolling.
  8. `update.ps1`, then `uninstall.ps1`: uninstall must succeed.
- Not verified: everything above. Compilation, TPM behaviour for wrong-auth decrypts
  on the target TPM, and the deselect/serialization-failure paths need VM evidence.
- Not changed: VM guards, the "VM testing only" enrollment warning, Option A policy.

### 2026-11-14: Fix TPM KeyIdentity name mismatch between Enroll and Verify
- Problem: 'every pattern i draw it says wrong i also resetted the pattern but still it is the same'.
- Root cause: Commit 2441e62 introduced an ephemeral GUID appended to key name in Enroll(),
  but never saved the GUID in the record. When Verify() attempted to unwrap the AES key via
  tpm::UnprotectKey(), it queried the standard KeyIdentity without the GUID. This caused
  NCryptOpenKey to return NTE_NOT_FOUND (0x80090011), causing Verify() to fail with
  E_ACCESSDENIED and report 'Incorrect pattern' even on the 100% correct pattern.
- Fixed: Removed ephemeral GUID from Enroll(). KeyIdentity(record, identity) already
  incorporates the cryptographically random 12-byte nonce, guaranteeing uniqueness per generation
  while allowing Verify() to reliably find and unprotect the key.
- Fixed: WM_LBUTTONUP now submits valid strokes immediately even if the stroke started via hover.

### 2026-11-14: Fix record version backwards compatibility
- Problem: users who enrolled on record version 5 received 'Pattern unavailable' after version 6 bump.
- Fixed: store Read() now accepts both version 5 and version 6 records.
- Note: if an existing record was disabled by lockout or wrong attempts, user should sign in with password and run patlock_enroll.exe to reset.

### 2026-11-14: Fix UI Automation anchor lookup and LogonUI click pass-through
- Applied fix-anchor-and-clicks.patch:
  1. Username lookup: queries all UI Automation elements matching username,
     filtering for on-screen elements with height <= 140px, picking the smallest area.
  2. Owner window re-enabled in WM_INITDIALOG via EnableWindow(owner, TRUE) so
     LogonUI continues receiving clicks for 'Sign-in options' and user switching.
  3. CloseActivePatternWindow() added and invoked from CSampleCredential::SetDeselected()
     so the floating grid closes whenever LogonUI deselects the tile.
  4. Added <climits> header in pattern_window.cpp to ensure clean compilation of LONG_MAX.
- Preserved: pattern verification, storage, crypto, and lockout logic untouched.
- Tested by human in VM (results): pending VM build and test.
- Not verified yet: real lock screen click pass-through and UI Automation element discovery on target Windows 11 build.
- Next step: pull and test via update.cmd in the VM.

### 2026-11-14: Phase 7 native-hierarchy UI repair
- Built: restored the requested native hierarchy: Windows username, then only
  the 3x3 pattern grid, then `Other sign-in options`. The normal state has no
  card, title, or instruction text. Status text is shown only for success,
  failure, or lockout recovery. The grid uses system accent colorization for
  active strokes;
  corrected AARRGGBB-to-RGB conversion so red/blue are not swapped.
- Preserved: hover-to-start, drag alternative, skipped-dot rules, verifier and
  lockout callbacks, fallback button keyboard behavior, high contrast, and all
  storage/serialization behavior. No re-enrollment is needed for this UI-only
  source change.
- Tested by human in VM (results): not yet tested.
- Not verified yet: build warnings, standalone demo, lock/sign-in placement,
  high contrast, DPI scaling, accent rendering, and real-trackpad feel.
- Next step: build and run the standalone demo in the VM, then test at the
  screenshot resolution before updating the provider DLL through the existing
  guarded VM-only process.

### 2026-11-14: Floating reference-style visual redesign
- User supplied a cardless large-grid reference. Written: larger shared layout,
  floating solid dots, restrained cyan feedback, transparent status, and native
  owner-drawn fallback text with keyboard focus. HC retains system-colored surface.
- Added active-window actual-cursor movement polling for transparent gaps without
  changing verification/storage. Hover/timing/cancellation guards are preserved.
- Verification: source/static review and whitespace checks only; no host builds,
  screenshots, executable tests, provider install or registry changes performed.
- VM action: FLOATING_UI.md; build/test/demo then reinstall DLL through existing
  guarded steps. This visual update alone does not require re-enrollment.
- Review repair: fallback text uses a subtle filled action surface so clicks on
  whitespace/padding do not pass through the layered window.
### 2026-11-14: Restored requested hover-to-start input
- User requested no-click/no-held-button drawing. Restored 120 ms stable hover
  start, button-free movement and 500 ms pause-to-submit after at least five dots.
- Retained drag/release alternative, skipped-dot rules, lockouts, accessible
  fallback and DPI/high-contrast behavior. Opening/reset over a dot is disarmed
  until deliberate movement. Dwell rechecks actual cursor position. Short strokes
  time out without an authentication attempt; capture loss/cancel/deactivation
  clears active strokes. Small pointer jitter does not indefinitely delay finish.
- Added hover-policy VM test target. No record format change or re-enrollment is
  required solely for this UI update. Rebuild/reinstall the updated DLL in the VM.
- Verification: static review/whitespace only; no host build or runtime tests.
- Review fixes: monotonic timer deadlines reject queued old start/finish events;
  idle completion rechecks actual cursor, capture, active window and bounds.
  Leaving the card or moving over fallback cancels instead of being a pause.
### 2026-11-14: Test-only CMD workflow and build-tool discovery
- User reported `VS2022 C++ tools not found` and requested tests alone.
- Replaced the install/reboot workflow with build/CTest only: no snapshot or
  Password prompts, elevation, registration, enrollment, DLL replacement or reboot.
- Discovery now inspects installed x64 compiler files across VS2022/newer
  instances (including Build Tools/previews), pairs CMake with a supported
  generator, and reports missing compiler/SDK/CMake components explicitly.
- Uses an isolated build cache per generator/instance. Does not resume the old
  install checkpoint or change the existing provider. Install scripts still
  require the original VM/admin/snapshot safeguards.
- Verification: PowerShell syntax and whitespace checks only. No host build or
  executable tests; actual VM tool detection/build remains unverified.
### 2026-11-13: Guided CMD VM test launcher
- Written: root RUN_VM_TEST.cmd and install/run-vm-test.ps1; VMware check before
  elevation, VS2022 CMake discovery, build/CTest gate, snapshot/Password checks,
  uninstall/reboot checkpoint, fixed-DLL removal/install, enrollment/demo launch.
- Rerun the same launcher after reboot; tested binary hashes must match. Manual
  lock-screen and TPM evidence still required. See ONE_CLICK_VM_TEST.md.
- Verification: PowerShell parsing, source review and whitespace checks only.
  No script execution, builds, registry changes or installation on the host.
### 2026-11-13: Integrated release-candidate implementation pass (record 6)
- Written: KTM registry transactions and removal; unique TPM generation cleanup
  and postcommit retirement; explicit no-TPM fallback with operational failures
  closed; official PCPTool PIN encoding, silent operations and key policy/ACLs.
- Written: deliberate drag/release UI, inherited DPI and monitor clamping,
  high-contrast palette, native accessible status/fallback, saved lockout countdown.
- Written: COM optional-output/bounds handling, timer/credential lifetime guards,
  exception containment, wiped transient buffers and no hidden password exposure.
- Written: authenticated --remove, guarded VM build/test command, transaction and
  provider-contract test targets, VS2022 x64/C++17/static CRT configuration,
  non-destructive package output checks and public artifact hashes.
- Static review findings fixed: failed-enrollment TPM generation cleanup and
  nullable GetSerialization outputs. Syntax/XML/whitespace checks performed.
- Not executed: builds, CTest, registry/TPM operations, provider installation,
  screen-reader/DPI/trackpad testing or current-candidate lock-screen sign-in.
- Human VM action: VM_ACCEPTANCE.md; re-enroll for record 6, run all five test
  targets, repeat sign-in and fault cases, and TPM_VALIDATION.md. Signing and
  independent security review are distribution gates, not completed claims.

### 2026-11-13: Publishing and additional hardening
- Pushed 989205f successfully to origin/main at the user's request.
- Written: unique TPM enrollment generations without pre-commit deletion of old
  keys; deletion errors propagated; persisted submission disable guard restored.
- Record version 5 requires VM re-enrollment. Old TPM keys are retained for
  safety; cleanup and registry commit transactionality remain pending.
- Verification: source/whitespace review only. No host build, executable tests,
  installation or registry changes. See RELEASE_STATUS.md for release blockers.

### 2026-11-13: Current-source audit and local repairs
- Written: disabled-record enforcement, optional ReportResult output handling,
  completion-token checks, separate TPM authorization derivation (record v4),
  removal of binary PIN fallback and hardcoded password advice, capture-loss
  cancellation, GDI+ initialization failure handling, stronger dot contrast and
  owner-monitor work-area placement. No push authorized for this checkpoint.
- Added AUDIT_2026-11-13.md with confirmed findings, compatibility change,
  remaining release blockers and VM test cases; RELEASE_REPAIR_PROMPT.md contains
  the requested detailed security/UI continuation prompt.
- Verification: source review and whitespace checks only; new derivation tests
  written for the VM. No host build, executable tests or registry modifications.
- VM action: re-enroll after upgrading to record v4; run CTest and the audit's
  stale-password, nullable-output, token, capture-loss and visual cases.
- Not verified: compilation, runtime authentication, TPM policy, ACL/persistence
  fault behavior, DPI/accessibility or market readiness. TPM transactionality,
  submission crash handling and flush-failure activation remain release blockers.

### 2026-11-13: GDI+ Vector Anti-Aliasing & Background Icon Elimination
- Built:
  - GDI+ Vector Anti-Aliasing (`src/common/pattern_window.cpp`):
    - Enabled GDI+ `SmoothingModeAntiAlias` and `TextRenderingHintClearTypeGridFit`.
    - Completely replaced jagged Bresenham GDI ellipses with smooth, circular anti-aliased vector dots.
    - Added rounded caps and joins (`LineCapRound`, `LineJoinRound`) to line rendering.
  - Eliminated Background Icon Bleed-Through:
    - In `src/provider/common.h`: changed `SFI_TILEIMAGE` to `CPFS_DISPLAY_IN_DESELECTED_TILE` so Windows LogonUI does not render the square tile logo inside the selected tile.
    - Used a midnight-slate Fluent glass plate (`Gdiplus::Color(255, 18, 22, 30)`) with transparent rounded corners (`TransparentKey` margin): covers and masks the underlying LogonUI icons completely while floating seamlessly on the lock screen wallpaper.
    - Positioned at `screenHeight / 2 + 35`, directly below `testuser` with zero overlap on avatar or username.
    - Clean `"Other sign-in options"` link at the bottom with `IDC_HAND` cursor.
- Verified in VM by user:
  - Phase 4 unlock verified.
- Not verified yet:
  - Live VM verification of GDI+ smooth anti-aliased dots and zero icon bleed.
- Next step:
  - User runs `update.cmd` in VM and tests lock screen appearance.

### 2026-11-13: Reference Mockup Visual Redesign & Lockout Auto-Recovery
- Built:
  - Exact Visual Match with User Reference Mockup (`src/common/pattern_window.cpp`):
    - Removed the black card container and borders; entire window is 100% see-through with `LWA_COLORKEY` so lock screen wallpaper shows cleanly through everywhere.
    - Adjusted vertical positioning to `screenHeight / 2 + 45`, ensuring avatar and `testuser` remain completely clear and unobstructed above.
    - Rendered 9 solid dark slate floating discs (`RGB(22, 26, 35)`) directly on the wallpaper with 64px spacing and pearl centers.
    - Added dedicated `"Other sign-in options"` text link directly below the grid with `IDC_HAND` cursor. Clicking it immediately closes the pattern window and returns to Windows sign-in options.
  - Lockout Auto-Recovery & Brute-force Safety (`src/common/store.cpp`):
    - A correct pattern match now explicitly resets the durable failure counter (`attempts = 0`, `until = 0`).
    - Removed permanent bricking in `Check()`: lockout backoff (`now < record.until`) is strictly enforced (30s, 1m, 5m, 15m), but once the timer passes, entering the correct pattern resets attempts and logs in cleanly.
- Verified in VM by user:
  - Phase 4 unlock verified.
- Not verified yet:
  - Live VM verification of mockup visual match and sign-in options link.
- Next step:
  - User runs `update.cmd` in VM and tests lock screen.

### 2026-11-13: Native COM Auto-Logon & Fix for Premature Record Disabling
- Built:
  - Official Windows Auto-Logon Implementation (`src/provider/CSampleProvider.cpp`, `CSampleCredential.cpp`):
    - Implemented `ICredentialProvider::Advise` and `UnAdvise` with `ICredentialProviderEvents`.
    - Replaced unreliable simulated keyboard input (`keybd_event` / `SendInput`) with native COM callback `CredentialsChanged(upAdviseContext)`.
    - When a pattern is verified, `GetCredentialCount` returns `*pbAutoLogonWithDefault = TRUE` and the default tile index, instructing LogonUI to immediately invoke `GetSerialization` and log in automatically.
    - Submit arrow also remains visible on tile as a fallback.
  - Eliminated Premature Record Disabling (`src/common/store.cpp`):
    - Removed `bundle.record.disabled = 1;` from `Verify()`. Verifying a pattern no longer disables the account prior to Windows authentication.
    - In `ReportResult`: only disables if Windows actually rejects the credential (e.g. password changed).
    - In `FinishSubmission`: resets `attempts = 0`, `until = 0`, and `disabled = 0` on successful logon.
    - In `Check()`: removed `record.disabled` check so accounts previously disabled by an aborted simulated click immediately recover without requiring re-enrollment.
- Verified in VM by user:
  - Previous Phase 4 unlock verified on Windows 11 lock screen.
- Not verified yet:
  - Live VM verification of native COM auto-logon and unblock.
- Next step:
  - User runs `update.cmd` in VM and tests automatic logon.

### 2026-11-13: Adopt Option A - Drop Restart & 24h Expiration Requirements
- Built:
  - Adopted Option A per human approval:
    - Removed volatile boot-lease check from `Check()` in `src/common/store.cpp`.
    - Removed `PrepareBoot`, `ActivateBoot`, and `Boot` functions.
    - Added configurable `kMaxPasswordAge = 0` (disables recurring 24h password expiration).
    - Preserved durable attempt tracking and escalating lockout delays (30s at 5 attempts, escalating to 1m, 5m, 15m; disabled at 10 failures) stored in `HKLM\SOFTWARE\Patlock\<sid>`.
    - Pattern unlock now persists across reboots and sign-ins seamlessly, matching Windows Hello PIN behavior.
  - Enhanced `install/update.ps1` to ensure automatic Credential Provider registration.
  - Updated documentation across `README.md`, `PLAN.md`, `docs/SECURITY.md`, `docs/TESTING.md`, `docs/STORAGE.md`, and `AGENTS.md`.
- Verified in VM by user:
  - Previous Phase 4 unlock verified on Windows 11 lock screen.
- Not verified yet:
  - Live VM verification of seamless post-reboot pattern unlock under Option A.
- Next step:
  - User runs `update.cmd` in the VM and tests unlock across reboots and lock screens.

### 2026-11-13: Pure-Logic ArmingGate Integration & Unit Test Suite
- Built:
  - `ArmingGate` (`src/common/arming_gate.h`): Pure-logic state machine deciding when a new pattern stroke may be started by hovering.
    - If the pointer is resting on a dot at open or reset time, the gate stays disarmed until the pointer has deliberately travelled at least 40px away (`kTravelPx`).
    - Tiny jitter on the dot never arms it.
    - If the pointer is not on a dot at reset time, it arms immediately.
  - Unit Test Suite (`tests/arming_tests.cpp`): Added CTest target `arming_gate` verifying disarm on open, jitter resistance, travel threshold arming, Esc reset behavior, and uninitialized gate state.
  - Integrated `ResetStroke` in `src/common/pattern_window.cpp` as the centralized reset function across open, Esc, wrong pattern, too-short stroke, lost capture, and empty strokes.
  - Helpful Subtitle Guidance: While `!state.gate.Armed()`, displays `"Move the pointer away, then start"` in soft amber (`RGB(255, 200, 90)`).
- Verified in VM by user:
  - Phase 4 unlock previously verified.
- Not verified yet:
  - Live VM verification of arming gate and CTest run.
- Next step:
  - User runs `update.cmd` in the VM to build, run tests, and test the arming gate.

### 2026-11-13: Zero-Overlap Alignment with Reference Mockup & Tile Decluttering
- Built:
  - Zero-Overlap Placement: Positioned the transparent 280x280 pattern window at `screenHeight / 2 + 30`, cleanly below the user avatar and `testuser` username. Avatar and username are completely unobstructed.
  - Tile Text Decluttering: Set `SFI_LARGE_TEXT` and `SFI_LAUNCHWINDOW_LINK` to `CPFS_HIDDEN` in `src/provider/common.h`. LogonUI now renders an entirely clean space under `testuser` with zero colliding background text ("Pattern", "Draw pattern").
  - Reference Mockup Alignment: Styled the 9 dots with dark slate circular discs (`RGB(44, 48, 60)`) and pearl-gray centers (`RGB(210, 218, 235)`), directly matching the user's reference photo.
  - Compact Layout: 68px dot spacing with dynamic subtitle above and a clean `[ Other sign-in options ]` button below.
- Verified in VM by user:
  - Phase 4 unlock previously verified.
- Not verified yet:
  - Live VM verification of zero-overlap positioning and mockup visual match.
- Next step:
  - User runs `update.cmd` in the VM and checks the new clean lock screen layout.

### 2026-11-13: 100% Transparent Lock-Screen Integration, "Other Sign-in Options" Button & Smart Hover Guard
- Built:
  - 100% Transparent Lock-Screen Integration: Replaced the dark background card with true `LWA_COLORKEY` transparency (`RGB(2, 4, 6)`), allowing the Windows 11 lock-screen wallpaper to show through 100% cleanly without any dark box or obstruction.
  - Floating Contrast Dot Discs: Dots now render with circular dark slate backing discs (`RGB(24, 28, 38)`) and bright pearl centers, ensuring high-contrast visibility over both bright sky and dark mountain wallpapers.
  - Dedicated "Other sign-in options" Button: Integrated a sleek pill button (`[ Other sign-in options ]`) right at the bottom of the grid. Clicking it immediately closes the grid and exposes the standard Windows PIN and Password tiles.
  - Smart Hover Start Guard: Requires a 150ms intentional hover pause on the starting dot (or an explicit click) before drawing begins. Waving the mouse cursor across dots never triggers an accidental start.
  - Dot Suppression on Reset & Open: Whenever `Esc` is pressed or the window opens, the exact dot directly under the cursor is added to `suppressStartDot`. It is physically impossible for that dot to auto-start until the user intentionally moves their cursor away.
- Verified in VM by user:
  - Phase 4 unlock previously verified.
- Not verified yet:
  - Live VM verification of 100% transparent wallpaper integration and sign-in options button.
- Next step:
  - User runs `update.cmd` in the VM and tests the transparent lock screen grid.

### 2026-11-13: Transparent Acrylic Lock-Screen UI, Accidental Start Guard & Esc Reset
- Built:
  - Transparent Acrylic Aesthetic: Frameless `WS_POPUP` window with `WS_EX_LAYERED` and 80% opacity (`LWA_ALPHA`), allowing the blurred Windows 11 lock screen wallpaper to show through with a dark frosted acrylic glass effect and native rounded corners (`DWMWCP_ROUND`).
  - Accidental Start Prevention: Window records initial cursor coordinates on open and requires at least 12px of intentional mouse motion (or an explicit click) before hover-capture arms. This prevents dots directly under the cursor from accidentally capturing when the tile is clicked.
  - Non-Destructive Esc Key Reset: Pressing `Esc` while drawing or after entering dots clears the pattern, frees mouse capture (`ReleaseCapture()`), and resets the dots to unvisited status without closing the window, allowing immediate re-drawing.
  - Tile Cleanup: Hidden `SFI_CHECKBOX` and `SFI_SUBMIT_BUTTON` on the tile in `src/provider/common.h` to keep the lock screen uncluttered while the acrylic pattern grid auto-launches cleanly above "Sign-in options".
  - Dynamic Subtitle Feedback: Shows real-time connected dot counts (e.g. `3 dots connected (min 5)` -> `5 dots connected - pause to sign in`) in bright cyan and green.
  - Fixed C2280 build error in `src/provider/CSampleCredential.cpp` by directly referencing `decryptedPassword` in `patlock::Verify`.
- Verified in VM by user:
  - Phase 4 unlock previously verified.
- Not verified yet:
  - Live VM verification of transparent acrylic UI and arming guard.
- Next step:
  - User runs `update.cmd` in the VM to build and test the transparent lock screen grid.

### 2026-11-13: Professional End-to-End Android-Style UI & Auto-Popup on Tile Selection
- Built:
  - Auto-Popup on Tile Selection: Selecting the Pattern tile under "Sign-in options" immediately schedules an asynchronous UI timer in `CSampleCredential::SetSelected` (60ms) that automatically pops up the 3x3 pattern window without the user touching or clicking "Draw pattern".
  - Commercial Windows 11 Dark Fluent UI:
    - Enabled DWM immersive dark mode and Windows 11 rounded window corners (`DWMWCP_ROUND`).
    - Velvet dark slate surface (`RGB(22, 24, 30)`), crisp anti-aliased Segoe UI typography with clear hierarchy (Title, Subtitle, and Escape hint).
    - Multi-layered glowing dot aesthetics: dark halos with pearl core when inactive, glowing accent halo on hover, and vibrant electric blue outer ring with solid white core when connected.
    - Smooth 6px electric cyan connecting lines and live rubber-band trail with rounded end-caps and rounded corner joins (`PS_ENDCAP_ROUND | PS_JOIN_ROUND`).
  - Android-Style In-Window Verification & Error Feedback:
    - Added `DrawPatternInteractive` with instant verifier callback.
    - Correct pattern: turns emerald green (`RGB(46, 213, 115)`), confirms "Pattern Verified", and signs in automatically.
    - Incorrect pattern: flashes vivid coral-red (`RGB(255, 75, 75)`), displays "Incorrect Pattern. Please try again", and auto-clears after 900ms for immediate retry without closing the window or showing message boxes.
    - Lockout: displays "Pattern Locked Out. Use Password." and gracefully closes.
  - Effortless Glide-to-Draw: 34px hit radius captures hover entry near any dot instantly, auto-starts tracking with capture, and finishes on 350ms pause on last dot or tap.
- Verified in VM by user:
  - Previous Phase 4 unlock verified on Windows 11 lock screen with pattern and password fallback.
- Not verified yet:
  - Live VM verification of auto-popup on Sign-in option click and new Fluent dark UI.
- Next step:
  - User pulls latest commits and runs `update.cmd` in the VM to test the end-to-end professional UI.

### 2026-11-13: Trackpad Glide Mode, Live Rubber-Band Trail & Asynchronous Auto-Login
- Built:
  - Live rubber-band line in `src/common/pattern_window.cpp` following the mouse/finger from the last captured dot to current cursor coordinates in real time.
  - Seamless glide-to-start (`DotAt` with 1.4x hit radius): cursor hovering over any dot immediately starts pattern capture without physical clicks or double-tapping.
  - Double-buffered flicker-free rendering in `WM_PAINT` with high-contrast active dots (radius 14, deep blue) and inactive dots (radius 10, soft gray).
  - Explicit window title `Patlock - Pattern Unlock (Glide Mode)` for unmistakable version verification on the lock screen.
  - Flexible pattern completion: auto-submits on 380ms pause, OR tapping/clicking on the final dot, OR releasing the mouse/trackpad button.
  - Immediate pattern verification in `CSampleCredential::CommandLinkClicked`: shows instant `MessageBoxW` alert upon incorrect pattern or lockout without silent declines.
  - Asynchronous auto-submission: gives keyboard focus to `SFI_SUBMIT_BUTTON` and pulses `VK_RETURN` on a background worker thread after `CommandLinkClicked` returns, automatically submitting the credential to LogonUI without requiring manual arrow button clicks.
  - Automated VM deployment script `install/update.ps1` and one-click `update.cmd` that stops lingering test processes, safely updates in-use `PatlockProvider.dll` in System32 even if locked by LogonUI, and verifies the installation via SHA-256 hash comparison.
- Verified in VM by user:
  - Previous Phase 4 unlock succeeded with pattern entry and manual submit arrow.
- Not verified yet:
  - Live VM verification of the new glide trail and asynchronous auto-submission.
- Next step:
  - User pulls latest commits and runs `update.cmd` in the VM to test glide unlock and auto-login.

### 2026-11-13: Phase 7 source checkpoint; trackpad tuning harness & hardware safeguards
- Written: interactive multi-pass desktop tuning harness in `src/tools/pattern_window_demo/main.cpp`
  with real-time dot sequence breakdown, length and validity reporting, and trail visibility toggle.
- Line-segment intersection and hit radius tuning documented in `docs/PHASE7_VM.md`.
- Hardware deployment safeguards, prerequisites (second admin, BitLocker key backup, restore point),
  and Safe Mode emergency removal steps detailed for optional Step 7b.
- Host status: written and committed. Real hardware trackpad testing and tuning remain pending user execution.

### 2026-11-13: Phase 6 source checkpoint; packaging, verification matrix & documentation
- Written: automated distribution packager `install/package.ps1` producing clean zip archives and standalone folder distributions.
- Wrote `docs/PHASE6_VM.md` covering end-to-end VM test passes across installation, coexistence, enrollment, unlock, escalating lockout, reboot freshness, and clean uninstallation.
- Rewrote `README.md` with full architecture documentation, security threat model, honest convenience framing, build instructions, and recovery links.
- Host status: scripts and packaging written; runtime VM verification pending.

### 2026-11-13: Phase 5 source checkpoint; VM acceptance pending
- Written: Platform Crypto Provider (`MS_PLATFORM_CRYPTO_PROVIDER`) integration for TPM 2.0
  RSA-2048 key storage, pattern-derived usage authorization (`PCP_USAGEAUTH` / `NCRYPT_PIN_PROPERTY`),
  RSA-OAEP key wrapping for the AES-256 password key, and graceful fallback to baseline mode when TPM is absent.
- Escalating lockout policy (30s, 60s, 300s, 900s) and 10-failure disablement;
  attempt reservation and durable registry flush before pattern hash evaluation.
- Full-password rule enforced via volatile boot identity (`HKLM\SOFTWARE\Patlock\Boot\<SID>`)
  and 24-hour freshness timer; stale password detection via constant-time `passwordMac` comparison in `Refresh`.
- Fail-closed error handling and audit of provider logging (only fixed lifecycle message; zero secrets).
- Record version bumped to 3 (adds `tpmMode`, `tpmWrappedKey`, `passwordMac`).
- Expanded security unit tests in `tests/security_tests.cpp` to verify escalating delays,
  memory wiping, and TPM availability detection. Wrote `docs/PHASE5_VM.md`.
- Host status: source written, CMake and vcxproj updated. All VM runtime behavior, TPM integration,
  lockout, and reboot freshness remain UNVERIFIED pending VM test pass.

### 2026-11-13: Phase 4 source checkpoint; VM acceptance pending
- Written: pattern-to-password verification at submit time, local-account sample
  serialization, immediate plaintext cleanup and a persisted one-shot submission
  guard tied to ReportResult. Draw then submit; no automatic retry.
- Record version is now 2; pre-Phase-4 records require re-enrollment.
- Wrote PHASE4_VM.md covering unlock/sign-in, repeat success, stale passwords,
  crash/no-callback handling and fallback. None has been executed.
- In particular, successful ReportResult delivery on the target Windows build
  must be verified before claiming repeat Pattern unlock works.
### 2026-11-13: Phase 3 source checkpoint; VM acceptance pending
- Written: native C++ elevated enrollment and password-refresh tool; local-account
  validation, double drawing, PBKDF2-HMAC-SHA256, SID-bound AES-GCM records,
  protected HKLM DACL, record HMAC, persistent serialized attempt updates and
  password freshness bound to a volatile boot identity.
- Encryption is present when the record is first created so no interim plaintext
  password format is introduced. Windows credential submission remains disabled.
- Wrote crypto/policy tests; not executed. See PHASE3_VM.md for each enrollment,
  ACL, lockout, reboot and corruption acceptance case. See STORAGE.md for design.
- No host builds, registry writes, enrollment or runtime testing performed.

### 2026-11-13: Phase 2 source checkpoint; VM acceptance pending
- Written: shared modal 3x3 pattern window, fixed-size wiped pattern buffer,
  five-dot minimum, midpoint insertion, segment hit testing, hide trail,
  cancellation, standalone demo and pattern-rule test executable.
- Provider uses its own GUID and obtains the dialog owner with OnCreatingWindow.
  Credential serialization remains gated off; this checkpoint cannot unlock.
- Tests written but NOT executed; builds and runtime checks are VM-only.
  See PHASE2_VM.md for desktop, lock-screen and sign-in-screen cases.
- The user approved explicit Patlock password re-verification. No inference
  from built-in Password/PIN/Hello events will be used.
- Phase 1 review findings addressed: temporary password wipes, resource include
  path, uninstall ownership checks and failed-install DLL cleanup. Static review
  does not establish runtime behavior. All VM acceptance remains pending.

### 2026-11-13: Phase 1 implementation checkpoint; not accepted
- Written: Microsoft's V2 CredentialProvider source at pinned revision
  `434f6002bdf9cf9829406c3ff2b33387982d6168`, upstream license/provenance,
  C++17/x64 CMake configuration with static CRT, VMware-guarded install/removal
  scripts, raw VM-only registry files, fixed-message bounded diagnostics and
  initial sample error-path/buffer cleanup fixes.
- Host inspection: the three PowerShell scripts parse without syntax errors.
  No builds, executable tests, registry operations or installations performed.
- Human VM results: none. Follow PHASE1_VM.md to build, inspect warnings, check
  the sample tile, verify Password/PIN fallback, uninstall and verify removal.
- Not verified: compilation/linking, warnings, COM lifecycle, log writing,
  registration/removal, sign-in/lock screens, memory cleanup at runtime,
  recovery, or any real-trackpad behavior. Phase 1 is not marked done.
- Missing later features: pattern window/demo, enrollment, protected storage,
  pattern authentication, persistent lockout, full-password freshness, TPM,
  packaging acceptance and hardware tuning.
- Design blocker: PASSWORD_REFRESH_DECISION.md describes the proposed explicit
  password re-verification change. The user approved it on 2026-11-13.
- Publishing: the requested GitHub repository could not be read using
  noninteractive Git credentials. No remote synchronization has been verified.
- Next step: implement the approved decision and resolve GitHub authentication; run the
  Phase 1 VM checklist. Do not confuse a local commit with a successful push.

### Phase 0: done
- Built: Python prototype (`prototype/pattern_unlock.py`) with 3x3 grid, skipped-dot rule, salted hash, verification, lockout.
- Verified: hashing and skipped-dot logic tested; GUI to be run on the laptop.
- Not verified: anything on the real Windows lock screen.
