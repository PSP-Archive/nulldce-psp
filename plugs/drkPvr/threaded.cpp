#include <queue>

#include "threaded.h"

#include <stdio.h>
#include <stdlib.h>

#include "Renderer_if.h"
#include "ta.h"
#include "spg.h"

#include "plugins/plugin_manager.h"

static volatile  u32 TA_cachedIndex = 0;

#define vfpu_TADMA_cache_push(data) \
        { \
        const u32 idx = PSP_UC(TA_cachedIndex); \
            __asm__("ulv.q C000, %1\n" "sv.q C000, %0" : "=m"(TA_cached[idx])  : "m"(data[0])); \
            __asm__("ulv.q C000, %1\n" "sv.q C000, %0" : "=m"(TA_cached[idx+4])  : "m"(data[4]));\
            PSP_UC(TA_cachedIndex) = (idx + 8) & TA_MAX_COUNT_MASK; \
        }

using namespace TASplitter;

u32 mutex = 0;

volatile u32 threaded_CurrentList=ListType_None; 

// send "Job done" irq immediately, and let the pvr thread handle it when it can :)
bool threaded_ImmediateIRQ(u32 * data)
{
    Ta_Dma * td = (Ta_Dma*) data;

    switch (td->pcw.ParaType)
    {
        case ParamType_End_Of_List:

            if (threaded_CurrentList==ListType_None)
                threaded_CurrentList=td->pcw.ListType;
            
//            printf("RaiseInterrupt %d\n",threaded_CurrentList);
            params.RaiseInterrupt(ListEndInterrupt[threaded_CurrentList]);

			threaded_CurrentList=ListType_None;
            break;

        case ParamType_Sprite:
        case ParamType_Polygon_or_Modifier_Volume:

            if (threaded_CurrentList==ListType_None)
                threaded_CurrentList=td->pcw.ListType;
            
            break;
    }

    return true;
}

extern "C" void test_and_set(u32 * m);
extern "C" void unlock_mutx(u32 * m);

static void ME_mutex_lock(u32 * m){
    test_and_set(m);
}

static void ME_mutex_unlock(u32 * m){
    unlock_mutx(m);
}

void threaded_TADma(u32* data,u32 size)
{
}

extern u64 time_pref;

void threaded_TASQ(u32* data)
{
}


int threaded_task(int data)
{
    return 0;
}

void threaded_init()
{
    /*ME_Init();
    InitFunction(threaded_task, 0);
    StartFunction();*/
}

void threaded_term()
{

}