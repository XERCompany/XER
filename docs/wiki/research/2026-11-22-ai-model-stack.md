# AI model stack — research (matting, inpainting, SR, SAM, depth, detect, transcribe)

Research for shipping open AI models across the suite (Photo smart-select /
object tools, Cut transcription & tracking, upscalers). Repo claims verified in
the working tree, 2026-11-22; external claims carry URLs and are dated where
they matter. Subagent research; parent filed the page.

## 0. Where we stand (repo inventory, 2026-11-22)

The inference plumbing is further along than "no AI yet":

| Asset | Where | What it is |
|---|---|---|
| FindObject | `src/content/ai/find_object/find_object.{h,cc}` | **Dual-backend ONNX detector, already wired.** ORT C API backend (delay-loaded, `LoadLibrary`-probed — reports `loadable:false` on broken DLL, never crashes) + `cv::dnn::readNetFromONNX` fallback. Letterbox 640², `[1,4+C,N]` YOLO-style decode, per-class IoU NMS in-house. The reusable pattern for every image-in/image-out model below |
| ORT status | `find_object.cc:13` comment | `onnxruntime.dll` **broken today**: built against a since-bumped libabsl — fails probe, cv::dnn path serves instead. See §8 |
| cv::dnn status | `src/wrapper/opencv/` | OpenCV 5.0 delay-loaded, `DNN_BACKEND_OPENCV`/`DNN_TARGET_CPU` proven working on YOLO-style ONNX |
| Whisper | `tools/video_editor/ai/whisper_transcribe.{h,cc}` | **Already integrated**: runtime-probes `libwhisper-1.dll`, finds ggml model (`$XER_WHISPER_MODEL` → `<exe>/models/` → `~/.cache/whisper/`), 16 kHz mono float in → `TranscriptSegment{t0,t1,text}` for timeline markers |
| ggml | `src/content/ai/CMakeLists.txt:80` | `ggml::ggml` linked into `XER_ContentAI` when found — llama.cpp-family door is open |
| sd_base | `src/content/ai/sd_base/` | In-tree SD skeleton: unet, vae, controlnet branch/router, CLIP tokenizer+text-encoder stub, schedulers, latent tensor, fp16 — scaffolding, not a working pipeline |
| Own ML framework | `src/content/ai/ml/`, `models_builder/` | In-house layers/training/onnx_stack parser — a framework, not a runtime for pretrained ONNX; not the ship vehicle |
| GPU reality | toolchain | CUDA **driver + nvenc** present, **no toolkit headers** — nothing CUDA-EP/CUDA-backend can be *compiled* here. Vulkan driver path is the only GPU inference we can build today |

**Honest gaps:** only the YOLO 640² contract is *proven* end-to-end; every
model below is unverified against our actual OpenCV 5.0 build until a smoke
test says otherwise. No model files ship in `assets/models/` yet. No GPU
inference path exists at all today (cv::dnn CPU only in this build).

## 1. The runtime reality — pick the backend before the model

OpenCV 5.0's dnn module is materially better than 4.x, which changes what
"cv::dnn-only" can run. Per OpenCV's own 5.0 docs (checked 2026-09):

| Engine | Coverage | Notes |
|---|---|---|
| `ENGINE_OPENCV` (a.k.a. `ENGINE_NEW`) | ~75–80% of ONNX ops | rewritten graph, dynamic shapes, folding; CPU-only; **default** |
| `ENGINE_CLASSIC` | ~22% of ONNX ops | the 4.x engine; has DNN_BACKEND_CUDA/OpenVINO — but a CUDA backend build needs the toolkit we don't have |
| `ENGINE_ORT` | whatever ORT runs | only if OpenCV was built with ORT integration — MSYS2's wasn't (verify) |

