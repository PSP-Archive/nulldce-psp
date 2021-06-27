#include "mem.h"
#include "arm7.h"
#include "dc/aica/aica_if.h"

#include "plugs/nullAICA/aica.h"

//Set to true when aica interrupt is pending
bool aica_interr=false;
u32 aica_reg_L=0;
//Set to true when the out of the intc is 1
bool e68k_out = false;
u32 e68k_reg_L;
u32 e68k_reg_M=0;	//constant ?


u32 FASTCALL libAICA_ReadMem_aica_reg(u32 addr,u32 sz)
{
	addr &= 0x7FFF;

	if (addr<0x2800)
	{
		ReadMemArrRet(aica_reg,addr,sz);
	}
	if (addr < 0x2818)
	{
		if (sz==1)
		{
			ReadCommonReg(addr,true);
			ReadMemArrRet(aica_reg,addr,1);
		}
		else
		{
			ReadCommonReg(addr,false);
			ReadMemArrRet(aica_reg,addr,2);
		}
	}

	ReadMemArrRet(aica_reg,addr,sz);
}


void FASTCALL  libAICA_WriteMem_aica_reg(u32 addr,u32 data,u32 sz)
{	
	if (sz == 1) WriteAicaReg<1>(addr,data);
	else		 WriteAicaReg<2>(addr,data);
}

void FASTCALL  libAICA_WriteMem_reg(u32 addr,u32 data,u32 sz)
{	
	addr &= 0x7FFF;
	
	if (addr < 0x2000)
	{
		//Channel data
		u32 chan=addr>>7;
		u32 reg=addr&0x7F;
		if (sz==1)
		{
			WriteMemArr(aica_reg,addr,data,1);
			WriteChannelReg8(chan,reg);
		}
		else
		{
			WriteMemArr(aica_reg,addr,data,2);
			WriteChannelReg8(chan,reg);
			WriteChannelReg8(chan,reg+1);
		}
		return;
	}

	if (addr<0x2800)
	{
		if (sz==1)
		{
			WriteMemArr(aica_reg,addr,data,1);
		}
		else 
		{
			WriteMemArr(aica_reg,addr,data,2);
		}
		return;
	}

	if (addr < 0x2818)
	{
		if (sz==1)
		{
			WriteCommonReg8(addr,data);
		}
		else
		{
			WriteCommonReg8(addr,data&0xFF);
			WriteCommonReg8(addr+1,data>>8);
		}
		return;
	}

	if (addr>=0x3000)
	{
		if (sz==1)
		{
			WriteMemArr(aica_reg,addr,data,1);
		}
		else
		{
			WriteMemArr(aica_reg,addr,data,2);
		}
	}

	if (sz == 1) WriteAicaReg<1>(addr,data);
	else		 WriteAicaReg<2>(addr,data);
}


void update_e68k()
{
	if (!e68k_out && aica_interr)
	{
		//Set the pending signal
		//Is L register holded here too ?
		e68k_out=1;
		e68k_reg_L=aica_reg_L;
	}
}

void FASTCALL ArmInterruptChange(u32 bits,u32 L)
{
	aica_interr=bits!=0;
	if (aica_interr)
		aica_reg_L=L;
	update_e68k();
}

void e68k_AcceptInterrupt()
{
	e68k_out=false;
	update_e68k();
}

//Reg reads from arm side ..
template <u32 sz,class T>
inline T fastcall arm_ReadReg(u32 addr)
{
	addr&=0x7FFF;
	if (addr==REG_L)
		return e68k_reg_L;
	else if(addr==REG_M)
		return e68k_reg_M;	//shoudn't really happen
	else
		return libAICA_ReadMem_aica_reg(addr,sz);
}		
template <u32 sz,class T>
inline void fastcall arm_WriteReg(u32 addr,T data)
{
	addr&=0x7FFF;
	if (addr==REG_L)
		return;				//shoudn't really happen (read only)
	else if(addr==REG_M)
	{
		//accept interrupts
		if (data&1)
			e68k_AcceptInterrupt();
	}
	else
		return libAICA_WriteMem_reg(addr,data,sz);
}
//Map using _vmem .. yay
void init_mem()
{
	LIBAICA_init_mem();
	AICA_Init();
}
//kill mem map & free used mem ;)
void term_mem()
{
	
}

//00000000~007FFFFF @DRAM_AREA* 
//00800000~008027FF @CHANNEL_DATA 
//00802800~00802FFF @COMMON_DATA 
//00803000~00807FFF @DSP_DATA 

//Force alignment for read/writes to mem
#define ACCESS_MASK (ARAM_SIZE-(sz))

template<int sz,typename T>
T ReadMemArm(u32 addr)
{

	T rv;

	addr&=0x00FFFFFF;
	if (addr<0x800000)
	{
		rv = *(T*)&aica_ram.data[addr&ACCESS_MASK];
	}
	else
	{
		rv = arm_ReadReg<sz,T>(addr);
	}
		
	if (unlikely(sz == 4 && addr & 3))
	{
		u32 sf = (addr & 3) * 8;
		return (rv >> sf) | (rv << (32 - sf));
	}
	else
		return rv;
}

template<int sz,typename T>
void WriteMemArm(u32 addr,T data)
{
	addr&=0x00FFFFFF;
	if (addr<0x800000)
	{
		*(T*)&aica_ram.data[addr&ACCESS_MASK]=data;
	}
	else
	{
		arm_WriteReg<sz,T>(addr,data);
	}
}

template u8 ReadMemArm<1,u8>(u32 adr);
template u16 ReadMemArm<2,u16>(u32 adr);
template u32 ReadMemArm<4,u32>(u32 adr);

template void WriteMemArm<1>(u32 adr,u8 data);
template void WriteMemArm<2>(u32 adr,u16 data);
template void WriteMemArm<4>(u32 adr,u32 data);

