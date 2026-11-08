#pragma once

#include <string>

namespace XER::ModelsBuilder::Optimization {

/// @brief Selects ISA-level code paths (AVX2, AVX-512, NEON, etc.)
class OptInstructionSet {
 public:
  enum class ISA { Generic, SSE42, AVX2, AVX512, NEON };

  static ISA Detect();
  static std::string ToName(ISA isa);
};

}  // namespace XER::ModelsBuilder::Optimization