https://docs.opencv.org/5.0/main%5Fmodules/dnn_engine_selection.html ,
https://opencv.org/opencv-dnn-module/ — the official 5.0 tutorial literally
runs YOLO26 + **RF-DETR** through `readNetFromONNX` on the new engine.
`opencv_zoo` (Apache-2.0, https://huggingface.co/opencv/opencv_zoo) is the
set of models *guaranteed* to work under cv::dnn — it includes EfficientSAM,
YOLOX, NanoDet, YuNet faces, PP-HumanSeg, **VitTrack** (tracking), PP-OCR
text detection, WeChatQRCode.

**Backend matrix for us (decision order):**

1. **cv::dnn `ENGINE_OPENCV`** — zero new deps, already the working fallback.
   Ship list should prefer models that load here.
2. **ncnn + Vulkan** — the GPU path that works *without* the CUDA toolkit.
   BSD-3, zero runtime deps, `pnnx` converts ONNX→ncnn, proven by official
   `realesrgan-ncnn-vulkan` / `rife-ncnn-vulkan` / `realsr-ncnn-vulkan`
   binaries (nihui, MIT impl code). https://github.com/Tencent/ncnn
3. **ONNX Runtime** — fix per §8. Even on MSYS2 it's CPU-only (no DirectML EP
   in MinGW packages); the *prebuilt* Microsoft ORT dll (DML/CUDA EPs) is
   LoadLibrary-compatible through the C API — see §8.
4. **ggml** (whisper.cpp / llama.cpp `libmtmd`) — audio+vision-language,
   separate format (.gguf/.bin), already half-integrated via whisper.

A general "image→image ONNX runner" modeled on FindObject (resize → NCHW
blob → forward → tensor out) covers ~80% of this page with one class.

## 2. Background removal / matting

| Model | License | ONNX | cv::dnn? | I/O contract | Perf | Difficulty |
|---|---|---|---|---|---|---|
| **BiRefNet** (SOTA DIS) https://github.com/ZhengPeng7/BiRefNet | **MIT** (code+weights) | Official ONNX in GitHub releases since 2024-08; `onnx-community/BiRefNet-ONNX` for transformers.js; `BiRefNet_lite` (Swin-T) smaller | Probably — export replaces non-ONNX DeformConv with standard ops; unverified | `input_image` f32 `[1,3,1024,1024]` ImageNet-norm → logits `[1,1,1024,1024]`, sigmoid → matte | ~165ms A100 fp32 Swin-L, ~93.8ms lite (repo numbers); CPU = seconds — tile/downsample | **Low-med** — best quality-per-license on the page |
| **ISNet / DIS-general** https://github.com/xuebinqin/DIS | Apache-2.0 | imgly export (MIT) `isnet-general-use.onnx` 176MB; int8 44MB `xrds/isnet-general-onnx-int8` | Likely — pure conv U-Net | `[1,3,1024,1024]`, `(x-128)/256` → saliency 0..1 | ~U2-Net class; int8 helps CPU | Low — rembg's default backend |
| **U²-Net / u2netp** https://github.com/xuebinqin/U-2-Net | Apache-2.0 | `u2net.onnx` 176MB / `u2netp.onnx` 4.7MB via rembg release (rembg code MIT; weights Apache per repo LICENSE — provenance documented in rembg#837) | **Yes** — long-standing cv::dnn compat | `[1,3,320,320]` → saliency | u2netp real-time-ish CPU; full 176MB slower | **Low — the baseline to ship first** |
| **MODNet** https://github.com/ZHKKKe/MODNet | Apache-2.0 (code+models+demos) | Official export script in `onnx/`; `Xenova/modnet` ONNX | Likely — standard conv graph | portrait only; ~512² dynamic | real-time claim, portrait matting | Low — people-cutout only |
| RMBG-1.4 / 2.0 (BRIA) | **CC BY-NC 4.0 — NON-COMMERCIAL** | ONNX exists (gated repo) | — | — | — | **❌ cannot ship**; needs paid BRIA license |
| RobustVideoMatting (RVM) | **GPL-3.0** (re-licensed 2021-09) | ONNX export official | — | video matting, recurrent | 4K@76fps claim (1080Ti) | **❌ GPL** — do not link; per-frame BiRefNet/ISNet is the clean substitute |

**Verdict:** ship **BiRefNet** (hero) + **u2netp** (instant preview) +
**isnet int8** (memory-constrained path). All MIT/Apache, all single-ONNX,
all fit the FindObject runner pattern. RMBG and RVM are license-blocked.

