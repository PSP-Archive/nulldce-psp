#include "mem.h"
#include "arm7.h"
#include "dc/aica/aica_if.h"

//Set to true when aica interrupt is pending
bool aica_interr=false;
u32 aica_reg_L=0;
//Set to true when the out of the intc is 1
bool e68k_out = false;
u32 e68k_reg_L;
u32 e68k_reg_M=0;	//constant ?

InterruptInfo* MCIEB;
InterruptInfo* MCIPD;
InterruptInfo* MCIRE;
InterruptInfo* SCIEB;
InterruptInfo* SCIPD;
InterruptInfo* SCIRE;
CommonData_struct* CommonData;
DSPData_struct* DSPData;

#define SH4_IRQ_BIT (1<<(u8)holly_SPU_IRQ)

u8 * aica_reg;

struct AicaTimerData
{
	union
	{
		struct 
		{
			u32 count:8;
			u32 md:3;
			u32 nil:5;
			u32 pad:16;
		};
		u32 data;
	};
};
class AicaTimer
{
public:
	AicaTimerData* data;
	s32 c_step;
	u32 m_step;
	u32 id;
	void Init(u8* regbase,u32 timer)
	{
		data=(AicaTimerData*)&regbase[0x2890 + timer*4];
		id=timer;
		m_step=1<<(data->md);
		c_step=m_step;
	}
	void StepTimer()
	{
		c_step--;
		if (c_step==0)
		{
			c_step=m_step;
			data->count++;
			if (data->count==0)
			{
				if (id==0)
				{
					SCIPD->TimerA=1;
					MCIPD->TimerA=1;
				}
				else if (id==1)
				{
					SCIPD->TimerB=1;
					MCIPD->TimerB=1;
				}
				else
				{
					SCIPD->TimerC=1;
					MCIPD->TimerC=1;
				}
			}
		}
	}

	void RegisterWrite()
	{
		u32 n_step=1<<(data->md);
		if (n_step!=m_step)
		{
			m_step=n_step;
			c_step=m_step;
		}
	}
};

AicaTimer timers[3];

u32 GetL(u32 witch)
{
	if (witch>7)
		witch=7;	//higher bits share bit 7

	u32 bit=1<<witch;
	u32 rv=0;

	if (CommonData->SCILV0 & bit)
		rv=1;

	if (CommonData->SCILV1 & bit)
		rv|=2;
	
	if (CommonData->SCILV2 & bit)
		rv|=4;

	return rv;
}

void update_arm_interrupts()
{
	u32 p_ints=SCIEB->full & SCIPD->full;

	u32 Lval=0;
	if (p_ints)
	{
		u32 bit_value=1;//first bit
		//scan all interrupts , lo to hi bit.I assume low bit ints have higher priority over others
		for (u32 i=0;i<11;i++)
		{
			if (p_ints & bit_value)
			{
				//for the first one , Set the L reg & exit
				Lval=GetL(i);
				break;
			}
			bit_value<<=1;	//next bit
		}
	}

	ArmInterruptChange(p_ints,Lval);
}

u32 UpdateSh4Ints()
{
	u32 p_ints = MCIEB->full & MCIPD->full;
	if (p_ints)
	{
		if ((*aica_params.SB_ISTEXT & SH4_IRQ_BIT )==0)
		{
			//if no interrupt is allready pending then raise one :)
			aica_params.RaiseInterrupt(holly_SPU_IRQ);
		}
	}
	else
	{
		if (*aica_params.SB_ISTEXT&SH4_IRQ_BIT)
		{
			aica_params.CancelInterrupt(holly_SPU_IRQ);
		}
	}

	return p_ints;
}

void ReadCommonReg(u32 reg,bool byte)
{
	switch(reg)
	{
	case 0x2808:
	case 0x2809:
		CommonData->MIEMP=1;
		CommonData->MOEMP=1;
		break;
	}
}

void WriteCommonReg8(u32 reg,u32 data)
{
	WriteMemArr(aica_reg,reg,data,1);
}

