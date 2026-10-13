# MPC-HC settings integration — ARE Audio Renderer 3.1.1

Official MPC-HC enables its Output audio settings gear only for its built-in
audio renderers. Earlier ARE installations used a patched MPC-HC 2.8.2
executable. An official player update removed that patch and disabled ARE's
gear, even though the renderer's COM property page was still available.

Version 3.1.0 installs a per-user settings helper. It identifies MPC-HC's
selected **ARE Audio Renderer** combobox and its adjacent Windows button,
then connects the gear to ARE's property page. It does not replace or patch
the player executable, inspect machine-code offsets, or whitelist player
version numbers. Current and older MPC-HC releases with this Output layout
can use the same renderer package. Official player updates do not replace
the helper.

Version 3.1.1 fixes helper argument parsing for quoted flags and trailing
whitespace from Windows PowerShell. The installer reuses identical loaded
files and places changed binaries in another directory; uninstall waits for
hidden helper processes to exit. An open player/settings dialog can retain
its old DLL until it closes, and no longer prevents reinstalling the renderer.

## Installation and operation

Run the matching release's `Install.cmd`. The installer registers ARE, starts
`ARE-Audio-Renderer-Settings.exe --mpc-integration`, and adds an architecture-
specific entry under the current user's Windows Run key. The helper has no
window or tray icon; x64 and x86 installations run independently. It scans
visible MPC-HC windows every 750 ms, and installs a thread-specific Windows
message hook only when ARE's Output controls are present. It leaves other
renderers' click handlers with MPC-HC.

After opening Output or selecting ARE, allow about one second for the gear
to activate. Run MPC-HC at normal user privilege; an unelevated helper cannot
hook an administrator-elevated player. Restart MPC-HC after upgrading the
renderer because Windows retains loaded renderer/hook DLLs until player exit.

The gear opens the ARE renderer attached to the player's graph for live
status and Apply. Before playback, it creates a configuration instance.
The helper is separate from ASIO processing and does not change playback
formats, device preferences, or audio engine settings.

`Install.ps1 -NoMpcIntegration` installs the renderer without the helper.
`Uninstall.bat` stops it, removes that architecture's Windows startup entry,
and unregisters ARE. Settings are also available through **Play > Filters >
ARE Audio Renderer** and the standalone settings executable.

## Compatibility and validation

The integration requires standard Windows combobox/button controls and an
unambiguous adjacent gear. It does not depend on resource IDs, UI language,
or a specific MPC-HC version. It deliberately skips unfamiliar or ambiguous
layouts. A future player UI redesign may require an update; compatibility
with every possible future release cannot be guaranteed.

Regression fixtures exercise both architectures: control discovery, repeated
idle disabling during a mouse press, keyboard/accessibility clicks, modal
property pages before playback, active/stale graph instances, selection
changes, detachment and helper shutdown. See `VALIDATION.md`.

Stop an installed helper with
`ARE-Audio-Renderer-Settings.exe --stop-mpc-integration` before running the
integration test fixture locally. Restart it with `--mpc-integration` afterward.
The test restores prior per-user renderer registration and serializes with
the other tests that temporarily use renderer preferences.

The old host patch is retained for reference in
[`legacy-2.8.2.md`](legacy-2.8.2.md). Its build script intentionally checks the
matching historical source revision; it is not used by the 3.1.0 installer.