## 3. Inpainting / object removal

| Model | License | ONNX | cv::dnn? | I/O | Notes | Difficulty |
|---|---|---|---|---|---|---|
| **LaMa (big-lama)** https://github.com/advimman/lama | Apache-2.0 | `Carve/LaMa-ONNX` `lama_fp32.onnx` opset-17 (FFT rewritten as MatMul/Cos/Sin, **fixed 512²**); `opencv/inpainting_lama` ONNX 93MB exists in the wild | Plausible — no DFT ops in the Carve export; verify | `image [1,3,512,512]` + `mask [1,1,512,512]` → rgb | **Fixed input size** → must tile+stitch large masks; Places2 training data is research-NC (card flags it) | Med — tiling logic is the work |
| **MI-GAN** https://github.com/Picsart-AI-Research/MI-GAN | **MIT (LICENSE-WEIGHTS, author-confirmed 2026-09)** | Official `migan_pipeline_v2.onnx`: `image`+`mask` **uint8, dynamic H×W** → composited result | Verify (GAN gen, convs) | easiest contract on this page | mobile-designed, ~10× cheaper than LaMa class; **provenance caveats**: trained on Places2 (NC data terms) + distilled from Co-Mod-GAN (NVIDIA-NC teacher) — legal gray, flag for counsel | **Low-med** — simplest integration |
| MAT https://github.com/fenglinglwb/MAT | **CC BY-NC 4.0** | — | — | — | research-only | **❌ cannot ship** |
| IOPaint https://github.com/Sanster/IOPaint | Apache-2.0 (tool, archived) | — | — | — | the reference UX/architecture for erase/replace features; harvest design, not code | n/a |

**Verdict:** **MI-GAN pipeline ONNX first** (dynamic-size, composited output,
MIT), **LaMa-512 second** for quality on big holes behind a tiler. MAT is
license-blocked; diffusion inpainting deferred to §7.

## 4. Super-resolution / upscale

| Model | License | ONNX | cv::dnn? | I/O | Perf | Difficulty |
|---|---|---|---|---|---|---|
| **Real-ESRGAN x4plus** https://github.com/xinntao/Real-ESRGAN | **BSD-3** (code+weights) | Community exports w/ dynamic H×W, opset-17, fp32 67MB / fp16 34MB (`othytenk/realesrgan-x4plus`); **official `realesrgan-ncnn-vulkan`** binaries (impl MIT) | Likely — RRDBNet = convs + pixel-shuffle; verify on ENGINE_OPENCV | `[1,3,H,W]` RGB /255 → `[1,3,4H,4W]` | CPU: seconds+ per MP (23 RRDB blocks); **ncnn-Vulkan = the realtime path**, tiled `-t` built in | Low (ONNX) / Med (ncnn port of the exe flow) |
| **SwinIR** https://github.com/JingyunLiang/SwinIR | Apache-2.0 (deps MIT) | exports exist; also `swinir-ncnn-vulkan`-style community ports | Doubtful on CLASSIC; maybe ENGINE_OPENCV — shifted-window reshape-heavy | `[1,3,H,W]` → `[1,3,sH,sW]` | heavier than RRDBNet per px | Med — only if RRDBNet quality insufficient |
| realesr-animevideov3 | BSD-3 (same repo) | ncnn official | — | video-tuned SR, temporal | ncnn-Vulkan realtime-class | Med — video workspace payoff |

**Verdict:** **Real-ESRGAN via ncnn-Vulkan is the flagship GPU feature we can
actually build** (no toolkit). ONNX/cv::dnn path as CPU fallback. This is the
single most demo-able AI feature: 4× upscale + tile progress + cancel.

## 5. Segmentation for smart-select (SAM family)

