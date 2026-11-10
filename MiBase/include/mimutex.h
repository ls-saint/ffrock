#ifndef mimutex_h_
#define mimutex_h_
#include "mibase.h"

#ifdef __cplusplus
extern "C" {
#endif

	typedef struct mi_Mutex
	{
		void  (*Lock)(struct mi_Mutex* mutex);
		void  (*UnLock)(struct mi_Mutex* mutex);
	}mi_Mutex;

	struct mi_Mutex* mi_CreateMutex(int reentrant);
	void mi_DelMutex(struct mi_Mutex* mutex);

#ifdef __cplusplus
}
#endif

#endif
