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
  password re-verification change. Approval is needed before changing PLAN.md.
- Publishing: the requested GitHub repository could not be read using
  noninteractive Git credentials. No remote synchronization has been verified.
- Next step: resolve that design decision and GitHub authentication; run the
  Phase 1 VM checklist. Do not confuse a local commit with a successful push.

### Phase 0: done
- Built: Python prototype (`prototype/pattern_unlock.py`) with 3x3 grid, skipped-dot rule, salted hash, verification, lockout.
- Verified: hashing and skipped-dot logic tested; GUI to be run on the laptop.
- Not verified: anything on the real Windows lock screen.