| Model | License | ONNX | cv::dnn? | Contract | Perf | Difficulty |
|---|---|---|---|---|---|---|
| **SAM ViT-B/L/H** https://github.com/facebookresearch/segment-anything | Apache-2.0 | encoder/decoder split is the standard pattern; `vietanhdev/samexporter` (MIT) produces pairs + quantized variants (`segment-anything-onnx-models` HF, ViT-B quant 72MB) | Encoder probably ORT-friendlier; decoder is light | enc: `[1,3,1024,1024]` → embedding; dec: prompt coords + embedding → 3 masks+IoU | enc ~1s-class CPU (ViT-B); dec ms-level → cache embedding per image, interactive clicks | Med — two-model pipeline + prompt encode |
| **MobileSAM** https://github.com/ChaoningZhang/MobileSAM | Apache-2.0 | official `scripts/export_onnx_model.py`; HF zip **35MB / 10.5MB quant** | same split contract | same SAM decoder contract | ~8ms-class encoder on GPU; CPU-friendly TinyViT | **Med — best interactive pick for CPU** |
| **EfficientSAM-Ti/S** https://github.com/yformer/EfficientSAM | Apache-2.0 | **official split ONNX**; **in opencv_zoo** = guaranteed cv::dnn | **Yes (zoo-verified)** | box/point prompts → masks | smallest validated pair | **Med — safest cv::dnn-only smart-select** |
| SAM 2.1 https://github.com/facebookresearch/sam2 | Apache-2.0 (code+weights) | samexporter covers enc/dec for image use; **video propagation needs memory-attention state — ONNX VOS is not a drop-in** | partial | image: SAM-like; video: stateful masklet tracking | hiera tiny/base practicable | Med (image) / **High** (video masklets) |
| SAM 3 / 3.1 (Meta, 2025-11 / 2026-03) | **custom "SAM License"** — gated weights, redistribution under same agreement, military-use bar, unilateral-amend clause | — | — | text-prompt "concept" segmentation | — | **❌ license review required — treat as blocked** |

**Verdict:** **EfficientSAM-Ti** (zoo-verified cv::dnn) + **MobileSAM**
(10MB quant) for click-to-select; SAM ViT-B optional HQ encoder. SAM 2.1
image mode shares the plumbing; video masklets = later milestone.
SAM 3's bespoke license kills the "text-prompt select" dream for now —
Florence-2's `<REFERRING_EXPRESSION_SEGMENTATION>` (§7) partially covers it.

## 6. Depth estimation (DOF / relayering)

| Model | License | ONNX | cv::dnn? | I/O | Notes | Difficulty |
|---|---|---|---|---|---|---|
| **Depth-Anything V2 Small** https://github.com/DepthAnything/Depth-Anything-V2 | **Apache-2.0 — Small only.** Base/Large/Giant weights = CC-BY-NC-4.0 (repo LICENSE; author-confirmed code Apache, weights NC, issue #81) | `fabio-sim/Depth-Anything-ONNX` official-linked; `onnx-community` & `mtbui2010` dynamic-H×W exports | Plausible (DPT/DINOv2 ViT) — verify | `[1,3,H,W]` H,W mult. of 14, ImageNet-norm → `[1,H,W]` relative inverse depth | 24.8M params Small; feeds lens-blur/3D-warp/relight features | Med |
| — | — | — | — | — | Open caveat: Small's training-data license questioned in repo issues (#81/#320) — Apache claim stands on weights, data provenance unresolved | — |

**Verdict:** ship **Small only**, gate the UI string to "Depth (Small)".
One ONNX, one output plane → trivial runner; the DOF/re-layer feature code
is the real work.

## 7. Detection better than YOLOv8n — and a license problem

**Flag:** current FindObject contract documents "YOLOv8n / YOLO11n as
reference". All Ultralytics models (v8, 11, 26) are **AGPL-3.0 or paid
Enterprise** — and Ultralytics asserts AGPL covers weights produced by its
training code. Fine for dev, **do not ship** the `.onnx` in a closed build.
(https://docs.ultralytics.com/models/yolo11, simuletic licensing explainer
2026-10)

