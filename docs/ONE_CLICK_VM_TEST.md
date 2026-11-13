# Automated VM tests only

Pull the latest repository inside the VMware VM, then double-click
**RUN_VM_TEST.cmd**. The console remains open so you can read any error.

This launcher only configures, builds and runs the five CTest targets. It does
not elevate, ask for snapshot/Password confirmations, install/uninstall, delete
System32 files, enroll an account, restart Windows or change the provider's
registration. Existing `.patlock-vm-test.json` checkpoints are ignored.

It locates actual x64 MSVC compiler files in VS2022 or newer installations,
including Build Tools and previews, and selects a generator supported by an
available CMake/CTest pair. Build output uses a separate cache per generator and
Visual Studio instance; the console prints its location. The old `build/Release`
deployment output and installed provider are not replaced by this test run.

If compiler tools are genuinely missing, open **Visual Studio Installer > Modify
> Desktop development with C++** and include MSVC x64/x86 tools, a Windows SDK,
and C++ CMake tools for Windows. Then rerun the CMD. If newer Visual Studio is
present but CMake does not support its generator, update CMake.

The prior VS2022-only discovery did not find a matching installed compiler. That
message alone cannot establish whether the workload is missing or a different
Visual Studio version is installed; the revised diagnostics distinguish these.

Snapshots and Password fallback are still required before installing/upgrading
the sign-in DLL. That is a separate manual workflow in VM_ACCEPTANCE.md; it is
not part of this test-only launcher. Automated tests do not prove live sign-in,
TPM anti-hammering, or trackpad behavior.

Sources checked:

- https://github.com/microsoft/vswhere/wiki/Find-VC
- https://cmake.org/cmake/help/latest/variable/CMAKE_GENERATOR_INSTANCE.html

Only source/syntax checks were performed on the host; the launcher has not been
executed there. Build and runtime verification remain VM-only.
