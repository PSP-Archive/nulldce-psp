//gah , ccn emulation
//CCN: Cache and TLB controller

#include "types.h"
#include "dc/mem/sh4_internal_reg.h"
#include "dc/pvr/pvr_if.h"
#include "dc/sh4/sh4_if.h"
#include "plugins/plugin_manager.h"
#include "ccn.h"
#include "dc/mem/mmu.h"
#include "sh4_registers.h"


//Types
CCN_PTEH_type CCN_PTEH;
CCN_PTEL_type CCN_PTEL;
u32 CCN_TTB;
u32 CCN_TEA;
CCN_MMUCR_type CCN_MMUCR;
u8  CCN_BASRA;
u8  CCN_BASRB;
CCN_CCR_type CCN_CCR;
u32 CCN_TRA;
u32 CCN_EXPEVT;
u32 CCN_INTEVT;
CCN_PTEA_type CCN_PTEA;
CCN_QACR_type CCN_QACR[2];
u32 		  CCN_QACR_TR[2];

template<u32 idx>
void CCN_QACR_write(u32 value)
{
	
	CCN_QACR[idx].reg_data=value;
	CCN_QACR_TR[idx]=(CCN_QACR[idx].Area<<26)-0xE0000000;

	u32 area = ((CCN_QACR_type&)value).Area;

	switch(area)
	{
		case 3: 
			do_sqw_nommu=&do_sqw_nommu_area_3_nonvmem;
		break;

		/*case 4:
			do_sqw_nommu=&TAWriteSQ_nommu;
			break;*/
		default: do_sqw_nommu=&do_sqw_nommu_full;
	}
}

