#include "model_quantizer.h"

#include "model_cloner.h"

namespace XER::ModelsBuilder::Operations {

std::shared_ptr<Core::Model> ModelQuantizer::Quantize(const Core::Model& model,
                                                      QuantizationMode mode,
                                                      QuantizationReport* report) const {
  ModelCloner cloner;
  auto quantized = cloner.Clone(model, "_quantized");

  if (report != nullptr) {
    report->mode = mode;
    report->original_bytes_estimate = model.GetLayerCount() * 4096U;

    switch (mode) {
      case QuantizationMode::Float16:
        report->quantized_bytes_estimate = report->original_bytes_estimate / 2U;
        break;
      case QuantizationMode::Int8:
        report->quantized_bytes_estimate = report->original_bytes_estimate / 4U;
        break;
      case QuantizationMode::None:
      default:
        report->quantized_bytes_estimate = report->original_bytes_estimate;
        break;
    }
  }

  return quantized;
}

}  // namespace XER::ModelsBuilder::Operations
