/*
	SPG emulation; Scanline/Raster beam registers & interrupts
	H blank interrupts aren't properly emulated
*/

#include "spg.h"
#include "Renderer_if.h"
#include "regs.h"
#include <psprtc.h>

u32 spg_InVblank=0;
s32 spg_ScanlineSh4CycleCounter;
u32 spg_ScanlineCount=512;
u32 spg_CurrentScanline=-1;
u32 spg_VblankCount=0;
u32 spg_LineSh4Cycles=0;
u32 spg_FrameSh4Cycles=0;

#define PROFILER_REG_BASE 0xBC400000
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

	if (FB_R_CTRL.vclk_div)
	{
		//VGA :)
		pixel_clock=PIXEL_CLOCK;
	}
	else
	{
		//It is half for NTSC/PAL
		pixel_clock=PIXEL_CLOCK/2;
	}

	//Derive the cycle counts from the pixel clock
	spg_ScanlineCount=SPG_LOAD.vcount+1;

	//Rounding errors here but meh
	spg_LineSh4Cycles=(u32)((u64)SH4_CLOCK*(u64)(SPG_LOAD.hcount+1)/(u64)pixel_clock);

	if (SPG_CONTROL.interlace)
	{
		//this is a temp hack and needs work ...
		spg_LineSh4Cycles/=2;
		u32 interl_mode=(VO_CONTROL>>4)&0xF;

		scale_y=1;
	}
	else
	{
		if ((SPG_CONTROL.NTSC == 0 && SPG_CONTROL.PAL ==0) ||
			(SPG_CONTROL.NTSC == 1 && SPG_CONTROL.PAL ==1))
		{
			scale_y=1.0f;//non interlaced vga mode has full resolution :)
		}
		else
			scale_y=0.5f;//non interlaced modes have half resolution
	}

	//rend_set_fb_scale(scale_x,scale_y);

	spg_FrameSh4Cycles=spg_ScanlineCount*spg_LineSh4Cycles;
}

u32 last_fps=0;
s32 render_end_pending_cycles;
bool render_end_pending;
//called from sh4 context , should update pvr/ta state and evereything else
void FASTCALL libPvr_UpdatePvr(u32 cycles)
{
	
	if (unlikely(spg_LineSh4Cycles == 0)) return;

	spg_ScanlineSh4CycleCounter += cycles;

	if (spg_ScanlineSh4CycleCounter > spg_LineSh4Cycles)//60 ~herz = 200 mhz / 60=3333333.333 cycles per screen refresh
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

		if (SPG_CONTROL.interlace)
			SPG_STATUS.fieldnum=~SPG_STATUS.fieldnum;
		else
			SPG_STATUS.fieldnum=0;

		SPG_STATUS.vsync=spg_InVblank;
		SPG_STATUS.scanline=spg_CurrentScanline;

		//Vblank/etc code
		if (spg_CurrentScanline == 0)
		{
			//Vblank counter
			spg_VblankCount++;

			
			// This turned out to be HBlank btw , needs to be emulated ;(
			params.RaiseInterrupt(holly_HBLank);
				
			u64 tdiff;
			sceRtcGetCurrentTick(&tdiff);
			if (tdiff - spg_last_vps >= 1000000)
			{
				spg_last_vps=tdiff;

				char fpsStr[256];
				const char* mode=0;
				const char* res=SPG_CONTROL.interlace?"480i":"240p";

				if (SPG_CONTROL.NTSC==0 && SPG_CONTROL.PAL==1)
					mode="PAL";
				else if (SPG_CONTROL.NTSC==1 && SPG_CONTROL.PAL==0)
					mode="NTSC";
				else
				{
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

		if (likely(render_end_pending))
		{
			if (render_end_pending_cycles<cycles)
			{
				params.RaiseInterrupt(holly_RENDER_DONE);
				params.RaiseInterrupt(holly_RENDER_DONE_isp);
				params.RaiseInterrupt(holly_RENDER_DONE_vd);
				rend_end_render();
			}

			render_end_pending_cycles-=cycles;
		}
		
}


bool spg_Init()
{
	return true;
}

void spg_Term()
{
}

void spg_Reset(bool Manual)
{
	CalculateSync();
}

