#include <nan.h>

NAN_MODULE_INIT(Init) {
    Nan::Set(exports, Nan::New("add").ToLocalChecked(),
        Nan::New<v8::FunctionTemplate>(Add).GetFunction(Nan::GetCurrentContext()).ToLocalChecked());
}

NODE_MODULE(NODE_GYP_MODULE_NAME, Init)