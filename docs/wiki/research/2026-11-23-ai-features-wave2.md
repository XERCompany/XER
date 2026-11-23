# AI features wave 2 — research (frame-interp, upscale, stems, faces, depth, VAD, beats, align)

Second wave of ship-candidate evaluation for the creative suite, scoped to
`tools/video_editor` consumers. Repo claims verified in the working tree
2026-11-23; ONNX tensor contracts verified by actually downloading the
weights and running `tools/video_editor/assets/models/inspect_onnx.py`
(the zero-dep protobuf inspector) — those rows say **verified** instead of
"card claims". License claims are cross-checked against card text / repo
LICENSE files / maintainer statements, not HF tags alone (several tags are
mislabels — see §3 and §9). Subagent research; parent filed the page.

## 0. Integration seams that already exist (verified 2026-11-23)

| Seam | Where | What it gives a new model |
|---|---|---|
| Shared ONNX session | `src/content/ai/onnx_session/onnx_session.h` | Dual-backend (ORT delay-load-probed → cv::dnn fallback), named-input/f32-tensor-agnostic `Session::Run`; `ProbeInputSquare` for fixed-square models. Every image→image model below drops into this |
| Model staging | `tools/video_editor/lab/ai_ops.cc:36` `FindAiModel` | `$XER_AI_MODELS_DIR` → `<exe>/assets/models` → `<exe>/models`; `assets/models/README.md` is the tracked manifest (source URL + license + contract per model) |
| Audio FX bounce | `builtin_fx.cc:1349` `xer.af.voice_isolate` → `voice_isolate.cc` | `XER_PLUGIN_AUDIO_FX` descriptor + Media Bin "Audio FX →" bounce; GTCRN pattern = resample→16k→per-frame Run→resample→dry-blend. Any audio-ML filter copies this file's shape |
| PCM front-end | `ai/audio_pcm.h` `ExtractMonoPcm` | Whole-file decode → mono float @ target rate (linear resample). VAD/beat/stems all start here (stems need a stereo variant) |
| Clip scan + track storage | `ai/smart_reframe.h:98` `ScanClipDetections`, `ai/object_track.h` | Decode→detect→`pick` per probe → `Clip::object_track` (single `ClipTrack`, `timeline_model.h:199`), smoothed by `TrackCenterAt`; consumed live by `xer.fx.track_mosaic` (pixelates the tracked box) and pin-to-title key baking |
| Clip rate / time-stretch | `timeline_model.h:179` `playback_rate`, `SetClipRate` | Source-time mapping `t = in_point + (t-start)*rate` — the natural home for frame interpolation |
| DOF effect | `compositor.cc:1740` `bokeh_dof`, `bokeh_blur` | Existing bokeh DOF is flat; a depth map makes it Z-aware |
| Silence detect | `ai/silence_detect.h` | **RMS-envelope only** — `threshold_db` + 50 ms windows + `min_silence` merge. A VAD swaps the criterion from "quiet" to "not speech" |
| Beat markers | `ai/beat_markers.h` | Already shipped — wraps `Audio::AnalysisAI::BeatTracker` (energy/onset DSP, no model) |
| Whisper words | `ai/whisper_transcribe.cc:258` | Token timestamps already harvested → `TranscriptSegment::words` via optional `whisper_full_get_token_tstamp_data` symbols |

**Backend reality (from `architecture/nn-backend-stack.md`, dated
2026-11-22):** cv::dnn `ENGINE_OPENCV` is the working backend (CPU).
onnxruntime.dll is delay-load-probed but the MSYS2 binary is stale →
cv::dnn serves today. **No GPU inference is compiled** (driver+nvenc
yes, CUDA toolkit no). "CUDA cost" below means *when ORT with the CUDA
EP lands via the prebuilt Microsoft dll* (documented route) — it is not
runnable in-tree today. fp16/int8 ONNX variants exist for most models but
the cv::dnn path is fp32-only.

