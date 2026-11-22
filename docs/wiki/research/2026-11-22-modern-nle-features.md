# Modern NLE feature survey — what 2025-2026 editors ship that we don't

Research for the Cut workspace roadmap. Surveys DaVinci Resolve 19/20
(incl. 20.2/20.3), Premiere Pro 25.x–27.0, Final Cut Pro 11.x, CapCut
desktop, Clipchamp, Descript, Filmora 14/15, Runway, and a fast pass on
Kdenlive / Shotcut / Olive. Repo claims verified in the working tree,
2026-11-22; external claims carry URLs and dates. Subagent research;
parent filed the page.

## 0. Where we stand (repo inventory, 2026-11-22)

| Asset | Where | What it is |
|---|---|---|
| Multitrack timeline | `tools/video_editor/timeline_model.{h,cc}` | Project/tracks/clips, `.xerproj` store (per `nle-architectures.md`) |
| Adjustment layers | `compositor.cc:1520`, `is_adjustment` flag | **Already shipped** — the feature FCP only added in 11.1 (Mar 2025) |
| 29 blend modes | `compositor.h` `ApplyBlendModePixels` | Verified symbol; count per project docs |
| 48 builtin effects + JS fx + VST3 | `js_fx.*`, `vst/`, plugin host | Per project docs |
| **Whisper** | `tools/video_editor/ai/whisper_transcribe.{h,cc}` | In-tree, delay-loaded libwhisper → `TranscriptSegment{t0,t1,text}` → **timeline markers** |
| **Beat markers** | `tools/video_editor/ai/beat_markers.{h,cc}` | **Already shipped** — onset tracker → timeline markers ("edit to the beat"), wraps `XER::Audio::AnalysisAI::BeatTracker` |
| RF-DETR detect | `tools/video_editor/ai_tags.cc`, `assets/models/rfdetr_nano.onnx` + `.names` | **Model file vendored in-tree**; used for asset auto-tagging |
| BiRefNet matting | `tools/video_editor/lab/ai_ops.cc`, `assets/models/birefnet_lite.onnx` | **Model file vendored**; wired into lab ops |
| EfficientSAM | `tools/video_editor/lab/ai_ops.cc`, `assets/models/efficientsam_ti.onnx` | **Model file vendored**; smart-select |
| CUDA decode / nvenc | per project docs | hw decode + encode paths |
| Photo editor | `tools/video_editor/photo/` | M3 workspace shipped |
| Extension platform | `extensions/`, `ve.core.h` v3 | `.xexrj` packages, in-process Clang+LLJIT |
| Rate stretch | `ui/tools/rate_stretch_tool.h` | Premiere-style flat retime by edge-drag (`SetClipRate`) — **no keyframed ramp** |
| Speed dialog | `ui/speed_dialog.h` | exists; curves unverified |
| LUT pipeline | `tools/video_editor/lut_manager.*`, `assets/luts/`, `color/ocio_transform.*` | .cube/.3dl + OCIO display transform |

**Honest gaps verified this pass:** no subtitle/caption clip type
(whisper output lands only as markers — grep for `srt|subtitle|caption`
hits nothing in the editor model), no silence-removal (the "silence"
strings in `audio_mixer` are EOF-gap comments), no auto-duck/sidechain,
no proxy/multicam/nested-clip machinery, no keyframed speed ramp, no
stabilization, no tracked masks/effect-pinning, no text-based editing
surface. Also still open from `nle-architectures.md`: no frame cache,
no proxies, no temporal-fx ABI (VF1 only).

## 1. What each editor shipped recently

### 1.1 DaVinci Resolve 20 (Apr 2025) + 20.2/20.3

Headline AI tools (most **Studio-only** — the paid tier), per the New
Features Guide and launch coverage:
https://documents.blackmagicdesign.com/SupportNotes/DaVinci_Resolve_20_New_Features_Guide.pdf ,
https://www.businesswire.com/news/home/20250404998243/en/Blackmagic-Design-Announces-DaVinci-Resolve-20

- **AI IntelliScript** — feed a .txt script; matches transcribed clips
  to dialog lines, builds a timeline with best takes on V1, alternates
  parked disabled on upper tracks. Tolerates ad-lib phrasing.
- **AI Animated Subtitles** — word-by-word animated captions (karaoke
  style for shorts).
- **AI Multicam SmartSwitch** — analyzes audio + lip movement,
  auto-switches multicam angles by active speaker.
