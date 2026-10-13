# Archived MPC-HC 2.8.2 host patch

Base: MPC-HC **2.8.2**, commit
`a84d0cf38a1866f3518bb819c901300dacff5a9b` from
<https://github.com/clsid2/mpc-hc>.

The patch changes four upstream files:

- `PPageOutput.h/.cpp`: enable the existing audio settings gear for external
  renderers with COM property pages. Cache an unopened configuration instance
  for use before playback, and open the active graph instance during playback.
- `MainFrm.cpp`: locate property-page objects directly in the current graph,
  even before the Filters menu has been opened; retain the calling modal owner.
- `DSUtil.cpp`: recognize an audio renderer with `IBasicAudio` and the standard
  `IAMFilterMiscFlags` renderer flag before connecting its input. The audio
  switcher previously rejected ARE Audio Renderer because its audio pin was
  still unconnected when checked and its CLSID was not in a fixed whitelist.

No ASIO-specific settings or signal processing are compiled into the player.
The renderer owns its settings and its live status. The player patch permits
other external renderers with standard COM property pages too.

## Rebuild

Install Visual Studio 2026 with Desktop development with C++, ATL, MFC and a
current Windows SDK. Put NASM 3.02 or a compatible later release on PATH.
Git is required to apply the patch. No legacy Windows SDK or Yasm is needed
for this executable build. It preserves internal-filter support and uses the
existing MPC-HC 2.8.2 LAVFilters DLLs at runtime.

The player patch and executables are unchanged from ARE Audio Renderer 0.2.0;
the volume, transition, PCM precision and sample-rate conversion features are
implemented in the renderer DLL.

MPC-HC is not included in renderer release ZIPs. To build the optional player
integration, clone the exact base and initialize its submodules:

```powershell
git clone https://github.com/clsid2/mpc-hc.git C:\src\mpc-hc
git -C C:\src\mpc-hc checkout a84d0cf38a1866f3518bb819c901300dacff5a9b
git -C C:\src\mpc-hc submodule update --init
git -C C:\src\mpc-hc\src\thirdparty\LAVFilters\src submodule update --init ffmpeg
.\scripts\Build-MpcHc.ps1 -SourceDirectory 'C:\src\mpc-hc'
```

The script applies the patch if needed and builds with `v145` and the newest
installed Windows 10/11 SDK. Override `-WindowsSdkVersion` if needed.
`-LocalMfcProps` optionally supplies an MSBuild property file for a locally
extracted matching Microsoft MFC component; a normal VS MFC installation needs
no additional properties. The validated builds used MSVC 14.51.36231,
SDK 10.0.26100.0 and NASM 3.02.
