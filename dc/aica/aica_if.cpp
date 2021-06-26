#include "types.h"
#include "aica_if.h"
#include "dc/mem/sh4_mem.h"
#include "dc/mem/sb.h"
#include "plugins/plugin_manager.h"
#include "dc/asic/asic.h"

#include "dc/sh4/sh4_sched.h"

#include "dc/arm7/vbaARM.h"

//arm 7 is emulated within the aica implementation
//RTC is emulated here tho xD
//Gota check what to do about the rest regs that are not aica olny .. pfftt [display mode , any other ?]
#include <time.h>

#define likely(x) __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)

int dma_sched_id;

VArray2 aica_ram;
u32 VREG;//video reg =P
u32 ARMRST;//arm reset reg
u32 rtc_EN=0;
bool _firstTime = true;
u32 old_RTC = 0;
u32 GetRTC_now()
{
	// The Dreamcast Epoch time is 1/1/50 00:00 but without support for time zone or DST.
	// We compute the TZ/DST current time offset and add it to the result
	// as if we were in the UTC time zone (as well as the DC Epoch)
	time_t rawtime = time(NULL);
	struct tm localtm, gmtm;
	localtm = *localtime(&rawtime);
	gmtm = *gmtime(&rawtime);
	gmtm.tm_isdst = -1;
	time_t time_offset = mktime(&localtm) - mktime(&gmtm);
	// 1/1/50 to 1/1/70 is 20 years and 5 leap days
	return (20 * 365 + 5) * 24 * 60 * 60 + rawtime + time_offset;
}

extern s32 rtc_cycles;
u32 ReadMem_aica_rtc(u32 addr,u32 sz)
{
	//settings.dreamcast.RTC=GetRTC_now();
	switch( addr & 0xFF )
	{
	case 0:	
		return settings.dreamcast.RTC>>16;
	case 4:	
		return settings.dreamcast.RTC &0xFFFF;
	case 8:	
		return 0;
	}

	//printf("ReadMem_aica_rtc : invalid address\n");
	return 0;
}

void WriteMem_aica_rtc(u32 addr,u32 data,u32 sz)
{
	//printf("hhhh\n");
	switch( addr & 0xFF )
	{
	case 0:	
		if (rtc_EN)
		{
			settings.dreamcast.RTC&=0xFFFF;
			settings.dreamcast.RTC|=(data&0xFFFF)<<16;
			rtc_EN=0;
			//SaveSettings();
		}
		return;
	case 4:	
		if (rtc_EN)
		{
			settings.dreamcast.RTC&=0xFFFF0000;
			settings.dreamcast.RTC|= data&0xFFFF;
			//rtc_cycles=SH4_CLOCK;	//clear the internal cycle counter ;)
		}
		return;
	case 8:	
		rtc_EN=data&1;
		return;
	}

	return;
}
u32 FASTCALL ReadMem_aica_reg(u32 addr,u32 sz)
{
	addr&=0x7FFF;

	if (sz==1)
	{
		if (addr==0x2C01)
		{
			return VREG;
		}
		else if (addr==0x2C00)
		{
			return ARMRST;
		}
		else
			return libAICA_ReadMem_aica_reg(addr,sz);
	}
	else
	{
		if (addr==0x2C00)
		{
			return (VREG<<8) | ARMRST;
		}
		else
			return libAICA_ReadMem_aica_reg(addr,sz);
	}
}

s32 aica_pending_dma = 0;

void aica_periodical(u32 cycl)
{
	if (aica_pending_dma > 0)
	{
		verify(SB_ADST==1);

		cycl = (aica_pending_dma <= 0) ? 0 : cycl;
		aica_pending_dma-=cycl;

		if (aica_pending_dma <= 0)
		{
			//log("%u %d\n",cycl,(s32)aica_pending_dma);
			asic_RaiseInterrupt(holly_SPU_DMA);
			aica_pending_dma = 0;
			SB_ADST=0;
		}
	}
}

