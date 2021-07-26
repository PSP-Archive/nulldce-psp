#include "types.h"

#if HOST_OS==OS_WINDOWS
#include <windows.h>
#elif HOST_OS==OS_LINUX
#include <unistd.h>
#include <sys/mman.h>
#endif

#include "../sh4_interpreter.h"
#include "../sh4_opcode_list.h"
#include "../sh4_registers.h"
#include "../sh4_if.h"
#include "../dmac.h"
#include "../intc.h"
#include "../tmu.h"

#include "dc/mem/sh4_mem.h"
#include "dc/pvr/pvr_if.h"
#include "dc/aica/aica_if.h"
#include "dc/gdrom/gdrom_if.h"

#include <time.h>
#include <float.h>

#include "blockmanager.h"
#include "ngen.h"
#include "decoder.h"

#ifndef HOST_NO_REC
//uh uh
extern volatile bool  sh4_int_bCpuRun;


#if HOST_OS != OS_LINUX
u8 CodeCache[CODE_SIZE];
#elif HOST_OS == OS_LINUX
u8 pMemBuff[CODE_SIZE+4095];
u8 *CodeCache;
#endif


u32 LastAddr;
u32 LastAddr_min;
u32* emit_ptr=0;
void* emit_GetCCPtr() { return emit_ptr==0?(void*)&CodeCache[LastAddr]:(void*)emit_ptr; }
void emit_SetBaseAddr() { LastAddr_min = LastAddr; }
void emit_WriteCodeCache()
{
	wchar path[512];
	sprintf(path,"code_cache_%08X.bin",CodeCache);
	wchar* pt2=GetEmuPath(path);
	printf("Writing code cache to %s\n",pt2);
	FILE*f=fopen(pt2,"wb");
	if (f)
	{
		free(pt2);
		fwrite(CodeCache,LastAddr,1,f);
		fclose(f);
		printf("Writen!\n");
	}
}
void recSh4_ClearCache()
{
	LastAddr=LastAddr_min;
	bm_Reset();

	printf("recSh4:Dynarec Cache clear at %08X\n",curr_pc);
}

u32 startAddr = 0;

void StartCodeDump(){
	startAddr = LastAddr;
}

void CodeDump(const char * filename){
	FILE*f=fopen(filename,"wb");
	fwrite(&CodeCache[startAddr],LastAddr-startAddr,1,f);
	fclose(f);
}

void recSh4_Run()
{
	sh4_int_bCpuRun=true;

	ngen_mainloop();

	//sh4_int_bCpuRun=false;
}

void emit_Write8(u8 data)
{
	verify(!emit_ptr);
	CodeCache[LastAddr]=data;
	LastAddr+=1;
}
void emit_Write16(u16 data)
{
	verify(!emit_ptr);

	*(u16*)&CodeCache[LastAddr]=data;
	LastAddr+=2;
}

void emit_Write32(u32 data)
{
	if (emit_ptr)
	{
		*emit_ptr=data;
		emit_ptr++;
	}
	else
	{
		*(u32*)&CodeCache[LastAddr]=data;
		LastAddr+=4;
	}
}
void emit_Skip(u32 sz)
{
	LastAddr+=sz;
}
u32 emit_FreeSpace()
{
	return CODE_SIZE-LastAddr;
}


bool DoCheck(u32 pc, u32 len)
{
	// is on bios or such
	if (!GetMemPtr(pc, len))
	{
		return false;
	}

	if (IsOnRam(pc))
	{
		pc&=0xFFFFFF;
		switch(pc)
		{
			//DOA2LE
			case 0x3DAFC6:
			case 0x3C83F8:

			//Shenmue 2
			case 0x348000:
				
			//Shenmue
			case 0x41860e:
				return true;

			default:
				return false;
		}
	}
	
	return false;
}

u32 dynarecIdle = 2;
bool BETcondPatch = false;

void AnalyseBlock(DecodedBlock* blk);
DynarecCodeEntry* rdv_CompileBlock(u32 bpc)
{
	DecodedBlock* blk=dec_DecodeBlock(bpc,fpscr,SH4_TIMESLICE/2);
	AnalyseBlock(blk);
	DynarecCodeEntry* rv=ngen_Compile(blk,DoCheck(blk->start,blk->sh4_code_size));
	dec_Cleanup();
	return rv;
}
u32 rdv_FailedToFindBlock_pc;

DynarecCodeEntry* rdv_CompilePC()
{
	u32 pc=next_pc;
	DynarecCodeEntry* rv;

	rv=rdv_CompileBlock(pc);
	
	if (rv==0)
	{
		recSh4_ClearCache();
		return rdv_CompilePC();
	}
	bm_AddCode(pc,rv);

	//check if the game or bootloader boots, and if so reset the dynarec cache
	//also make sure to never cache the boot addresses
	if ((pc&0xFFFFFF)==0x08300 || (pc&0xFFFFFF)==0x10000)
	{
		//**NOTE** the block has to still be valid after this
		//or it'l crash
		recSh4_ClearCache();
	}

	return rv;
}

DynarecCodeEntry* rdv_FailedToFindBlock()
{
	next_pc=rdv_FailedToFindBlock_pc;

	//printf("rdv_FailedToFindBlock ~ %08X\n",next_pc);

	return rdv_CompilePC();
}

DynarecCodeEntry* FASTCALL rdv_BlockCheckFail(u32 pc)
{
	next_pc=pc;
	recSh4_ClearCache();
	return rdv_CompilePC();
}

