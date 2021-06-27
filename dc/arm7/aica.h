#pragma once
#include "vbaARM.h"
#include "assert.h"

void FASTCALL UpdateARM(u32 Cycles);
void FASTCALL ArmInterruptChange(u32 bits,u32 L);

//u32 ReadAicaReg(u32 reg);
void WriteAicaReg8(u32 reg,u32 data);
extern bool e68k_out;

template<u32 sz>
void WriteAicaReg(u32 reg,u32 data);