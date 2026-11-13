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
