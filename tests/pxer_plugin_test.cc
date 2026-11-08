#include "native/plugin/pxer/pxer_host.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <string>

using namespace XER::Native::Plugin::Pxer;

void test_plugin_unload_and_conflict() {
    PluginHost& host = PluginHost::Shared();
    std::string error;
    PluginSummary summary1, summary2;

    // 1. Load first plugin
    bool ok1 = host.Load("plugins/build/libpxer_example_plugin.so", &summary1, &error);
    assert(ok1 && "First plugin should load");
    std::cout << "Loaded plugin1: " << summary1.id << std::endl;

    // 2. Check extensions registered
    auto jsmods1 = host.ListJsModulesByInstance(summary1.instance_id);
    assert(!jsmods1.empty() && "Plugin1 should register JS modules");

    // 3. Unload first plugin
    bool un1 = host.Unload(summary1.instance_id, &error);
    assert(un1 && "First plugin should unload");
    std::cout << "Unloaded plugin1\n";

    // 4. Ensure extensions are gone
    auto jsmods1_after = host.ListJsModulesByInstance(summary1.instance_id);
    assert(jsmods1_after.empty() && "Extensions should be removed after unload");

    // 5. Load second plugin (simulate different .so with same module names)
    bool ok2 = host.Load("plugins/build/libpxer_example_plugin.so", &summary2, &error);
    assert(ok2 && "Second plugin should load");
    std::cout << "Loaded plugin2: " << summary2.id << std::endl;

    // 6. Register same JS module name (should succeed, no conflict)
    auto jsmods2 = host.ListJsModulesByInstance(summary2.instance_id);
    assert(!jsmods2.empty() && "Plugin2 should register JS modules");

    // 7. Unload second plugin
    bool un2 = host.Unload(summary2.instance_id, &error);
    assert(un2 && "Second plugin should unload");
    std::cout << "Unloaded plugin2\n";
}

int main() {
    test_plugin_unload_and_conflict();
    std::cout << "PXER plugin unload/conflict test passed.\n";
    return 0;
}