## 1. RIFE — frame interpolation / optical-flow slow-mo — **ADOPT (M)**

| Field | Value |
|---|---|
| Model | Practical-RIFE 4.25 **lite** — `t9749/rife-4.25-lite-onnx` `rife_v425lite.onnx` (hf.co) |
| License | **MIT, verified in card text** — card quotes hzwer's Practical-RIFE README: "the content of these links is under the same MIT license as this project"; repo LICENSE = MIT © 2021 hzwer. IFRNet repo (ltkong218/IFRNet) is also MIT but has no comparable ONNX export trail |
| ONNX | Community export, opset 17, 22.7 MB fp32. Alternatives: `yuvraj108c/rife-onnx` (v4.7/4.8/4.9 ensemble variants, untagged license but same upstream), `SOR2171/RIFE-ONNX` (MIT, rife2_26h) |
| Contract (**verified**) | `img0`, `img1` f32 `[1,3,h,w]` RGB 0..1 + `timestep` `[1,1,1,1]` (0..1) → `frame` `[1,3,h,w]`. **h,w must be multiples of 64** (pad, crop back); dynamic H/W |
| Cost @1080p | CPU: seconds-class per interpolated frame (~40–80 GFLOP/pair; measure — lite is the cheapest variant). CUDA: ~20–60 fps (240 Hz-class interpolation feasible). Per 2× slow-mo of a 10 s clip = ~240/480 synthetic frames — needs progress+cancel+chunked output |
| Seam | **Not `xer.fx.*`** (temporal op: needs frame *pairs* + fractional t). Lands in the `playback_rate` path — when mapped source t falls between decoded frames (rate < 1 or non-integer), fetch bracketing pair → `timestep = frac`. Simpler first cut: "Smooth slow-mo" bounce op re-encoding the clip (like voice_isolate's bounce), plus a `SetClipRate`-aware export pass. Frame-pair cache is the real work (decode-side pair retention, keyed on GOP) |
| Novelty | **5/5** — nothing temporal exists; flagship NLE feature (Resolve "Optical Flow", CapCut "Smooth") |

**Backend flag:** RIFE is `grid_sample`-heavy — ONNX `GridSample` under
cv::dnn `ENGINE_OPENCV` is **unverified**; if it fails there this model
is ORT-only and gated on the ORT fix. Verify before scheduling.

## 2. Real-ESRGAN / realesr-general-x4v3 — media upscale — **ADOPT (S–M)**

| Field | Value |
|---|---|
| Model | `Samo629/real-esrgan-onnx` — `realesr-general-x4v3-dynfix.onnx` (hf.co). Upstream `xinntao/Real-ESRGAN` = **BSD-3-Clause (repo API verified)**; `qualcomm/Real-ESRGAN-General-x4v3` also BSD-3 (release-assets hosting) |
| ONNX | Community export, **4.9 MB**, dynamic H×W ("dynfix" variant fixes the axes). x4v3 = SRVGG compact — ~14× lighter than x4plus RRDB (67 MB fp32) |
| Contract (**verified**) | `input` `[B,3,H,W]` f32 RGB 0..1 → `output` `[B,3,outH,outW]` = 4×. Trivially tileable |
| Cost @1080p | CPU fp32: ~3–15 s/frame (bounce-class, never realtime; measure). CUDA: ~0.1–0.3 s/frame. SRVGG = convs+pixelshuffle → **very likely loads on cv::dnn today** (still verify) |
| Seam | Media Bin "Enhance/Upscale" **bounce** (mirror of the `xer.af.*` bounce: decode frames → Onnx::Session tiled → re-encode new media) or an `export_preset` upscale pass. A live `xer.fx.ai_upscale` is possible but dishonest — fx run inside the canvas pixel budget, and CPU cost forces offline anyway |
| Novelty | **4/5** — no SR shipped; prior doc called Real-ESRGAN the "most demo-able AI feature" |

## 3. Stem separation — Demucs / MDX23 / BS-RoFormer — **DEFER (license-blocked; BYO plumbing only)**

The whole category is walled off — every SOTA separator is trained on
musdb18 (CC-BY-NC-SA dataset) or released research-only:

| Model | License reality | Evidence |
|---|---|---|
| **htdemucs / htdemucs_ft** | Code MIT; **weights research-only** | Maintainer adefossez, verbatim (issue #327, 2022-05-23): *"The model weights are not covered by the MIT license, and are provided only for scientific purposes"*; #508 (2023-08-02): *"Weights are provided for research purpose!"* — `Intel/demucs-openvino`'s MIT tag is a **mislabel** |
| **MDX23C** | **CC-BY-NC-SA-4.0** | `Intel/drumsep_mdx23c_jarredou_openvino` card honestly tagged NC; `Essid/Essid-MDX23C` ships a .ckpt with **no license** |
| **BS-RoFormer (viperx ep317)** | **Ambiguous → treat as NC** | `xycld/BS-RoFormer-ONNX` (MIT tag, 615 MB fp32 + 158 MB uint8, 2-stem vocals/instr) and `SYH99999/bs_roformer_4stems_ft` (apache tag) both trace to a viperx/UVR checkpoint trained on `musdb18-hq`; upstream never granted weights terms — relabels unbacked |
| Open-Unmix umxl/umx | CC-BY-NC-SA-4.0 | sigsep README line 86, explicit |

**Verdict:** do **not** ship weights — `xer.af.separate_stems` behind a
BYO-model contract is the only legal near-term path (plumbing = M: stereo
ExtractPcm variant + waveform/STFT shell — the GTCRN shell already proves
we can host STFT-in/out models; BS-RoFormer is *no-STFT* so host does
STFT+ISTFT). Contract for design: htdemucs takes `[1,2,T]`@44.1 kHz →
`[1,4,2,T]` stems. Novelty 5/5, license 0/5.

## 4. Forced alignment (word timestamps) — **DEFER (existing path adequate)**

Current state: `whisper_transcribe.cc` already turns
`whisper_full_get_token_tstamp_data` into `TranscriptWordSpan`s (token-ts
fallback leaves `words` empty → callers degrade gracefully). Candidates
checked: wav2vec2-960h-style CTC aligner (`facebook/wav2vec2-base-960h`,
**Apache-2.0** — clean if ever needed), whisperX pipeline (BSD-2 code),
aeneas (**AGPL-3.0 — skip**). Whisper.cpp's own token-timestamp quality
also improves free with a large-v3-turbo upgrade.

**Verdict:** defer — the gain (±100–300 ms → ~20 ms word edges) only pays
off for karaoke-grade captions; effort L for a quality delta. Revisit when
word-accurate caption styling ships.

## 5. Face detection — YuNet — **ADOPT (S–M)**

| Field | Value |
|---|---|
| Model | `opencv/face_detection_yunet` `face_detection_yunet_2023mar.onnx` — **233 KB fp32 / 100 KB int8**; dynamic-shape variant `meinvirgos/face_detection_yunet_dynamic` (Apache-2.0) |
| License | **MIT — LICENSE file verified in repo** (© 2020 Shiqi Yu). In opencv_zoo ⇒ cv::dnn-guaranteed + `cv::FaceDetectorYN` built-in decode |
| Alternatives rejected | **SCRFD** — better small-face accuracy but provenance unresolved: `immich-app/scrfd_34g_gnkps` tags MIT, yet insightface's README (L40–44) declares *all* released models "non-commercial research purposes only" — the immich tag is not backed by the rights-holder. **YOLOv8/v11-face** — Ultralytics AGPL umbrella (already blocklisted in wave-1 doc §7) |
| Contract | YuNet heads `loc`/`conf`/`iou` (FaceDetectorYN decode); 5-point landmarks included → feeds future faces-tab keypoints, not just boxes |
| Cost | sub-ms–few ms per 640×480 probe CPU — effectively free |
| Seam | `ScanClipDetections` — but it hardwires `FindObj::Detector` (YOLO/DETR decode). Delta = a YuNet decode adapter (~150 lines or the cv::face wrapper), then `ScanClipTrack("face")` + `xer.fx.track_mosaic` = **auto face-blur almost free**. Multi-face: `Clip::object_track` is a *single* `ClipTrack` — extend to a track list (the model struct already carries `label`/`classes` for a pick UI). Bonus: "prefer face" pick policy in `PickPrimarySubject` for reframe |
| Novelty | **4/5** — auto-blur is the flagship privacy feature; infra mostly exists |

## 6. Beat / tempo — Beat This! ONNX — **DEFER (shipped feature, ML upgrade)**

`beat_markers.cc` already ships energy/onset BeatTracker → markers. The ML
upgrade exists and is clean: `tuneperfect/beat-this-onnx` (MIT —
`beat_this.onnx` 82 MB **+ `logmel.onnx` which folds feature extraction
into a second graph**, halving host work) and `musetric/beat-this-onnx`
(MIT; ships `mel-filterbank.bin`). Upstream `CPJKU/beat_this` = **MIT
(GitHub API verified)**.

- Contract (**verified**): `spect` `[1,frames,128]` → `beat`,`downbeat`
  `[1,frames]` activations. Host owns: 22 050 Hz mono **arithmetic-mean**
  downmix (ffmpeg `-ac 1` is wrong — energy-preserving rematrix), STFT
  n_fft 1024 hop 441 periodic Hann, mel-128 via the shipped filterbank,
  `log1p(1000·x)`; 1500-frame (30 s) windows, borderSize 6 overlap;
  activation→times pick (DBN or peak-pick) host-side.
- **Verdict:** defer — adds downbeats + robustness on loose material over
  shipped DSP, S–M effort, but it's a quality delta not a feature. Bump if
  beat-sync complaints land. ExtractMonoPcm + the GTCRN STFT shell make the
  host side cheap when we do it.

## 7. DepthAnything-v2-small — depth maps → Z-aware fx — **ADOPT (M)**

Wave-1 doc §6 already picked "Small only"; this wave verified the export:

| Field | Value |
|---|---|
| Model | `onnx-community/depth-anything-v2-small` — **Apache-2.0 card-verified**; Small only (Base/Large/Giant = CC-BY-NC). 99 MB fp32 / 49.6 MB fp16 / 27 MB int8 / 19 MB q4f16 |
| Contract (**verified**, fp16 file) | `pixel_values` `[B,3,H,W]` (preprocessor: 518², ImageNet norm, `ensure_multiple_of:14`) → `predicted_depth` `[B, 14⌊H/14⌋, 14⌊W/14⌋]` relative inverse depth |
| Cost | CPU ~0.5–2 s @518² — analysis-scan cadence, not per-frame compositor; CUDA ~10–30 ms |
| Seam | `bokeh_dof`/`bokeh_blur` (`compositor.cc:1740`) gets a depth-weighted input → true auto-DOF; probe-scan per-clip depth cache (same cadence as `ScanClipDetections`, 8-bit quantized) unlocks relayer / 2.5-D parallax zoom later |
| Novelty | **4/5** — flat DOF → Z-aware; matches CapCut/Resolve depth-blur |

Standing caveat (wave-1 §6): Small's *weights* are Apache-2.0 but
training-data provenance is questioned in repo issues (#81/#320) — Apache
claim stands, flag remains.

## 8. Silero VAD — speech-aware silence cut — **ADOPT (S)**

| Field | Value |
|---|---|
| Model | `onnx-community/silero-vad` — **MIT LICENSE file verified**; 2.24 MB fp32 / **0.64 MB int8** |
| Contract (**verified**) | `input` f32 `[B,N]` @16 kHz (N=512; 256 @8 kHz) + `state` `[2,B,128]` + `sr` scalar → `output` `[B,1]` speech prob + `stateN` `[2,B,128]`. Stateful streaming — same cache-carry pattern as GTCRN in `voice_isolate.cc` |
| Cost | RTF ~1e-3 — a 10-min clip scans in well under a second; trivially realtime |
| Seam | `silence_detect.cc` is RMS-only — VAD turns `DetectSilences` from "below −40 dB" into "not speech" (holds quiet speech, kills true silence). Also feeds: auto-cut, whisper segment refinement, "speech regions" for auto-ducking, envelope fallback kept as fast-path |
| Novelty | **2/5** (upgrade, not feature) but the cheapest quality win on this page |

## 9. License blocklist (new entries this wave)

- **Demucs weights — research-only** (maintainer-verbatim above). Code MIT.
- **MDX23C — CC-BY-NC-SA-4.0.** `Essid-MDX23C` unlicensed entirely.
- **BS-RoFormer / MDX / UVR community checkpoints — unbacked relabels**;
  musdb18 (CC-BY-NC-SA) trained → treat as non-commercial.
- **SCRFD — provenance conflict** (immich MIT tag vs InsightFace's
  non-commercial umbrella statement covering all released models).
- **aeneas — AGPL-3.0.** Ultralytics AGPL stays blocklisted (wave-1).
- HF `license:` tags proven unreliable as sole evidence in 4 cases this
  wave (Intel demucs MIT, immich SCRFD MIT, xycld bs-roformer MIT,
  Audante mvsep MIT) — always trace to upstream.

## 10. Adoption order — top 3 + the 3-wave plan

**Ranked top 3 (value ÷ effort, license-clean, no new deps):**
1. **Silero VAD** (S) — smallest model, biggest honesty upgrade to
   silence-cut; copies the GTCRN shell verbatim.
2. **YuNet face detect** (S–M) — auto face-blur = flagship feature built
   on shipped scan/track/mosaic infra; only a decode adapter + multi-track
   storage are new.
3. **realesr-general-x4v3** (S–M) — 4.9 MB, cv::dnn-plausible, the
   "AI upscale" demo; establishes the tiled image→image runner that RIFE
   and depth reuse.

**Waves:**
- **Wave 1 (quick wins, cv::dnn-safe):** Silero VAD → YuNet faces
  (decode adapter → multi-ClipTrack → mosaic-per-face) → optional
  face-preferred reframe policy.
- **Wave 2 (image pipeline):** tiled image→image runner →
  realesr-general-x4v3 bounce op → **RIFE** slow-mo (adds the frame-pair
  cache + `playback_rate` fractional-time hook; **Gate:** GridSample on
  cv::dnn verified, else ORT-fix dependency).
- **Wave 3 (depth + BYO audio-ML):** DepthAnything-v2-S probe-scan depth
  cache → depth-weighted `bokeh_dof` (+parallax later); stem-separation
  plumbing `xer.af.separate_stems` behind a BYO-ONNX contract; Beat This!
  if beat quality complaints land.

## 11. Open questions for implementation

1. RIFE under cv::dnn: does `GridSample` load on `ENGINE_OPENCV`? (5-min
   smoke test with the staged ONNX before scheduling wave 2.)
2. x4v3 numeric check on cv::dnn vs reference (SRVGG is plain convs —
   expected pass).
3. Multi-face storage: extend `Clip` with `std::vector<ClipTrack>` vs a
   `faces` sub-struct — serialization migration needed either way.
4. Whether a "Smooth slow-mo" bounce op ships before the live
   `playback_rate` hook (bounce is less risky: offline, no compositor
   contract change).
5. Stereo `ExtractPcm` variant only becomes needed if/when stem plumbing
   lands (wave 3) — don't build it early.
