#include "opt_instruction_set.h"

#if defined(__AVX512F__)
  #define XER_HAS_AVX512 1
#elif defined(__AVX2__)
  #define XER_HAS_AVX2 1
#elif defined(__SSE4_2__)
  #define XER_HAS_SSE42 1
#elif defined(__ARM_NEON)
  #define XER_HAS_NEON 1
#endif

namespace XER::ModelsBuilder::Optimization {

OptInstructionSet::ISA OptInstructionSet::Detect() {
#if defined(XER_HAS_AVX512)
  return ISA::AVX512;
#elif defined(XER_HAS_AVX2)
  return ISA::AVX2;
#elif defined(XER_HAS_SSE42)
  return ISA::SSE42;
#elif defined(XER_HAS_NEON)
  return ISA::NEON;
#else
  return ISA::Generic;
#endif
}

std::string OptInstructionSet::ToName(ISA isa) {
  switch (isa) {
    case ISA::AVX512:  return "AVX-512";
    case ISA::AVX2:    return "AVX2";
    case ISA::SSE42:   return "SSE4.2";
    case ISA::NEON:    return "NEON";
    default:           return "Generic";
  }
}

}  // namespace XER::ModelsBuilder::Optimization
