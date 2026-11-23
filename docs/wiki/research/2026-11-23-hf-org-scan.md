# HF org scan — targeted sweep for creative-suite gaps (tracking, TTS, music, VLM tagging, video-gen)

Org-level sweep of Hugging Face for ship-candidates that close the gap vs a
Premiere+AI feature set: mask/object **tracking** (SAM2/SAM3), **TTS
voiceover**, **music generation**, **VLM auto-tagging**, **video generation**
(LTX). Follows `2026-11-23-ai-features-wave2.md` (waves 1–3 adopted
VAD+YuNet, planned x4v3+RIFE+depth). Method identical: HF `api/models`
listings + `raw/main` card/LICENSE text fetched 2026-11-23, licenses quoted
from card text / LICENSE files — **not** HF tags (4 mislabels found last
wave). Subagent research; parent filed the page.

Integration seams referenced below are the ones verified in the wave-2 doc §0:
shared `Onnx::Session` (ORT probe → cv::dnn fallback), `FindAiModel` staging,
`ScanClipFrames`/`ScanClipDetections` probe-scan → `Clip::object_track`,
`xer.fx.track_mosaic` track consumer, `xer.af.*` bounce ops,
`ExtractMonoPcm` audio front-end, `playback_rate` time-map. **Backend
reality unchanged:** cv::dnn CPU is the working backend; ORT/CUDA EP is the
documented-but-not-yet-landed GPU path — every "GPU" cost below is gated on it.

## 0. Verdict summary

