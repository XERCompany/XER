// xer.OpenAI namespace — OpenAI-compatible server bindings.

#include "util.h"

namespace xer::addon::openai {
void RegisterServer(v8::Local<v8::Object> exports);
}

namespace xer::addon {

void InitOpenAI(v8::Local<v8::Object> exports) {
    openai::RegisterServer(exports);
}

}  // namespace xer::addon
