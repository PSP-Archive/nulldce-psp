/*
	SPG emulation; Scanline/Raster beam registers & interrupts
	H blank interrupts aren't properly emulated
*/

#include "spg.h"
#include "Renderer_if.h"
#include "regs.h"
#include <psprtc.h>

#include "dc/sh4/sh4_sched.h"

u32 spg_InVblank=0;
s32 spg_ScanlineSh4CycleCounter;
u32 spg_ScanlineCount=512;
u32 spg_CurrentScanline=-1;
u32 spg_VblankCount=0;
u32 spg_LineSh4Cycles=0;
u32 spg_FrameSh4Cycles=0;

u32 vblank_count_monotonic = 0;

int render_end_schid;
static int vblank_schid;
static int time_sync;

#define PROFILER_REG_BASE 0x1C400000
#define PROFILER_REG_COUNT 21

void usrDebugProfilerEnable(void)
{
	_sw(1, PROFILER_REG_BASE);
}

void usrDebugProfilerDisable(void)
{
	_sw(0, PROFILER_REG_BASE);
	asm("sync\r\n");
}

void usrDebugProfilerClear(void)
{
	u32 addr;
	int i;

	addr = PROFILER_REG_BASE;
	
	for(i = 1; i < PROFILER_REG_COUNT; i++)
	{
		addr += 4;
		_sw(0, addr);
	}
}


double spg_last_vps=0;
#if HOST_OS == OS_PSP
extern "C"
{

	typedef struct _PspDebugProfilerRegs2
	{
		volatile u32 enable;
		volatile u32 systemck;
		volatile u32 cpuck;
		//volatile u32 totalstall; ??
		volatile u32 internal;
		volatile u32 memory;
		volatile u32 copz;
		volatile u32 vfpu;
		volatile u32 sleep;
		volatile u32 bus_access;
		volatile u32 uncached_load;
		volatile u32 uncached_store;
		volatile u32 cached_load;
		volatile u32 cached_store;
		volatile u32 i_miss;
		volatile u32 d_miss;
		volatile u32 d_writeback;
		volatile u32 cop0_inst;
		volatile u32 fpu_inst;
		volatile u32 vfpu_inst;
		volatile u32 local_bus;
		volatile u32 waste[5];
	} PspDebugProfilerRegs2;

	void usrDebugProfilerGetRegs(PspDebugProfilerRegs2 *regs);
}
#endif


//54 mhz pixel clock (actually, this is defined as 27 .. why ? --drk)
#define PIXEL_CLOCK (54*1000*1000/2)


//Called when spg registers are updated
void CalculateSync()
{
	u32 pixel_clock;
	float scale_x=1,scale_y=1;

	pixel_clock =  (FB_R_CTRL.vclk_div ? PIXEL_CLOCK : PIXEL_CLOCK / 2);

	//Derive the cycle counts from the pixel clock
	spg_ScanlineCount=SPG_LOAD.vcount+1;

	//Rounding errors here but meh
	spg_LineSh4Cycles=(u32)((u64)SH4_CLOCK*(u64)(SPG_LOAD.hcount+1)/(u64)pixel_clock);

	if (SPG_CONTROL.interlace)
	{
		//this is a temp hack and needs work ...
		spg_LineSh4Cycles/=2;
		scale_y=1;
	}
	else
	{
		 if (FB_R_CTRL.vclk_div)
		{
			scale_y = 1.0f;//non interlaced VGA mode has full resolution :)
		}
		else
		{
			scale_y = 0.5f;//non interlaced modes have half resolution
		}
	}

	//rend_set_fb_scale(scale_x,scale_y);

	spg_FrameSh4Cycles=spg_ScanlineCount*spg_LineSh4Cycles;
	spg_CurrentScanline = 0;

	sh4_sched_request(vblank_schid, spg_LineSh4Cycles);
}

u32 last_fps=0;
s32 render_end_pending_cycles;
bool render_end_pending;
char fpsStr[256];

int elapse_time(int tag, int cycl, int jit)
{
	return min(max(spg_FrameSh4Cycles, (u32)1 * 1000 * 1000), (u32)8 * 1000 * 1000);
}

