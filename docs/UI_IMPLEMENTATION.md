# Pattern-window implementation and VM acceptance

This checkpoint is source implementation with static inspection only. No host build,
execution, registration, installation, screen-reader test, VM lock-screen test, or
real-trackpad validation was performed. This is not a claim of release readiness.

## Behavior

The existing rounded card and nine-dot style remain. A stroke starts only when the
left button is pressed on a dot, follows pointer movement while that button is held,
and submits only on release. Hover and pause timers never begin or finish a stroke.
The skipped-dot rule and configurable minimum in `pattern.h` still apply. Capture
loss, cancel mode, deactivation during drawing, or DPI/layout changes discard an
unfinished stroke. Escape, closing, inactivity timeout, and the native fallback
button close the dialog without authentication. The built-in Password tile remains
provided by Windows; the button returns control to its sign-in options.

The fallback is a standard focusable BUTTON, initially focused, with an Alt+O mnemonic
and normal dialog keyboard activation. Status uses a standard STATIC with text
updates and accessibility name-change notifications. The grid itself remains pointer
input: there is no full keyboard pattern entry or custom UI Automation grid provider.
Screen-reader announcements and focus behavior require acceptance testing below.

Geometry, vector drawing, hit targets, child controls, and fonts scale from the
window DPI. The dialog inherits its host awareness and never sets process or thread
DPI awareness inside LogonUI. Initial bounds use the owner monitor work area;
WM_DPICHANGED uses the suggested monitor/position and clamps the scaled window into
that work area. Small work areas reduce the scale; an area requiring less than 50%
scale declines the dialog. High contrast reads SPI_GETHIGHCONTRAST and uses system
window/text/highlight colors, including after system-setting/color changes.

The verifier contract is unchanged. An optional RetryDelay callback reports S_OK
and authoritative remaining seconds, E_ACCESSDENIED for Password-only fallback,
or another failure. A lockout disables drawing, refreshes its displayed countdown
from that callback once per second, and resumes only at zero. Without the callback,
lockout stays Password-only. Callback exceptions/errors, control/font/brush creation
failures, timer failures, and detected display-buffer failures decline the dialog.
Pattern buffers are cleared on cancellation, callback errors and after output transfer.

## VM acceptance (not yet verified)

1. Build x64 with Visual Studio 2022 in the Windows 11 VM. Run the standalone demo
   before registering the provider. Confirm there are no actionable compiler warnings.
2. Move the pointer across every dot and pause over dots without clicking: no stroke
   or submission. Hold a stroke at five dots for several seconds: no submission until
   release. Verify fast drags, skipped dots, too-short strokes, hide-trail, retries.
3. During a stroke press Escape, switch activation, lose capture, change scale, or
   trigger cancel mode. Confirm no verifier call/submission and no retained stroke.
   Escape and the fallback button must always return to sign-in options.
4. Use Tab, Shift+Tab, Enter, Space and Alt+O on the fallback. Inspect its accessible
   name/role and the status text with Narrator/accessibility tooling. Confirm status
   announcements; do not infer screen-reader acceptance from standard controls alone.
5. Test 100%, 125%, 150%, 200%, mixed DPI monitors, negative monitor origins, and
   small resolutions. Check card/control/text bounds, dot/hit alignment, capture
   cancellation during changes, and no process-wide awareness changes.
6. Toggle high contrast and change its palette while open; verify all dots, lines,
   statuses and focus indicators remain readable, including error/accepted states.
7. With the real verifier, confirm timed lockouts display the persisted remaining
   seconds and cannot draw until zero. Confirm permanent disable reads Use Password,
   callback errors close, reopening/reboot cannot bypass storage policy, and Password
   still works. Validate both sign-in and unlock desktops in the VM.
8. Real trackpad feel, tap-and-drag, palm rejection and hardware display changes remain
   Phase 7 work and cannot be established with VMware virtual mouse input.

## Official API references consulted

- [GetDpiForWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getdpiforwindow)
- [WM_DPICHANGED](https://learn.microsoft.com/en-us/windows/win32/hidpi/wm-dpichanged)
- [Static controls](https://learn.microsoft.com/en-us/windows/win32/controls/static-controls)
- [SystemParametersInfoW / SPI_GETHIGHCONTRAST](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-systemparametersinfow)
