// nullAICA.cpp : Defines the entry point for the DLL application.
//

#include "EmptyAICA.h"
#include "aica_hax.h"
#include "aica_hle.h"

using namespace HACK_AICA;
s32 FASTCALL libAICA_Load()
{
	return rv_ok;
}

void FASTCALL libAICA_Unload()
{
}
aica_init_params aparam;
s32 FASTCALL HACK_libAICA_Init(aica_init_params* param)
{
	aparam=*param;
	aica_ram=param->aica_ram;
	init_mem();
	InitHLE();

	return rv_ok;
} 

void FASTCALL HACK_libAICA_Term()
{
	term_mem();
	TermHLE();
}

void FASTCALL HACK_libAICA_Reset(bool Manual)
{
		ResetHLE();
}
