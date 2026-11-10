#ifndef misemaphore_h_
#define misemaphore_h_

#include "mibase.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MI_SEMAPHORE_OK              0
#define MI_SEMAPHORE_ERR_PARAM     -1
#define MI_SEMAPHORE_ERR_TIMEOUT   -2
#define MI_SEMAPHORE_ERR_SYSTEM    -3

typedef struct mi_Semaphore mi_Semaphore;

mi_Semaphore* mi_CreateSemaphore(int initial_count, int max_count);
void mi_DelSemaphore(mi_Semaphore* semaphore);
int mi_SemaphoreWait(mi_Semaphore* semaphore, uint32_t timeout_ms);
int mi_SemaphorePost(mi_Semaphore* semaphore, int count);

#ifdef __cplusplus
}
#endif

#endif