| Model | License | ONNX | cv::dnn? | Output | mAP vs v8n | Difficulty |
|---|---|---|---|---|---|---|
| **RT-DETR / RT-DETRv2 / v4** https://github.com/lyuwenyu/RT-DETR | **Apache-2.0** from Baidu repo (the *Ultralytics-packaged* rtdetr is AGPL — same weights, different repo, pick the Apache one) | Official export; also in HF `transformers`; LibreYOLO re-impl MIT/Apache | verify — DETR decoder graph is heavier than YOLO head | `dets` boxes + `labels` logits, **no NMS needed** | rtdetr-r50 ≈ 53 mAP ≫ v8n 37.3 | Med — different decode contract than FindObject's |
| **RF-DETR** https://github.com/roboflow/rf-detr | **Apache-2.0** for Nano/Small/Medium/Large + **all Seg sizes**; Atto/Femto/Pico/XL/2XL under Roboflow PML-1.0 (paid self-host) | Official ONNX export; **OpenCV 5 tutorial runs it under cv::dnn** | **Yes (tutorial-verified)** | raw `dets`+`labels`, NMS-free | Nano 48.4 AP50:95 ≫ v8n; Seg variant doubles as instance-mask source | **Low-med — the drop-in v8n replacement** |
| **YOLOX** (zoo) https://github.com/Megvii-BaseDetection/YOLOX | Apache-2.0 | opencv_zoo ONNX, int8 variants | **Yes (zoo)** | YOLO decode ≈ current contract | yolox-s ≈ 40 mAP | **Low — keeps FindObject decode** |
| NanoDet (zoo) | Apache-2.0 | zoo | **Yes (zoo)** | anchor-free | small/fast | Low |
| **YuNet face** (zoo) | MIT | zoo ONNX, dynamic-shape 2026 export | **Yes (zoo)** | face boxes+landmarks | 0.88 easy AP | Low — face-aware features |
| **VitTrack** (zoo) | zoo per-model (verify) | zoo | **Yes (zoo)** | template+search → box | — | Med — **Cut workspace: track-select over time** |

**Verdict:** swap shipped detector to **RF-DETR-N** (Apache, NMS-free,
cv::dnn-verified) and keep **YOLOX** for the existing decode path. Add
**YuNet** (faces) + **VitTrack** (object-follow on timeline) later.

## 8. Style / color / relight / generative wildcards

| Model | License | ONNX | I/O | Value vs cost | Verdict |
|---|---|---|---|---|---|
| Fast-neural-style (candy/mosaic/udnie/rain-princess/pointilism) | weights BSD-3 (pytorch examples), onnx model zoo | **Yes, opset-8/9, 6.6MB, dynamic** | `[1,3,H,W]` 0..255 → same | trivial cv::dnn, instant "AI styles" gallery | **Ship — nearly free** |
| **DDColor** https://github.com/piddnad/DDColor | Apache-2.0 (code+weights) | official `scripts/export_onnx.py` (`ddcolor-tiny-op12.onnx`) | `[1,3,512,512]` → `[1,2,512,512]` Lab ab-channels | B/W colorization, killer for photo restore | **Ship — med** (Lab plumbing + resize strategy) |
| **Florence-2 base/large** https://huggingface.co/microsoft/Florence-2-base | **MIT** | `onnx-community/Florence-2-{base,large}`: 4 files (vision_encoder + encoder + decoder + embed_tokens), fp16 + q4 quant | task-prompt → text/boxes/masks: caption, `<OCR>`, `<OD>`, `<REFERRING_EXPRESSION_SEGMENTATION>` | one 0.23–0.77B model = auto-tag, caption, OCR, **"select the X" mask** — huge suite value; needs ORT + tokenizer + decode loop | **High value, med-high cost — the ORT-fix justification** |
| PowerPaint https://github.com/open-mmlab/PowerPaint | code MIT; weights = SD-inpaint finetune (verify inherited SD license) | no single-ONNX; needs UNet+VAE+CLIP diffusion loop | text-guided inpaint/outpaint/remove | best-in-class generative erase | **Defer — needs sd_base completion or ORT diffusion graph** |
| BrushNet https://github.com/TencentARC/BrushNet | "Other"/Apache-ish (repo license non-standard; HF space apache-2.0) — **read before shipping** | same diffusion constraint | plug-in dual-branch for SD1.5 | as above | Defer + license-read |
| IC-Light v1 https://github.com/lllyasviel/IC-Light | code Apache-2.0; models are SD1.5-derived (OpenRAIL-M terms; v1's RMBG dep already swapped for commercial use per issue #65) | none packaged | foreground → relit composite | **relight = marquee feature** | Defer (diffusion); **IC-Light v2 explicitly non-commercial** |
| AnimeGAN/other style | mixed | — | — | — | skip until gallery proves out |

