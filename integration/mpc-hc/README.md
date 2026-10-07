# MPC-HC compatibility

The validated host is **MPC-HC 2.8.2**, upstream commit
[`a84d0cf38a1866f3518bb819c901300dacff5a9b`](https://github.com/clsid2/mpc-hc/tree/a84d0cf38a1866f3518bb819c901300dacff5a9b).
The player is obtained and built separately from the renderer release.

ARE 0.3.0 uses the same compatibility patch as 0.2.0. The rate/Apply fixes are
inside the renderer DLL; an already patched player needs no additional rebuild.

With Audio Switcher enabled, unpatched MPC-HC 2.8.2 rejects ARE during pin
connection because its renderer detection uses connected pins and a CLSID
whitelist. The included patch recognizes the standard renderer flag. It also
lets the Output settings button open external COM property pages and finds the
active renderer before the Filters menu has been opened.

The patch changes four host files; audio processing stays in the ARE DLL.
Renderer playback and properties were validated with this integration. Other
MPC-HC versions require a review of their renderer detection and patch context.

## Build the compatible host

Install Visual Studio 2026 with Desktop development with C++, **ATL and MFC**,
a Windows SDK, Git and NASM. The helper defaults to toolset `v145`, detects the
installed SDK, verifies the pinned source revision and applies the patch once.
From the ARE source folder:

```powershell
git clone https://github.com/clsid2/mpc-hc.git C:\src\mpc-hc
git -C C:\src\mpc-hc checkout a84d0cf38a1866f3518bb819c901300dacff5a9b
git -C C:\src\mpc-hc submodule update --init
git -C C:\src\mpc-hc\src\thirdparty\LAVFilters\src submodule update --init ffmpeg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-MpcHc.ps1 `
  -SourceDirectory C:\src\mpc-hc -Architecture x64
```

Choose `x86` for a 32-bit player. Use the resulting executable with runtime
files from official MPC-HC 2.8.2 of the same architecture. The helper only builds
inside the supplied source directory; it does not replace an installed player.
Keep a copy of the original executable before manually installing the result.
An official player update replaces these host changes.

## License

This patch contains modified MPC-HC code and is **GPL-3.0-or-later**. Upstream
copyright notices remain in the affected files: Gabest and the MPC-HC authors.
The GPLv3 text is included in [third_party/ASIO-GPL-3.0.txt](../../third_party/ASIO-GPL-3.0.txt).
The renderer's original source remains MIT licensed. MPC-HC source, executables
and runtime codecs are not included in ARE binary ZIPs.
