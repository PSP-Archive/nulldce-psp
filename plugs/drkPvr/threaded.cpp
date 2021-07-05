#include <queue>

#include "threaded.h"

#include <stdio.h>
#include <stdlib.h>

#include "Renderer_if.h"
#include "ta.h"
#include "spg.h"

#include "plugins/plugin_manager.h"

#include "Protothread.h"

#include "me.h"

PSP_SECTION_START(sc_write)
PSP_SECTION_START(me_write)

// 
PSP_SECTION(sc_write)
    volatile bool threaded_pvr=false;

// 
PSP_SECTION(me_write)
   static volatile bool running=false;
   static volatile bool ta_working=false;
   volatile bool draw=true;

PSP_SECTION_END(sc_write)
PSP_SECTION_END(me_write)


#define TA_MAX_COUNT (1 << 16)

static u32 TA_cached [TA_MAX_COUNT][8]; 

static volatile  u32 TA_cachedIndex = 0;

#define vfpu_TA_cache_push(data) \
        __asm__("ulv.q C000, %1\n" "sv.q C000, %0" : "=m"(PSP_UC(TADMA_cached_addr[TADMA_queueI][0]))  : "m"(data[0])); \
        __asm__("ulv.q C000, %1\n" "sv.q C000, %0" : "=m"(PSP_UC(TADMA_cached_addr[TADMA_queueI][3]))  : "m"(data[4])); 

#define TA_cache_push(data) \
        for (int i = 0; i < 7; i++) PSP_UC(TA_cached[PSP_UC(TA_cachedIndex)][i]) = data[i];

#define dup4(x) x x x x  
#define dup8(x) dup4(x) dup4(x)  

using namespace TASplitter;

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

void threaded_TADma(u32* data,u32 size)
{
    if(threaded_pvr) {
        u32 * lda=data;
        u32   lsi=size;

        while(lsi)
        {
            TA_cache_push(lda);

             --lsi;
            lda+=8;
        }

       PSP_UC(TA_cachedIndex) = (PSP_UC(TA_cachedIndex) + size) % TA_MAX_COUNT;
        
    }else
    {
        libPvr_TaDMA(data,size);
    }

    u32 * end=data+size*8;
    while(data<=end)
    {
        threaded_ImmediateIRQ(data);
        data+=8;
    }
}

extern u64 time_pref;

void threaded_TASQ(u32* data)
{
    if(threaded_pvr)
    {
        TA_cache_push(data);

        PSP_UC(TA_cachedIndex)++;
    }
    else
    {
        //threaded_wait(true);
    	libPvr_TaSQ(data);
    }
    
    threaded_ImmediateIRQ(data);
}


int threaded_task(int data)
{
    u32 ta_cache_idx = 0;

	while(1)
	{

        const u32 currTA_index = PSP_UC(TA_cachedIndex); 

        if((currTA_index - ta_cache_idx) > 0)
        {
            //PSP_UC(ta_working)=true;

            u32 _sz = currTA_index-ta_cache_idx;

            libPvr_TaDMA(PSP_UCPTR(TA_cached[ta_cache_idx]),_sz);   

            //meUtilityDcacheWritebackInvalidateAll();

            //PSP_UC(ta_working)=false; 

            ta_cache_idx = (ta_cache_idx+_sz) % TA_MAX_COUNT;
        }
	}
}

void threaded_init()
{
	PSP_UC(running) = true;

    if (threaded_pvr){
        /*J_Init(false);
        J_EXECUTE_ME_ONCE(&threaded_task, 0);*/
    }
}

void threaded_term()
{
	PSP_UC(running) = false;
	//KILL_ME();
}