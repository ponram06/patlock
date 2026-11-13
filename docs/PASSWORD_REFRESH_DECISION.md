# Approval needed: proving a full-password login

Status: proposal only, 2026-11-13. PLAN.md has not been changed.

PLAN.md sections 5 and 8 require a full password after restart, after 24 hours,
and after repeated pattern failures. TESTING.md further requires that use of the
built-in Password tile clears Patlock's counters. The proposed provider-only
architecture has no specified trusted signal for that separate tile's success.

Microsoft documents ICredentialProviderCredential::ReportResult as reporting a
logon result back to a credential. ICredentialProvider lists no callback for
other providers' password logins. The Microsoft sample only processes its own
credential's result. These sources do not establish a supported way for our
tile to distinguish a successful built-in Password login from PIN/Hello or our
own stored-password submission. This is an unresolved design gap, not a claim
that every possible Windows integration is impossible.

- https://learn.microsoft.com/en-us/windows/win32/api/credentialprovider/nf-credentialprovider-icredentialprovidercredential-reportresult
- https://learn.microsoft.com/en-us/windows/win32/api/credentialprovider/nn-credentialprovider-icredentialprovider
- https://github.com/microsoft/Windows-classic-samples/tree/434f6002bdf9cf9829406c3ff2b33387982d6168/Samples/CredentialProvider/cpp

## Recommended concrete change for approval

Add explicit password re-verification inside Patlock for the local enrolled
account. Keep the built-in Password provider unchanged. When Pattern requires
refresh, the user supplies their full password to a dedicated Patlock flow.
Only a successful Windows authentication of that newly entered password can
update freshness and reset pattern failures. Drawing a valid pattern, a desktop
session event, and successful submission of the stored password must never do so.

This is a behavior change: signing in through the built-in Password tile alone
would not re-enable Pattern. The UI and tests must say so. The implementation
must distinguish newly entered passwords from stored-password submissions,
bind state to the account and boot, persist state before granting eligibility,
reject future/invalid timestamps, and fail closed on persistence failure.

If approved, revise PLAN.md sections 4, 5 and 8; SECURITY.md; PHASES.md Phase 5;
and TESTING.md's lockout/full-password requirements. Then implement the flow
and add VM acceptance cases for password/PIN/Hello/pattern transitions.

Alternative: retain the exact original behavior and research a documented,
trusted cross-provider mechanism before implementing freshness. No event-log
heuristic or presumed callback has been selected.

## Other remaining evidence

TPM pattern authorization and anti-hammering remain an explicit spike in
PLAN.md section 11. Merely creating a key with the Platform Crypto Provider
does not demonstrate the required authorization/lockout behavior. Do not label
TPM hardening complete without API validation and VM/hardware evidence.

AGENTS.md requires: "If something in PLAN.md looks wrong or impossible, stop
and say so rather than silently working around it" and "Wait for approval if
the plan changes anything in PLAN.md." The approval request is for the above
concrete behavior change, not for ordinary implementation or pushing commits.
