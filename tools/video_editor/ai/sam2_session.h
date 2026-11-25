// SAM2.1-hiera-tiny session wrappers for the VideoEditor — the
// "Track Mask (SAM)" feature (docs/wiki/research/
// 2026-11-23-hf-org-scan.md §1, top-1 ADOPT: Apache-2.0, ungated).
// Two ONNX graphs from onnx-community/sam2.1-hiera-tiny-ONNX, staged
// under assets/models/sam21/ (external-data pair — the .onnx_data
// sidecars must sit next to their .onnx with upstream names):
//
//   vision_encoder.onnx (354 KB + 134 MB fp32 sidecar)
//     in  pixel_values      f32 [1,3,1024,1024] RGB stretched to the
//                           model square, /255 + ImageNet-normalized
//                           (preprocessor_config: mean .485/.456/.406
//                           std .229/.224/.225, size 1024x1024)
//     out image_embeddings.0 f32 [1,32,256,256]   (high-res feat 0)
//         image_embeddings.1 f32 [1,64,128,128]   (high-res feat 1)
//         image_embeddings.2 f32 [1,256,64,64]    (image embedding)
//
//   prompt_encoder_mask_decoder.onnx (213 KB + 21 MB fp32 sidecar)
//     in  input_points      f32 [1,1,N,2]  — point coords in the
//                           1024 encoder space (frame px scaled by
//                           1024/dim — SamImageProcessor's
//                           reshape_input_points), empty [1,1,0,2]
//                           for box-only prompts
//         input_labels      i64 [1,1,N]    — 1 fg / 0 bg / -1 pad;
//                           [1,M,0] (empty) when only boxes prompt
//         input_boxes       f32 [1,M,4]    — xyxy in encoder space;
//                           [1,0,4] empty when only points prompt.
//                           num_boxes must be 1 when both are fed.
//         image_embeddings.{0,1,2} verbatim from the encoder
//     out iou_scores           f32 [1,n,3]
//         pred_masks           f32 [1,n,3,256,256] logits (>=0 = mask)
//         object_score_logits  f32 [1,n,1]
//   (n = point_batch_size for point prompts, num_boxes for box
//   prompts — both are 1 in every prompt this wrapper builds.)
//
// The export is IMAGE-MODE: no SAM2 video-memory graphs exist in
// onnx-community, so tracking = per-frame re-prompt (ai/sam2_track.h).
// The two-stage split is the point: the encoder runs ONCE per probed
// frame (~150 ms CPU-class) while the decoder is milliseconds — so
// re-prompts and multi-prompt probing stay cheap per frame.
//
// Pure worker/model code like the rest of ai/ — sessions are
// thread-hostile; run on the scan worker, never the UI thread.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace XER::AI::Onnx {
class Session;
}

