// fx_script_server.cc — XERFxScript's request handlers: the sandboxed
// JS/V8 script-fx engine runs in this worker process so a throwing or
// runaway script kills the WORKER, never the editor (the in-worker
// watchdog terminates over-budget calls first; a hard crash still
// only costs a respawn).
//
// Served kinds (media_worker_protocol.h):
//   fx_list       — parse the fx.json catalog at `dir` (JSON only —
//                   no V8 needed) and echo the descriptor lists.
//   fx_validate   — `src` compiles+registers an effect, or `fx_id` is
//                   present in the catalog-loaded runtime.
//   fx_audio      — run a catalog audio fx on a base64 f32 PCM block
//                   (stereo interleaved), echo the mutated block.
//   fx_transition — weight(progress) scalar for a script transition.
//
// The JsEngine/V8 platform is initialized lazily inside the first
// JsFxRuntime::EnsureLoaded call (JsEngine::Create's std::call_once) —
// the worker keeps ONE runtime per catalog dir and reloads when `dir`
// changes, so per-clip script state (ctx.clip_id) behaves exactly as
// in-process.
#include "media_worker_server.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "js_fx.h"
#include "media_worker_protocol.h"

namespace XER::VideoEditor {
namespace {

MediaWorker::FxListItem ToItem(const JsFxRuntime::FxDesc& d) {
    MediaWorker::FxListItem item;
    item.id = d.id;
    item.title = d.title;
    item.file = d.file;
    item.params.reserve(d.params.size());
    for (const auto& p : d.params) {
        MediaWorker::FxParamItem def;
        def.id = p.id;
        def.label = p.label;
        def.def = p.def;
        def.min = p.min;
        def.max = p.max;
        item.params.push_back(def);
    }
    return item;
}

MediaWorker::FxListReply CollectCatalog() {
    MediaWorker::FxListReply list;
    for (const auto& d : JsFxRuntime::VideoEffects()) {
        list.video.push_back(ToItem(d));
    }
    for (const auto& d : JsFxRuntime::AudioEffects()) {
        list.audio.push_back(ToItem(d));
    }
    for (const auto& d : JsFxRuntime::ScriptTransitions()) {
        list.transitions.push_back(ToItem(d));
    }
    return list;
}

// One cached runtime per catalog dir — per-clip JS state must survive
// across fx_audio calls on the same dir.
struct FxEngine {
    std::string dir;
    std::unique_ptr<JsFxRuntime> runtime;
};

JsFxRuntime* EnsureRuntime(FxEngine& engine, const std::string& dir,
                           std::string* error) {
    if (dir.empty()) {
        if (error) *error = "fx request missing dir";
        return nullptr;
    }
    if (dir != engine.dir) {
        engine.runtime.reset();
        engine.dir.clear();
        std::string load_error;
        if (!JsFxRuntime::LoadCatalog(dir, &load_error)) {
            if (error) *error = load_error;
            return nullptr;
        }
        engine.dir = dir;
    }
    if (!engine.runtime) {
        auto runtime = std::make_unique<JsFxRuntime>();
        std::string ensure_error;
        if (!runtime->EnsureLoaded(&ensure_error)) {
            if (error) *error = ensure_error;
            return nullptr;
        }
        engine.runtime = std::move(runtime);
    }
    return engine.runtime.get();
}

std::string HandleFxList(std::uint64_t id,
                         const MediaWorker::FxRequest& req) {
    std::string error;
    if (req.dir.empty() ||
        !JsFxRuntime::LoadCatalog(req.dir, &error)) {
        return MediaWorker::EncodeErrorReply(
            id, error.empty() ? "fx catalog load failed" : error);
    }
    return MediaWorker::EncodeFxListReply(id, CollectCatalog());
}

std::string HandleFxValidate(FxEngine& engine, std::uint64_t id,
                             const MediaWorker::FxRequest& req) {
    if (!req.source.empty()) {
        // Compile+run in a scratch runtime — catalog not needed: the
        // source itself registers the descriptor being validated.
        JsFxRuntime scratch;
        std::string error;
        const bool valid = scratch.ValidateSource(req.source, &error);
        return MediaWorker::EncodeFxValidateReply(id, valid, error);
    }
    if (!req.fx_id.empty()) {
        std::string error;
        JsFxRuntime* runtime = EnsureRuntime(engine, req.dir, &error);
        if (runtime == nullptr) {
            return MediaWorker::EncodeErrorReply(id, error);
        }
        const bool valid = runtime->HasVideoEffect(req.fx_id) ||
                           runtime->HasAudioEffect(req.fx_id) ||
                           runtime->HasTransition(req.fx_id);
        return MediaWorker::EncodeFxValidateReply(
            id, valid,
            valid ? std::string() : "effect not registered");
    }
    return MediaWorker::EncodeErrorReply(
        id, "fx_validate needs src or fx_id");
}

std::string HandleFxAudio(FxEngine& engine, std::uint64_t id,
                          const MediaWorker::FxRequest& req) {
    std::string error;
    if (req.fx_id.empty() || req.frames <= 0 || req.channels != 2 ||
        req.sample_rate <= 0 ||
        req.pcm.size() != static_cast<std::size_t>(req.frames) *
                              req.channels) {
        return MediaWorker::EncodeErrorReply(
            id, "fx_audio: malformed pcm block (stereo f32 required)");
    }
    JsFxRuntime* runtime = EnsureRuntime(engine, req.dir, &error);
    if (runtime == nullptr) {
        return MediaWorker::EncodeErrorReply(id, error);
    }
    std::vector<float> pcm = req.pcm;   // process() mutates in place
    if (!runtime->ProcessAudio(req.fx_id, pcm.data(), req.frames,
                               req.sample_rate, req.params, req.time_s,
                               req.clip_id, &error)) {
        return MediaWorker::EncodeErrorReply(
            id, error.empty() ? "fx_audio failed" : error);
    }
    return MediaWorker::EncodeFxAudioReply(id, pcm.data(), pcm.size());
}

std::string HandleFxTransition(FxEngine& engine, std::uint64_t id,
                               const MediaWorker::FxRequest& req) {
    std::string error;
    if (req.fx_id.empty()) {
        return MediaWorker::EncodeErrorReply(
            id, "fx_transition needs fx_id");
    }
    JsFxRuntime* runtime = EnsureRuntime(engine, req.dir, &error);
    if (runtime == nullptr) {
        return MediaWorker::EncodeErrorReply(id, error);
    }
    return MediaWorker::EncodeFxTransitionReply(
        id, runtime->TransitionWeight(req.fx_id, req.progress));
}

}  // namespace

int RunFxScriptWorker(const char* role) {
    // The engine cache lives for the whole loop (and the V8 isolate
    // inside it for the whole process).
    FxEngine engine;
    return RunJsonWorkerLoop(
        role, "fx_list,fx_validate,fx_audio,fx_transition,ping,info",
        [&engine](const MediaWorker::Request& req,
                  const std::string& line) -> std::string {
            std::uint64_t id = 0;
            MediaWorker::FxRequest fx;
            if (!MediaWorker::DecodeFxRequest(line, &id, &fx)) {
                return MediaWorker::EncodeErrorReply(
                    req.id, "malformed fx request");
            }
            switch (req.kind) {
                case MediaWorker::Kind::kFxList:
                    return HandleFxList(req.id, fx);
                case MediaWorker::Kind::kFxValidate:
                    return HandleFxValidate(engine, req.id, fx);
                case MediaWorker::Kind::kFxAudio:
                    return HandleFxAudio(engine, req.id, fx);
                case MediaWorker::Kind::kFxTransition:
                    return HandleFxTransition(engine, req.id, fx);
                default:
                    return MediaWorker::EncodeErrorReply(
                        req.id, "kind not implemented in this worker "
                                "role");
            }
        });
}

}  // namespace XER::VideoEditor
