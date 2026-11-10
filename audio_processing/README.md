# audio_processing

`audio_processing` is a standalone C audio-processing module extracted from
WebRTC-style AECM/NS code and adapted to the MiIot build layout.

## Features

- AECM mobile echo cancellation: `aecm/echo_control_mobile.h`
- Floating-point noise suppression: `ns/noise_suppression.h`
- Fixed-point noise suppression: `ns/noise_suppression_x.h`
- Shared SPL, FFT, ring buffer, delay estimator, and platform helpers

## NS And NSX

`NS` is the floating-point noise suppression path. It is suitable for PC/server
targets or embedded chips with a usable FPU and enough CPU budget.

`NSX` is the fixed-point noise suppression path. It is usually the better
default for small embedded targets and RTOS devices because CPU cost and power
use are more predictable.

## Build

```sh
make -C audio_processing
```

On Windows with Strawberry/MinGW, use:

```sh
gmake -C audio_processing
```

The default target builds `libaudio_processing.a` and copies it to:

```text
output/$(DevType)$(CpuType)lib/libaudio_processing.a
```

For local PC builds, unset `DevType` and `CpuType` place the library under
`output/lib/`.

The default build enables AECM, NS, and NSX:

```sh
make -C audio_processing ENABLE_AECM=1 ENABLE_NS=1 ENABLE_NSX=1
```

For a smaller embedded fixed-point build:

```sh
make -C audio_processing ENABLE_AECM=0 ENABLE_NS=0 ENABLE_NSX=1
```

## Include

Add these include paths when using the module:

```text
audio_processing
audio_processing/include
audio_processing/signal_processing/include
```

The module-level public header is:

```c
#include "mi_audio_processing.h"
```

When linking floating-point NS on Linux-like toolchains, add `-lm`.

## Processing Pipeline

The high-level wrapper is `mi_audio_processor_t`:

```c
mi_audio_processor_config_t cfg;
mi_audio_processor_t* ap = NULL;

mi_audio_processor_config_init(&cfg);
cfg.sample_rate_hz = 16000;
cfg.frame_samples = 160; /* 10 ms */
cfg.enable_aecm = 1;
cfg.ns_mode = MI_AUDIO_PROCESSOR_NS_FIXED;

mi_audio_processor_create(&cfg, &ap);

/* Optional: feed render/farend audio for echo cancellation. */
mi_audio_processor_buffer_farend(ap, farend_10ms, cfg.frame_samples);

/* nearend_10ms -> AECM -> NS/NSX -> output_10ms */
mi_audio_processor_process(ap, nearend_10ms, NULL, output_10ms,
                           cfg.frame_samples);

mi_audio_processor_destroy(ap);
```

`mi_audio_processor_process()` also accepts `farend` directly, in which case it
buffers that 10 ms render frame before processing the nearend frame.

## Notes

The default build uses the generic C implementations. NEON/MIPS optimized
source files are kept in the tree but are not enabled by the Makefile until the
target toolchain and matching SPL optimized dependencies are wired in.
