#include <iostream>

#include "flux/core/flux_config.h"
#include "flux/core/flux_core.h"

int main() {
    flux::core::FluxConfigFromEnvironment();

    flux::core::FluxCore core_flux;

    auto logger = core_flux.context().logger();

    logger.Info("Message", "Hello World!");

    return 0;
}