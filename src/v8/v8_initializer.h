#pragma once

namespace XER::V8Runtime {

void EnsureInitialized(int platform_workers);
void Shutdown();

}  // namespace XER::V8Runtime
