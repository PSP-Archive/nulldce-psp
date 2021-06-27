/*
	Highly inefficient and boring interpreter. Nothing special here
*/

#include "types.h"

#include "sh4_interpreter.h"
#include "sh4_opcode_list.h"
#include "sh4_registers.h"
#include "sh4_if.h"
#include "dc/pvr/pvr_if.h"
#include "dc/aica/aica_if.h"
#include "dmac.h"
#include "dc/gdrom/gdrom_if.h"
#include "dc/maple/maple_if.h"
#include "intc.h"
#include "tmu.h"
#include "dc/mem/sh4_mem.h"
//#include "dc/arm7/arm7.h"
#include "plugs/EmptyAICA/aica_hle.h"
#include "ccn.h"

#include "dc/sh4/sh4_sched.h"

#include <time.h>
#include <float.h>

#define SH4_TIMESLICE	(448)
#define CPU_RATIO		(2) 

//uh uh
volatile bool  sh4_int_bCpuRun=false;

#define GetN(str) ((str>>8) & 0xf)
#define GetM(str) ((str>>4) & 0xf)

void Sh4_int_Run()
{
	sh4_int_bCpuRun=true;

	s32 l=SH4_TIMESLICE;

	do
	{
		do
		{
			u32 op=ReadMem16(next_pc);		
			next_pc+=2;

			OpPtr[op](op);
			l-=CPU_RATIO;
		} while(l>0);
		l+=SH4_TIMESLICE;
		
		// model how the jit works
		// only check for sh4_int_bCpuRun on interrupts
		if (UpdateSystem()) {
			UpdateINTC();
			if (!sh4_int_bCpuRun)
				break;
		}

	} while(sh4_int_bCpuRun);

	sh4_int_bCpuRun=false;
}

void Sh4_int_Stop()
{
	if (sh4_int_bCpuRun)
	{
		sh4_int_bCpuRun=false;
	}
}

void Sh4_int_Step()
{
	if (sh4_int_bCpuRun)
	{
		printf("Sh4 Is running , can't step\n");
	}
	else
	{
		u32 op=ReadMem16(next_pc);
		next_pc+=2;
		ExecuteOpcode(op);
	}
}

void Sh4_int_Skip()
{
	if (sh4_int_bCpuRun)
	{
		printf("Sh4 Is running , can't Skip\n");
	}
	else
	{
		next_pc+=2;
	}
}

void Sh4_int_Reset(bool Manual)
{
	if (sh4_int_bCpuRun)
	{
		printf("Sh4 Is running , can't Reset\n");
	}
	else
	{
		next_pc = 0xA0000000;

		memset(r,0,sizeof(r));
		memset(r_bank,0,sizeof(r_bank));

		gbr=ssr=spc=sgr=dbr=vbr=0;
		mach=macl=pr=fpul=0;

		sr.SetFull(0x700000F0);
		old_sr=sr;
		UpdateSR();

		fpscr.full = 0x0004001;
		old_fpscr=fpscr;
		UpdateFPSCR();

		//Any more registers have default value ?
		printf("Sh4 Reset\n");
	}
}

void GenerateSinCos()
{
	/*printf("Generating sincos tables ...\n");
	wchar* path=GetEmuPath("data/fsca-table.bin");
	FILE* tbl=fopen(path,"rb");
	free(path);
	if (!tbl)
		die("fsca-table.bin is missing!");
	fread(sin_table,1,4*0x8000,tbl);
	fclose(tbl);

	for (int i=0x0000;i<0x8000;i++)
	{
		(u32&)sin_table[i]=host_to_le<4>((u32&)sin_table[i]);
	}

	for (int i=0x8000;i<0x10000;i++)
	{
		if (i==0x8000)
			sin_table[i]=0;
		else
			sin_table[i]=-sin_table[i-0x8000];
	}
	for (int i=0x10000;i<0x14000;i++)
	{
		sin_table[i]=sin_table[i&0xFFFF];//warp around for the last 0x4000 entries
	}*/
}
void Sh4_int_Init()
{
	BuildOpcodeTables();
}

void Sh4_int_Term()
{
	Sh4_int_Stop();
	printf("Sh4 Term\n");
}

bool Sh4_int_IsCpuRunning()
{
	return sh4_int_bCpuRun;
}

//TODO : Check for valid delayslot instruction
void ExecuteDelayslot()
{
	u32 op=IReadMem16(next_pc);
	next_pc+=2;
	if (op!=0)
		ExecuteOpcode(op);
}

void ExecuteDelayslot_RTE()
{
	sr.SetFull(ssr);

	ExecuteDelayslot();
}

//General update
s32 rtc_cycles=0;
u32 update_cnt = 0;
u32 gcp_timer=0;
u32 aica_sample_cycles=0;

bool clc_fk = false;

extern void aica_periodical(u32 cycl);

#define AICA_SAMPLE_GCM 441
#define AICA_SAMPLE_CYCLES (SH4_CLOCK/(44100/AICA_SAMPLE_GCM))

//448 Cycles (fixed)
int FASTCALL UpdateSystem()
{
	/*__asm__ 
	(
		"LW             $16, 0(%1)			\n"
		"ANDI			$5, $16, 0x7		\n"
		"BLTZ			$5, _NoMUpdate		\n"
		"ADDIU			$5, $0, 448			\n"
		"JAL			MediumUpdate		\n"
		"NOP								\n"
		"_NoMUpdate:						\n"
		"NOP								\n"
		: "+m"(update_cnt)
		: "r"(late_hack), "r"(Sh4cntx.interrupt_pend)
	);*/

	Sh4cntx.sh4_sched_next -= SH4_TIMESLICE;
    if (Sh4cntx.sh4_sched_next < 0)
        sh4_sched_tick(SH4_TIMESLICE);
	
	return Sh4cntx.interrupt_pend;// | (sh4_int_bCpuRun == false);
}

int UpdateSystem_INTC(){
	if (UpdateSystem())
        return UpdateINTC();
    else
        return 0;
}

void sh4_int_resetcache() { }
//Get an interface to sh4 interpreter
void Get_Sh4Interpreter(sh4_if* rv)
{
	rv->Run=Sh4_int_Run;
	rv->Stop=Sh4_int_Stop;
	rv->Step=Sh4_int_Step;
	rv->Skip=Sh4_int_Skip;
	rv->Reset=Sh4_int_Reset;
	rv->Init=Sh4_int_Init;
	rv->Term=Sh4_int_Term;
	rv->IsCpuRunning=Sh4_int_IsCpuRunning;

	rv->ResetCache=sh4_int_resetcache;
}

