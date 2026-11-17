#pragma once

#include <v8.h>

namespace XER {
namespace flow_script_detail {

void ImportModuleCallback(const v8::FunctionCallbackInfo<v8::Value>& args);

}  // namespace flow_script_detail
}  // namespace XER
