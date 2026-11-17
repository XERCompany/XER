#pragma once

#include <v8.h>

#include "javascript/bridge/function_bridge.h"
#include "javascript/engine/runtime_context.h"
#include "javascript/events/event_router.h"
#include "javascript/pipeline/pipeline_executor.h"

namespace XER {
namespace flow_script_detail {

struct JsEventBusState {
    engine::javascript::runtime::RuntimeContext runtime;
    engine::javascript::bridge::FunctionBridge bridge{&runtime};
    engine::javascript::events::EventRouter router{&bridge};
    engine::javascript::pipeline::PipelineExecutor pipeline{&router};
};

bool BindEventBus(v8::Isolate* isolate, v8::Local<v8::Context> context, JsEventBusState* state);

}  // namespace flow_script_detail
}  // namespace XER