void FASTCALL WriteMem_aica_reg(u32 addr,u32 data,u32 sz)
{
	if (sz==1)
	{
		if ((addr & 0x7FFF)==0x2C01)
		{
			VREG=data;
			//printf("VREG = %02X\n",VREG);
		}
		else if (addr==0x2C00)
		{
			ARMRST=data;
			//log("ARMRST = %02X\n",ARMRST);
			ARMRST&=1;
			SetResetState(ARMRST);
		}
		else
		{
			libAICA_WriteMem_aica_reg(addr,data,sz);
		}
	}
	else
	{
		if (addr==0x2C00)
		{
			VREG=(data>>8)&0xFF;
			ARMRST=data&0xFF;
			printf("VREG = %02X ARMRST %02X\n",VREG,ARMRST);
			ARMRST&=1;
			//SetResetState(ARMRST);
			libAICA_WriteMem_aica_reg(addr,data&(~0xFF00),sz);
		}
		else
		libAICA_WriteMem_aica_reg(addr,data,sz);
	}
}
//Init/res/term
void aica_Init()
{
	//mmnnn ? gota fill it w/ something
}

void aica_Reset(bool Manual)
{
	if (!Manual)
	{
		aica_ram.Zero();
	}

	//settings.dreamcast.RTC = GetRTC_now();
}

void aica_Term()
{

}

int dma_end_sched(int tag, int cycl, int jitt)
{
	u32 len = SB_ADLEN & 0x7FFFFFFF;

	if (SB_ADLEN & 0x80000000)
		SB_ADEN = 1;//
	else
		SB_ADEN = 0;//

	SB_ADSTAR += len;
	SB_ADSTAG += len;
	SB_ADST = 0x00000000;//dma done
	SB_ADLEN = 0x00000000;

	// indicate that dma is not happening, or has been paused
	SB_ADSUSP |= 0x10;

	asic_RaiseInterrupt(holly_SPU_DMA);

	return 0;
}

void Write_SB_ADST(u32 data)
{
	//0x005F7800	SB_ADSTAG	RW	AICA:G2-DMA G2 start address 
	//0x005F7804	SB_ADSTAR	RW	AICA:G2-DMA system memory start address 
	//0x005F7808	SB_ADLEN	RW	AICA:G2-DMA length 
	//0x005F780C	SB_ADDIR	RW	AICA:G2-DMA direction 
	//0x005F7810	SB_ADTSEL	RW	AICA:G2-DMA trigger select 
	//0x005F7814	SB_ADEN	RW	AICA:G2-DMA enable 
	//0x005F7818	SB_ADST	RW	AICA:G2-DMA start 
	//0x005F781C	SB_ADSUSP	RW	AICA:G2-DMA suspend 

	if (data&1)
	{
		if (SB_ADEN&1)
		{
			if (unlikely(SB_ADDIR==1))
				msgboxf("AICA DMA : SB_ADDIR==1 !!!!!!!!",MBX_OK | MBX_ICONERROR);

			u32 src=SB_ADSTAR;
			u32 dst=SB_ADSTAG;
			u32 len=SB_ADLEN & 0x7FFFFFFF;

			WriteMemBlock_nommu_dma(dst, src, len);

			// idicate that dma is in progress
			SB_ADSUSP &= ~0x10;
			
			// Schedule the end of DMA transfer interrupt
			int cycles = len * (SH4_CLOCK / 2 / 25000000);       // 16 bits @ 25 MHz
			if (cycles < 4096)
				dma_end_sched(0, 0, 0);
			else
				sh4_sched_request(dma_sched_id, cycles);
		}
	}
}

