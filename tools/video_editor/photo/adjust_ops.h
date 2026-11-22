// adjust_ops.h — the M4 adjustment-layer op registry. Each op maps a
// name to a mutator over a bridge-packed u32 buffer
// (R<<24|G<<16|B<<8|A, straight alpha), implemented on the existing
// XER::Bridge::Skia adjust suite (AdjustHSL/AdjustLevels/
// AdjustCurves/AdjustExposure/AdjustColorBalance/AdjustVibrance/...).
//
// Params travel inside AdjustmentSpec as an ordered name/value list;
// each op declares its own schema (ordered param names with defaults
// and [min,max] ranges) so the workspace can drive a minimal param
// editor and ApplyAdjustment can clamp/validate. Missing params fall
// back to the op's defaults — an all-default spec must be a no-op for
// the identity-preserving ops where a sane identity exists.
#pragma once

#include <string>
#include <vector>

#include "photo/document.h"

namespace XER::VideoEditor::Photo {

// One op's declaration: name (the serialized id), a UI label, and the
// ordered param schema — AdjustmentSpec::params entries match by
// name; missing entries take `defaults`.
struct AdjustmentOpInfo {
    std::string name;
    std::string label;
    std::vector<AdjustmentParam> defaults;      // ordered
    std::vector<std::pair<double, double>> ranges;  // per-param min,max
};

// The registry — order is the UI's add-layer/Select order.
const std::vector<AdjustmentOpInfo>& AdjustmentOpRegistry();

// nullptr for unknown names.
const AdjustmentOpInfo* FindAdjustmentOp(const std::string& name);

// Ordered display names (labels) for UI Selects — index-aligned with
// AdjustmentOpRegistry().
std::vector<std::string> AdjustmentOpLabels();

// An AdjustmentSpec filled with the op's default params — what a new
// adjustment layer starts from.
AdjustmentSpec DefaultAdjustmentSpec(const std::string& op_name);

// Serializes `spec.params` as "name=value;name=value" — the params
// TextInput's display format. Values print %g.
std::string AdjustmentParamsToText(const AdjustmentSpec& spec);

// Inverse of AdjustmentParamsToText: parses "name=value;..." pairs
// and overwrites the matching entries of `spec` (unknown names and
// malformed pairs are ignored — the editor stays forgiving).
// Returns false when nothing parsed.
bool AdjustmentParamsFromText(const std::string& text,
                              AdjustmentSpec* spec);

// Runs the op on `px` (w x h bridge-packed) in place. Params are
// resolved against the op's defaults and clamped to the declared
// ranges. Returns false on an unknown op, an empty spec, or a bad
// buffer — callers treat false as "leave the composite untouched".
bool ApplyAdjustment(const AdjustmentSpec& spec,
                     std::vector<uint32_t>* px, int w, int h);

}  // namespace XER::VideoEditor::Photo
