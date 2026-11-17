# Builder-check of video-editor design — 2026-11-15

Independent subagent reviewed
`docs/superpowers/specs/2026-11-15-video-editor-design.md` against the
repo. ~35 min run. Findings + resolutions below; full detail in session
transcript (not persisted elsewhere).

## Questions raised → resolutions

1. **Transition geometry** (highest impact): how does a centered
   transition get frames past clip bounds? → RESOLVED: standard NLE
   handle model — window extends duration/2 each side, reads source
   media beyond in/out (handles), clamps to file bounds / freezes.
   Same-track overlap stays illegal; transitions overlap the CUT, not
   the clips.
2. **Audio-only files**: `VideoDecoder::Open` fails without video
   stream; `DecodeAudioFrames` has no seek; `AudioSourceLoader` loads
   whole file. → RESOLVED: support audio-only in v1 by relaxing
   Bridge `VideoDecoder::Open` to accept audio-only containers (audio
   tap then works; `NextFrame` returns false). Fallback:
   `AudioSourceLoader` for short files.
3. **Preview fidelity**: raw clip vs composited? → RESOLVED: raw clip
   default + "Full composite" toggle (Premiere playback-resolution
   analogue). Documented as accepted simplification for default mode.
4. **`Clip::muted` semantics**: → RESOLVED: clip.muted = audio only;
   video tracks get `hidden`; audio tracks get `muted`.
5. **Decoder pooling**: doc contradicted itself (per-path vs per-clip).
   → RESOLVED: per-clip instances (per-path thrashes on shared sources).
6. **Same-track overlap**: → RESOLVED: illegal; enforced by edit
   commands (needed for transition adjacency anyway).

## Factual corrections applied to the design doc

- `kSeekMaxDiscardFrames` (~120) is in Content `VideoSession`
  (video_session.cc:15,282-296), NOT the bridge decoder; bridge `Seek`
  = keyframe+flush only.
- Audio tap starves unless `NextFrame()` pumps; queue drops oldest at
  2048 packets → export audio must pump video decode per clip.
- `MixSystem::Blend` is an instance method.
- `ImageToJpeg` decodes only first frame → poster needs decode→encode
  path (VideoDecoder → FrameExporter::ToFile PNG, or similar).
- Several `VideoEffects` are Skia-only, silent no-op without Skia;
  `XER_Video` won't link at all without the Skia port.
- CMake sketch needs a JSON library (jsoncpp/nlohmann both in repo).
- Off-Windows `--info` can't probe media (bridge is stubbed).

## Verified true (no action)

Orphan status of XER_Video; no A/V mux; EncodeVideoFrameStream takes
BGRA; CompositingEngine::Layer lacks transform; mocks confirmed;
NodeGraph::History pattern; TempPathForTarget/CommitTempFile; SQLite
API; widget kit; tools/preview pattern; all link targets exist.
