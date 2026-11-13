# Continuation prompt: release engineering and UI

Review and repair the current Patlock source, starting with AGENTS.md, PLAN.md,
docs/PHASES.md, docs/SECURITY.md and docs/AUDIT_2026-11-13.md. Treat older reports
as historical context. Preserve user changes. Work in small local commits; do
not push unless requested. Never build/test/install/register on the host. All
runtime verification belongs in the Windows 11 VM. Never touch built-in providers.

First map each audit finding to current code and separate confirmed defects,
design trade-offs and unverified behavior. Give a brief file/risk/test plan for
each phase. Request approval only for actual PLAN.md changes. Keep Option A's
current reboot/freshness policy unless the user approves changing it. Do not
announce market readiness based on source inspection or screenshots.

Implement the remaining security and persistence repairs:

1. Make TPM enrollment transactional with unique persisted key generations.
   Preserve the existing key/record until the replacement is durably committed.
   Handle each creation, write, flush, cleanup and crash boundary. Do not replace
   unsupported authorization properties with guessed APIs or raw binary PINs.
2. Verify PCP usage authorization, hardware backing, SYSTEM access, reset behavior
   and anti-hammering using official Microsoft sources and VM/hardware evidence.
   Distinguish unsupported hardware from implementation failures. Disclose mode;
   do not silently downgrade a configured TPM record to baseline during unlock.
3. Complete the stale-password one-shot protocol: persist pending submission with
   a unique token before returning plaintext; match token on success; disable on
   failure or interrupted submission; preserve repeat successful unlock. Test
   nullable ReportResult outputs, duplicate calls, user switching and re-enrollment.
4. Ensure failed record flush cannot newly enable credentials or clear lockout.
   Use a documented commit/activation protocol. Fault-inject registry write/flush
   failures and abandoned/timed-out mutex acquisition. Never log secret contents.
5. Keep constant-time comparisons, random salts/nonces and wiped buffers. Confirm
   version-4 records cannot be interpreted as older layouts. Explain re-enrollment
   and recovery without exporting stored credentials.

Make the sign-in UI restrained and consistent with Windows 11:

- Keep the Windows avatar/account header. Use one quiet dark card with consistent
  spacing, legible Segoe typography and bright enough neutral grid targets.
- Design in device-independent units; handle initial DPI and DPI changes, owner
  monitor/work area, small displays and mixed scale factors without changing
  LogonUI's process-wide DPI policy or obscuring system controls.
- Show one clear instruction for the selected input mode. Prefer deliberate
  drag/release; evaluate hover as an explicit alternative rather than surprising
  users with automatic start or premature pause-to-submit.
- Define idle, drawing, checking, pattern accepted, Windows failure, timed lockout,
  password refresh/re-enrollment required, unavailable and cancelled states.
  Derive lockout countdown from policy state, not a guessed delay. Prevent retries
  while locked. Never claim Windows success merely because the pattern matched.
- Make the fallback control a keyboard-focusable, accessible control that closes
  the modal and restores usable native sign-in options. Support Escape, focus
  indication, high contrast, reduced motion and screen-reader status updates.
- Keep generic authentication errors from revealing the correct path. Respect
  hide-trail and never expose pattern sequences in logs or diagnostics.
- Add a professional desktop enrollment/recovery flow with confirmation, clear
  account identity, honest baseline/TPM mode and no example credentials.

Before release, provide VM build/test commands and fault-injection coverage,
installer update/rollback and removal tests, recovery instructions, signing and
distribution requirements, and real-trackpad test cases. Update PROGRESS.md with
what was written, what the human actually verified, and what remains unverified.
Deliver a reviewable local commit and concise list of remaining release blockers.