u32 FASTCALL libAICA_ReadMem_aica_reg(u32 addr,u32 sz)
{
	addr &= 0x7FFF;

	return 0;

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


void FASTCALL  libAICA_LLE_WriteMem_aica_reg(u32 addr,u32 data,u32 sz)
{	
	return;

	addr &= 0x7FFF;
	
	if (addr < 0x2000)
	{
		//Channel data
		u32 chan=addr>>7;
		u32 reg=addr&0x7F;
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
	switch (addr)
	{
	case SCIPD_addr:
		verify(sz!=1);
		if (data & (1<<5))
		{
			SCIPD->SCPU=1;
			update_arm_interrupts();
		}
		//Read olny
		return;

	case SCIRE_addr:
		{
			verify(sz!=1);
			SCIPD->full&=~(data /*& SCIEB->full*/ );	//is the & SCIEB->full needed ? doesn't seem like it
			data=0;//Write olny
			update_arm_interrupts();
		}
		break;

	case MCIPD_addr:
		if (data & (1<<5))
		{
			verify(sz!=1);
			MCIPD->SCPU=1;
			UpdateSh4Ints();
		}
		//Read olny
		return;

	case MCIRE_addr:
		{
			verify(sz!=1);
			MCIPD->full&=~data;
			UpdateSh4Ints();
			//Write olny
		}
		break;

	case TIMER_A:
		WriteMemArr(aica_reg,addr,data,sz);
		timers[0].RegisterWrite();
		break;

	case TIMER_B:
		WriteMemArr(aica_reg,addr,data,sz);
		timers[1].RegisterWrite();
		break;

	case TIMER_C:
		WriteMemArr(aica_reg,addr,data,sz);
		timers[2].RegisterWrite();
		break;

	default:
		WriteMemArr(aica_reg,addr,data,sz);
		break;
	}

	//should never come here
}

void FASTCALL libAICA_Update(u32 Cycles)
{
	while(Cycles>0)
	{
		Cycles--;
		SCIPD->SAMPLE_DONE=1;

		for (int i=0;i<3;i++)
			timers[i].StepTimer();
	}

	update_arm_interrupts();
	UpdateSh4Ints();
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
		return libAICA_LLE_WriteMem_aica_reg(addr,data,sz);
}
//Map using _vmem .. yay
void init_mem()
{
	aica_reg=(u8*)malloc(0x8000);
	memset(aica_reg,0,0x8000);

	CommonData=(CommonData_struct*)&aica_reg[0x2800];
	DSPData=(DSPData_struct*)&aica_reg[0x3000];

	SCIEB=(InterruptInfo*)&aica_reg[0x289C];
	SCIPD=(InterruptInfo*)&aica_reg[0x289C+4];
	SCIRE=(InterruptInfo*)&aica_reg[0x289C+8];
	//Main cpu (sh4)
	MCIEB=(InterruptInfo*)&aica_reg[0x28B4];
	MCIPD=(InterruptInfo*)&aica_reg[0x28B4+4];
	MCIRE=(InterruptInfo*)&aica_reg[0x28B4+8];

	for (int i=0;i<3;i++)
		timers[i].Init(aica_reg,i);
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
#define ACCESS_MASK (ARAM_MASK-(sz-1))

template<int sz,typename T>
T fastcall ReadMemArm(u32 addr)
{
	addr&=0x00FFFFFF;
	if (addr<0x800000)
	{
		T rv=*(T*)&aica_ram[addr&ACCESS_MASK];
		
		if (sz==4)
		{
			//32 bit misalligned reads: rotated
			u32 rot=(addr&3)<<3;
			return (rv>>rot) | (rv<<(32-rot));
		}
		else
			return rv;
	}
	else
	{
		return arm_ReadReg<sz,T>(addr);
	}
}

template<int sz,typename T>
void WriteMemArm(u32 addr,T data)
{
	addr&=0x00FFFFFF;
	if (addr<0x800000)
	{
		*(T*)&aica_ram[addr&ACCESS_MASK]=data;
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