## 9. Video-editor-specific models

| Model | License | Format | I/O | Value | Verdict |
|---|---|---|---|---|---|
| **whisper.cpp** https://github.com/ggml-org/whisper.cpp | MIT code + MIT weights | ggml .bin (tiny 75MB/273MB → large; large-v3-turbo = sweet spot) | 16kHz mono float → segments, `-osrt/-ovtt` | **Already integrated** (`whisper_transcribe.cc`); add SRT export + Vulkan GPU offload (no toolkit needed) | **DONE — polish it** |
| **RIFE-ncnn-vulkan** https://github.com/nihui/rife-ncnn-vulkan | MIT (impl) + MIT (RIFE upstream) | ncnn | two frames → interpolated frame | optical-flow slow-mo / 24→60fps — headline NLE feature; multi-GPU `-g` flag | **Ship — med** (vendor the ncnn net + frame-pair loop; needs ncnn dependency) |
| RVM (video matting) | **GPL-3.0** | — | — | — | **❌** — per-frame BiRefNet instead |
| SAM 2.1 video | Apache-2.0 | ONNX partial | stateful tracking | roto-by-click across frames | Later — high integration cost |
| VitTrack (zoo) | zoo | ONNX cv::dnn | template track | point-tracking lite | **Low-med — pairs with smart-select** |

## 10. ggml / llama.cpp side — vision beyond whisper

llama.cpp's `libmtmd` (multimodal, merged 2025) takes image/**audio/video**
input via `model.gguf` + `mmproj.gguf` pairs — and ggml is already linked:

| Family | License | Notes |
|---|---|---|
| **SmolVLM / SmolVLM2** (HF, 256M/500M/2.2B, incl. **Video-Instruct** variants) | Apache-2.0 | pre-quantized GGUFs on `ggml-org`; tiny model doing OCR/caption — auto-metadata on CPU |
| LLaVA 1.5/1.6 | MIT-ish (model-dependent) | classic VLM, supported per `docs/multimodal/llava.md` |
| Gemma-3 4B-it GGUF | Gemma license (permissive w/ restrictions — read it) | bigger VLM option |
| **PaddleOCR-VL, GLM-OCR, DeepSeek-OCR** | mostly Apache-2.0 (verify per-model) | llama.cpp merged dedicated support PRs (2025) — text-in-image/subtitle-OCR via ggml |
| whisper (done §9) | MIT+MIT | — |

**Verdict:** **SmolVLM2-500M** behind a `libmtmd` binding = auto-caption /
auto-tag / OCR layer for the asset browser — no ONNX needed, reuses ggml
infra, second-cheapest "AI" feature after the ONNX drop-ins.

## 11. Fixing the onnxruntime stack (Windows)

