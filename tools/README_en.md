# Tools

[English](README_en.md) | [Русский](README_ru.md)

Standalone command-line utilities and small apps shipped with XER.

## Utilities

| Directory | Purpose |
|---|---|
| `EngineScanVST3/` | VST3 plugin scanner. Enumerates `.vst3` modules without loading them, then probes each module in an isolated `EngineScanVST3 --probe <module>` process. Async/future based, concurrency bounded by CPU cores. Emits JSON success/failure records |
| `XERVirtualCable/` | CLI for the virtual audio cable: named cross-process PCM pipes, WASAPI bridges, routing, built-in effects and VST3 stages per cable |
| `audioconvert/` | Audio format conversion |
| `audioapplyeffect/` | Apply an audio effect to a file |
| `audioplayer/` | CLI audio playback |
| `audioprobe/` | Audio file probing |
| `video_editor/` | Video editing utility |
| `preview/` | Preview utility |
| `XERUIRender/` | UI render harness |
| `XERVacUI/` | Virtual audio cable UI |
| `ServerAudio/` | Audio server tool |
| `ArchiveManager/` | Archive management |
| `ControllAccountsWindows/` | Windows account control |
| `HubCivitAIBrowser/` | CivitAI browser hub |
| `SdGenerate/` | Stable Diffusion generation tool |
| `SecurityFlashHello/` | Security flash (Hello) tool |
| `SecurityFlashService/` | Security flash service |
| `tz/` | Timezone utility |

## EngineScanVST3 usage

```bat
bin\EngineScanVST3.exe [root ...] [--out file.json] [--pretty]
                       [--jobs N] [--timeout MS] [--in-process]
bin\EngineScanVST3.exe --probe <module.vst3>
```

`--jobs N` bounds the number of concurrent probe processes (default:
CPU core count). Each probe loads exactly one module, so a crashing
plugin cannot take down the scan.
