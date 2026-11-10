#ifndef dv500warp_h_
#define dv500warp_h_
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include "sample_comm.h"

#ifdef __cplusplus
extern "C" {
#endif /* end of #ifdef __cplusplus */

hi_s32 sample_venc_normal(td_void);
void AppMppExit(void);

#ifdef __cplusplus
}
#endif /* end of #ifdef __cplusplus */

#endif
