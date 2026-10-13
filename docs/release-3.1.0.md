# ARE Audio Renderer 3.1.0

Fixes the disabled settings gear in **MPC-HC → Options → Playback → Output**
after updating the player. The previous integration depended on a modified
MPC-HC 2.8.2 executable. Version 3.1.0 uses the installed player's standard
Windows controls without replacing its executable or checking version numbers.

The installer starts a silent per-user helper and registers it for Windows
sign-in. It restores ARE's adjacent gear within about one second of opening
Output or selecting ARE. During playback the gear opens the active renderer
for live status and Apply; before playback it opens configuration settings.
Mouse presses remain valid through MPC-HC's idle button-disable updates.

Download x64 for mpc-hc64.exe or x86 for mpc-hc.exe, close MPC-HC, extract the
ZIP and run **Install.cmd**. Restart the player once after installing the new
renderer. Existing ASIO device and processing preferences are retained.
Run MPC-HC at normal user privilege so its per-user helper can reach the UI.

To omit the helper, run Install.ps1 -NoMpcIntegration. Settings remain available
through Play → Filters → ARE Audio Renderer and the standalone settings tool.
Uninstall.bat stops the helper, removes its startup entry and unregisters ARE.
The helper uses the standard Output combobox/gear layout; future UI redesigns
may require an update. There is no MPC-HC version whitelist.

Validation: clean MSVC release builds and all nine CTest suites pass on each
architecture, including audio/DirectShow contracts and settings integration.
The integration regression covers interrupted mouse presses, keyboard and
accessibility clicks, modal settings, active/stale graph instances, renderer
selection changes, detachment, shutdown and ambiguous layouts.

Assets: ARE-Audio-Renderer-3.1.0-x64.zip, ARE-Audio-Renderer-3.1.0-x86.zip,
ARE-Audio-Renderer-3.1.0-source.zip and SHA256SUMS.txt. Complete source, build
scripts, vendored dependencies, licenses and text validation evidence are
included. Original renderer source retains its MIT license; combined Windows
binaries use GPL-3.0 with the bundled third-party notices.
