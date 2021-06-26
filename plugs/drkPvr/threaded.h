#pragma once

#include "drkPvr.h"


#define end_list (1<<2)
#define fill_data (1<<3) 

extern volatile bool threaded_pvr;
extern volatile bool draw;
extern volatile u8   doInterrupt;
extern volatile u32 threaded_CurrentList;

void threaded_wait(bool wait_ta_working);
void threaded_call(void (*call)());
void threaded_TADma(u32* data,u32 size);
void threaded_TASQ(u32* data);
void threaded_init();
void threaded_term();