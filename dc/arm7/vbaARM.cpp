// nullAICA.cpp : Defines the entry point for the DLL application.
//

#include "vbaARM.h"
#include "aica.h"
#include "arm7.h"
#include "mem.h"
#include "plugins/plugin_manager.h"
#include "dc/aica/aica_if.h"

//called when plugin is used by emu (you should do first time init here)
aica_init_params aica_params;
arm_init_params arm_params;


s32 FASTCALL libAICA_Init(aica_init_params* param)
{
	memcpy(&aica_params,param, sizeof(aica_params));

	printf("AICA LLE\n");

	arm_params.aica_ram=param->aica_ram;
	arm_params.ReadMem_aica_reg = ReadMem_aica_reg;
	arm_params.WriteMem_aica_reg = WriteMem_aica_reg;

	init_mem();
	arm_Init();

	return rv_ok;
} 

//called when plugin is unloaded by emu , olny if dcInit is called (eg , not called to enumerate plugins)
void FASTCALL libAICA_Term()
{
	term_mem();
	//arm7_Term ?
}

//It's suposed to reset anything 
void FASTCALL libAICA_Reset(bool Manual)
{
	arm_Reset();
}

void FASTCALL SetResetState(u32 state)
{
	arm_SetEnabled(state==0);
}