//called from sh4 context , should update pvr/ta state and evereything else
int spg_line_sched(int tag, int cycl, int jit)
{
	spg_ScanlineSh4CycleCounter += cycl;

	while (spg_ScanlineSh4CycleCounter >= spg_LineSh4Cycles)//60 ~herz = 200 mhz / 60=3333333.333 cycles per screen refresh
	{
		//ok .. here , after much effort , a full scanline was emulated !
		//now , we must check for raster beam interupts and vblank
		spg_CurrentScanline=(spg_CurrentScanline+1)%spg_ScanlineCount;
		spg_ScanlineSh4CycleCounter -= spg_LineSh4Cycles;
		
		//Test for scanline interrupts

		if (SPG_VBLANK_INT.vblank_in_interrupt_line_number == spg_CurrentScanline)
			params.RaiseInterrupt(holly_SCANINT1);

		if (SPG_VBLANK_INT.vblank_out_interrupt_line_number == spg_CurrentScanline)
			params.RaiseInterrupt(holly_SCANINT2);

		if (SPG_VBLANK.vbstart == spg_CurrentScanline)
			spg_InVblank=1;

		if (SPG_VBLANK.vbend == spg_CurrentScanline)
			spg_InVblank=0;

		SPG_STATUS.vsync=spg_InVblank;
		SPG_STATUS.scanline=spg_CurrentScanline;

		//Vblank/etc code
		if (spg_CurrentScanline == 0)
		{
			if (SPG_CONTROL.interlace)
				SPG_STATUS.fieldnum=~SPG_STATUS.fieldnum;
			else
				SPG_STATUS.fieldnum=0;

			//Vblank counter
			spg_VblankCount++;
			
			// This turned out to be HBlank btw , needs to be emulated ;(
			params.RaiseInterrupt(holly_HBLank);
				
			u64 tdiff;
			sceRtcGetCurrentTick(&tdiff);
			if (tdiff - spg_last_vps >= 1000000)
			{
				spg_last_vps=tdiff;

				const char* mode=0;
				const char* res=SPG_CONTROL.interlace?"480i":"240p";

				if (!SPG_CONTROL.interlace){
					settings.Enhancements.AspectRatioMode = 5;
				}

				if (SPG_CONTROL.NTSC==0 && SPG_CONTROL.PAL==1)
					mode="PAL";
				else if (SPG_CONTROL.NTSC==1 && SPG_CONTROL.PAL==0)
					mode="NTSC";
				else
				{
					settings.Enhancements.AspectRatioMode = 1;
					res=SPG_CONTROL.interlace?"480i":"480p";
					mode="VGA";
				}

				sprintf(fpsStr,"FPS GPU: %d VBLANK: %d MODE: %s RES: %s",
					FrameCount,
					spg_VblankCount,
					mode,res);

				VertexCount=0;
				FrameCount=0;
				spg_VblankCount=0;

				rend_set_fps_text(fpsStr);

				//notify for vblank :)
				rend_vblank();

			}
		}
	}

	u32 min_scanline = spg_CurrentScanline + 1;
	u32 min_active = spg_ScanlineCount;

	if (min_scanline < SPG_VBLANK_INT.vblank_in_interrupt_line_number)
		min_active = min(min_active, SPG_VBLANK_INT.vblank_in_interrupt_line_number);

	if (min_scanline < SPG_VBLANK_INT.vblank_out_interrupt_line_number)
		min_active = min(min_active, SPG_VBLANK_INT.vblank_out_interrupt_line_number);

	if (min_scanline < SPG_VBLANK.vbstart)
		min_active = min(min_active, SPG_VBLANK.vbstart);

	if (min_scanline < SPG_VBLANK.vbend)
		min_active = min(min_active, SPG_VBLANK.vbend);

	if (min_scanline < spg_ScanlineCount)
		min_active = min(min_active, spg_ScanlineCount);

	min_active = max(min_active, min_scanline);

	return (min_active - spg_CurrentScanline) * spg_LineSh4Cycles;
		
}

int rend_end_sch(int tag, int cycl, int jitt)
{
	params.RaiseInterrupt(holly_RENDER_DONE);
	params.RaiseInterrupt(holly_RENDER_DONE_isp);
	params.RaiseInterrupt(holly_RENDER_DONE_vd);
	rend_end_render();
	return 0;
}


bool spg_Init()
{
	render_end_schid = sh4_sched_register(0, rend_end_sch);
	vblank_schid = sh4_sched_register(0, spg_line_sched);
	time_sync = sh4_sched_register(0, elapse_time);

	sh4_sched_request(time_sync, 8 * 1000 * 1000);

	vblank_count_monotonic = 0;
		
	return true;
}

void spg_Term()
{
}

void spg_Reset(bool Manual)
{
	CalculateSync();

	vblank_count_monotonic = 0;
}

