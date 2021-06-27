#pragma once
#include "EmptyAICA.h"
namespace HACK_AICA
{
u32 FASTCALL HACK_ReadMem_reg(u32 addr,u32 size);
void FASTCALL HACK_WriteMem_reg(u32 addr,u32 data,u32 size);
 
u32 HACK_ReadMem_ram(u32 addr,u32 size);
void HACK_WriteMem_ram(u32 addr,u32 data,u32 size);

void FASTCALL HACK_UpdateAICA(u32 Cycles);

#define AICA_MEM_SIZE (2*1024*1024)
#define AICA_MEM_MASK (AICA_MEM_SIZE-1)

void init_mem();
void term_mem();



extern u8 *aica_reg;
extern u8 *aica_ram;
}