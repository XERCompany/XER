// js_fx.h — JavaScript-backed effects & transitions for the editor.
//
// Scripts live in <assets>/scripts/fx/ next to a fx.json manifest:
//   fx.json     — UI catalog (id/title/param defs per kind); pure
//                 data, loaded without V8 so menus and smoke can read
//                 it anywhere.
//   *.js        — each file registers ONE descriptor via a global:
//                   xerFxVideo({ id, title, params, apply(frame, p, ctx) })
//                   xerFxAudio({ id, title, params, process(pcm, p, ctx) })
//                   xerFxTransition({ id, title, weight(progress, ctx) })
//   frame = { data: Uint8ClampedArray(BGRA), width, height, stride }
//   pcm   = Float32Array, interleaved stereo [-1, 1]
//   ctx   = { time, sample_rate, clip_id }   // clip_id = owning clip
// All buffer args are ZERO-COPY views over native memory — scripts
// mutate them in place.
//
// STATE: one registered fn object is shared by every clip that uses
// its id, so a script that keeps closure state (delay lines, filter
// memory) MUST key it by ctx.clip_id or clips bleed into each other.
//
// BUDGET: every script invocation runs under a time watchdog
// (shared helper thread -> Isolate::TerminateExecution). A script
// that overruns returns failure and the caller falls back to
// identity; the isolate stays usable for subsequent calls.
//
// THREADING: a JsFxRuntime owns a private JsEngine whose isolate is
// strictly single-threaded — create, use and destroy the runtime on
// ONE thread (compositor/mixer worker, export worker). The process-
// wide catalog (VideoEffects()/AudioEffects()/ScriptTransitions())
// is JSON-only; LoadCatalog publishes immutable snapshots under a
// shared_mutex, so the accessors are safe to call from anywhere.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace video { class Frame; }   // ::video::Frame — src/video/frame.h

namespace XER::VideoEditor {

class JsFxRuntime {
public:
    struct ParamDef {
        std::string id;
        std::string label;
        float def = 0.0f;
        float min = 0.0f;
        float max = 1.0f;
    };
    struct FxDesc {
        std::string id;
        std::string title;
        std::string file;                 // relative to FxDir()
        std::vector<ParamDef> params;
    };

    // Parses <fx_dir>/fx.json. Soft-fail: returns false + error text,
    // lists stay empty. Safe to call repeatedly.
    static bool LoadCatalog(const std::string& fx_dir,
                            std::string* error);
    static const std::vector<FxDesc>& VideoEffects();
    static const std::vector<FxDesc>& AudioEffects();
    static const std::vector<FxDesc>& ScriptTransitions();
    static const std::string& FxDir();

    JsFxRuntime();
    ~JsFxRuntime();
    JsFxRuntime(const JsFxRuntime&) = delete;
    JsFxRuntime& operator=(const JsFxRuntime&) = delete;

    // Lazily creates the private JsEngine on first call and evaluates
    // every file listed in the catalog. Idempotent; repeated calls are
    // cheap. Without V8 or a catalog returns false + error text — the
    // caller degrades to identity/warns once.
    bool EnsureLoaded(std::string* error);

    bool HasVideoEffect(const std::string& id) const;
    bool HasAudioEffect(const std::string& id) const;
    bool HasTransition(const std::string& id) const;

    // Runs apply() in place on a BGRA8 frame. `params` is positional —
    // index i maps to the manifest's params[i].id (defaults fill the
    // tail). `clip_id` is forwarded as ctx.clip_id so stateful
    // scripts can keep per-clip state. Returns false (frame
    // untouched) on missing effect, a JS exception or a watchdog
    // termination — callers warn-once.
    bool ApplyVideo(const std::string& id, video::Frame* frame,
                    const std::vector<float>& params, double time_s,
                    const std::string& clip_id, std::string* error);

    // Runs process() in place on interleaved stereo float samples.
    // `params` maps manifest-param ORDINAL -> value (ClipAudioFx
    // positional convention). `clip_id` -> ctx.clip_id.
    // On Windows the call first tries the XERFxScript.exe subprocess
    // (a runaway script kills the worker, not the editor); a transport
    // failure falls back to this runtime's in-process isolate, while
    // an answered error is authoritative.
    bool ProcessAudio(const std::string& id, float* interleaved,
                      int frames, int sample_rate,
                      const std::map<std::uint32_t, float>& params,
                      double time_s, const std::string& clip_id,
                      std::string* error);

    // weight(progress) -> clamped [0,1]. Missing/failed script falls
    // back to linear progress — the transition still renders.
    double TransitionWeight(const std::string& id, double progress);

    // Validates arbitrary script source: compiles and runs it in this
    // runtime under the script-load budget (a file-scope while(1){}
    // is terminated, not hung on) — true only when it registered at
    // least one new effect/transition descriptor. The XERFxScript
    // worker uses this for its fx_validate-by-source requests.
    bool ValidateSource(const std::string& source, std::string* error);

private:
    // Lazily creates the JsEngine and installs the xerFx* registration
    // globals — WITHOUT evaluating the catalog (ValidateSource needs
    // engine+globals but no catalog). Called by EnsureLoaded.
    bool EnsureEngineReady(std::string* error);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace XER::VideoEditor
