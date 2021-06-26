#pragma once
#include "types.h"
#include "dc/sh4/ccn.h"

struct TLB_Entry
{
	CCN_PTEH_type Address;
	CCN_PTEL_type Data;
	CCN_PTEA_type Assistance;
};

extern TLB_Entry UTLB[64];
extern TLB_Entry ITLB[4];
extern u32 sq_remap[64];

//These are working only for SQ remaps on ndce
void UTLB_Sync(u32 entry);
void ITLB_Sync(u32 entry);

void MMU_Init();
void MMU_Reset(bool Manual);
void MMU_Term();
bool mmu_match(u32 va,CCN_PTEH_type Address,CCN_PTEL_type Data);

#define mmu_TranslateSQW(addr) (sq_remap[(addr>>20)&0x3F] | (addr & 0xFFFE0))

static INLINE bool mmu_enabled()
{
	return false;
}
