#ifndef SocImp_H_
#define SocImp_H_

#ifdef WIN32
#include <Windows.h>
#endif

#include "../IHard.h"

#ifdef __cplusplus
extern "C" 
{
#endif
    IHardSoc* CreateHardImp();
#ifdef __cplusplus
}
#endif
#endif