| # | Model | Org | License (verified) | Seam | Effort | Verdict |
|---|-------|-----|--------------------|------|--------|---------|
| 1 | **SAM2.1-hiera-tiny** (ONNX) | facebook → onnx-community | **Apache-2.0**, ungated | object_track → `xer.fx.track_mask` | M | **ADOPT** |
| 2 | **Kokoro-82M v1.0** (ONNX) | onnx-community | **Apache-2.0** (card + upstream hexgrad) | new TTS→clip op | M | **ADOPT** |
| 3 | **Florence-2-base-ft** (ONNX) | onnx-community (MS upstream) | **MIT** | probe-scan → clip tags | M | **ADOPT** |
| 4 | **MODNet** portrait matting | Xenova | **Apache-2.0** | `xer.fx.ai_matte` | S | **ADOPT** |
| 5 | **Depth-Anything-V3** (small→large) | onnx-community | **Apache-2.0** (card) — v3 relicensed! | wave-3 depth seam | S–M | **ADOPT** (upgrade of adopted v2) |
| 6 | mediapipe selfie segmentation | onnx-community | Apache-2.0 | same matte seam | S | Adopt (cheap alt to #4) |
| 7 | sam3-tracker / facebook/sam3 | Meta | **SAM License** (permissive-ish, gated) | same as #1 | M | **DEFER** (license class + gated) |
| 8 | Chatterbox / -multilingual (ONNX) | onnx-community (Resemble) | **MIT** (card) | TTS w/ voice-clone | M–L | **DEFER** (behind Kokoro) |
| 9 | suno/bark | Suno | **MIT** | TTS/SFX | L | **DEFER** (no clean ONNX path) |
| 10 | CLIP ViT-B/32 (ONNX) | Xenova (openai upstream) | MIT | zero-shot tag/semantic search | S–M | Defer-lite (fold into #3 eval) |
| 11 | SigLIP2 (google) | google | Apache-2.0 | same | S–M | Defer-lite |
| 12 | moonshine-base / parakeet-tdt-0.6b (ONNX) | onnx-community | MIT / CC-BY-4.0 | ASR | S–M | **SKIP** (whisper.cpp shipped) |
| 13 | pyannote-segmentation-3.0 (ONNX) | onnx-community | MIT tag (6 MB) | speaker-turn probs → captions | M | Defer (diarization needs 2nd model) |
| 14 | bge-m3 / rerankers | BAAI | MIT/Apache | media-library semantic search | M | Defer (P4 feature) |
| 15 | LTX-Video 0.9.x (2B/13B) | Lightricks | **LTXV Open Weights 0.X** — free < $10M rev | video-gen op | L + GPU gate | **DEFER** (legal review + ORT-CUDA) |
| 16 | LTX-2.x | Lightricks | LTX-2.x Community ($10M cap) | same | L | Defer |
| 17 | MiniMax-Music3 | MiniMaxAI | MiniMax Community (MIT-like + branding + $20M trigger) | music-gen | L, no ONNX | **SKIP** (runtime-infeasible) |
| 18 | MiniMax-H3-Image (T2I) | MiniMaxAI | H3-Image Community ($1M cap + branding + territory) | T2I | L | Skip |
| 19 | stable-video-diffusion | stabilityai | SVD Community (revenue cap) | video-gen | L | Skip |
| 20 | MusicGen (all sizes) | facebook / Xenova | **CC-BY-NC-4.0** | music-gen | — | **SKIP** (NC, re-confirmed) |
| 21 | sapiens-seg | onnx-community / Meta | upstream = **CC-BY-NC-4.0** | seg | — | Skip |
| 22 | KRAFTON Raon TTS/Speech | KRAFTON | CC-BY-NC-4.0 | TTS | — | Skip |
| 23 | yolo26 / yolov10 ONNX | onnx-community | AGPL-3.0 | detect | — | Skip (blocklisted class) |
| 24 | huihui-ai / unsloth / RadixArk | — | LLM quants (Apache/MIT upstreams) | LLM | — | Skip (no media value) |
| 25 | metric3d-vit (CC0) | onnx-community | **CC0-1.0** | metric depth | M | Defer (nice alt to DA) |

**Top-5 for the next implementation wave:** SAM2.1 tracking → Kokoro TTS →
Florence-2 tagging → MODNet matting → Depth-Anything-V3 upgrade.
LTX-Video stays the strategic flagship gap — see §10.

## 1. SAM2.1-hiera-tiny — prompt→mask, mask tracking — **ADOPT (M)**

| Field | Value |
|---|---|
| Org / repo | `onnx-community/sam2.1-hiera-tiny-ONNX` (hf.co); upstream `facebook/sam2.1-hiera-tiny` |
| License | **Apache-2.0 — verified via HF API** on the upstream repo (`license:apache-2.0`, `gated: none`). SAM2 line is clean Apache; only SAM3 moved to the gated SAM License (§2) |
| Size / ONNX | Two graphs already exported: `vision_encoder.onnx` 134 MB + `prompt_encoder_mask_decoder.onnx` 21 MB; fp16/int8/q4/bnb4 variants shipped. Tiny only in this repo; small/base+/large ONNX repos also exist under onnx-community |
| Contract sketch | Encoder: `[1,3,1024,1024]` image → `image_embed` (+ high-res feats `feats_0/feats_1`); Decoder: embed + `point_coords [1,N,2]` + `point_labels [1,N]` (or box prompt) → `masks [1,3,H,W]` + `iou_scores [1,3]` — pick best of 3 |
| Video tracking caveat | The export is **image-mode** — no SAM2 memory/streaming graphs. Practical NLE tracking = per-frame re-prompt: run mask on frame *t*, derive its bbox/center-point, feed as prompt on *t+1* (drift-compensated by re-picking best IoU head); or prompt once and track the mask's centroid via the existing track smooth. A full SAM2 video-memory export doesn't exist in onnx-community — flag as follow-up |
| Cost | Encoder ~150 ms/frame @1024 CPU (est.; hiera-tiny) — probe-scan cadence; decoder ~ms |
| Seam | `ScanClipFrames` + `Clip::object_track` (already detector-agnostic post-YuNet). New consumer `xer.fx.track_mask` (per-frame alpha mask → rotoscope/isolate/blur-behind) or reuse `track_mosaic` with mask instead of box. The "click object → it stays masked" UX = Magic-Mask-class feature |
| Verdict | **ADOPT.** Apache-2.0, ungated, ONNX already built, fills the tracking gap at the exact seam wave-1 built. Novelty 5/5 |

## 2. SAM3 (tracker + sam-audio) — **DEFER (license class + gating)**

- `onnx-community/sam3-tracker-ONNX` ships the same two-graph ONNX layout as
  SAM2.1 (vision_encoder 134 MB-class + prompt_decoder), ungated on HF.
  Upstream `facebook/sam3` / `facebook/sam3.1` are **manual-gated** and
  licensed under the **SAM License** (verified from
  github.com/facebookresearch/sam3 LICENSE, "Last Updated: November 19,
  2025"): *"You are granted a non-exclusive, worldwide, non-transferable and
  royalty-free limited license ... to use, reproduce, distribute, copy,
  create derivative works of, and make modifications"* — **but**
  redistribution is permitted *"only ... under the terms of this Agreement"*,
  plus no-reverse-engineer, trade-controls, and military-use clauses.
  Royalty-free commercial use is allowed; it is a permissive-**ish** custom
  license, not MIT/Apache, and the gated upstream means provenance relies on
  the mirror. Same integration as §1, better masks (concept prompts).
- `facebook/sam-audio-large` — `license_name: sam-license`, gated. Text/span-
  prompted audio source separation — would be a **musdb-free stems answer**
  (the whole point of wave-2 §3 being blocked!). Gated + SAM License + no
  ONNX export yet → **DEFER but watch**; if a community ONNX lands, this is
  the legal path to `xer.af.separate_stems` that Demucs can never be.

## 3. Kokoro-82M v1.0 — TTS voiceover — **ADOPT (M)**

| Field | Value |
|---|---|
| Org / repo | `onnx-community/Kokoro-82M-v1.0-ONNX`; upstream `hexgrad/Kokoro-82M` |
| License | **Apache-2.0 — card-verified** (`license: apache-2.0` in README front-matter); upstream hexgrad repo also Apache-2.0. Voice packs (.bin) ship in-repo under same terms |
| Size / ONNX | `model.onnx` 325 MB fp32; **q8f16 86 MB** / q4 / quantized variants. `tokenizer.json` + ~50 voice `.bin` embeddings in-repo (en-us/en-gb/es/fr/ja/zh voices) |
| Contract sketch | `input_ids [1,T]` (tokenized phoneme ids) + `style [1,256]` (voice .bin, indexed by token count) + `speed` → `waveform [1,N]` @ **24 kHz** |
| Catch | Input wants **phoneme tokens** — Kokoro upstream uses espeak-ng/misaki G2P. transformers.js does this in JS; we need either (a) a small G2P table/rules port for en, (b) espeak-ng compiled in (C API exists, ~2 MB), or (c) ship English-phoneme-lite lookup. This is where the M comes from — the graph itself is trivial |
| Cost | ~RTF 0.5–2 CPU per sentence — minutes of VO per minute of text, fine for bounce |
| Seam | New op, not `xer.af.*` (it's a **generator**, not a filter): Media Bin / inspector "Generate Voiceover" → TTS → new audio clip asset (same shape as whisper→transcript producing an asset). Optionally `xer.af.tts` later for per-clip dub |
| Verdict | **ADOPT.** The Premiere gap area "voiceover/dub" with a clean license and a one-graph ONNX. Novelty 5/5 |

## 4. Florence-2-base-ft — VLM caption/tag/detect/ground — **ADOPT (M)**

| Field | Value |
|---|---|
| Org / repo | `onnx-community/Florence-2-base-ft` (+ `Florence-2-base`, `-large`, `-large-ft`); upstream Microsoft Florence-2 |
| License | **MIT — card-verified** (`license: mit` front-matter; upstream microsoft/Florence-2 MIT) |
| Size / ONNX | base-ft ≈ 1.1 GB fp32 across `vision_encoder` 366 MB + `encoder_model` 173 MB + `decoder_model_merged` 388 MB + `embed_tokens` 158 MB; **q4/int8/bnb4 variants** cut this ~4×. Encoder-decoder transformer — needs a host decode loop (greedy/beam ≤ ~20 tokens), same shape as the whisper.cpp host loop conceptually |
| Contract sketch | `pixel_values [1,3,768,768]` + `input_ids` for task token (`<CAPTION>`, `<DETAILED_CAPTION>`, `<OD>` object-detection, `<OCR>`, `<CAPTION_TO_PHRASE_GROUNDING>`, `<MORE_DETAILED_CAPTION>`) → generated text/locations |
| Cost | ~0.5–2 s/frame CPU — probe-scan cadence only |
| Seam | `ScanClipFrames` probe → caption + `<OD>` boxes per probe → **new `Clip::tags`/`Clip::caption` metadata** + Media Bin search index. Also cheap shot-listing/storyboard text. `<OD>` doubles as a second detector for `object_track` pick |
| Verdict | **ADOPT.** One model = auto-tagging + captioning + detection + OCR — the highest feature-density-per-byte in this scan. MIT. Novelty 5/5 for the "AI-organized media bin" gap |

## 5. MODNet / selfie-segmentation — portrait matting — **ADOPT (S)**

| Field | Value |
|---|---|
| Repos | `Xenova/modnet` — **Apache-2.0 card-verified**, `onnx/model.onnx` **26 MB** single graph; `onnx-community/mediapipe_selfie_segmentation` — **Apache-2.0**, **0.5 MB** (faster, rougher edges) |
| Contract | MODNet `[1,3,512,512]` RGB normalized → `output [1,1,512,512]` alpha matte (trimap-free portrait matting). Selfie-seg `[1,256,256,3]`→ mask |
| Seam | `xer.fx.ai_matte` compositor effect (alpha output → premultiply/luma-key path) + Media Bin "Remove background" bounce (x4v3 bounce shape). Chroma-key-without-green-screen = the other flagship CapCut/Resolve feature |
| Verdict | **ADOPT (S)** — single image→image graph on the existing `Onnx::Session`, trivially cv::dnn-safe (plain convs). Combine with §1: SAM-refined matte on frame 0 → MODNet/selfie-seg per frame |

## 6. Depth-Anything-V3 — depth upgrade — **ADOPT (S–M, supersedes v2 pick)**

- `onnx-community/depth-anything-v3-small` — `license: apache-2.0`
  **in card front-matter**; `-large` also tagged apache-2.0. **v3 was
  relicensed vs v2** (v2 base/large = CC-BY-NC, only small was Apache —
  the standing wave-1 caveat). Single `model.onnx` + `onnx_data`.
- Same contract family as the adopted v2-small (image → relative depth).
  v3-large Apache-2.0 is a straight quality upgrade for the wave-3
  depth-cache → `bokeh_dof` plan; verify metric/relative output semantics
  against v3 card before swapping.
- Adjacent: `onnx-community/metric3d-vit-large` — **CC0-1.0** — true
  *metric* depth if 2.5-D parallax ever wants real distances.

## 7. TTS alternates — Chatterbox, Bark, Kitten, Supertonic

| Model | License (verified) | Size/ONNX | Note | Verdict |
|---|---|---|---|---|
| `onnx-community/chatterbox-ONNX` + `-multilingual-ONNX` | **MIT** (card: "Licensed under MIT", Resemble AI upstream) | 4-graph pipeline: speech_encoder 592 MB + embed 62 MB + language_model 2.1 GB (q4 avail) + conditional_decoder 540 MB fp32 | **Voice-clone TTS** (reference-audio conditioned) — the ElevenLabs-clone feature. Multi-graph host loop = M–L | **DEFER** — adopt after Kokoro if voice-clone demanded |
| `suno/bark`, `bark-small` | **MIT card-verified** (only 2 repos in suno org) | PyTorch; no maintained ONNX export — community exports are partial/heavy | TTS + non-speech SFX/ambience + music-ish audio — the most flexible audio-gen license we have | **DEFER** — blocked on ONNX export quality; revisit for SFX/ambience generation later |
| `onnx-community/kitten-tts-nano-0.1-ONNX`, `KittenTTS-Mini` | Apache-2.0 | nano/mini — tiny | Lightweight Kokoro alternative if 325 MB is too heavy to ship | Defer-lite |
| `onnx-community/Supertonic-TTS(-2)-ONNX` | openrail tag | small | OpenRAIL (use-restricted but commercial-OK) | Defer |
| KRAFTON `Raon-OpenTTS-*`, `Raon-Speech-*` | **CC-BY-NC-4.0** | — | Whole KRAFTON speech line is NC | **SKIP** |

## 8. ASR / audio-analysis alternates — mostly SKIP (whisper shipped)

- `onnx-community/whisper-large-v3-turbo` ONNX — upstream `openai/whisper-*`
  = MIT/Apache tags; we already run whisper.cpp (ggml) — **skip**.
- `onnx-community/moonshine-(tiny|base)-ONNX` — **MIT** (Useful Sensors
  upstream verified via `base_model`) — 250 MB enc/dec, faster edge ASR —
  skip (whisper covers).
- `onnx-community/parakeet-tdt-0.6b-v2-ONNX` — upstream
  `nvidia/parakeet-tdt-0.6b-v2` = **CC-BY-4.0** — SOTA-ish English ASR —
  skip now, best fallback if whisper.cpp ever becomes a problem.
- `onnx-community/pyannote-segmentation-3.0` — MIT tag, **6 MB** — speech-
  turn segmentation (powers diarization). Alone it gives "is speech / who's
  active" probabilities; true "Speaker 1/2" needs the embedding+clustering
  stage. **Defer** — cheap to add later for multi-speaker captions.
- `facebook/wav2vec2-base-960h` — Apache-2.0 — already noted in wave-2 §4 as
  the clean CTC-aligner fallback.

## 9. Music generation — the gap stays open — **all SKIP/DEFER**

- `MiniMaxAI/MiniMax-Music3` — full-song T2M (8B LLM + 0.6B + Flow-VAE,
  32 kHz stereo, lyrics+style conditioned). LICENSE = "MiniMax-Music3
  COMMUNITY LICENSE": MIT-style grant *plus* — commercial products must
  *"prominently display 'MiniMax-Music3'"*, written auth required past
  **$20M/yr** revenue, AUP exhibit. Legal-wise it's small-team-usable, but
  it runs on sglang-omni with ~17 GB of weights and **no ONNX export** —
  **SKIP as in-tree runtime** (GPU-gated and pipeline-heavy). Revisit if a
  distilled/ONNX variant appears.
- `facebook/musicgen-*`, `Xenova/musicgen-small` — **CC-BY-NC-4.0**,
  re-confirmed. Nothing changed since wave-1.
- `stabilityai/stable-audio-*` — "other" = Stability Community License
  (revenue-capped). No maintained ONNX. Skip.
- **Verdict:** no permissive + ONNX-runnable music-gen exists today. Bark
  (§7, MIT) is the closest licit path to generated *ambience/SFX*, not
  scored music. Music stays a documented gap.

## 10. Video generation — LTX family — **DEFER (strategic flagship)**

- `Lightricks/LTX-Video` 0.9.x (2B + 13B dev/distilled) — **LTXV Open
  Weights License 0.X** (LICENSE text fetched, dated 2025-04-15):
  royalty-free use/reproduce/distribute/modify **for entities under
  $10,000,000 annual revenue**; ≥ $10M requires paid commercial license;
  OpenRAIL-style Attachment-A use restrictions + pass-along clause (3.1).
  Output is unencumbered ("Licensor claims no rights in the Output").
  Verdict: **usable today as a small entity, not MIT-class** — needs a
  legal call on the $10M trigger + redistribution pass-along before
  bundling.
- `Lightricks/LTX-2.x` (2/2.3/2.5, top-liked) — separate **LTX-2.x
  Community License** (GitHub LICENSE-2_x verified): same $10M commercial
  trigger, code parts Apache-2.0 ("Apache Code" carved out), gated
  auto-accept on HF. Newer arch = separate ONNX story.
- **ONNX path exists (verified file listing):** `RanaLLC/LTX-Video-onnx` —
  `transformer/model_q8` 2.2 GB + `text_encoder/model_q8` 5.2 GB
  (T5-XXL q8) + `vae_encoder/decoder` 0.84 GB + scheduler config —
  ≈8.3 GB download. This is the real consumer-ONNX path; but DiT video
  gen on CPU is hours-per-clip — **hard-gated on ORT+CUDA EP** (the
  documented prebuilt-dll route) plus a 3-model sampling loop host
  (denoise scheduler = host-side loop like RIFE's timestep, ×N steps).
- `stabilityai/stable-video-diffusion-*` — SVD Community License
  (revenue-capped "other"), no maintained ONNX — skip.
- **Verdict: DEFER** — flagship feature (text/image→clip insert),
  plausible once ORT-CUDA lands; do the license review in parallel.
  Effort L. If we ship, LTX-Video 0.9.8-2B-distilled q8 is the SKU
  (2.2 GB transformer, 4-8 steps).

## 11. Tagging/embedding alternates — fold into the Florence-2 eval

- `Xenova/clip-vit-base-patch32` — MIT tag (upstream openai/clip = MIT,
  well-documented) — image+text encoder pair → **zero-shot tagging** and
  semantic media-bin search by cosine sim. S–M. Worth pairing with §4:
  Florence-2 writes human-readable captions, CLIP powers "find similar
  shots" + arbitrary-label tagging.
- `google/siglip2-*` — **Apache-2.0** (API verified across the family) —
  better zero-shot than CLIP; transformers.js/ONNX exports exist in the
  wild; same seam. Pick SigLIP2-b/16-256 over CLIP if doing this fresh.
- `BAAI/bge-m3` — **MIT** (+ `bge-reranker-v2-m3` Apache-2.0) — multilingual
  text embeddings/reranker for transcript search + media-library semantic
  search. Defer — P4 platform feature, not an editor feature.
- `Xenova/vit-gpt2-image-captioning` — Apache-2.0, ~950 MB — caption-only,
  strictly worse than Florence-2. Skip.
- `Xenova/moondream2` — Apache-2.0 but **7.7 GB** ONNX — VLM-chat; too
  heavy vs Florence-2. Skip.
- `Xenova/slimsam-77-uniform` / `Xenova/sam-vit-*` — Apache-2.0 SAM-v1
  family — superseded by §1 SAM2.1 export. Skip.
- `Xenova/modnet` covered in §5; `Xenova/detr-resnet-50` (Apache-2.0)
  remains a FindObj alternative already covered by wave-1.

## 12. LLM houses — quick pass, nothing for media

- **unsloth** (30 top repos scanned): all LLM GGUF quants (Qwen/Gemma/
  GLM/DeepSeek/gpt-oss) + `LTX-2.3-GGUF` repack (same LTX license — no
  advantage). **Skip for media.**
- **huihui-ai**: abliterated (refusal-removed) LLM checkpoints incl. a few
  Qwen-VL — license tags follow upstreams (apache-2.0) but de-refused
  weights are a brand/safety problem and offer nothing Florence-2
  doesn't. **Skip.**
- **RadixArk**: NVFP4/GGUF LLM quant mirror (DaisyRose, Qwen, GLM, Kimi,
  Muse). No media models. **Skip.**
- **MiniMaxAI**: M1/M2.x text LLMs (M1 = Apache-2.0), MiniMax-VL-01
  (no license card), Music3 + H3-Image (§9, §18-row) — community licenses
  with revenue caps + branding. Nothing cleanly permissive for media.
- **google**: SigLIP2 is the media-relevant hit (§11); gemma family is
  `gemma`-license (custom, not MIT-class); no MediaPipe weights hosted by
  Google on HF (MediaPipe lives in Google's own distribution; the
  `onnx-community/mediapipe_selfie_segmentation` mirror already covers
  the useful one, §5).

## 13. Blocklist additions this scan

- `facebook/sapiens-seg-*` — upstream **CC-BY-NC-4.0**; the onnx-community
  `sapiens-seg-0.3b` mirror carries **no license tag** — treat as NC.
- `facebook/sam3*` / `sam-audio-*` — SAM License ≠ Apache; gated upstream.
  Legal-usable but requires explicit sign-off, not auto-adopt.
- `KRAFTON/Raon-*` speech/TTS — CC-BY-NC-4.0.
- MusicGen (re-confirmed CC-BY-NC), MiniMax community licenses (revenue
  caps + branding + territory), Stability "Community" licenses (revenue
  caps), LTX custom licenses ($10M caps) — all fail the hard
  MIT/Apache/BSD filter; listed because the caps are high enough that a
  deliberate legal decision could still enable them.
- HF `license:` tag misses noted this scan: `onnx-community/sam2.1-*-ONNX`
  (untagged; upstream Apache-2.0), `onnx-community/sam3-tracker-ONNX`
  (untagged; upstream SAM License), `parakeet` ONNX (untagged; upstream
  CC-BY-4.0), `sapiens-seg-0.3b` (untagged; upstream NC). Always trace.

## 14. Suggested wave ordering (next implementation wave)

1. **SAM2.1-hiera-tiny mask tracking (M)** — Apache-2.0, ONNX in hand;
   extends the shipped scan/track infra from "box" to "mask"; enables
   `track_mask` fx (roto/isolate/behind-subject text). Highest novelty.
2. **Kokoro-82M voiceover (M)** — Apache-2.0; one graph + voice bins;
   the G2P front-end is the only real work (evaluate espeak-ng C lib vs
   rule-table). Generates an asset — no realtime contract.
3. **Florence-2-base-ft auto-tag/caption (M)** — MIT; probe-scan → clip
   captions + `<OD>` boxes + tags → media-bin search. Needs a small
   encoder-decoder host loop.
4. **MODNet `xer.fx.ai_matte` (S)** — Apache-2.0, 26 MB, single graph —
   cheapest flagship-looking feature on this page.
5. **Depth-Anything-V3 swap (S–M)** — Apache-2.0 relicensing unlocks the
   larger, better depth model under the wave-3 plan already scheduled.

Deferred-not-dead: chatterbox voice-clone (MIT), bark ambience (MIT),
sam-audio stems (SAM License — *the* potential musdb-free stems answer),
pyannote speaker turns, SigLIP2 zero-shot tags, LTX-Video (legal + ORT-CUDA).
