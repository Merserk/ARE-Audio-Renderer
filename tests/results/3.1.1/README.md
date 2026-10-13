# ARE Audio Renderer 3.1.1 checks

Validation date: 2026-10-13 (Europe/Kiev).

All nine CTest suites pass on x64 and Win32, including the changed integration
fixture that launches the helper with a quoted flag and trailing whitespace.
See ctest-x64.txt and ctest-x86.txt. The audio engine is unchanged.

installer_tests.ps1 executes Install.cmd and Uninstall.bat through CMD, which
starts Windows PowerShell 5.1. It tests flag forwarding and exit status; reads
helper process metadata and the architecture-specific mutex to confirm a
hidden helper rather than an accidental settings dialog; and verifies that
reinstall/uninstall waits for the old helper process to exit.

Installed DLLs are held open with FileShare.Read to deny writes. Identical
reinstall, uninstall followed by reinstall, and another valid binary with the
same file version all succeed. The latter uses a harmless PE overlay and must
leave the locked original intact, register a separate payload, and reuse that
payload on subsequent identical installs. Installed-folder uninstall is tested
while the DLL remains locked. See installer-x64.txt and installer-x86.txt.

The installer fixture uses temporary LOCALAPPDATA folders, serializes with
other architecture-specific validation, and restores prior per-user renderer
registration/startup values. It leaves saved audio preferences intact. A local
check also successfully reinstalled the user's extracted 3.1.0 package while
the reported old process continued to hold its identical installed DLL.
