#include "types.h"
#include "pvr_if.h"
#include "pvrLock.h"
#include "dc/sh4/intc.h"
#include "dc/mem/_vmem.h"
#include "dc/mem/sh4_mem.h"
#include "dc/mem/sh4_internal_reg.h"

#include "plugs/drkPvr/threaded.h"

//TODO : move code later to a plugin
//TODO : Fix registers arrays , they must be smaller now doe to the way SB registers are handled
#include "plugins/plugin_manager.h"
#include "dc/asic/asic.h"

#include "pspDmac.h"
#include "melib.h"

 
//YUV converter code :)
//inits the YUV converter
u32 YUV_tempdata[512/4];//512 bytes

u32 YUV_index=0;
u32	YUV_dest=0;
u32 YUV_doneblocks;
u32 YUV_blockcount;

u32 YUV_x_curr;
u32 YUV_y_curr;

u32 YUV_x_size;
u32 YUV_y_size;

//writes 2 pixels to vram
INLINE void YUV_putpixel2(u32 x ,u32 y, u32 pixdata)
{
	*(u32*) (&(vram.data [YUV_dest + (YUV_x_curr+x+(YUV_y_curr+y)*YUV_x_size)*2]))  =pixdata;
}

void YUV_init()
{
	YUV_index=0;
	YUV_x_curr=0;
	YUV_y_curr=0;

	YUV_dest=pvr_readreg_TA(0x5F8148,4)&VRAM_MASK;//TODO : add the masking needed
	YUV_doneblocks=0;
	u32 TA_YUV_TEX_CTRL=pvr_readreg_TA(0x5F814C,4);
	YUV_blockcount=(((TA_YUV_TEX_CTRL>>0)&0x3F)+1)*(((TA_YUV_TEX_CTRL>>8)&0x3F)+1);

	if ((TA_YUV_TEX_CTRL>>16 )&1)
	{	//w00t ?
		YUV_x_size=16;
		YUV_y_size=16;
	}
	else
	{	//yesh!!!
		YUV_x_size=(((TA_YUV_TEX_CTRL>>0)&0x3F)+1)*16;
		YUV_y_size=(((TA_YUV_TEX_CTRL>>8)&0x3F)+1)*16;
	}
}


INLINE u8 GetY420(int x, int y,u8* base)
{
	if (x>7)
	{
		x-=8;
		base+=64;
	}

	if (y>7)
	{
		y-=8;
		base+=128;
	}
	
	return base[x+y*8];
}

INLINE u8 GetUV420(int x, int y,u8* base)
{
	int realx=x>>1;
	int realy=y>>1;

	return base[realx+realy*8];
}
INLINE void YUV_ConvertMacroBlock(u8* datap)
{
	u32 TA_YUV_TEX_CTRL=pvr_readreg_TA(0x5F814C,4);

	//do shit
	YUV_doneblocks++;
	YUV_index=0;

	int block_size=(TA_YUV_TEX_CTRL & (1<<24))==0?384:512;

	//YUYV
	if (block_size==384)
	{
		u8* U= datap; 
		u8* V=(u8*)&YUV_tempdata[64/4];
		u8* Y=(u8*)&YUV_tempdata[(64+64)/4];
		
		u8  yuyv[4];

		for (int y=0;y<16;y++)
		{
			for (int x=0;x<16;x+=2)
			{
				yuyv[1]=GetY420(x,y,Y);
				yuyv[0]=GetUV420(x,y,U);
				yuyv[3]=GetY420(x+1,y,Y);
				yuyv[2]=GetUV420(x,y,V);
				//pixel x,y , x+1,y
				YUV_putpixel2(x,y,*(u32*)yuyv);
			}
		}

		YUV_x_curr+=16;
		if (YUV_x_curr==YUV_x_size)
		{
			YUV_x_curr=0;
			YUV_y_curr+=16;
			if (YUV_y_curr==YUV_y_size)
			{
				YUV_y_curr=0;
			}
		}
	}
	else
	{
		printf("YUV4:2:2 not supported (YUV converter)\n");
	}

	if (YUV_blockcount==YUV_doneblocks)
	{
		YUV_init();
		//TODO : Check if it's alright to do it here?
		asic_RaiseInterrupt(holly_YUV_DMA);
	}
}
void YUV_data(u32* data , u32 count)
{
	if (YUV_blockcount==0)
	{
		printf("YUV_data : YUV decoder not inited , *WATCH*\n");
		//wtf ? not inited
		YUV_init();
	}
	//u32 TA_YUV_TEX_BASE=pvr_readreg_TA(0x5F8148,4);
	u32 TA_YUV_TEX_CTRL=pvr_readreg_TA(0x5F814C,4);


	//YUV420 is 384 bytes , YUV422 is 512 bytes
	u32 block_size=(TA_YUV_TEX_CTRL & (1<<24))==0?384:512;
	
	count*=32;
	while(count>0)
	{
		if ((YUV_index+count)>=block_size)
		{	//more or exactly one block remaining
			u32 dr=block_size-YUV_index;				//remaining byts til block end
			if (YUV_index == 0)
			{
				// Avoid copy
				YUV_ConvertMacroBlock((u8*)data);				//convert block
			}
			else
			{
				memcpy_vfpu(&YUV_tempdata[YUV_index>>2],data,dr);	//copy em
				YUV_ConvertMacroBlock((u8*)&YUV_tempdata[0]);					//convert block
				YUV_index = 0;
			}

			data+=dr>>2;								//count em
			count-=dr;
		}
		else
		{	//less that a whole block remaining
			memcpy_vfpu(&YUV_tempdata[YUV_index>>2],data,count);	//append it
			YUV_index+=count;
			count=0;
		}
	}
}

