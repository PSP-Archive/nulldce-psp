#include "mmu.h"
#include "dc/sh4/sh4_if.h"
#include "dc/sh4/ccn.h"
#include "dc/sh4/intc.h"
#include "dc/sh4/sh4_registers.h"
#include "plugins/plugin_manager.h"


#include "_vmem.h"

//SQ fast remap , mailny hackish , assumes 1 mb pages
//max 64 mb can be remapped on SQ
u32 sq_remap[64];

TLB_Entry UTLB[64];
TLB_Entry ITLB[4];

const u32 mmu_mask[4]= 
{
	((0xFFFFFFFF)>>10)<<10,	//1 kb page
	((0xFFFFFFFF)>>12)<<12,	//4 kb page
	((0xFFFFFFFF)>>16)<<16,	//64 kb page
	((0xFFFFFFFF)>>20)<<20	//1 MB page
};

//sync mem mapping to mmu , suspend compiled blocks if needed.entry is a UTLB entry # , -1 is for full sync
void UTLB_Sync(u32 entry)
{	

	if ((UTLB[entry].Address.VPN & (0xFC000000>>10)) == (0xE0000000>>10))
	{
		u32 vpn_sq=((UTLB[entry].Address.VPN & 0x7FFFF)>>10) &0x3F;//upper bits are allways known [0xE0/E1/E2/E3]
		sq_remap[vpn_sq]=UTLB[entry].Data.PPN<<10;
		printf("SQ remap %d : 0x%X to 0x%X\n",entry,UTLB[entry].Address.VPN<<10,UTLB[entry].Data.PPN<<10);
	}
	else
		printf("MEM remap %d : 0x%X to 0x%X\n",entry,UTLB[entry].Address.VPN<<10,UTLB[entry].Data.PPN<<10);
}
//sync mem mapping to mmu , suspend compiled blocks if needed.entry is a ITLB entry # , -1 is for full sync
void ITLB_Sync(u32 entry)
{
	printf("ITLB MEM remap %d : 0x%X to 0x%X\n",entry,ITLB[entry].Address.VPN<<10,ITLB[entry].Data.PPN<<10);
}

bool mmu_match(u32 va,CCN_PTEH_type Address,CCN_PTEL_type Data)
{
	if (Data.V==0)
		return false;

	u32 sz=Data.SZ1*2+Data.SZ0;
	u32 mask=mmu_mask[sz];

	if ( (((Address.VPN<<10)&mask)==(va&mask)) )
	{
		bool asid_match = (Data.SH==0) && ( (sr.MD==0) || (CCN_MMUCR.SV == 0) );

		if ( ( asid_match==false ) || (Address.ASID==CCN_PTEH.ASID) )
		{
			return true;
		}
	}

	return false;
}

void MMU_Init()
{

}

void MMU_Reset(bool Manual)
{
	memset(UTLB,0,sizeof(UTLB));
	memset(ITLB,0,sizeof(ITLB));
}

void MMU_Term()
{
}
