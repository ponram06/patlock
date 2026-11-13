# Floating sign-in UI: reference-led redesign

The user's second reference image guides this UI: no boxed dark card in normal
mode, larger grid below the native account header, solid dark dots and a quiet
white text fallback. The Windows avatar/account header is not recreated or moved.

## Written behavior

- 360 x 342 logical-unit layout, 90-unit dot spacing and 28-unit hit radius.
  Layout/hit-testing use shared constants in pattern_layout.h. Bounds continue
  to scale/clamp to the owner's monitor work area.
- Solid near-black idle dots with a fine lighter edge, cyan hover/selected rings
  and rounded 4-unit connecting lines. Error/accepted colors retain text status.
- Normal background is color-key transparent, without wallpaper capture or an
  undocumented acrylic API. High contrast has an opaque system-colored surface.
- The fallback is still a native BUTTON with its name/mnemonic/keyboard behavior,
  owner-drawn as text on a quiet rounded dark action surface. That surface keeps
  whitespace and padding clickable, unlike fully transparent text. Its focus
  indicator is rendered when Windows requests it; no default white button face.
- Status remains a native accessible STATIC with a transparent normal background.
- Hover start, pause completion, drag alternative, lockout countdown, Escape and
  capture/deactivation cancellation retain the existing authentication behavior.
- Because color-keyed pixels pass mouse messages through, a 30 ms UI timer samples
  actual cursor movement while this modal is active and ready for drawing. Changed
  positions feed the same pointer handler; stationary samples do not restart
  inactivity or completion deadlines. Cursor bounds/capture checks still apply.

This is source implementation, not a verified screenshot or a claim of Microsoft
endorsement. No provider build or executable was run on the development host.
No record format change; this UI update alone does not require re-enrollment.

## VM checks

1. Build/run RUN_VM_TEST.cmd in the VM; open the new standalone demo first.
2. Verify there is no card/white button in normal mode; inspect dots, text and
   transparent gaps on both bright and dark backgrounds. Native LogonUI content
   must not bleed through the grid; report a screenshot if it does.
3. Start by hovering in a generous target area, then draw through transparent
   gaps without clicking. Movement must not be mistaken for completion; only a
   stable valid stroke finishes after its configured pause.
4. Leave the window or move to fallback mid-stroke; the gesture must cancel.
   Click fallback letters, whitespace and padding; each visible area must respond.
   Check Escape, Alt+O, Tab, Space/Enter, focus indication and Narrator's name/role.
5. Leave the mouse stationary and verify inactivity still closes the dialog.
   Reopening on a dot must not start accidentally.
6. Test high contrast, display scales and small/mixed-monitor layouts. Solid HC
   background is intentional. Confirm no overlap with native username/system UI.
7. Install the updated DLL only through the VM upgrade steps, then test sign-in,
   repeated unlock, wrong patterns and Password fallback. Old automated/lock-screen
   results do not verify the redesigned candidate.

Official references:

- https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#layered-windows
- https://learn.microsoft.com/en-us/windows/win32/controls/wm-drawitem
