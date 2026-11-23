# AI models staged for the XER creative suite

License-clean (MIT / Apache-2.0 / BSD only) models wired per
`docs/wiki/research/2026-11-22-ai-model-stack.md`. Deployed to
`<exe>/assets/models/` by the `video_editor_assets` build target; the
whisper ggml model lives in `<exe>/models/` (see below).

## rfdetr_nano.onnx — object detection (WIRED: FindObject)

- **Source:** https://huggingface.co/onnx-community/rfdetr_nano-ONNX
  (`onnx/model.onnx`, fp32, 108 MB), export of Roboflow RF-DETR-Nano
- **License:** Apache-2.0 (model card; RF-DETR Nano/Small/Medium/Large
  are Apache — Atto/Femto/Pico/XL/2XL are Roboflow PML-1.0, do NOT use)
- **Replaces:** the AGPL-risk Ultralytics YOLOv8n reference
- **Consumer:** `src/content/ai/find_object` via
  `tools/video_editor/ai_tags.cc` ("AI: Find Objects" clip tagging)
  and `tools/video_editor/ai/smart_reframe.cc` ("Smart Reframe →"
  auto-reframe — subject tracking + crop windows)
- **Contract (verified by protobuf inspection):**
  - input `pixel_values` f32 `[B,3,H,W]` RGB /255, letterboxed —
    **dynamic H/W**; fed at 384 (trained resolution; preprocessor_config
    `size=384`, `do_normalize=false`, rescale 1/255 — matches
    FindObject's letterbox+/255 pipeline, no ImageNet norm needed)
  - output `pred_boxes` `[1,300,4]` cxcywh **normalized 0..1**
  - output `logits` `[1,300,91]` **un-activated** — per-class sigmoid;
    logit index == COCO category id (1..90, gaps = 'unused')
  - NMS-free by design; FindObject's NMS tail is harmless on top
- **Labels:** `rfdetr_nano.names` (91-line COCO-id-indexed map,
  generated from the HF config.json id2label)

## birefnet_lite.onnx — background removal / matting (STAGED)

- **Source:** official GitHub release asset —
  https://github.com/ZhengPeng7/BiRefNet/releases/download/v1/
  `BiRefNet-general-bb_swin_v1_tiny-epoch_232.onnx` (224 MB, fp32)
- **License:** MIT (code + weights)
- **Consumer:** none yet — matting feature lands per research wave W1;
  reuse the FindObject runner pattern (resize → NCHW blob → forward)
- **Contract (verified):**
  - input `input_image` f32 `[1,3,1024,1024]` RGB, **ImageNet-normalized**
    (mean .485/.456/.406, std .229/.224/.225) — NOT plain /255
  - output `output_image` f32 `[1,1,1024,1024]` logits → sigmoid = matte,
    then resize to source
  - CPU cost is seconds-class at 1024² — downsample/tile for previews

## efficientsam_ti.onnx — promptable segmentation (STAGED)

- **Source:** opencv_zoo (guaranteed cv::dnn) —
  https://huggingface.co/opencv/opencv_zoo `models/
  image_segmentation_efficientsam/image_segmentation_efficientsam_ti_
  2025april.onnx` (48 MB, fp32)
- **License:** Apache-2.0 (per-model LICENSE in the zoo dir)
- **Consumer:** none yet — smart-select feature (wave W2)
- **Contract (verified):**
  - inputs: `batched_images` f32 `[1,3,1024,1024]` RGB /255 (resize, no
    letterbox per zoo demo),
    `batched_point_coords` f32 `[1,1,6,2]` (x,y in input px, zero-padded
    to 6 points),
    `batched_point_labels` f32 `[1,1,6]` (1 fg / 2 box / -1 pad; bg
    points handled by rejection, see zoo efficientSAM.py)
  - outputs: `output_masks` f32 `[1,1,3,1024,1024]` logits (>=0 = mask),
    `iou_predictions` f32 `[1,1,3]` — pick best-IoU mask, resize to
    source

## gtcrn_simple.onnx — voice isolation / noise reduction (WIRED: Voice Isolate)

- **Source:** https://github.com/k2-fsa/sherpa-onnx/releases/download/
  speech-enhancement-models/gtcrn_simple.onnx (0.5 MB, fp32) — the
  streaming export from upstream
  https://github.com/Xiaobin-Rong/gtcrn (`stream/onnx_models/`,
  DNS3-trained) with sherpa-onnx metadata
- **License:** MIT (upstream repo LICENSE — code + weights)
- **Consumer:** `tools/video_editor/voice_isolate.cc` via the
  `builtin:xer.af.voice_isolate` audio effect (Media Bin
  "Audio FX →" bounce workflow)
- **Contract (verified by protobuf inspection):**
  - streaming, 16 kHz mono; **one STFT frame per Run**
  - input `mix` f32 `[1,257,1,2]` — complex STFT frame, interleaved
    re/im; STFT: n_fft=512, hop=256, win=512, **sqrt** symmetric Hann,
    centered/reflect-padded (knf conventions)
  - inputs `conv_cache` `[2,1,16,16,33]`, `tra_cache` `[2,3,1,1,16]`,
    `inter_cache` `[2,1,33,16]` — zero-init, then previous frame's
    `*_out` verbatim
  - outputs `enh` `[1,257,1,2]` + `conv_cache_out` / `tra_cache_out` /
    `inter_cache_out`
  - ISTFT: same sqrt-Hann at synthesis, OLA normalized by sum(w²),
    strip center pad; voice_isolate.cc resamples src_rate↔16 kHz and
    blends `amount` with the dry signal
  - model metadata (readable in Netron/onnx): model_type=gtcrn,
    sample_rate=16000, n_fft=512, hop=256, win=512, window=hann_sqrt

## silero_vad.onnx — voice-activity detection (WIRED: silence cut)

- **Source:** https://huggingface.co/onnx-community/silero-vad
  (`onnx/model.onnx`, fp32, 2.24 MB), export of snakers4/silero-vad v5
- **License:** MIT (upstream repo LICENSE — code + weights)
- **Consumer:** `tools/video_editor/ai/silero_vad.cc` via
  `ai/silence_detect.cc` (`DetectSilences` method "auto"/"vad" —
  falls back to the RMS envelope when the model or a backend is
  missing)
- **Contract (verified by protobuf inspection):**
  - input `input` f32 `[B,N]` — 16 kHz mono PCM, **N = 512**
    (32 ms window; 256 on the 8 kHz branch)
  - input `state` f32 `[2,B,128]` — zero-init, then previous `stateN`
    verbatim (streaming carry, same pattern as gtcrn caches)
  - input `sr` **int64** scalar — 16000 (the graph is a top-level
    `If(Equal(sr,16000))` selecting the 16 kHz subgraph; ORT runs it,
    cv::dnn rejects the If — the VAD path degrades to RMS there)
  - outputs `output` f32 `[B,1]` speech prob + `stateN` f32 `[2,B,128]`
  - RTF ~1e-3 — a 10-min clip scans in well under a second

## face_detection_yunet.onnx — face detection (WIRED: face blur)

- **Source:** opencv_zoo —
  https://github.com/opencv/opencv_zoo `models/face_detection_yunet/
  face_detection_yunet_2023mar.onnx` (233 KB, fp32)
- **License:** MIT (LICENSE in the zoo model dir, © 2020 Shiqi Yu)
- **Consumer:** `tools/video_editor/ai/face_detect.cc` —
  `ScanClipFaces` reuses `ScanClipFrames` (ai/smart_reframe.h) to
  land a `ClipTrack` labeled "face" on `Clip::object_track`; the
  `builtin:xer.fx.face_blur` / `xer.fx.track_mosaic` effect
  pixelates it live. UI: ctx-menu "Auto-blur Faces"
- **Contract (verified by protobuf inspection):**
  - input `input` f32 `[1,3,640,640]` BGR NCHW, raw 0..255
    (**fixed 640²** — probes are stretched, boxes mapped back;
    dnn::blobFromImage semantics, no mean/std)
  - outputs per stride s ∈ {8,16,32}: `cls_s`/`obj_s` `[1,N,1]`
    (activated), `bbox_s` `[1,N,4]` anchor-relative,
    `kps_s` `[1,N,10]` (5 landmarks: re, le, nose, rm, lm)
  - decode mirrors cv::FaceDetectorYN: score = sqrt(cls·obj),
    center = (anchor+bbox)·s, size = exp(bbox)·s — implemented in
    face_detect.cc DecodeYunet + greedy IoU NMS

## ggml-base.en.bin — whisper transcription (WIRED, different dir)

- **Location:** `<exe>/models/ggml-base.en.bin` — the whisper consumer
  (`tools/video_editor/ai/whisper_transcribe.cc`) probes
  `$XER_WHISPER_MODEL` → `<exe>/models/` → `%USERPROFILE%\.cache\
  whisper\` → `$XDG_CACHE_HOME/whisper`. It does NOT look in
  `assets/models/`; staged at `out/build/<preset>/bin/models/` next to
  the exe. Move/copy to the user cache dir if the build tree is wiped.
- **Source:** https://huggingface.co/ggerganov/whisper.cpp
  `resolve/main/ggml-base.en.bin` (148 MB)
- **License:** MIT (whisper.cpp code + OpenAI-derived weights)
- **Contract:** 16 kHz mono float PCM → TranscriptSegment list;
  whisper_init_from_file consumes the .bin directly
  (magic `ggml` verified)

## Notes

- `inspect_onnx.py` — zero-dep protobuf-wire inspector used to verify
  each ONNX's declared inputs/outputs (run:
  `python inspect_onnx.py <file>.onnx`).
- Detection label files resolve as `<model_stem>.names` then
  `coco.names` beside the model (`FindObjectLabels`).
- Binary model files are gitignored; this README + the .names +
  inspect_onnx.py are the tracked artifacts.