void CCN_MMUCR_write(u32 value)
{
	CCN_MMUCR_type temp;
	temp.reg_data=value;

	//const bool mmu_changed_state = temp.AT != CCN_MMUCR.AT;
	
	if (temp.TI)
	{
		for (u32 i = 0; i < 4; i++)
			ITLB[i].Data.V = 0;

		for (u32 i = 0; i < 64; i++)
			UTLB[i].Data.V = 0;

		temp.TI=0;
	}

	CCN_MMUCR=temp;
}
void CCN_CCR_write(u32 value)
{
	CCN_CCR_type temp;
	temp.reg_data=value;

	if (temp.ICI && curr_pc!=0xAC13DBF8)
	{
		temp.ICI = 0;
		//printf("Sh4: i-cache invalidation %08X\n",curr_pc);
		sh4_cpu.ResetCache();
		
	}

	temp.OCI = 0;
	
	CCN_CCR=temp;
}
//Init/Res/Term
void ccn_Init()
{

	do_sqw_nommu=&do_sqw_nommu_full;

	//CCN PTEH 0xFF000000 0x1F000000 32 Undefined Undefined Held Held Iclk
	CCN[(CCN_PTEH_addr&0xFF)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	CCN[(CCN_PTEH_addr&0xFF)>>2].readFunction=0;
	CCN[(CCN_PTEH_addr&0xFF)>>2].writeFunction=0;
	CCN[(CCN_PTEH_addr&0xFF)>>2].data32=&CCN_PTEH.reg_data;

	//CCN PTEL 0xFF000004 0x1F000004 32 Undefined Undefined Held Held Iclk
	CCN[(CCN_PTEL_addr&0xFF)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	CCN[(CCN_PTEL_addr&0xFF)>>2].readFunction=0;
	CCN[(CCN_PTEL_addr&0xFF)>>2].writeFunction=0;
	CCN[(CCN_PTEL_addr&0xFF)>>2].data32=&CCN_PTEL.reg_data;

	//CCN TTB 0xFF000008 0x1F000008 32 Undefined Undefined Held Held Iclk
	CCN[(CCN_TTB_addr&0xFF)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	CCN[(CCN_TTB_addr&0xFF)>>2].readFunction=0;
	CCN[(CCN_TTB_addr&0xFF)>>2].writeFunction=0;
	CCN[(CCN_TTB_addr&0xFF)>>2].data32=&CCN_TTB;

	//CCN TEA 0xFF00000C 0x1F00000C 32 Undefined Held Held Held Iclk
	CCN[(CCN_TEA_addr&0xFF)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	CCN[(CCN_TEA_addr&0xFF)>>2].readFunction=0;
	CCN[(CCN_TEA_addr&0xFF)>>2].writeFunction=0;
	CCN[(CCN_TEA_addr&0xFF)>>2].data32=&CCN_TEA;

	//CCN MMUCR 0xFF000010 0x1F000010 32 0x00000000 0x00000000 Held Held Iclk
	CCN[(CCN_MMUCR_addr&0xFF)>>2].flags= REG_32BIT_READWRITE | REG_READ_DATA ;
	CCN[(CCN_MMUCR_addr&0xFF)>>2].readFunction=0;
	CCN[(CCN_MMUCR_addr&0xFF)>>2].writeFunction=CCN_MMUCR_write;
	CCN[(CCN_MMUCR_addr&0xFF)>>2].data32=&CCN_MMUCR.reg_data;

	//CCN BASRA 0xFF000014 0x1F000014 8 Undefined Held Held Held Iclk
	CCN[(CCN_BASRA_addr&0xFF)>>2].flags=REG_8BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	CCN[(CCN_BASRA_addr&0xFF)>>2].readFunction=0;
	CCN[(CCN_BASRA_addr&0xFF)>>2].writeFunction=0;
	CCN[(CCN_BASRA_addr&0xFF)>>2].data8=&CCN_BASRA;

	//CCN BASRB 0xFF000018 0x1F000018 8 Undefined Held Held Held Iclk
	CCN[(CCN_BASRB_addr&0xFF)>>2].flags=REG_8BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	CCN[(CCN_BASRB_addr&0xFF)>>2].readFunction=0;
	CCN[(CCN_BASRB_addr&0xFF)>>2].writeFunction=0;
	CCN[(CCN_BASRB_addr&0xFF)>>2].data8=&CCN_BASRB;

	//CCN CCR 0xFF00001C 0x1F00001C 32 0x00000000 0x00000000 Held Held Iclk
	CCN[(CCN_CCR_addr&0xFF)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA;
	CCN[(CCN_CCR_addr&0xFF)>>2].readFunction=0;
	CCN[(CCN_CCR_addr&0xFF)>>2].writeFunction=CCN_CCR_write;
	CCN[(CCN_CCR_addr&0xFF)>>2].data32=&CCN_CCR.reg_data;

	//CCN TRA 0xFF000020 0x1F000020 32 Undefined Undefined Held Held Iclk
	CCN[(CCN_TRA_addr&0xFF)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	CCN[(CCN_TRA_addr&0xFF)>>2].readFunction=0;
	CCN[(CCN_TRA_addr&0xFF)>>2].writeFunction=0;
	CCN[(CCN_TRA_addr&0xFF)>>2].data32=&CCN_TRA;

	//CCN EXPEVT 0xFF000024 0x1F000024 32 0x00000000 0x00000020 Held Held Iclk
	CCN[(CCN_EXPEVT_addr&0xFF)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	CCN[(CCN_EXPEVT_addr&0xFF)>>2].readFunction=0;
	CCN[(CCN_EXPEVT_addr&0xFF)>>2].writeFunction=0;
	CCN[(CCN_EXPEVT_addr&0xFF)>>2].data32=&CCN_EXPEVT;

	//CCN INTEVT 0xFF000028 0x1F000028 32 Undefined Undefined Held Held Iclk
	CCN[(CCN_INTEVT_addr&0xFF)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	CCN[(CCN_INTEVT_addr&0xFF)>>2].readFunction=0;
	CCN[(CCN_INTEVT_addr&0xFF)>>2].writeFunction=0;
	CCN[(CCN_INTEVT_addr&0xFF)>>2].data32=&CCN_INTEVT;

	//CCN PTEA 0xFF000034 0x1F000034 32 Undefined Undefined Held Held Iclk
	CCN[(CCN_PTEA_addr&0xFF)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	CCN[(CCN_PTEA_addr&0xFF)>>2].readFunction=0;
	CCN[(CCN_PTEA_addr&0xFF)>>2].writeFunction=0;
	CCN[(CCN_PTEA_addr&0xFF)>>2].data32=&CCN_PTEA.reg_data;

	//CCN QACR0 0xFF000038 0x1F000038 32 Undefined Undefined Held Held Iclk
	CCN[(CCN_QACR0_addr&0xFF)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA;
	CCN[(CCN_QACR0_addr&0xFF)>>2].readFunction=0;
	CCN[(CCN_QACR0_addr&0xFF)>>2].writeFunction=CCN_QACR_write<0>;
	CCN[(CCN_QACR0_addr&0xFF)>>2].data32=&CCN_QACR[0].reg_data;

	//CCN QACR1 0xFF00003C 0x1F00003C 32 Undefined Undefined Held Held Iclk
	CCN[(CCN_QACR1_addr&0xFF)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA;
	CCN[(CCN_QACR1_addr&0xFF)>>2].readFunction=0;
	CCN[(CCN_QACR1_addr&0xFF)>>2].writeFunction=CCN_QACR_write<1>;
	CCN[(CCN_QACR1_addr&0xFF)>>2].data32=&CCN_QACR[1].reg_data;
}

void ccn_Reset(bool Manual)
{
	CCN_TRA					= 0x0;
	CCN_EXPEVT				= 0x0;
	CCN_MMUCR.reg_data		= 0x0;
	CCN_CCR.reg_data		= 0x0;
}

void ccn_Term()
{
}


template void CCN_QACR_write<0>(u32 value);
template void CCN_QACR_write<1>(u32 value);
