#include "regs.h"
#include "Renderer_if.h"
#include "../../dc/pvr/pvr_if.h"
#include "ta.h"
#include "spg.h"
/*
	Basic PVR emulation -- much more work is needed for real TA emulation
*/

#define likely(x) __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)

u8 regs[RegSize];

u32 FASTCALL libPvr_ReadReg(u32 addr,u32 size)
{
	if (size!=4)
	{
		return 0;
	}

	return PvrReg(addr,u32);
}

void PrintfInfo();
extern bool pal_needs_update;


void FASTCALL libPvr_WriteReg(u32 paddr,u32 data,u32 size)
{

	if (size!=4)
	{
		return;
	}

	u32 addr=paddr&RegMask;

	switch(addr)
	{
		case ID_addr:
		case REVISION_addr:
		case TA_YUV_TEX_CNT_addr:
			return;	//read only

		case STARTRENDER_addr:
			//start render
			
			rend_start_render();
			render_end_pending=true;
			return;
		
		case TA_LIST_INIT_addr:
			if (data>>31)
			{
				rend_list_init();
				data=0;
			}
			break;

		case SOFTRESET_addr:
			if (data!=0)
			{
				if (data&1)
					rend_list_srst();
				data=0;
			}
			break;

		case TA_LIST_CONT_addr:
			//a write of anything works ?
			rend_list_cont();
			break;

		
		case SPG_CONTROL_addr:
		case SPG_LOAD_addr:
			if (PvrReg(addr, u32) != data)
            {
                PvrReg(addr, u32) = data;
                CalculateSync();
            }
		return;

		case TA_YUV_TEX_BASE_addr:
		case TA_YUV_TEX_CTRL_addr:
			PvrReg(addr, u32) = data;
            YUV_init();
		return;

		case FB_R_CTRL_addr:
            PvrReg(addr, u32) = data;
            if ((PvrReg(addr, u32) ^ data) & (1 << 23)){
                CalculateSync();
			}
			return;
		

		default:
		break;
			/*if (addr>=PALETTE_RAM_START_addr)
			{
				if (PvrReg(addr,u32)!=data)
				{
					u32 pal=addr&1023;

					//are palettes handled ?
					//			pal_needs_update=true;
					//			_pal_rev_256[pal>>8]++;
					//			_pal_rev_16[pal>>4]++;
				}
			}*/
	}

	if (addr >= PALETTE_RAM_START_addr && PvrReg(addr, u32) != data)
    {
            pal_needs_update = true;
    }

	PvrReg(addr,u32)=data;
}

bool Regs_Init()
{
	return true;
}

void Regs_Term()
{
}

void Regs_Reset(bool Manual)
{
	ID					= 0x17FD11DB;
	REVISION			= 0x00000011;
	SOFTRESET			= 0x00000007;
	SPG_HBLANK_INT.full	= 0x031D0000;
	SPG_VBLANK_INT.full	= 0x01500104;
	FPU_PARAM_CFG		= 0x0007DF77;
	HALF_OFFSET			= 0x00000007;
	ISP_FEED_CFG		= 0x00402000;
	SDRAM_REFRESH		= 0x00000020;
	SDRAM_ARB_CFG		= 0x0000001F;
	SDRAM_CFG			= 0x15F28997;
	SPG_HBLANK.full		= 0x007E0345;
	SPG_LOAD.full		= 0x01060359;
	SPG_VBLANK.full		= 0x01500104;
	SPG_WIDTH.full		= 0x07F1933F;
	VO_CONTROL.full		= 0x00000108;
	VO_STARTX			= 0x0000009D;
	VO_STARTY			= 0x00000015;
	SCALER_CTL.full		= 0x00000400;
	FB_BURSTCTRL		= 0x00090639;
	PT_ALPHA_REF		= 0x000000FF;
}

