// VST3 params — parameter grouping/metadata for the FX inspector,
// plus a tiny k=v;k=v preset string for copy/paste.
//
// Where the names come from (honest capability map):
//  * Scan metadata (PluginInfo == Vst3PluginInfo) carries only
//    path/name/vendor/category — NO parameter list. Describing a
//    scanned-but-not-loaded plugin yields nothing.
//  * IPluginInstance exposes no parameter enumeration either — that
//    is the interface gap. A Vst3PluginInstance, however, keeps its
//    left-channel Vst3HostInterface reachable via PrimaryHost(), so
//    DescribeParams(instance) returns REAL ids, names and normalized
//    values there; any other implementation (test fakes, future
//    wrappers) yields {}.
//  * The inspector's offline fallback is ClipAudioFx.params (id ->
//    normalized value); DescribeStoredParams synthesizes "Param <id>"
//    names, and SetParamByName resolves that same synthetic form so
//    stored presets always round-trip even without enumeration.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "audio/plugin_wrappers/vst3_plugin_scanner.h"  // Vst3PluginInfo
#include "timeline_model.h"                            // ClipAudioFx
#include "vst/plugin_instance.h"                       // IPluginInstance

namespace XER::VideoEditor::Vst {

// What the plugin picker hands the inspector (scanner output).
using PluginInfo = XER::Audio::Plugin::Vst3PluginInfo;

// One row of the inspector's parameter table. Values are VST3
// normalized [0,1], matching IPluginInstance::SetParameter.
struct Vst3ParamInfo {
    int id = 0;
    std::string name;         // plugin name, or synthetic "Param <id>"
    float norm_value = 0.0f;  // normalized current value
};

// Scan-metadata view: Vst3PluginInfo carries no parameter
// information, so this always returns {} — kept for signature
// symmetry; the inspector should describe a live instance or the
// stored spec instead (documented gap).
std::vector<Vst3ParamInfo> DescribeParams(const PluginInfo& info);

// Live-instance enumeration. For Vst3PluginInstance this reads the
// primary host's parameter list — real ids, real names, live
// normalized values (GetParameter per id, falling back to the
// reported current value). Other IPluginInstance implementations
// expose no parameter metadata through the interface and yield {}.
std::vector<Vst3ParamInfo> DescribeParams(IPluginInstance* instance);

// Stored-spec fallback: one row per ClipAudioFx.params entry (sorted
// by id, the map's natural order) with synthesized "Param <id>"
// names — the same names SetParamByName resolves.
std::vector<Vst3ParamInfo> DescribeStoredParams(const ClipAudioFx& fx);

// Sets a normalized value, resolving `name` to a param id:
//  1. exact/case-insensitive match against DescribeParams(instance)
//     when the instance enumerates (real plugin names);
//  2. the synthetic form "Param <id>" (case-insensitive, also
//     "param<id>" / "Param_<id>") and bare decimal ids, which work on
//     ANY IPluginInstance — that's how stored presets apply to
//     instances that cannot enumerate.
// Value is clamped to [0,1]. False on no resolution, null instance,
// or SetParameter failure.
bool SetParamByName(IPluginInstance* instance, const std::string& name,
                    float norm_value);

// ------------------------------------------------------------------
// Preset copy/paste: "name=value;name=value"
// ------------------------------------------------------------------

// Serializes a param table; empty names fall back to "Param <id>".
// ';' and '=' inside names are flattened to '_' so the output always
// re-parses. Values use the shortest round-trip float form.
std::string FormatParamsString(const std::vector<Vst3ParamInfo>& params);

// Lenient inverse: ';' separated "name=value" pairs, whitespace
// trimmed; tokens without '=', empty names, or non-numeric values are
// skipped (a paste typo must never kill the preset). Pairs arrive in
// text order.
std::vector<std::pair<std::string, float>> ParseParamsString(
    const std::string& text);

// Parse + SetParamByName for every pair; returns how many applied.
std::size_t ApplyParamsString(IPluginInstance* instance,
                              const std::string& text);

}  // namespace XER::VideoEditor::Vst
