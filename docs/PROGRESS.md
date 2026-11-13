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
