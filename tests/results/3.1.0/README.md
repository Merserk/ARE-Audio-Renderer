# ARE Audio Renderer 3.1.0 checks

Validation date: 2026-10-13 (Europe/Kiev).

Clean source builds: Visual Studio 18 2026, MSVC 19.51.36260.0, SDK 10.0.26100.0,
Release configuration, C++23, x64 and Win32. The release DLL, settings helper
and control tool were built from the source prepared for the GitHub update.

Each architecture passes all nine CTest suites. See ctest-x64.txt and
ctest-x86.txt. The new integration fixture uses actual Windows controls,
a thread-specific hook, COM property frames, and a DirectShow graph; it
checks the displayed status to distinguish a configuration instance from
an active graph instance. It simulates repeated idle disable updates between
mouse-down and mouse-up, and exercises BM_CLICK and keyboard activation.
It also checks selection changes, stale-instance removal, helper shutdown,
restoring the original disabled state and refusing ambiguous button layouts.

The installed official MPC-HC 2.8.3 was inspected and its ARE gear became
enabled. Desktop automation could not complete its click because it reported
the owned modal control as outside/mismatched with the main target window;
modal click behavior is verified by the native integration fixtures.

Earlier physical audio/codec measurements remain in ../0.3.0/. The 3.1.0
release changes settings integration and renderer-instance discovery.
