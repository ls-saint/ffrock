#include "System/cpu_features.h"

static int MiniBase_GetCPUInfoNoASMImpl(CPUFeature feature) {
  (void)feature;
  return 0;
}

MiniBase_CPUInfo MiniBase_GetCPUInfo = MiniBase_GetCPUInfoNoASMImpl;
MiniBase_CPUInfo MiniBase_GetCPUInfoNoASM = MiniBase_GetCPUInfoNoASMImpl;

uint64_t MiniBase_GetCPUFeaturesARM(void) {
  return 0;
}