- **AI Audio Assistant** — one-click "professional mix" pass.
- **AI Set Extender** — text-prompt outpainting of the frame edge,
  generates background behind foreground objects.
- **AI Voice Convert** — timbre transfer / voice replacement (Studio;
  "controversial" per Blackmagic's own guide).
- **AI Detect Music Beats** — beat markers on music (we already have
  this, `beat_markers.cc` — verify parity).
- **IntelliCut** (in Studio feature matrix) + **Silence Buster**-class
  auto-cut of pauses/dead air.
- Non-AI: dedicated keyframe editor on cut/edit pages, voiceover
  palettes, multilayer compositing, Chroma Warper, Magic Mask 2,
  Depth Map v2, optical-flow vector tools.
- 20.2 (Sep 2025): immersive/URSA Cine, track patching + ripple trim,
  custom metadata fields, MultiText controls.
- 20.3 (Dec 2025): **named timeline snapshots** (versioning without
  duplicating timelines), metadata fields as bin columns, 32K on M5.
  https://www.cined.com/davinci-resolve-20-3-released-32k-support-on-apple-m5-timeline-snapshots-metadata-improvements/

**Resolve 19 carryovers still missing here** (Apr 2024):
https://www.blackmagicdesign.com/media/partial/release/20240412-03 —
**IntelliTrack** AI point tracker (track people/objects through
occlusions; also drives Fairlight audio-pan-to-video), **UltraNR**
AI denoise, **text-based timeline editing** (transcribe → search →
cut by text, with multi-speaker detection), **Dialogue Separator FX**
(rebalance voice/background/ambience), **Ducker Track FX**
(sidechain ducking), music remixing, scene cut detection, Smart
Reframe, Magic Mask, Speed Warp (optical-flow retime), SuperScale,
gyroscopic stabilization, shot matching.

### 1.2 Premiere Pro 25.2 → 27.0

- **25.2 (Apr 2025)**: Generative Extend GA (Firefly; up to ~2s video /
  10s audio, 4K+vertical, generative credits); **Media Intelligence +
  Search panel** (on-device models; NL search over objects/locations/
  camera angle + transcripts + metadata); caption translation 27
  langs; dynamic waveforms; sequence color tabs.
  https://blog.adobe.com/en/publish/2025/04/02/introducing-new-ai-powered-features-workflow-enhancements-premiere-pro-after-effects
- **25.3 (Jun 2025)**: search-panel metadata filters; Blackwell 4:2:2
  10-bit hwaccel.
- **25.5 (Sep 2025)**: **90+ Film Impact effects/transitions now
  built-in** (acquisition folded into the app), Effect Controls
  keyframing rework, MKV rewrite.
  https://blog.adobe.com/en/publish/2025/09/09/introducing-more-than-90-new-effects-transitions-animations-in-premiere-pro
- **26.5 (Sep 2026)**: **Paper Edit** (text-based rough-cut assembly —
  same idea as IntelliScript), Generative Media Tool.
- **27.0 (Nov 2026)**: **AI Assistant**, **Generate Music**, **Separate
  Crosstalk** (split overlapping speakers onto tracks), Color Mode
  replaces Lumetri, Clip Match replaces Apply Match; **Object Masking**
  graduated from beta.
  https://www.heyeddie.ai/learn/premiere-pro/26.5.2/ai-features-in-premiere-pro ,
  https://helpx.adobe.com/premiere/desktop/add-audio-effects/adjust-volume-and-levels/enhance-speech.html

**Established AI features (pre-2025)**: Enhance Speech (now "Enhance
Audio" — separates a clip into dialogue/music/ambience/SFX **editable
layers**), Essential Sound auto-duck, Auto Match loudness, Auto
Reframe, Scene Edit Detection, Morph Cut, Remix (retime music to
length), audio category auto-tagging, Content Credentials on export.

### 1.3 Final Cut Pro 11.x

- **11.0 (Nov 2024)**: **Magnetic Mask** (AI rotoscoping — people/
  objects isolated without greenscreen, composable with any effect's
  mask slot), **Transcribe to Captions** (Apple LLM, on-device),
  Smart Conform, Enhance Light and Color, spatial video, 90/100/120
  fps timelines, PiP + Callout effects, Modular transitions.
  https://www.apple.com/newsroom/2024/11/final-cut-pro-11-begins-a-new-chapter-for-video-editing-on-mac/
- **11.1 (Mar 2025)**: **adjustment clips** (a clip above the timeline
  applying effects to the range below — we already have this),
  Image Playground, Magnetic Mask fixes.
- **11.2 (Sep 2025)**: ProRes RAW / Apple Log 2 plumbing. Minor.
  https://support.apple.com/en-us/102825

### 1.4 CapCut desktop

https://www.capcut.com/tools/desktop-ai-power —
Auto Captions, **Remove Filler Words** (ums + pauses + repetitions,
one click), **Smart Search** (find clips by objects/speech/people),
**Auto Reframe** (subject-tracked aspect change), **Auto Cutout**
(bg removal), **AI Relight**, AI movement (simulated camera shake/
zoom), flicker removal, vocal isolation, loudness normalization,
voice enhancement, translate + lip-sync, TTS, scene detection,
stabilization, **speed curves** (keyframed ramp), face retouch.
Free-tier positioning is aggressive: most of this is free.

### 1.5 Clipchamp

https://clipchamp.com/en/ai-video-editing-clipchamp/ ,
https://uk.pcmag.com/video-editing/139991/clipchamp —
AI autocaptions (80+ langs, click-word → jump playhead), **AI silence
removal** (auto cut pauses >3 s on recordings), TTS (400 voices),
noise suppression. All free. Feature-light but the silence-remover +
autocaptions pair is exactly our whisper → captions gap.

### 1.6 Descript

https://www.descript.com/features , https://help.descript.com —
the reference for transcript-first editing:
- **Text-based editing** — the transcript IS the edit surface; delete
  word → cut media, move sentence → move footage.
- **Remove Filler Words** + "avoid harsh cuts" audio analysis (Jun
  2025), **Shorten Word Gaps**, **Remove Retakes**.
- **Studio Sound** (regenerative voice cleanup), **Eye Contact**
  (gaze correction), **Green Screen** (no-chroma-key bg swap),
  **Automatic Multicam** (speaker-change detection → angle cuts),
  **Center Active Speaker** (auto-crop on talker).
- **Underlord** — chat co-editor (prompt → edits, reviewable); and a
  **Descript MCP server** — Claude drives the editor via MCP. That's
  the shape of "AI assistant in an NLE" worth copying: agent →
  structured edit ops, not generative video.
- **Regenerate** — voice-clone + lip-sync correction of flubbed lines
  (consent-gated); **Create Clips** — auto-pick viral moments → shorts.

### 1.7 Filmora 14 / 15

- **14**: AI Color Palette (cinematic grade preserving skin), AI Sound
  Effect (text→SFX), AI Translation + lip sync, multicam editing,
  planar tracking, Smart Scene Cut, Auto Reframe, silence detection.
  https://me.pcmag.com/en/video-editing/16521/wondershare-filmora
- **15 (Nov 2025)**: **AI Mate** (conversational assistant, 4 modes:
  AIGC/Action/Guide/Auto), **AI Extend** (continue clips ±5/8 s),
  **Pen Tool** (draw paths/shapes, bind to tracked motion), Animated
  Charts, Source+Timeline dual preview, **dual-timeline tabs +
  importable subprojects**, Auto Normalization (LUFS targets), batch
  audio gain, Sora 2/Veo 3.1 gen integration.
  https://www.wondershare.com/news/wondershare-launches-filmora-v15-redefining-video-creation-through-deep-ai-integrated-workflow.html

### 1.8 Runway — adjacent, not an NLE

**Gen-4** (Mar 2025): consistent-character/scene generation from
reference images. **Aleph** (Jul 2025): in-context video model —
prompt "remove object / new camera angle / relight" on *existing*
footage. https://runway.com/research/introducing-runway-gen-4 ,
https://www.theaivideocreator.ai/p/runway-aleph . Cloud generative —
can't build locally; file under "optional API surface for extensions."

### 1.9 Open source fast pass

- **Kdenlive 25.04** (Apr 2025): **SAM2 object-segmentation plugin**
  (local, tracked masks for bg removal / per-object effects), OTIO
  import/export rewrite. 25.12: 10-bit pipeline, preview scaling,
  range markers, KDDockWidgets, vertical safe areas.
  https://kdenlive.org/news/releases/25.04.0/
- **Shotcut**: Speech-to-Text via whisper.cpp (Oct 2024; model
  downloader 25.07; Vulkan GPU path added later), TTS via Docker/
  Kokoro (25.10), HTML→image/video generator (25.10), vidstab
  stabilization, waveform-drag gain. https://shotcut.com/download/releasenotes/
- **Olive**: still alpha, quiet since ~2024-12 — nothing new to steal
  beyond the cache architecture already covered in
  `nle-architectures.md`.

## 2. Feature matrix — AI-assisted editing

| Feature | Who ships it | What it does | Effort | Our pieces |
|---|---|---|---|---|
| **Text-based editing** | Resolve 19+, Premiere 23+ (Paper Edit 26.5), Descript core, CapCut | Transcript as edit surface: delete/search words → ripple cuts; speaker names | **M** | whisper in-tree + word timestamps (whisper.cpp flag) + timeline model; needs transcript store + word→clip mapping + transcript panel |
| **Auto-captions / subtitle track** | everyone (FCP 11, Premiere, CapCut, Clipchamp 80 langs, Resolve) | Whisper-style ASR → timed caption clips + SRT | **M** | whisper done; **missing caption clip type** — new clip/subtitle-track model + renderer + SRT export |
| **Animated/karaoke captions** | Resolve 20 (AI Animated Subtitles), CapCut, Filmora | Word-by-word highlight/zoom captions for shorts | **S-M** | word timestamps + text fx; rides on caption track above |
| **Silence + filler-word removal** | Descript (w/ "avoid harsh cuts"), CapCut, Clipchamp (>3 s), Premiere delete-pauses, Resolve IntelliCut | Detect pauses/ums via transcript+energy → auto-cut with review UI | **S** | whisper segments + `ai/audio_pcm` energy; the honest 80% is pure data processing on what we already produce |
| **Scene cut detection** | Resolve, Premiere Scene Edit Detection, CapCut, Filmora Smart Scene Cut | Histogram-diff → split flattened video into clips/markers | **S** | decode path exists; classic FFmpeg `select=gt(scene)`-style algorithm, no ML needed |
| **Smart reframe / Auto reframe** | Premiere Auto Reframe, Resolve Smart Reframe, FCP Smart Conform, CapCut, Descript Center Speaker | Subject-tracked crop → 9:16/1:1 versions | **M** | **RF-DETR ships in-tree** + BiRefNet fallback; needs tracked crop window + smoothing + per-aspect export preset |
| **Tracked effects pin / point tracking** | Resolve IntelliTrack, Premiere Object Mask(ing), Filmora planar tracking, Kdenlive SAM2 | Track a point/region → pin effect, text, mask to it | **M** | VitTrack is opencv_zoo-verified cv::dnn per `ai-model-stack.md`; RF-DETR + temporal smoothing is plan B; needs keyframe track output → transform params |
| **Tracked roto mask** | Resolve Magic Mask 2, FCP Magnetic Mask, Kdenlive SAM2 | Click object → mask propagates across frames | **M-L** | EfficientSAM in-tree for frame-0 mask; propagation = tracker or per-frame SAM (flicker risk); SAM2 video = L per model stack |
| **Multicam speaker auto-switch** | Resolve SmartSwitch, Descript Automatic Multicam | Diarize audio → cut angles on speaker change | **M** | for separate-channel audio: per-track energy/VAD is enough (no pyannote needed); needs multicam infra first (below) |
| **Media intelligence / smart search** | Premiere 25.2, CapCut Smart Search | NL query over footage: objects, speech, people | **M** | `ai_tags.cc` already tags via RF-DETR; add transcript index; CLIP embeddings later |
| **Script→timeline assembly** | Resolve IntelliScript, Premiere 26.5 Paper Edit | Match transcript to script lines → ordered rough cut, alternates parked | **M** | transcripts + fuzzy text matching; no new ML |
| **AI co-editor (chat → edits)** | Descript Underlord + **MCP server**, Filmora AI Mate, Premiere 27 AI Assistant | Prompt → structured edit ops with review | **L** | extension platform + js_fx + native modules = the execution substrate exists; needs LLM binding + edit-op schema. Copy the **Descript MCP** shape |
| **Generative extend** | Premiere Gen Extend, Filmora AI Extend | Outpaint/generate extra frames at clip ends | **L / skip** | cloud diffusion; no local path (see model stack: IC-Light deferred) |
| **Eye contact / AI relight** | Descript, CapCut Relight, Descript Studio Lighting | Gaze correction; prompt-driven relighting | **L / skip** | generative gaze/lighting models; IC-Light v2 is non-commercial anyway |
| **Voice convert / regen / lip-sync** | Resolve Voice Convert, Descript Regenerate, Filmora AI lip sync | Voice clone, flub fix, dub lips | **L / skip** | ethics + consent plumbing + generative models |
| **Caption translation** | Premiere (27 langs), CapCut, Descript | Localize caption tracks | **M** | caption track + MT model (or API); gated on captions existing |
| **Color/shot match** | Resolve Shot Matching, Premiere Clip Match (27.0), Filmora AI Color Palette | Match a clip's grade to a reference frame | **M** | Lab/histogram stats match = deterministic, no model needed; we have the color pipeline |
| **Video object removal** | Resolve Object Removal, CapCut, Runway Aleph | Paint object → tracked inpaint across frames | **L** | MI-GAN/LaMa per-frame + tracking + seam fixes; stills version already planned for Photo |
| **Long→shorts auto-clip** | Descript Create Clips, CapCut, OpusClip | Pick "viral moments" → reframe → caption | **M** | transcript + tags; heuristic scoring; bundles captions+reframe |
| **Beat markers** | Resolve 20 AI Detect Music Beats | Music tempo/onsets → timeline markers | **done** | `ai/beat_markers.cc` already ships — polish/parity check only |

## 3. Feature matrix — workflow

| Feature | Who ships it | What it does | Effort | Our pieces |
|---|---|---|---|---|
| **Multicam editing** | Resolve, Premiere, FCP, Filmora 14, Kdenlive | Sync angles (audio waveform), angle viewer, switch-on-cut | **L** | nothing yet; audio cross-correlation sync is the known-good approach; biggest single work item on the page |
| **Proxy workflow** | Resolve Optimized Media, Premiere, Kdenlive, Blender StripProxy | Transcode source → low-res edit files, relink on export | **M** | media_worker + nvenc + media_catalog; `nle-architectures.md` already has the StripProxy plan |
| **Compound / nested clips** | Resolve compound, FCP compound, Premiere nested seq, Kdenlive sequences, Filmora 15 dual-timeline+subprojects | Timeline-in-timeline; reuse a sequence as a clip | **L** | GES "timeline-is-element" pattern — compositor must satisfy a producer interface (already flagged in NLE research) |
| **Adjustment layers** | FCP 11.1 (new!), all pros | Effect range clip above timeline | **done** | `Clip::is_adjustment` verified in compositor + export |
| **Markers + comments / range markers** | Resolve (marker notes + cloud collab), Premiere Frame.io, Kdenlive 25.12 ranges, Descript comments | Markers with text/range; review comments | **S-M** | markers exist (whisper+beats write them); add comment/range fields + panel. Real-time collab = defer |
| **Named timeline snapshots** | Resolve 20.3 | Version a timeline in place | **S** | `.xerproj` is JSON — snapshot = serialized copy + name |
| **Source/program dual monitor** | every pro NLE, Filmora 15 | Preview source vs. sequence separately | **M** | monitor widget exists; source-mode plumbing needed |
| **OTIO interchange** | Kdenlive, Shotcut, Resolve/others | Import/export editorial cuts | **M** | already in NLE research deliverable matrix ("later") |
| **Conformed audio / peaks+conform cache** | Premiere .cfa/.pek, Resolve | Uniform-samplerate cached audio + waveforms | **S-M** | peaks cache exists (`media_prep`); conform = resample-once-store |
| **Voiceover record-to-timeline** | Resolve 20 voiceover palettes, FCP, Shotcut | Arm track, record mic to clip | **M** | audio engine + capture device plumbing |
| **10-bit pipeline** | Kdenlive 25.12 | 10-bit end-to-end for effects/composite | **M** | pipeline-wide; ties to existing render-infra work |

## 4. Feature matrix — modern effects & audio

| Feature | Who ships it | What it does | Effort | Our pieces |
|---|---|---|---|---|
| **Auto-ducking** | Premiere Essential Sound, Resolve 19 Ducker FX, Filmora, CapCut normalize | Music ducks under detected dialogue automatically | **S-M** | sidechain compressor on the mixer (or a stock VST3); dialogue detect = whisper/VAD on speech tracks |
| **Voice isolation / enhance speech** | Resolve Voice Isolation + Dialogue Separator FX, Premiere Enhance Audio (stems!), Descript Studio Sound, CapCut vocal isolation, Clipchamp noise suppress | Separate voice from noise/ambience/music; rebalance | **M** | needs a separation model — DeepFilterNet3 (MIT) for denoise, Demucs (MIT) for stems; single ONNX runner shaped like FindObject |
| **Loudness normalize to targets** | Filmora 15 Auto Normalization (LUFS), Premiere Auto Match, Resolve Level Matcher | Measure + gain to platform LUFS | **S-M** | EBU R128 (libebur128 or FFmpeg loudnorm analysis) + gain pass on export/mixer |
| **Stabilization** | Resolve (incl. gyroscopic), Premiere Warp Stabilizer, CapCut, Shotcut vidstab | Feature-track + motion smooth → crop | **M** | OpenCV feature tracking + vidstab-style smoothing; no model needed |
| **Speed ramping / curves** | CapCut speed curves, Resolve retime curve, Premiere time remap | Keyframed rate over a clip | **M** | `rate_stretch_tool` is flat-rate only; needs rate keyframes on clips + UI curve editor |
| **Optical-flow retime** | Resolve Speed Warp, FCP optical flow | Frame-interpolated slow-mo | **M** | **RIFE-ncnn** is the planned model per `ai-model-stack.md` (MIT, no CUDA toolkit needed) |
| **Motion blur (rendered)** | all pros | Shutter-look blur on retime/transform | **M** | needs temporal-sampling ABI — the VF2/`getFramesNeeded` decision already flagged in NLE research |
| **Chroma-key-free bg removal (video)** | Descript Green Screen, CapCut Auto Cutout, Filmora Smart Cutout | Per-frame matte, no greenscreen | **M** | **BiRefNet ships in-tree**; work = batch per-frame + temporal deflicker (edge-flicker is the honest risk) |
| **Sky replacement** | CapCut templates, mobile apps | Segment sky → composite new sky | **M** | niche; needs a sky-class segmenter (segformer ONNX); deprioritize |
| **LUT browser + preview** | universal | Thumbnail/gallery LUT picking | **S** | `lut_manager` + `assets/luts/` exist — this is pure UI work |
| **TTS voiceover** | Clipchamp 400 voices, CapCut, Shotcut (Kokoro via Docker), Filmora | Text → spoken track on timeline | **M** | Kokoro ships as ONNX; generate → import to audio track |
| **AI auto-mix** | Resolve 20 AI Audio Assistant | Classify + balance a whole mix one-click | **L** | needs category classification + mix heuristics; Enhance-Audio-layer equivalent |
| **Pen/annotation tracked to motion** | Filmora 15 Pen Tool | Draw → stroke follows tracked object | **S-M** | rides on the tracker above |
| **AI sound effects (text→SFX)** | Filmora 14, Premiere Generate Music (27.0) | Prompt → generated audio asset | **L** | generative audio; skip for now |
| **Content Credentials / provenance** | Premiere | C2PA metadata on export marking AI edits | **S** | c2pa metadata embedding; cheap, low demand |

## 5. Top-10 worth building next (ranked, value ÷ effort)

Ranking weighs: creator demand, how much of the stack already exists,
and whether it compounds (e.g. caption track unlocks 4 features).

| # | Feature | Effort | Why now |
|---|---|---|---|
| 1 | **Caption/subtitle track + SRT** | M | Whisper is already integrated — but output only lands as markers. The single biggest creator-facing gap; unlocks items 2-3 and translation later. New subtitle clip type + renderer + SRT export |
| 2 | **Text-based editing** | M | Same whisper investment. Delete-word→ripple-cut is the feature Descript built a company on; we have the transcript and the transactional timeline model — this is plumbing + a transcript panel |
| 3 | **Silence + filler-word removal** | S | Whisper segments + PCM energy → gap list → auto-cut with review UI. Near-free once 1-2 land; CapCut/Clipchamp/Descript all market it as flagship |
| 4 | **Animated captions (karaoke)** | S-M | Word-level highlight captions ride on #1; the highest-visibility "shorts" feature after captions themselves (Resolve 20, CapCut) |
| 5 | **Auto-ducking** | S-M | Sidechain compressor on `audio_mixer` (or bundled VST3), driven by speech-track activity. Universal across CapCut/Premiere/Resolve; pure DSP, no model |
| 6 | **Voice isolation / enhance speech** | M | DeepFilterNet3 (MIT) or Demucs ONNX behind a FindObject-shaped runner. Biggest perceived-quality win for talking-head content; every competitor has one |
| 7 | **Smart reframe to 9:16** | M | RF-DETR model already vendored in `assets/models/` — tracked crop window + easing + per-aspect export. BiRefNet fallback for when detection fails |
| 8 | **Object/point tracking → pin effects+masks** | M | VitTrack (opencv_zoo, cv::dnn-verified per model-stack research) or RF-DETR+smoothing → keyframed transform/mask params. Unlocks tracked captions, pinned callouts, censored faces, tracked pen |
| 9 | **Proxy workflow** | M | Not flashy but unblocks real footage editing — nvenc transcode via media_worker + proxy records on the catalog. Compounds with the #1 gap from NLE research (frame cache) |
| 10 | **Compound/nested clips** | L | Compositor-as-producer (GES pattern). The structural feature all template/reuse workflows need — biggest item on the list, start when proxies stabilize |

**Honorable mentions (cheap, bundle with above):** scene cut detection
(S — pairs with silence removal for "re-edit a flattened export"),
timeline snapshots (S), loudness normalize (S-M, pair with #5),
markers+comments fields (S-M), LUT browser polish (S),
stabilization (M, OpenCV-only).

**Explicit skips:** generative extend / eye-contact / voice-convert /
lip-sync / AI relight (generative or license-blocked per
`ai-model-stack.md`), sky replacement (niche), Runway-class generative
video (cloud), real-time collaboration (infra-heavy), spatial video.

## 6. Honest verdicts

- **We already match FCP on adjustment layers and Resolve on beat
  markers** — verified in-tree (`is_adjustment`, `beat_markers.cc`).
  The "we're behind" narrative is real but narrower than it looks.
- **The transcript economy is the leverage point.** Whisper is in the
  tree and feeding *only* markers — captions, text editing, silence
  cutting, smart search, and script-matching are five flagship
  features that all hang off the same already-working transcription.
  That's why #1-3 top the list.
- **Detection/masking is similarly under-monetized.** RF-DETR,
  BiRefNet, EfficientSAM are all *vendored* (`assets/models/*.onnx`)
  but drive only tagging/select. Reframe, tracking, green-screen and
  pinned effects are downstream plumbing, not new models.
- **Multicam and proxies are the credibility features.** Creators
  won't take an NLE seriously past talking-head shorts without both.
  Proxies are M and unblock real-footage work; multicam is the
  biggest honest L on the page.
- **Nobody local ships generative extend well** — it's a cloud-credits
  business model (Adobe/Filmora). Not a gap; a choice. Descript's
  **MCP/agent-to-edit-ops** shape is the smarter version of "AI
  assistant" for us — our extension platform already runs code in-
  process, which is exactly what an agent needs.
- **Open source is converging on the same list**: Kdenlive's SAM2
  plugin and Shotcut's whisper.cpp integration are validation, not
  threat — and both chose the same permissive-model path we did.

## 7. Sources

Blackmagic DaVinci Resolve 20 New Features Guide (PDF, Apr 2025) +
20.2 guide + businesswire launch + CineD 20.3 + Resolve Studio 20
feature matrix (PDF) + Resolve 19 launch notes + creativevideotips
IntelliScript/SmartSwitch hands-on + tutsplus AI workflow rundown;
Adobe blog Premiere 25.2 + 25.5 posts, Adobe helpx feature summary +
Enhance Speech/ducking pages, heyeddie.ai 26.5/27.0 AI feature map,
larryjordan 25.5 notes, digitalproduction IBC-2025 writeup; Apple
newsroom FCP 11 + support.apple.com release notes 11.0–11.2 +
ProVideo Coalition FCP review; capcut.com desktop-ai-power + PC-guide
resources + primalvideo Pro review + thetechreview.net;
clipchamp.com ai-video-editing + blog posts + PCMag UK review;
descript.com/features + pricing + help.descript.com (filler words,
look-good tools, AI tools overview, automatic-multicam) + June 2025
changelog (harsh cuts, Regenerate); filmora.wondershare.com
whats-new V15 + V15 launch PR + user-guide PDF + PCMag Filmora 14
review + macworld V15; runway.com/research/introducing-runway-gen-4 +
theaivideocreator Aleph test + help.runwayml.com; kdenlive.org 25.04
release notes + docs.kdenlive.org whats_new + linuxiac/howtogeek
coverage; shotcut.org blog 24.10/25.07/25.10 + release notes +
features page.
