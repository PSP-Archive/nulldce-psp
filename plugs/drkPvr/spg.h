#pragma once
#include "drkPvr.h"

bool spg_Init();
void spg_Term();
void spg_Reset(bool Manual);

extern bool render_end_pending;


void FASTCALL spgUpdatePvr(u32 cycles);
bool spg_Init();
void spg_Term();
void spg_Reset(bool Manual);
void CalculateSync();
