// drkMapleDevices.cpp : Defines the entry point for the DLL application.
//
#include "plugins/plugin_header.h"
#include <string.h>
#include <math.h>

#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include "dc/dc.h"
#include "../drkPvr/drkPvr.h"
#include "dc/sh4/rec_v2/ngen.h"

#if HOST_OS==OS_WII
	#include <wiiuse/wpad.h>
	#include "dc/dc.h"
#endif

extern u8 CodeCache[CODE_SIZE];
extern u32 LastAddr,pc;


#define key_CONT_C  (1 << 0)
#define key_CONT_B  (1 << 1)
#define key_CONT_A  (1 << 2)
#define key_CONT_START  (1 << 3)
#define key_CONT_DPAD_UP  (1 << 4)
#define key_CONT_DPAD_DOWN  (1 << 5)
#define key_CONT_DPAD_LEFT  (1 << 6)
#define key_CONT_DPAD_RIGHT  (1 << 7)
#define key_CONT_Z  (1 << 8)
#define key_CONT_Y  (1 << 9)
#define key_CONT_X  (1 << 10)
#define key_CONT_D  (1 << 11)
#define key_CONT_DPAD2_UP  (1 << 12)
#define key_CONT_DPAD2_DOWN  (1 << 13)
#define key_CONT_DPAD2_LEFT  (1 << 14)
#define key_CONT_DPAD2_RIGHT  (1 << 15)

u16 kcode[4]={0xFFFF,0xFFFF,0xFFFF,0xFFFF};
u32 vks[4]={0};
s8 joyx[4]={0},joyy[4]={0};
u8 rt[4]={0},lt[4]={0};

#include <pspctrl.h>

void UpdateInputState(u32 port)
{
	SceCtrlData pad;
	sceCtrlPeekBufferPositive(&pad, 1);
	//kcode,rt,lt,joyx,joyy
	joyx[port]=pad.Lx-128;
	joyy[port]=pad.Ly-128;
	rt[port]=pad.Buttons&PSP_CTRL_RTRIGGER?255:0;
	lt[port]=pad.Buttons&PSP_CTRL_LTRIGGER?255:0;

	kcode[port]=0xFFFF;
	if (pad.Buttons&PSP_CTRL_CROSS)
		kcode[port]&=~key_CONT_A;
	if (pad.Buttons&PSP_CTRL_CIRCLE)
		kcode[port]&=~key_CONT_B;
	if (pad.Buttons&PSP_CTRL_TRIANGLE)
		kcode[port]&=~key_CONT_Y;
	if (pad.Buttons&PSP_CTRL_SQUARE)
		kcode[port]&=~key_CONT_X;

	if (pad.Buttons&PSP_CTRL_START)
		kcode[port]&=~key_CONT_START;

	if (pad.Buttons&PSP_CTRL_SELECT){
		/*static int codeNum = 0;
		FILE*f=fopen("dynarec_code.bin","wb");
		if (!f) f=fopen("dynarec_code.bin","wb");
		fwrite(CodeCache,LastAddr,1,f);
		fclose(f);
		{
			#undef printf
			printf("%d\n",LastAddr);
		}*/
		settings.Enhancements.AspectRatioMode++;
	}

	if (pad.Buttons&PSP_CTRL_UP)
		kcode[port]&=~key_CONT_DPAD_UP;
	if (pad.Buttons&PSP_CTRL_DOWN)
		kcode[port]&=~key_CONT_DPAD_DOWN;
	if (pad.Buttons&PSP_CTRL_LEFT)
		kcode[port]&=~key_CONT_DPAD_LEFT;
	if (pad.Buttons&PSP_CTRL_RIGHT)
		kcode[port]&=~key_CONT_DPAD_RIGHT;
}
