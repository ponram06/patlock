# Production deployment guide

Patlock is an additive Credential Provider: it must never remove, replace, or
filter Windows Password sign-in. Perform every build, installation, upgrade and
recovery test in a disposable Windows VM before considering deployment.

## Build a release

On a 64-bit Windows build machine with Visual Studio 2022 and the Windows SDK,
run `install\Build-Release.ps1`. It configures Release x64 through
`FindBuildTools.ps1`, builds, runs CTest, and stops before packaging if any test
fails. Use `-CertThumbprint <SHA1>` to Authenticode-sign both binaries with an
SHA-256 timestamp. The output is `dist\Patlock-Release.zip`, containing the
binaries, hashes, installer, uninstaller, recovery guide and README.

## Install and upgrade

Extract the release archive and run `Install.cmd` as an administrator. The
installer verifies `SHA256SUMS.txt`, requires a valid DLL signature unless
`-AllowUnsigned` is explicitly provided for a throwaway test VM, and attempts a
restore point. It installs a hash-versioned DLL under `Program Files\Patlock\bin`,
locks down the folder ACL, registers the COM class, then registers the provider
last. The previous generation is unregistered first and its DLL is deleted or
scheduled for deletion after reboot. The installer registers an Applications
event source and an Add/Remove Programs entry.

After installation, enroll only a throwaway test account, lock the VM, and
verify that **Password** remains available through Sign-in options before testing
the Pattern tile. Never deploy a build that has not passed the matrix below.

## Uninstall and recovery

Run `Uninstall.cmd`, or choose Patlock in Installed Apps. It requires typing
`YES` unless `-Quiet` is supplied. It unregisters only registry entries carrying
the Patlock ownership marker, removes the event source, and deletes provider
files (scheduling a reboot deletion if LogonUI has one loaded). By default it
also deletes enrollment records and Patlock TPM keys. Use `-KeepEnrollments`
only when intentionally preserving them for a later reinstall.

If sign-in is impaired, use the built-in Password tile first. Otherwise boot
Safe Mode (which normally omits third-party Credential Providers) and run the
uninstaller as administrator. For a VM, reverting the pre-install snapshot is
the preferred recovery path. See `RECOVERY.md` for offline-registry recovery.

## Required VM test matrix

Before release, record results for each supported Windows 10 and Windows 11
build, with TPM present and absent, for local, Microsoft and domain accounts;
multiple local users; Windows Hello enabled; 100%, 150% and 200% DPI; first
install; signed and explicitly allowed unsigned test install; upgrade; uninstall;
reinstall; restart; and Safe Mode recovery. For each row verify: Password stays
available, corrupt/missing data hides or declines the Pattern tile, five wrong
patterns start the persisted escalating delay, ten failures require a password
refresh, success resets the counter, and Event Viewer contains only SID/counter/
HRESULT metadata—never patterns, passwords, keys or hashes.

## Release gate

This code is not production-ready until the full matrix passes, the DLL is
signed with a production certificate, rollback/recovery is rehearsed, and an
independent security review approves the current `SECURITY.md` threat model.