//Regs

u32 pvr_readreg_TA(u32 addr,u32 sz)
{
	if ((addr&0xFFFFFF)==0x5F8150)	//TA_YUV_TEX_CNT
		return YUV_doneblocks;
	
	return libPvr_ReadReg(addr,sz);
}

void pvr_writereg_TA(u32 addr,u32 data,u32 sz)
{
	libPvr_WriteReg(addr,data,sz);

	if ((addr&0xFFFFFF)==0x5F8148)
		YUV_init();
}

//vram 32-64b

//read
u8 FASTCALL pvr_read_area1_8(u32 addr)
{
	printf("8 bit vram reads are not possible\n");
	return 0;
}

u16 FASTCALL pvr_read_area1_16(u32 addr)
{
	addr =vramlock_ConvOffset32toOffset64(addr);
	return *host_ptr_xor((u16*)&vram[addr]);
}
u32 FASTCALL pvr_read_area1_32(u32 addr)
{
	addr =vramlock_ConvOffset32toOffset64(addr);
	return *(u32*)&vram[addr];
}

//write
void FASTCALL pvr_write_area1_8(u32 addr,u8 data)
{
	printf("8 bit vram writes are not possible\n");
}

#define VRAM_BANK_BIT 0x400000

u32 pvr_map32(u32 offset32)
{
	//64b wide bus is achieved by interleaving the banks every 32 bits
	//const u32 bank_bit = VRAM_BANK_BIT;
	const u32 static_bits = VRAM_MASK - (VRAM_BANK_BIT * 2 - 1) + 3;
	const u32 offset_bits = (VRAM_BANK_BIT - 1) & ~3;
	
	u32 bank = (offset32 & VRAM_BANK_BIT) / VRAM_BANK_BIT;

	u32 rv = offset32 & static_bits;

	rv |= (offset32 & offset_bits) * 2;

	rv |= bank * 4;

	return rv;
}

void FASTCALL pvr_write_area1_16(u32 addr,u16 data)
{
	*(u16*)&vram.data[pvr_map32(addr)]=data;
}
void FASTCALL pvr_write_area1_32(u32 addr,u32 data)
{
	*(u32*)&vram.data[pvr_map32(addr)] = data;
}



void FASTCALL TAWrite(u32 address,u32* data,u32 count)
{
	u32 address_w=address&0x1FFFFFF;//correct ?
	if (address_w<0x800000)//TA poly
	{
		libPvr_TaDMA(data,count);		
	}
	else if(address_w<0x1000000) //Yuv Converter
	{
		YUV_data(data,count);
	}
	else //Vram Writef
	{
		//This actually works on dc, need to handle lmmodes
		memcpy_vfpu(&vram.data[address&VRAM_MASK], data, count*32);
		//int _cont = count*32;
		/*sceKernelDcacheWritebackRange(&vram.data[address&VRAM_MASK], _cont);
		sceKernelDcacheWritebackRange(data, _cont);
		sceKernelDcacheWritebackInvalidateAll();
		sceDmacMemcpy(&vram.data[address&VRAM_MASK], data, _cont);*/
		//sceKernelDcacheWritebackInvalidateAll();
	}
}

void FASTCALL TAWriteSQ(u32 address,u8* sqb)
{
	u32 address_w=address&0x1FFFFFF;//correct ?
	u8* sq=&sqb[address&0x20];

	if (likely(address_w<0x800000))//TA poly
	{
		libPvr_TaSQ((u32*)sq);
	}
	else if(likely(address_w<0x1000000)) //Yuv Converter
	{
		YUV_data((u32*)sq,1);
	}
	else //Vram Writef
	{
		/*if (SB_LMMODE0 == 0)
		{
			// 64b path
			MemWrite32(&vram[address_w&(VRAM_MASK-0x1F)],sq);
		}
		else*/
		{
			// 32b path
			for (int i = 0; i < 8; i++, address_w += 4)
			{
				pvr_write_area1_32(address_w, ((u32 *)sq)[i]);
			}
		}
	}
}


void FASTCALL TAWriteSQ_nommu(u32 address)
{
	u32 address_w=address&0x1FFFFFF;//correct ?
	u8* sq=&sq_both[address & 0x20];

	if (likely(address_w<0x800000))//TA poly
	{
		libPvr_TaSQ((u32*)sq);
	}
	else if(likely(address_w<0x1000000)) //Yuv Converter
	{
		YUV_data((u32*)sq,1);
	}
	else //Vram Writef
	{
		for (int i = 0; i < 8; i++, address_w += 4)
		{
			pvr_write_area1_32(address_w, sq[i]);
		}
	}
}
//Misc interface

//Init/Term , global
void pvr_Init()
{
	threaded_init();
}
void pvr_Term()
{
	threaded_term();
}
//Reset -> Reset - Initialise to defualt values
void pvr_Reset(bool Manual)
{
	if (!Manual)
		vram.Zero();
}


