# Guided VM test launcher

Double-click **RUN_VM_TEST.cmd** in the repository root **inside the VMware VM**.
Accept the Windows administrator prompt. No Developer PowerShell session is
required: the script locates VS2022 bundled CMake when needed. Install the VS2022
C++ workload including CMake tools first.

The launcher checks VMware before elevation or mutation, asks you to confirm a
snapshot and working backup administrator Password login, builds and runs CTest,
and stops before uninstalling if anything fails. It then unregisters the old
provider and offers a reboot. Save other VM work before typing RESTART.

**After reboot, sign in using Password and double-click the same CMD again.**
The checkpoint verifies a reboot occurred and that the tested DLL/enrollment/demo
hashes still match. The launcher verifies Patlock is unregistered, removes only
its old System32 DLL, installs the tested DLL, opens enrollment and opens the
desktop demo. Re-enroll for record format 6 using a throwaway local account's
actual Windows password, not its PIN.

The final prompts guide you to lock with Windows+L inside the VM. The script
cannot prove lock-screen authentication, trackpad feel, Password fallback or TPM
anti-hammering. Complete VM_ACCEPTANCE.md and TPM_VALIDATION.md and report results.
Clicking Cancel in a GUI is not a completed enrollment or input test even if the
GUI exits without an error; do not proceed unless you saw the relevant success.

The ignored `.patlock-vm-test.json` contains boot/build hashes and workflow stage,
never credentials or patterns. It is a convenience checkpoint, not a security
attestation. If build files change, restart the workflow after removing only that
checkpoint. Do not delete a registered or loaded DLL manually to bypass checks.
If interrupted after installation, the launcher keeps the checkpoint for retry.
Type COMPLETE after your tests to clear it.

Source/syntax reviewed only; this launcher has not been executed on the host or
verified inside the VM. It changes the registry/installation only after its VM,
elevation, human fallback confirmation and build/test gates.
