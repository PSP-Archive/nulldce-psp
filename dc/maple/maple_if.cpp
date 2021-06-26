#include "types.h"
#include <string.h>

#include "maple_if.h"

#include "config/config.h"

#include "dc/sh4/intc.h"
#include "dc/mem/sb.h"
#include "dc/mem/sh4_mem.h"
#include "plugins/plugin_manager.h"
#include "dc/asic/asic.h"
#include "dc/maple/maple_helper.h"

#include "dc/sh4/sh4_sched.h"

maple_device* MapleDevices[4][6];
s32 maple_dma_pending=0;

#define likely(x) __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)
#define SWAP32(a) ((((a) & 0xff) << 24)  | (((a) & 0xff00) << 8) | (((a) >> 8) & 0xff00) | (((a) >> 24) & 0xff))


static int maple_schid;

/*
	Maple host controller
	Direct processing, async interrupt handling
	Device code is on maple_devs.cpp/h, config&managment is on maple_cfg.cpp/h

	This code is missing many of the hardware details, like proper trigger handling,
	dma continuation on suspect, etc ...
*/

void maple_DoDma();

//realy hackish
//misses delay , and stop/start implementation
//ddt/etc are just hacked for wince to work
//now with proper maple delayed dma maby its time to look into it ?
bool maple_ddt_pending_reset=false;
void maple_vblank()
{
	if (SB_MDEN &1)
	{
		if (SB_MDTSEL&1)
		{
			if (!maple_ddt_pending_reset)
			{
				//printf("DDT vblank\n");	
				maple_DoDma();
				if ((SB_MSYS>>12)&1)
				{
					maple_ddt_pending_reset=true;
				}
			}
		}
		else
		{
			maple_ddt_pending_reset=false;
		}
	}
}
void maple_SB_MSHTCL_Write(u32 data)
{
	if (data&1)
		maple_ddt_pending_reset=false;
}
void maple_SB_MDST_Write(u32 data)
{
	if (data & 0x1)
	{
		if (SB_MDEN &1)
		{
			SB_MDST = 1;
			maple_DoDma();
		}
	}
}

bool IsOnSh4Ram(u32 addr)
{
	if (((addr>>26)&0x7)==3)
	{
		if ((((addr>>29) &0x7)!=7))
		{
			return true;
		}
	}

	return false;
}

u32 dmacount=0;
void maple_DoDma()
{
	verify(SB_MDEN &1)
	verify(SB_MDST==0)

#if debug_maple
	printf("Maple: DoMapleDma\n");
#endif
	const bool swap_msb = (SB_MMSEL == 0);
	u32 addr = SB_MDSTAR;
	u32 xfer_count=0;
	bool last = false;

	while (last != true)
	{
		u32 header_1 = ReadMem32_nommu(addr);
		u32 header_2 = ReadMem32_nommu(addr + 4) &0x1FFFFFE0;

		last = (header_1 >> 31) == 1;//is last transfer ?
		u32 plen = (header_1 & 0xFF )+1;//transfer length
		u32 maple_op=(header_1>>8)&7;
		xfer_count+=plen*4;

		//this is kinda wrong .. but meh
		//really need to properly process the commands at some point
		if (maple_op==0)
		{
			if (unlikely(!IsOnSh4Ram(header_2)))
			{
				printf("MAPLE ERROR : DESTINATION NOT ON SH4 RAM 0x%X\n",header_2);
				header_2&=0xFFFFFF;
				header_2|=(3<<26);
			}

			u32* p_out=(u32*)GetMemPtr(header_2,4);

			u32* p_data =(u32*) GetMemPtr(addr + 8,(plen)*sizeof(u32));

			const u32 frame_header = swap_msb ? SWAP32(p_data[0]) : p_data[0];
			
			//Command code 
			u32 command = frame_header & 0xFF;
			//Recipient address 
			u32 reci = (frame_header >> 8) & 0xFF;//0-5;
			//Sender address 
			//u32 send=(p_data[0] >> 16) & 0xFF;
			//Number of additional words in frame 
			u32 inlen = (frame_header >> 24) & 0xFF;

			u32 port=maple_GetPort(reci);
			u32 bus=maple_GetBusId(reci);

			if (MapleDevices[bus][5] && MapleDevices[bus][port])
			{
				if (swap_msb)
				{
					static u32 maple_in_buf[1024 / 4];
					static u32 maple_out_buf[1024 / 4];
					maple_in_buf[0] = frame_header;
					for (u32 i = 1; i < inlen; i++)
						maple_in_buf[i] = SWAP32(p_data[i]);
					p_data = maple_in_buf;
					p_out = maple_out_buf;
				}

				u32 outlen = MapleDevices[bus][port]->RawDma(&p_data[0], inlen * 4 + 4, &p_out[0]);

				xfer_count += outlen;

				if (swap_msb)
				{
					u32 *final_out = (u32 *)GetMemPtr(header_2, outlen);
					for (u32 i = 0; i < outlen / 4; i++)
						final_out[i] = SWAP32(p_out[i]);
				}
			}
			else
			{
				p_out[0]=0xFFFFFFFF;
			}

			//goto next command
			addr += 2 * 4 + plen * 4;
		}
		else
		{
			addr += 1 * 4;
		}
	}

	//printf("Maple XFER size %d bytes ms\n",xfer_count);
	//maple_dma_pending= ((xfer_count*200000000)/262144)+1;
	/*maple_dma_pending= (xfer_count*763);
	SB_MDST = 1;*/

	sh4_sched_request(maple_schid, xfer_count * (SH4_CLOCK / (2 * 1024 * 1024 / 8)));
}

void maple_Update(u32 cycles)
{
	if (maple_dma_pending>0)
	{
		cycles = (maple_dma_pending <= 0) ? 0 : cycles;
		maple_dma_pending-=cycles;

		if (maple_dma_pending<=0)
		{
			asic_RaiseInterrupt(holly_MAPLE_DMA);
			maple_dma_pending = 0;
			SB_MDST=0;
		}
	}
}

int Update(int tag, int c, int j)
{
	if (SB_MDEN & 1)
	{
		SB_MDST = 0;
		asic_RaiseInterrupt(holly_MAPLE_DMA);
	}
	else
	{
		printf("WARNING: MAPLE DMA ABORT\n");
		SB_MDST = 0; //I really wonder what this means, can the DMA be continued ?
	}

	return 0;
}

//Init registers :)
void maple_Init()
{
	sb_regs[(SB_MDST_addr-SB_BASE)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA;
	sb_regs[(SB_MDST_addr-SB_BASE)>>2].writeFunction=maple_SB_MDST_Write;

	sb_regs[(SB_MSHTCL_addr-SB_BASE)>>2].flags=REG_32BIT_READWRITE;
	sb_regs[(SB_MSHTCL_addr-SB_BASE)>>2].writeFunction=maple_SB_MSHTCL_Write;

	maple_schid = sh4_sched_register(0, Update);
}

void maple_Reset(bool Manual)
{
	maple_ddt_pending_reset=false;
	maple_dma_pending=0;
	SB_MDTSEL	= 0x00000000;
	SB_MDEN	= 0x00000000;
	SB_MDST	= 0x00000000;
	SB_MSYS	= 0x3A980000;
	SB_MSHTCL	= 0x00000000;
	SB_MDAPRO = 0x00007F00;
	SB_MMSEL	= 0x00000001;
}

void maple_Term()
{
	
}