void Write_SB_E1ST(u32 data)
{
	//0x005F7800	SB_ADSTAG	RW	AICA:G2-DMA G2 start address 
	//0x005F7804	SB_ADSTAR	RW	AICA:G2-DMA system memory start address 
	//0x005F7808	SB_ADLEN	RW	AICA:G2-DMA length 
	//0x005F780C	SB_ADDIR	RW	AICA:G2-DMA direction 
	//0x005F7810	SB_ADTSEL	RW	AICA:G2-DMA trigger select 
	//0x005F7814	SB_ADEN	RW	AICA:G2-DMA enable 
	//0x005F7818	SB_ADST	RW	AICA:G2-DMA start 
	//0x005F781C	SB_ADSUSP	RW	AICA:G2-DMA suspend 

	if (data&1)
	{
		if (SB_E1EN&1)
		{
			u32 src=SB_E1STAR;
			u32 dst=SB_E1STAG;
			u32 len=SB_E1LEN & 0x7FFFFFFF;

			if (SB_E1DIR==1)
			{
				u32 t=src;
				src=dst;
				dst=t;
				printf("G2-EXT1 DMA : SB_E1DIR==1 DMA Read to 0x%X from 0x%X %d bytes\n",dst,src,len);
			}
			else
				printf("G2-EXT1 DMA : SB_E1DIR==0:DMA Write to 0x%X from 0x%X %d bytes\n",dst,src,len);

			WriteMemBlock_nommu_dma(dst, src, len);

			if (SB_E1LEN & 0x80000000)
				SB_E1EN=1;//
			else
				SB_E1EN=0;//

			SB_E1STAR+=len;
			SB_E1STAG+=len;
			SB_E1ST = 0x00000000;//dma done
			SB_E1LEN = 0x00000000;

			
			asic_RaiseInterrupt(holly_EXT_DMA1);
		}
	}
}

void Write_SB_E2ST(u32 data)
{
	if ((data & 1) && (SB_E2EN & 1))
	{
		u32 src = SB_E2STAR;
		u32 dst = SB_E2STAG;
		u32 len = SB_E2LEN & 0x7FFFFFFF;

		if (SB_E2DIR == 1)
		{
			u32 t = src;
			src = dst;
			dst = t;
			printf("G2-EXT2 DMA : SB_E2DIR==1 DMA Read to 0x%X from 0x%X %d bytes\n", dst, src, len);
		}
		else
			printf("G2-EXT2 DMA : SB_E2DIR==0:DMA Write to 0x%X from 0x%X %d bytes\n", dst, src, len);

		WriteMemBlock_nommu_dma(dst, src, len);

		if (SB_E2LEN & 0x80000000)
			SB_E2EN = 1;
		else
			SB_E2EN = 0;

		SB_E2STAR += len;
		SB_E2STAG += len;
		SB_E2ST = 0x00000000;//dma done
		SB_E2LEN = 0x00000000;


		asic_RaiseInterrupt(holly_EXT_DMA2);
	}
}

void aica_sb_Init()
{
	//NRM
	//6
	sb_regs[((SB_ADST_addr-SB_BASE)>>2)].flags=REG_32BIT_READWRITE | REG_READ_DATA;
	sb_regs[((SB_ADST_addr-SB_BASE)>>2)].writeFunction=Write_SB_ADST;

	//I realy need to implement G2 dma (and rest dmas actualy) properly
	//THIS IS NOT AICA, its G2-EXT (BBA)
	sb_regs[((SB_E1ST_addr-SB_BASE)>>2)].flags=REG_32BIT_READWRITE | REG_READ_DATA;
	sb_regs[((SB_E1ST_addr-SB_BASE)>>2)].writeFunction=Write_SB_E1ST;

	sb_regs[((SB_E2ST_addr-SB_BASE)>>2)].flags=REG_32BIT_READWRITE | REG_READ_DATA;
	sb_regs[((SB_E2ST_addr-SB_BASE)>>2)].writeFunction=Write_SB_E2ST;

	dma_sched_id = sh4_sched_register(0, dma_end_sched);
}

void aica_sb_Reset(bool Manual)
{
}

void aica_sb_Term()
{
}
