# ARE Audio Renderer

[![Downloads](https://img.shields.io/github/downloads/Merserk/ARE-Audio-Renderer/total.svg?style=flat-square&label=Downloads&color=brightgreen)](https://github.com/Merserk/ARE-Audio-Renderer/releases) ![Platform](https://img.shields.io/badge/Platform-Windows-0078D4?style=flat-square&logo=windows11&logoColor=white) ![Architecture](https://img.shields.io/badge/Architecture-x64%20%7C%20x86-6f42c1?style=flat-square) [![Host](https://img.shields.io/badge/Host-MPC--HC-5C6BC0?style=flat-square)](https://github.com/clsid2/mpc-hc) ![Audio](https://img.shields.io/badge/Audio-ASIO-8A2BE2?style=flat-square) ![SRC](https://img.shields.io/badge/SRC-r8brain%20%7C%20SoX%20Sinc-57A64A?style=flat-square) [![Patreon](https://img.shields.io/badge/Patreon-Merserk-FF424D?style=flat-square&logo=patreon&logoColor=white)](https://www.patreon.com/Merserk)

**ASIO audio output for [MPC-HC](https://github.com/clsid2/mpc-hc), with precise, configurable sample rate conversion.**


<p align="center">
  <img
    src="https://github.com/user-attachments/assets/de47b93f-ca7a-4ec1-a43d-76e171193f1c"
    alt="image"
    style="width: 450px; max-width: 100%; height: auto;"
  />
</p>

ARE Audio Renderer is a DirectShow audio renderer for MPC-HC that sends decoded PCM audio to an ASIO device. It focuses on precise output control, high-quality sample-rate conversion, and a clear view of the active audio path.

## Features

- **Direct ASIO output** with selectable device, first output channel, buffer size, and output precision.
- **High-quality sample-rate conversion** using r8brain or SoX Sinc, processed in Float64 with linear-phase filters.
- **Device-rate playback** with optional resampling to keep the ASIO device at its current sample rate.
- **Mono, stereo, and surround support.** Mono can feed both stereo outputs; multichannel audio is preserved when enough outputs are available and downmixed when necessary.
- **Automatic or explicit PCM output** with 16-, 24-, and 32-bit choices and dithering when integer quantization is required.
- **Live playback status** for input/output formats, sample rates, channel mapping, buffer latency, conversion state, and underruns.
- **Runtime settings and playback-rate changes** designed to minimize unnecessary ASIO device reinitialization.

## Installation

You need **Windows**, **MPC-HC**, and an **ASIO driver** matching the architecture of MPC-HC.

1. Download the package for your MPC-HC architecture from [Releases](https://github.com/Merserk/ARE-Audio-Renderer/releases).
2. Prepare a compatible MPC-HC build using the included [compatibility patch](integration/mpc-hc/README.md).
3. Close MPC-HC, extract the package, and run `Install.cmd`.
4. Select your ASIO device in the renderer settings and click **Apply**.
5. Restart MPC-HC, then select **ARE Audio Renderer** under **Options → Playback → Output → Audio Renderer**.

During playback, open the renderer settings from **Play → Filters → ARE Audio Renderer**. You can also use `ARE-Audio-Renderer-Settings.exe` to configure defaults outside the player.

To unregister the renderer, run `Uninstall.bat` from the installed or extracted folder.

## Audio behavior

ARE receives decoded PCM from the player's audio decoder and sends it through the selected ASIO path.

| Input / device situation | Behavior |
| --- | --- |
| Mono on a stereo output | Duplicated to left and right |
| Stereo | Preserved as stereo |
| Multichannel with enough ASIO outputs | Channels are preserved in speaker-mask order |
| Multichannel with fewer outputs | Downmixed to stereo, or mono when only one output is available |
| Source and device sample rates differ | Resampled when device-rate playback is enabled |
| Compatible source/device rate and format | Conversion can be bypassed for a direct PCM path |

The first ASIO output channel is configurable, and unused device outputs are not opened.

### Decoder configuration

Configure LAV Audio or another upstream decoder to output **PCM** when using ARE. Compressed Dolby/DTS bitstream passthrough, DSD/DoP passthrough, and object-based audio metadata are outside the PCM/ASIO path.

## Sample-rate conversion

Two conversion engines are available:

- **r8brain** — the default high-quality converter.
- **SoX Sinc** — an alternative high-quality sinc converter.

Both operate internally in Float64. Resampling is only used when required by the selected device-rate configuration; otherwise the renderer can keep the original sample rate.

## Build from source

Install Visual Studio with the **Desktop development with C++** workload, a Windows SDK, CMake, and Python. All required DSP sources and ASIO interface headers are included in the repository.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build.ps1
```

Build a single architecture with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build.ps1 -Architecture x64
```

Use `x86` instead for a 32-bit build. Generated packages are written under `dist/`.

For the MPC-HC host patch and build steps, see [`integration/mpc-hc/README.md`](integration/mpc-hc/README.md).

## License

The original ARE Audio Renderer source is licensed under the [MIT License](LICENSE).

Windows release binaries are distributed under **GPL-3.0** because the included Steinberg ASIO SDK headers use their GPL option. The MPC-HC compatibility patch is GPL-3.0-or-later, and bundled third-party components retain their own licenses.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details.
