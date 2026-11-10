/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

/* The global function contained in this file initializes SPL function
 * pointers, currently only for ARM platforms.
 *
 * Some code came from common/rtcd.c in the WebM project.
 */

#include "include/signal_processing_library.h"
#include "System/cpu_features.h"

/* Declare function pointers. */
MaxAbsValueW16 MiniRtcSpl_MaxAbsValueW16;
MaxAbsValueW32 MiniRtcSpl_MaxAbsValueW32;
MaxValueW16 MiniRtcSpl_MaxValueW16;
MaxValueW32 MiniRtcSpl_MaxValueW32;
MinValueW16 MiniRtcSpl_MinValueW16;
MinValueW32 MiniRtcSpl_MinValueW32;
CrossCorrelation MiniRtcSpl_CrossCorrelation;
DownsampleFast MiniRtcSpl_DownsampleFast;
ScaleAndAddVectorsWithRound MiniRtcSpl_ScaleAndAddVectorsWithRound;

#if (!defined(MINIRTC_HAS_NEON)) && !defined(MIPS32_LE)
/* Initialize function pointers to the generic C version. */
static void InitPointersToC(void) {
	MiniRtcSpl_MaxAbsValueW16 = MiniRtcSpl_MaxAbsValueW16C;
	MiniRtcSpl_MaxAbsValueW32 = MiniRtcSpl_MaxAbsValueW32C;
	MiniRtcSpl_MaxValueW16 = MiniRtcSpl_MaxValueW16C;
	MiniRtcSpl_MaxValueW32 = MiniRtcSpl_MaxValueW32C;
	MiniRtcSpl_MinValueW16 = MiniRtcSpl_MinValueW16C;
	MiniRtcSpl_MinValueW32 = MiniRtcSpl_MinValueW32C;
	MiniRtcSpl_CrossCorrelation = MiniRtcSpl_CrossCorrelationC;
	MiniRtcSpl_DownsampleFast = MiniRtcSpl_DownsampleFastC;
	MiniRtcSpl_ScaleAndAddVectorsWithRound =
		MiniRtcSpl_ScaleAndAddVectorsWithRoundC;
}
#endif

#if defined(MINIRTC_HAS_NEON)
/* Initialize function pointers to the Neon version. */
static void InitPointersToNeon(void) {
  MiniRtcSpl_MaxAbsValueW16 = MiniRtcSpl_MaxAbsValueW16Neon;
  MiniRtcSpl_MaxAbsValueW32 = MiniRtcSpl_MaxAbsValueW32Neon;
  MiniRtcSpl_MaxValueW16 = MiniRtcSpl_MaxValueW16Neon;
  MiniRtcSpl_MaxValueW32 = MiniRtcSpl_MaxValueW32Neon;
  MiniRtcSpl_MinValueW16 = MiniRtcSpl_MinValueW16Neon;
  MiniRtcSpl_MinValueW32 = MiniRtcSpl_MinValueW32Neon;
  MiniRtcSpl_CrossCorrelation = MiniRtcSpl_CrossCorrelationNeon;
  MiniRtcSpl_DownsampleFast = MiniRtcSpl_DownsampleFastNeon;
  MiniRtcSpl_ScaleAndAddVectorsWithRound =
	  MiniRtcSpl_ScaleAndAddVectorsWithRoundC;
}
#endif

#if defined(MIPS32_LE)
/* Initialize function pointers to the MIPS version. */
static void InitPointersToMIPS(void) {
	MiniRtcSpl_MaxAbsValueW16 = MiniRtcSpl_MaxAbsValueW16_mips;
	MiniRtcSpl_MaxValueW16 = MiniRtcSpl_MaxValueW16_mips;
	MiniRtcSpl_MaxValueW32 = MiniRtcSpl_MaxValueW32_mips;
	MiniRtcSpl_MinValueW16 = MiniRtcSpl_MinValueW16_mips;
	MiniRtcSpl_MinValueW32 = MiniRtcSpl_MinValueW32_mips;
	MiniRtcSpl_CrossCorrelation = MiniRtcSpl_CrossCorrelation_mips;
	MiniRtcSpl_DownsampleFast = MiniRtcSpl_DownsampleFast_mips;
#if defined(MIPS_DSP_R1_LE)
	MiniRtcSpl_MaxAbsValueW32 = MiniRtcSpl_MaxAbsValueW32_mips;
	MiniRtcSpl_ScaleAndAddVectorsWithRound =
		MiniRtcSpl_ScaleAndAddVectorsWithRound_mips;
#else
	MiniRtcSpl_MaxAbsValueW32 = MiniRtcSpl_MaxAbsValueW32C;
	MiniRtcSpl_ScaleAndAddVectorsWithRound =
		MiniRtcSpl_ScaleAndAddVectorsWithRoundC;
#endif
}
#endif

static void InitFunctionPointers(void) {
#if defined(MINIRTC_HAS_NEON)
  InitPointersToNeon();
#elif defined(MIPS32_LE)
  InitPointersToMIPS();
#else
  InitPointersToC();
#endif  /* WEBRTC_HAS_NEON */
}

#if defined(BASE_POSIX) || defined(__linux__) || defined(__unix__) || \
    defined(__APPLE__)
#include <pthread.h>

static void once(void (*func)(void)) {
  static pthread_once_t lock = PTHREAD_ONCE_INIT;
  pthread_once(&lock, func);
}

#elif defined(_WIN32)
#include <windows.h>

static void once(void (*func)(void)) {
  static volatile LONG lock = 0;
  static volatile LONG done = 0;

  while (InterlockedCompareExchange(&lock, 1, 0) != 0) {
    Sleep(0);
  }
  if (!done) {
    func();
    done = 1;
  }
  InterlockedExchange(&lock, 0);
}

#else
static void once(void (*func)(void)) {
  static int done = 0;
  if (!done) {
    func();
    done = 1;
  }
}
#endif  /* WEBRTC_POSIX */

void MiniRtcSpl_Init(void) {
  once(InitFunctionPointers);
}
