#include "aica_hax.h"
#include "aica_hle.h"
#include "dc/mem/sh4_mem.h"


namespace HACK_AICA
{
u8 *aica_reg;
u8 *aica_ram;
}
using namespace HACK_AICA;


#define 	ReadMemArrRet(arr,addr,sz)				\
			{if (sz==1)								\
				return arr[addr];					\
			else if (sz==2)							\
				return *(u16*)&arr[addr];			\
			else if (sz==4)							\
				return *(u32*)&arr[addr];}	

#define WriteMemArrRet(arr,addr,data,sz)				\
			{if(sz==1)								\
				{arr[addr]=(u8)data;return;}				\
			else if (sz==2)							\
				{*(u16*)&arr[addr]=(u16)data;return;}		\
			else if (sz==4)							\
			{*(u32*)&arr[addr]=data;return;}}	
#define WriteMemArr(arr,addr,data,sz)				\
			{if(sz==1)								\
				{arr[addr]=(u8)data;}				\
			else if (sz==2)							\
				{*(u16*)&arr[addr]=(u16)data;}		\
			else if (sz==4)							\
			{*(u32*)&arr[addr]=data;}}	

void FASTCALL  HACK_libAICA_WriteMem_aica_reg(u32 addr,u32 data,u32 sz){
	if ((addr & 0xFFFF) == 0x2c00)
	{
		printf("Write to ARM reset, value= %x\n",data);
		if ((data&1)==0)
		{
			ARM_Katana_Driver_Info();
			arm7_on=true;
		}
		else
		{
			arm7_on=false;
		}
	}
	WriteMemArrRet(aica_reg,addr&0x7FFF,data,sz);
}

u32 HACK_libAICA_ReadMem_aica_ram(u32 addr,u32 size)
{

	//Some infos here:
	//Seems that these addr reacts differently when given these values:
	// 0x0 		-> Makes the emulator reading undefined opcodes
	// 0x1 		-> Freezes the emulator
	// 0xFFFFFF -> Crashes the emulator 
	// 0x80000 	-> Resets the emulator and crahes after the dreamcast boot
	// My theory: This is an entry point for the AICA chip to start reading and excuting code
	//if (addr>=0x800400 && addr<=0x800500) return 0x80000;

	if (HleEnabled())
	{
		if ( (addr& AICA_MEM_MASK) > (aud_drv->cmdBuffer & AICA_MEM_MASK))
		{
			if ((addr& AICA_MEM_MASK) < ((aud_drv->cmdBuffer +0x8000) & AICA_MEM_MASK))
				return 0x80000;
		}
	}

	//if((addr&0xFF)==0x5C)
	//	return 1;			// hack naomi aica ram check ?

	//kos/katana
	*(u32*)&aica_ram[((0x80FFC0-0x800000)&0x1FFFFF)]=*(u32*)&aica_ram[((0x80FFC0-0x800000)&0x1FFFFF)]?0:3;
	//return 0x3;			//hack snd_dbg

	//the kos command list is 0x810000 to 0x81FFFF
	//here we hack the first and last comands
	//seems to fix everything ^^

	if (addr==0x81000C || addr==0x81FFFF) return 0x1;

	//Games like Code Veronica waits for this to give 0(?). Currently I don't know how can this be detected
	/*if (addr>=0x81000C && addr<0x81FFFD){
		//printf("GOT IT: %x\n",addr);
		return 0x1;			//hack kos command que
	}*/
	//printf("addr: %x\n",addr);

	//crazy taxi / doa2 /*
	if (addr>0x800100 && addr<0x800300)
		return 0x800000;			//hack katana command que
	//if (calls & 0x10)
		//memset(&aica_ram[0x100],0x800000,16);
	if (addr ==0x00832040 )
		return 0xFFFFFF;			//hack katana command que
	if (addr ==0x00832060 )
		return 0xFFFFFF;			//hack katana command que

	//trickstyle 
	//another position for command queue ?
	if (addr>=0x813400 && addr<=0x813900)
	{
		//what to return what to return ??
		//this not works good
		//return 0x1;
	}
	//memset(a&ica_ram[13400],0x800000,0x400);

	//hack Katana wait
	//Waits for something , then writes to the value returned here
	//Found in Roadsters[also in Toy cmd]
	//it seems it is somehow rlated to a direct mem read after that 
	// im too tired to debug and find the actualy relation but it is
	/*if (addr>=0x8000C0 && addr<=0x8000E8)
		return 0x80000;
*/
	if (addr==0x8000C0)
		return 0x80000;
	if (addr==0x8000EC)
		return 0x80000;
	if (addr==0x80005C)
		return 0x80000;

	/*if (addr == 0x880000){
		*(u32*)&aica_ram[((0x880000-0x800000)&0x1FFFFF)]=*(u32*)&aica_ram[((0x880000-0x800000)&0x1FFFFF)]?0:3;
		return 0x80000;
	}

	if (addr == 0x880018){
		return 0x1;
	}*/

	//Some info about this address:
	//Return value: 0x80000 -> hangs
	//Return value: 0xFFFFF -> hangs but wait some interrupt(?)
	//Return value: 0x1     -> crash
	//If that addr is not returned 800020 will be the next addr
	/*if (addr==0x80005c) {
		return 0x80000;
	}*/

	/*Rez, not realy working tho*/
	/*static u32 tempyy=0xDEAD;
	if (addr==0x8000F8)
		return (tempyy++)^(tempyy^=-1);*/
	//*(u32*)&aica_ram[0x000EC]=0x1;

	//and recv!
	
	//*(u32*)&aica_ram[0x000E8]=0x1;

	//addr == 0x008000f8 -> recv , locks while reading from it ;)


	//some status bits/queue , hacked of course :p
	if ((addr>0x8014e0) && (addr<0x8015e0))
	{
		return 0xFFFFFF;
	}

	static int thebighax=0;
	thebighax++;

	if (thebighax&0x10)
	{
		if ((thebighax&0xF0)>0x80)
		{
			thebighax+=0x100;
			thebighax&=~0xFF;
		}
		switch(thebighax>>8)
		{
		case 0:
			break;
		case 1:
			return 0x80000;
		case 2:
			return 0xFFFFF;
		case 3:
			return 0xfFFFFFFF;
		case 4:
			return 0x80000000;
		case 5:
			return 0x1;
		case 6:
			thebighax=0;
			return 0x12345678;
		}
	}

	//TODO : Add Warn
	if (size==1)
		return aica_ram[addr&AICA_MEM_MASK];
	else if (size==2)
		return HOST_TO_LE16(*(u16*)&aica_ram[addr&AICA_MEM_MASK]);
	else if (size==4)
		return HOST_TO_LE32(*(u32*)&aica_ram[addr&AICA_MEM_MASK]);
	return 0;
}
 
void HACK_libAICA_WriteMem_aica_ram(u32 addr,u32 data,u32 size)
{
	if (size==1)
		aica_ram[addr&AICA_MEM_MASK]=(u8)data;
	else if (size==2)
		*(u16*)&aica_ram[addr&AICA_MEM_MASK]=HOST_TO_LE16((u16)data);
	else if (size==4)
		*(u32*)&aica_ram[addr&AICA_MEM_MASK]=HOST_TO_LE32(data);
}


int calls=0;
namespace HACK_AICA
{
void init_mem()
{
	aica_reg=(u8*)malloc(0x8000);
	memset(aica_reg,0,0x8000);
}

void term_mem()
{
	free(aica_reg);
	//free(aica_ram);
}
}