namespace XER::VideoEditor::Ai {

// Staged model paths under assets/models/ (the sam21/ subdirectory
// keeps the upstream external-data names verbatim — the .onnx
// hardcodes "vision_encoder.onnx_data" as its sidecar location).
inline constexpr const char* kSam2EncoderFile =
    "sam21/vision_encoder.onnx";
inline constexpr const char* kSam2DecoderFile =
    "sam21/prompt_encoder_mask_decoder.onnx";
// The export's fixed square encoder input edge.
inline constexpr int kSam2InputSize = 1024;
// The decoder's low-res mask output edge (pred_masks is [.,.,3,S,S]
// with S dynamic — 256 on the staged export; Decode() reads the
// real dims and Sam2Mask carries them).
inline constexpr int kSam2MaskSize = 256;

// Resolves the staged models — empty when the files are not
// installed (XER_AI_MODELS_DIR / assets/models / models).
std::string Sam2EncoderModel();
std::string Sam2DecoderModel();
const char* Sam2ModelHint();
// Both graphs resolvable — the feature gate the UI uses.
bool Sam2ModelsStaged();

// The encoder's three-tensor bundle, OWNED (TensorViews borrow the
// session — a tracking pass must keep the embedding alive across
// decoder runs while the encoder session gets reused for the next
// frame, so copying is required anyway).
struct Sam2Embedding {
    std::vector<float> feats[3];
    std::vector<std::int64_t> dims[3];   // declared output dims
    int src_w = 0;                       // frame px this embedding
    int src_h = 0;                       // was encoded from
    bool Valid() const {
        return src_w > 0 && src_h > 0 && !feats[0].empty() &&
               !feats[1].empty() && !feats[2].empty();
    }
};

// One prompt in FRAME PIXELS of the image the embedding was encoded
// from (the decoder wrapper scales into the 1024 encoder space via
// Sam2Embedding::src_w/h). Points pair with labels (1 fg / 0 bg);
// box is xyxy. At least one of {points, has_box} must be set —
// matching the transformers.js host contract, box+points requires
// exactly one box.
struct Sam2Prompt {
    std::vector<float> points;          // x,y pairs, frame px
    std::vector<std::int64_t> labels;   // same count as points/2
    bool has_box = false;
    float box[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // x1,y1,x2,y2 frame px
};

// One decoder output: the best-of-3 mask candidate's logits at the
// decoder's native mask resolution (256x256 on the staged export)
// plus the scores that chose it. `logits` is w*h f32; mask = >=0.
struct Sam2Mask {
    int w = 0;
    int h = 0;
    std::vector<float> logits;
    float iou = 0.0f;            // iou_scores of the chosen head
    float object_score = 0.0f;   // sigmoid(object_score_logits)
};

// ---- frame-space mask helpers (model-free, smoke-testable) ------

// Bounding box of the logit>=0 region as FRAME FRACTIONS (0..1 of
// the mask bitmap — the mask maps onto the frame's full extent).
// Returns false when no pixel is masked (empty mask => the caller
// treats the probe as a miss, never a zero-area box).
bool Sam2MaskBounds(const float* logits, int mw, int mh,
                    float* cx, float* cy, float* w, float* h);

// Centroid of the logit>=0 region, frame fractions. False when
// empty (same contract as Sam2MaskBounds).
bool Sam2MaskCentroid(const float* logits, int mw, int mh,
                      float* cx, float* cy);

// Resamples the mask onto a packed 1bpp grid (gw x gh cells,
// row-major, MSB-first, ceil(gw*gh/8) bytes) — ClipMaskSample::bits
// format. Box-averaged coverage >= 0.5 sets the bit.
std::vector<std::uint8_t> Sam2MaskBits(const float* logits, int mw,
                                       int mh, int gw, int gh);

// IoU of two packed bitmaps of identical dims. 0 on any mismatch.
float Sam2BitsIoU(const std::uint8_t* a, const std::uint8_t* b,
                  int gw, int gh);

// Reads one bit of a packed grid bitmap (row-major MSB-first).
inline bool Sam2BitAt(const std::uint8_t* bits, int gw, int gh,
                      int x, int y) {
    const std::size_t i =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(gw) +
        static_cast<std::size_t>(x);
    (void)gh;
    return (bits[i >> 3] >> (7 - (i & 7))) & 1u;
}

// ---- encoder session ---------------------------------------------
// One ONNX session = one encoder (thread-hostile). Encode() accepts
// packed BGRA8 frames of arbitrary size — internally stretched to
// the 1024 model square (default_to_square in the preprocessor
// config — the contract YuNet/RFDETR already rely on here).
class Sam2Encoder {
public:
    Sam2Encoder();
    ~Sam2Encoder();
    Sam2Encoder(const Sam2Encoder&) = delete;
    Sam2Encoder& operator=(const Sam2Encoder&) = delete;

    bool Load(const std::string& model_path,
              std::string* error_out = nullptr);
    bool IsLoaded() const;
    const std::string& BackendId() const;

    // BGRA8 -> the three embedding tensors (owned copy).
    // False with error_out when not loaded, the frame is degenerate,
    // or the runtime rejects the run.
    bool Encode(const std::uint8_t* bgra, int w, int h, int stride,
                Sam2Embedding* out, std::string* error_out = nullptr);

private:
    std::unique_ptr<XER::AI::Onnx::Session> session_;
    std::vector<float> nchw_;   // 3*1024*1024 scratch
};

// ---- decoder session ---------------------------------------------
// Stateless per call: embedding + prompt -> best-IoU mask logits.
// The embedding's tensors are fed verbatim (encoder output doubles
// as decoder input — the whole point of the two-graph export).
class Sam2Decoder {
public:
    Sam2Decoder();
    ~Sam2Decoder();
    Sam2Decoder(const Sam2Decoder&) = delete;
    Sam2Decoder& operator=(const Sam2Decoder&) = delete;

    bool Load(const std::string& model_path,
              std::string* error_out = nullptr);
    bool IsLoaded() const;
    const std::string& BackendId() const;

    // Runs the prompt decoder and returns the mask candidate with
    // the highest iou_scores head. Prompt coords are frame px of the
    // embedding's source image (scaled to the 1024 encoder space
    // inside). False with error_out when not loaded, the embedding/
    // prompt is degenerate, or the runtime rejects the run.
    bool Decode(const Sam2Embedding& emb, const Sam2Prompt& prompt,
                Sam2Mask* out, std::string* error_out = nullptr);

private:
    std::unique_ptr<XER::AI::Onnx::Session> session_;
};

}  // namespace XER::VideoEditor::Ai