DynarecCodeEntry* rdv_FindCode()
{
	DynarecCodeEntry* rv=bm_GetCode(next_pc);
	if (rv==ngen_FailedToFindBlock)
		return 0;
	
	return rv;
}

DynarecCodeEntry* rdv_FindOrCompile()
{
	DynarecCodeEntry* rv=bm_GetCode(next_pc);
	if (rv==ngen_FailedToFindBlock)
		rv=rdv_CompilePC();
	
	return rv;
}
void recSh4_Stop()
{
	Sh4_int_Stop();
}

void recSh4_Step()
{
	Sh4_int_Step();
}

void recSh4_Skip()
{
	Sh4_int_Skip();
}

void recSh4_Reset(bool Manual)
{
	Sh4_int_Reset(Manual);
}

void recSh4_Init()
{
	printf("recSh4 Init\n");
	Sh4_int_Init();
	bm_Reset();

#if HOST_OS == OS_WINDOWS
	DWORD old;
	VirtualProtect(CodeCache,CODE_SIZE,PAGE_EXECUTE_READWRITE,&old);
#elif HOST_OS == OS_LINUX

	//some overcomplicated code
    CodeCache = (u8*)(((unat)pMemBuff+4095)& ~4095);

    printf("\n\t CodeCache addr: %p | from: %p | addr here: %p\n", CodeCache, pMemBuff, recSh4_Init);

    if (mprotect(CodeCache, CODE_SIZE, PROT_EXEC|PROT_READ|PROT_WRITE)) {
        perror("\n\tError,Couldn’t mprotect CodeCache!");
        verify(false);
    }

    memset(CodeCache,0xCD,CODE_SIZE);

#endif

	/*#define ADDR_8BIT(psp_base_addr, sh4_base_addr, sh_size,sz) ((addr % sz) >> 21) & 255
	#define ADDR_32BIT(psp_base_addr, sh4_base_addr, sh_size,sz) psp_base_addr + ((sh4_base_addr - sh_size) % sz)

	for (int i = 0x00000000; i < 0x00800000; i+= 0x200000) Sh4cntx.mem_lut[i/0x200000] =  ADDR_32BIT(0x0B800000, i, 0, ARAM_SIZE);
	for (int i = 0x00800000; i < 0x01000000; i+= 0x200000) Sh4cntx.mem_lut[i/0x200000] =  ADDR_32BIT(0x0B800000, i, 0x00800000, ARAM_SIZE);
	for (int i = 0x20000000; i < 0x20000000 + ARAM_SIZE; i+= 0x200000) Sh4cntx.mem_lut[i/0x200000] =  ADDR_32BIT(0x0B800000 , i, 0x20000000, ARAM_SIZE);

    for (int i = 0x04000000; i < 0x05000000; i+= 0x200000) Sh4cntx.mem_lut[i/0x200000] =  ADDR_32BIT(0x0B000000 , i, 0x04000000, VRAM_SIZE);
    for (int i = 0x06000000; i < 0x07000000; i+= 0x200000) Sh4cntx.mem_lut[i/0x200000] =  ADDR_32BIT(0x0B000000 , i,  0x06000000, VRAM_SIZE);

    for (int i = 0x0C000000; i < 0x10000000; i+= 0x200000) Sh4cntx.mem_lut[i/0x200000] =  ADDR_32BIT(0x0A000000 , i, 0x0C000000,  RAM_SIZE);

	FILE * f = fopen("lut_dump.txt","w");
	for (int i = 0; i < 256; i++){
		fprintf(f,"SH addr: %x --> PSP Mapped: %x\n", (i * 0x200000),Sh4cntx.mem_lut[i]);
	}
	fclose(f);*/

	/*#define Unused_ADDR 0x0B800000
	#define ARAM_ADDR   0x0B800000
	#define VRAM_ADDR   0x0B000000
	#define RAM_ADDR    0x0A000000 

	memset(Sh4cntx.mem_lut,Unused_ADDR,255); //Map everything to aica mem (even unused)

	for (int i = 0b00010000; i < 0b00010100; i++) Sh4cntx.mem_lut[i] =  VRAM_ADDR;
	for (int i = 0b00011000; i < 0b00011100; i++) Sh4cntx.mem_lut[i] =  VRAM_ADDR;
	for (int i = 0b00110000; i < 0b01000000; i++) Sh4cntx.mem_lut[i] =  RAM_ADDR; */

	memset(CodeCache,0,CODE_SIZE);
}

void recSh4_Term()
{
	printf("recSh4 Term\n");
	Sh4_int_Term();

#if HOST_OS == OS_LINUX
	//hum ?
#endif
}

bool recSh4_IsCpuRunning()
{
	return Sh4_int_IsCpuRunning();
}
#endif

void Get_Sh4Recompiler(sh4_if* rv)
{
    #ifdef HOST_NO_REC
    Get_Sh4Interpreter(rv);
    #else
	rv->Run=recSh4_Run;
	rv->Stop=recSh4_Stop;
	rv->Step=recSh4_Step;
	rv->Skip=recSh4_Skip;
	rv->Reset=recSh4_Reset;
	rv->Init=recSh4_Init;
	rv->Term=recSh4_Term;
	rv->IsCpuRunning=recSh4_IsCpuRunning;
	//rv->GetRegister=Sh4_int_GetRegister;
	//rv->SetRegister=Sh4_int_SetRegister;
	rv->ResetCache=recSh4_ClearCache;
	#endif
}

