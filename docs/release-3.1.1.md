# ARE Audio Renderer 3.1.1

Fixes Install.cmd failing with **ARE-Audio-Renderer.dll is being used by
another process**, including reinstalling after Uninstall.bat.

Windows PowerShell could append a space to --mpc-integration. Version 3.1.0
then opened a normal settings dialog instead of its hidden helper, leaving
the DLL loaded and outside the helper's shutdown mechanism. Version 3.1.1
parses quoted flags and trailing whitespace correctly.

Reinstall reuses byte-identical files without overwriting mapped DLLs or
executables. Changed binaries with the same version are installed separately.
Uninstall waits for hidden helper processes to exit, removes startup/renderer
registration, and preserves saved device preferences. Open player/settings
windows can retain their old DLL until they close; they no longer prevent
reinstalling. The wrappers forward arguments and preserve exit codes.

Download the matching x64/x86 ZIP, extract the complete folder and run
**Install.cmd**. Restart MPC-HC and close any older ARE settings window to
load 3.1.1. The settings gear compatibility introduced in 3.1.0 is retained.

Validation: all nine CTest suites pass on both architectures. Additional
regressions execute the actual CMD/BAT wrappers under Windows PowerShell,
hold installed DLLs against writes, test same-version changed binaries and
uninstall/reinstall, and verify hidden helper startup and completed shutdown.

Assets: ARE-Audio-Renderer-3.1.1-x64.zip, ARE-Audio-Renderer-3.1.1-x86.zip,
ARE-Audio-Renderer-3.1.1-source.zip and SHA256SUMS.txt. Original renderer source
retains its MIT license; combined binaries use GPL-3.0 and third-party notices.