| Route | Status (2026-11-22) | Detail |
|---|---|---|
| **MSYS2 package** | `mingw-w64-clang-x86_64-onnxruntime` **1.29.0-2** (built 2026-09-25), MIT | `pacman -S` on clang64 — rebuilt against current absl = **direct fix for the stale-DLL failure**. CPU EP only; no DirectML in the MinGW build (find_object.cc already documents `dml_provider_factory.h` absent). `python-onnxruntime` also packaged for tooling |
| vcpkg `onnxruntime` | port exists, **1.31.1** (renamed from `onnxruntime-gpu` per microsoft/vcpkg#47152) | `x64-windows` triplets ok, `!uwp`. Features: `cuda`, `tensorrt`, `openvino`, `kleidiai`, `coreml` — cuda feature pulls `cuda`, `cudnn`, `cudnn-frontend`, `nvidia-cutlass`. `onnxruntime-gpu` 1.23.2 remains as prebuilt-binary variant |
| **Prebuilt ORT dll via C API** | official GitHub/nuget `onnxruntime.dll` (MSVC-built, **ships DML + CUDA EPs**) | `OrtApi` is extern-"C" → LoadLibrary from clang64 works regardless of compiler. Delay-load already implemented in FindObject — **this is the only route to a GPU EP on this machine** (DML needs no toolkit; CUDA EP still needs toolkit-installed DLLs at runtime but no headers to consume) |
| cudnn | vcpkg port `cudnn` 7.6.5 = **system-finder**: Windows must manually install NVIDIA cuDNN first; empty-package wrapper | same caveat as Linux flow in AGENTS.md |
| onednn | vcpkg port `onednn` 3.12, x64, builds from source → `dnnl` cmake package | CPU MKL alternative for own-ML paths |
| intel-mkl | vcpkg port `intel-mkl` 2025.2 downloads Intel oneMKL installer (ilp64, dynamic CRT) | heavy; only if own-ML framework needs BLAS |
| ncnn | vcpkg port exists (`ncnn`); also trivially vendored | **the GPU inference dependency actually worth adding** |

## 12. Recommended ship order

| Wave | Feature | Model + runtime | Why |
|---|---|---|---|
| W1 | Background removal | u2netp + BiRefNet + isnet-int8 ONNX, cv::dnn | 3 permissive models, one runner class, instant marquee feature |
| W1 | Object removal | MI-GAN pipeline ONNX (dynamic-size) → LaMa-512 tiler | MIT/Apache, non-generative = predictable |
| W1 | Subtitles | whisper.cpp (in-tree) + SRT export | already built — finish UX |
| W2 | Smart select | EfficientSAM-Ti (zoo cv::dnn) + MobileSAM quant | click-select that works CPU-only today |
| W2 | 4× upscale | Real-ESRGAN ncnn-Vulkan + ONNX CPU fallback | **first GPU feature, no toolkit needed** |
| W2 | Detector swap | RF-DETR-N ONNX (replace AGPL YOLOv8n risk) + YuNet | Apache, cv::dnn-verified |
| W3 | Depth DOF | Depth-Anything V2 **Small** ONNX | Apache Small; DOF/re-layer features |
| W3 | Auto-tag/OCR/caption | SmolVLM2-500M via libmtmd/ggml | reuses linked ggml |
| W3 | Slow-mo | RIFE-ncnn-vulkan | MIT, video-editor differentiator |
| W4 | Florence-2 (needs ORT fixed) | onnx-community 4-file q4 | text-prompt segmentation + full auto-tagging — the ORT milestone's prize |
| Later | Generative inpaint / relight | PowerPaint / BrushNet / IC-Light on sd_base or ORT | engineering-heavy, weight licenses to read |
| ❌ | RMBG-1.4/2.0, RVM, MAT, SAM3, Ultralytics YOLO, IC-Light v2 | — | license-blocked (NC / GPL / bespoke SAM / AGPL) |

## 13. Verify-before-ship list (honest gaps)

- **Every "cv::dnn?" above marked *verify*** — only the YOLO 640² contract is
  proven on our actual OpenCV 5.0 build. First task: a `--diag` smoke test
  that readNet's each candidate ONNX and reports engine/node coverage.
- Whether MSYS2's opencv package was built with `ENGINE_ORT` — probably not;
  then ORT stays a separate probe path as today.
- LaMa fixed-512 → tile-and-stitch seam quality unproven (Carve notes
  dynamic-padding blockers in `irfft`; the fp32 export is the good one).
- Depth-Anything-Small dataset-provenance question (repo issues #81/#320)
  and MI-GAN's NVIDIA-NC teacher lineage — both flagged for a legal read.
- PowerPaint/BrushNet weight licenses (SD-derived) — code licenses don't
  cover weights; read the HF cards before any diffusion milestone.
- ncnn as a new dependency (vcpkg port or vendor) — small, BSD-3, but still
  a new dep decision; alternatively keep GPU models as **user-sidecar
  executables** (realesrgan/rife official .exe flow, zero deps).
- SAM prompt-encoder UX (point/box→mask tokens) is real work regardless of
  backend — same for all SAM variants.
