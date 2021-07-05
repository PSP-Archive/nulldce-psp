#include "config.h"


#if HOST_OS==OS_WINDOWS
#include <windows.h>
#endif

#include "nullRend.h"
#include "regs.h"
#include "threaded.h"

#include "dc/mem/sh4_mem.h"
#include "dc/sh4/sh4_sched.h"

#define likely(x) __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)

extern int render_end_schid;

using namespace TASplitter;
#if HOST_SYS == SYS_PSP
#include <pspgu.h>
#include <pspgum.h>
#include <pspdisplay.h>
#include <dirent.h>
#include <pspctrl.h>
#include <pspkernel.h>
#include <pspusb.h>
#include <pspsdk.h>

#include "pspDmac.h"
#include "me.h"


static unsigned int staticOffset = 0;

volatile bool FB_DIRTY;

bool changed = true;


static unsigned int getMemorySize(unsigned int width, unsigned int height, unsigned int psm)
{
	switch (psm)
	{
		case GU_PSM_T4:
			return (width * height) >> 1;

		case GU_PSM_T8:
			return width * height;

		case GU_PSM_5650:
		case GU_PSM_5551:
		case GU_PSM_4444:
		case GU_PSM_T16:
			return 2 * width * height;

		case GU_PSM_8888:
		case GU_PSM_T32:
			return 4 * width * height;

		default:
			return 0;
	}
}

void* getStaticVramBuffer(unsigned int width, unsigned int height, unsigned int psm)
{
	unsigned int memSize = getMemorySize(width,height,psm);
	void* result = (void*)staticOffset;
	staticOffset += memSize;

	return result;
}

void* getStaticVramTexture(unsigned int width, unsigned int height, unsigned int psm)
{
	void* result = getStaticVramBuffer(width,height,psm);
	return (void*)(((unsigned int)result) + ((unsigned int)sceGeEdramGetAddr()));
}

#else
	//#define ALIGN16
	#include "win-x86/plugs/drkPvr/gu_emu.h"
#endif

float PSP_DC_AR[][2] =
{
	{640,480},//FS, Streched
	{847,480},//FS, Aspect correct, Extra geom
	{614,460},//NTSC Safe text area (for the most part),Streched
	{812,460},//NTSC Safe text area, Aspect Correct, Extra geom
	{640,362},//Partial H, Apsect correct
	{660,500},//Partial H,Aspect correct, Extra geom
};


static unsigned int ALIGN16 list[262144];
bool PSP_720_480 = false;
bool pal_needs_update=true;
u32 BUF_WIDTH=512;
u32 SCR_WIDTH=480;
u32 SCR_HEIGHT=272;

int palette_index = 0;

ScePspFMatrix4 curr_mtx;
float dc_width,dc_height;

void* fbp0;
void* fbp1;
void* zbp;
u32 __attribute__((aligned(16))) palette_lut[1024];

const u32 MipPoint[8] =
{
	0x00006,//8
	0x00016,//16
	0x00056,//32
	0x00156,//64
	0x00556,//128
	0x01556,//256
	0x05556,//512
	0x15556//1024
};


struct Vertex
{
	float u,v;
    unsigned int col;
    float x, y, z;
};

struct VertexList
{
	union
	{
		Vertex* ptr;
		s32 count;
	};
};

struct PolyParam
{
	PCW pcw;
	ISP_TSP isp;

	TSP tsp;
	TCW tcw;
};

const u32 PalFMT[4]=
{
	GU_PSM_5551,
	GU_PSM_5650,
	GU_PSM_4444,
	GU_PSM_8888,
};

Vertex  ALIGN16 vertices[42*1024]; //(Vertex*)(getStaticVramTexture(480,512, GU_PSM_4444));
VertexList  ALIGN16 lists[8*1024];
PolyParam  ALIGN16 listModes[8*1024];

float unkpack_bgp_to_float[256];

Vertex * curVTX=vertices;
VertexList* curLST=lists;
VertexList* TransLST=0;
PolyParam* curMod=listModes-1;
bool global_regd;
float vtx_min_Z;
float vtx_max_Z;

bool _doRender = true;

//no rendering .. yay (?)
namespace NORenderer
{
	char fps_text[512];

	struct VertexDecoder;
	FifoSplitter<VertexDecoder> TileAccel;


	#define ABGR8888(x) ((x&0xFF00FF00) |((x>>16)&0xFF) | ((x&0xFF)<<16))
	#define ABGR4444(x) ((x&0xF0F0) |((x>>8)&0xF) | ((x&0xF)<<8))
	#define ABGR0565(x) ((x&(0x3F<<5)) |((x>>11)&0x1F) | ((x&0x1F)<<11))
	#define ABGR1555(x) ((x&0x83E0) |((x>>10)&0x1F) | ((x&0x1F)<<10))

	#define ABGR4444_A(x) ((x)>>12)
	#define ABGR4444_R(x) ((x>>8)&0xF)
	#define ABGR4444_G(x) ((x>>4)&0xF)
	#define ABGR4444_B(x) ((x)&0xF)

	#define ABGR0565_R(x) ((x)>>11)
	#define ABGR0565_G(x) ((x>>5)&0x3F)
	#define ABGR0565_B(x) ((x)&0x1F)

	#define ABGR1555_A(x) ((x>>15))
	#define ABGR1555_R(x) ((x>>10)&0x1F)
	#define ABGR1555_G(x) ((x>>5)&0x1F)
	#define ABGR1555_B(x) ((x)&0x1F)


	#define clamp(minv,maxv,x) min(maxv,max(minv,x))

	#define PSP_DC_AR_COUNT 6

	u32 YUV422(s32 Y,s32 Yu,s32 Yv)
	{
		Yu-=128;
		Yv-=128;

		s32 R = Y + Yv*11/8;            // Y + (Yv-128) * (11/8) ?
		s32 G = Y - (Yu*11 + Yv*22)/32; // Y - (Yu-128) * (11/8) * 0.25 - (Yv-128) * (11/8) * 0.5 ?
		s32 B = Y + Yu*110/64;          // Y + (Yu-128) * (11/8) * 1.25 ?

		return clamp(0, 255, R) | (clamp(0, 255, G) << 8) | (clamp(0, 255, B) << 16) | 0xFF000000;
	}


	u32 old_aspectRatio = 0;

	void VBlank()
	{
		if (unlikely(old_aspectRatio != settings.Enhancements.AspectRatioMode)){ 
			changed = true;
			old_aspectRatio = settings.Enhancements.AspectRatioMode;

			dc_width=PSP_DC_AR[settings.Enhancements.AspectRatioMode%PSP_DC_AR_COUNT][0];
			dc_height=PSP_DC_AR[settings.Enhancements.AspectRatioMode%PSP_DC_AR_COUNT][1];
		}

		if (FB_R_CTRL.fb_enable && !VO_CONTROL.blank_video) _doRender = true;
	}

	//pixel convertors !
#define pixelcvt_start(name,x,y)  \
	struct name \
	{ \
	static const u32 xpp=x;\
	static const u32 ypp=y;	\
	__forceinline static void fastcall Convert(u16* pb,u32 pbw,u8* data) \
	{

#define pixelcvt_end } }
#define pixelcvt_next(name,x,y) pixelcvt_end;  pixelcvt_start(name,x,y)

	#define pixelcvt_startVQ(name,x,y)  \
	struct name \
	{ \
	static const u32 xpp=x;\
	static const u32 ypp=y;	\
	__forceinline static u32 fastcall Convert(u16* data) \
	{

#define pixelcvt_endVQ } }
#define pixelcvt_nextVQ(name,x,y) pixelcvt_endVQ;  pixelcvt_startVQ(name,x,y)

	inline void pb_prel(u16* dst,u32 x,u32 col)
	{
		dst[x]=col;
	}
	inline void pb_prel(u16* dst,u32 pbw,u32 x,u32 y,u32 col)
	{
		dst[x+pbw*y]=col;
	}
	//Non twiddled
	pixelcvt_start(conv565_PL,4,1)
	{
		//convert 4x1 565 to 4x1 8888
		u16* p_in=(u16*)data;
		//0,0
		pb_prel(pb,0,ABGR0565(p_in[0]));
		//1,0
		pb_prel(pb,1,ABGR0565(p_in[1]));
		//2,0
		pb_prel(pb,2,ABGR0565(p_in[2]));
		//3,0
		pb_prel(pb,3,ABGR0565(p_in[3]));
	}
	pixelcvt_next(conv1555_PL,4,1)
	{
		//convert 4x1 1555 to 4x1 8888
		u16* p_in=(u16*)data;
		//0,0
		pb_prel(pb,0,ABGR1555(p_in[0]));
		//1,0
		pb_prel(pb,1,ABGR1555(p_in[1]));
		//2,0
		pb_prel(pb,2,ABGR1555(p_in[2]));
		//3,0
		pb_prel(pb,3,ABGR1555(p_in[3]));
	}
	pixelcvt_next(conv4444_PL,4,1)
	{
		//convert 4x1 4444 to 4x1 8888
		u16* p_in=(u16*)data;
		//0,0
		pb_prel(pb,0,ABGR4444(p_in[0]));
		//1,0
		pb_prel(pb,1,ABGR4444(p_in[1]));
		//2,0
		pb_prel(pb,2,ABGR4444(p_in[2]));
		//3,0
		pb_prel(pb,3,ABGR4444(p_in[3]));
	}
	pixelcvt_next(convYUV_PL,4,1)
	{
		//convert 4x1 4444 to 4x1 8888
		u32* p_in=(u32*)data;


		s32 Y0 = (p_in[0]>>8) &255; //
		s32 Yu = (p_in[0]>>0) &255; //p_in[0]
		s32 Y1 = (p_in[0]>>24) &255; //p_in[3]
		s32 Yv = (p_in[0]>>16) &255; //p_in[2]

		//0,0
		pb_prel(pb,0,YUV422(Y0,Yu,Yv));
		//1,0
		pb_prel(pb,1,YUV422(Y1,Yu,Yv));

		//next 4 bytes
		p_in+=1;

		Y0 = (p_in[0]>>8) &255; //
		Yu = (p_in[0]>>0) &255; //p_in[0]
		Y1 = (p_in[0]>>24) &255; //p_in[3]
		Yv = (p_in[0]>>16) &255; //p_in[2]

		//0,0
		pb_prel(pb,2,YUV422(Y0,Yu,Yv));
		//1,0
		pb_prel(pb,3,YUV422(Y1,Yu,Yv));
	}
	pixelcvt_end;
	//twiddled
	pixelcvt_start(conv565_TW,2,2)
	{
		//convert 4x1 565 to 4x1 8888
		u16* p_in=(u16*)data;
		//0,0
		pb_prel(pb,pbw,0,0,ABGR0565(p_in[0]));
		//0,1
		pb_prel(pb,pbw,0,1,ABGR0565(p_in[1]));
		//1,0
		pb_prel(pb,pbw,1,0,ABGR0565(p_in[2]));
		//1,1
		pb_prel(pb,pbw,1,1,ABGR0565(p_in[3]));
	}
	pixelcvt_next(convPAL4_TW,4,4)
	{
		u8* p_in=(u8*)data;
		u32* pal=&palette_lut[palette_index];

		pb_prel(pb,pbw,0,0,pal[p_in[0]&0xF]);
		pb_prel(pb,pbw,0,1,pal[(p_in[0]>>4)&0xF]);p_in++;
		pb_prel(pb,pbw,1,0,pal[p_in[0]&0xF]);
		pb_prel(pb,pbw,1,1,pal[(p_in[0]>>4)&0xF]);p_in++;
				
		pb_prel(pb,pbw,0,2,pal[p_in[0]&0xF]);
		pb_prel(pb,pbw,0,3,pal[(p_in[0]>>4)&0xF]);p_in++;
		pb_prel(pb,pbw,1,2,pal[p_in[0]&0xF]);
		pb_prel(pb,pbw,1,3,pal[(p_in[0]>>4)&0xF]);p_in++;
				
		pb_prel(pb,pbw,2,0,pal[p_in[0]&0xF]);
		pb_prel(pb,pbw,2,1,pal[(p_in[0]>>4)&0xF]);p_in++;
		pb_prel(pb,pbw,3,0,pal[p_in[0]&0xF]);
		pb_prel(pb,pbw,3,1,pal[(p_in[0]>>4)&0xF]);p_in++;
				
		pb_prel(pb,pbw,2,2,pal[p_in[0]&0xF]);
		pb_prel(pb,pbw,2,3,pal[(p_in[0]>>4)&0xF]);p_in++;
		pb_prel(pb,pbw,3,2,pal[p_in[0]&0xF]);
		pb_prel(pb,pbw,3,3,pal[(p_in[0]>>4)&0xF]);p_in++;
	}
	pixelcvt_next(convPAL8_TW,2,4)
	{
		u8* p_in=(u8*)data;
		u32* pal=&palette_lut[palette_index];

		pb_prel(pb,pbw,0,0,pal[p_in[0]]);p_in++;
		pb_prel(pb,pbw,0,1,pal[p_in[0]]);p_in++;
		pb_prel(pb,pbw,1,0,pal[p_in[0]]);p_in++;
		pb_prel(pb,pbw,1,1,pal[p_in[0]]);p_in++;

		pb_prel(pb,pbw,0,2,pal[p_in[0]]);p_in++;
		pb_prel(pb,pbw,0,3,pal[p_in[0]]);p_in++;
		pb_prel(pb,pbw,1,2,pal[p_in[0]]);p_in++;
		pb_prel(pb,pbw,1,3,pal[p_in[0]]);p_in++;
	}
	pixelcvt_next(conv1555_TW,2,2)
	{
		//convert 4x1 1555 to 4x1 8888
		u16* p_in=(u16*)data;
		//0,0
		pb_prel(pb,pbw,0,0,ABGR1555(p_in[0]));
		//0,1
		pb_prel(pb,pbw,0,1,ABGR1555(p_in[1]));
		//1,0
		pb_prel(pb,pbw,1,0,ABGR1555(p_in[2]));
		//1,1
		pb_prel(pb,pbw,1,1,ABGR1555(p_in[3]));
	}
	pixelcvt_next(conv4444_TW,2,2)
	{
		//convert 4x1 4444 to 4x1 8888
		u16* p_in=(u16*)data;
		//0,0
		pb_prel(pb,pbw,0,0,ABGR4444(p_in[0]));
		//0,1
		pb_prel(pb,pbw,0,1,ABGR4444(p_in[1]));
		//1,0
		pb_prel(pb,pbw,1,0,ABGR4444(p_in[2]));
		//1,1
		pb_prel(pb,pbw,1,1,ABGR4444(p_in[3]));
	}
	pixelcvt_next(convYUV422_TW,2,2)
	{
		//convert 4x1 4444 to 4x1 8888
		u16* p_in=(u16*)data;


		s32 Y0 = (p_in[0]>>8) &255; //
		s32 Yu = (p_in[0]>>0) &255; //p_in[0]
		s32 Y1 = (p_in[2]>>8) &255; //p_in[3]
		s32 Yv = (p_in[2]>>0) &255; //p_in[2]

		//0,0
		pb_prel(pb,pbw,0,0,YUV422(Y0,Yu,Yv));
		//1,0
		pb_prel(pb,pbw,1,0,YUV422(Y1,Yu,Yv));

		//next 4 bytes
		p_in+=1;

		Y0 = (p_in[1]>>8) &255; //
		Yu = (p_in[1]>>0) &255; //p_in[0]
		Y1 = (p_in[3]>>8) &255; //p_in[3]
		Yv = (p_in[3]>>0) &255; //p_in[2]

		//0,1
		pb_prel(pb,pbw,0,1,YUV422(Y0,Yu,Yv));
		//1,1
		pb_prel(pb,pbw,1,1,YUV422(Y1,Yu,Yv));
	}
	pixelcvt_end;
	//VQ PAL Stuff
	pixelcvt_startVQ(conv565_VQ,2,2)
	{
		u32 R=ABGR0565_R(data[0]) + ABGR0565_R(data[1]) + ABGR0565_R(data[2]) + ABGR0565_R(data[3]);
		u32 G=ABGR0565_G(data[0]) + ABGR0565_G(data[1]) + ABGR0565_G(data[2]) + ABGR0565_G(data[3]);
		u32 B=ABGR0565_B(data[0]) + ABGR0565_B(data[1]) + ABGR0565_B(data[2]) + ABGR0565_B(data[3]);
		R>>=2;
		G>>=2;
		B>>=2;

		return R | (G<<5) | (B<<11);
	}
	pixelcvt_nextVQ(conv1555_VQ,2,2)
	{
		u32 R=ABGR1555_R(data[0]) + ABGR1555_R(data[1]) + ABGR1555_R(data[2]) + ABGR1555_R(data[3]);
		u32 G=ABGR1555_G(data[0]) + ABGR1555_G(data[1]) + ABGR1555_G(data[2]) + ABGR1555_G(data[3]);
		u32 B=ABGR1555_B(data[0]) + ABGR1555_B(data[1]) + ABGR1555_B(data[2]) + ABGR1555_B(data[3]);
		u32 A=ABGR1555_A(data[0]) + ABGR1555_A(data[1]) + ABGR1555_A(data[2]) + ABGR1555_A(data[3]);
		R>>=2;
		G>>=2;
		B>>=2;
		A>>=2;

		return R | (G<<5) | (B<<10)  | (A<<15);
	}
	pixelcvt_nextVQ(conv4444_VQ,2,2)
	{
		u32 R=ABGR4444_R(data[0]) + ABGR4444_R(data[1]) + ABGR4444_R(data[2]) + ABGR4444_R(data[3]);
		u32 G=ABGR4444_G(data[0]) + ABGR4444_G(data[1]) + ABGR4444_G(data[2]) + ABGR4444_G(data[3]);
		u32 B=ABGR4444_B(data[0]) + ABGR4444_B(data[1]) + ABGR4444_B(data[2]) + ABGR4444_B(data[3]);
		u32 A=ABGR4444_A(data[0]) + ABGR4444_A(data[1]) + ABGR4444_A(data[2]) + ABGR4444_A(data[3]);
		R>>=2;
		G>>=2;
		B>>=2;
		A>>=2;

		return R | (G<<4) | (B<<8)  | (A<<12);
	}

	pixelcvt_nextVQ(convYUV422_VQ,4,1)
	{
		//convert 4x1 4444 to 4x1 8888
		u16* p_in=(u16*)data;


		s32 Y0 = (p_in[0]>>8) &255; //
		s32 Yu = (p_in[0]>>0) &255; //p_in[0]
		s32 Y1 = (p_in[2]>>8) &255; //p_in[3]
		s32 Yv = (p_in[2]>>0) &255; //p_in[2]

		return YUV422(16+((Y0-16)+(Y1-16))/2,Yu,Yv);
	}

	pixelcvt_endVQ;


u32 detwiddle[2][8][1024];
//input : address in the yyyyyxxxxx format
//output : address in the xyxyxyxy format
//U : x resolution , V : y resolution
//twidle works on 64b words
u32 fastcall twiddle_fast(u32 x,u32 y,u32 bcx,u32 bcy)
{
	return detwiddle[0][bcy][x]+detwiddle[1][bcx][y];
}

u32 unpack_4_to_8_tw[16];
u32 unpack_5_to_8_tw[32];
u32 unpack_6_to_8_tw[64];

u32 fastcall twiddle_razi(u32 x,u32 y,u32 x_sz,u32 y_sz)
	{
		//u32 rv2=twiddle_optimiz3d(raw_addr,U);
		u32 rv=0;//raw_addr & 3;//low 2 bits are directly passed  -> needs some misc stuff to work.However
		//Pvr internaly maps the 64b banks "as if" they were twidled :p

		//verify(x_sz==y_sz);
		u32 sh=0;
		x_sz>>=1;
		y_sz>>=1;
		while(x_sz!=0 || y_sz!=0)
		{
			if (y_sz)
			{
				u32 temp=y&1;
				rv|=temp<<sh;

				y_sz>>=1;
				y>>=1;
				sh++;
			}
			if (x_sz)
			{
				u32 temp=x&1;
				rv|=temp<<sh;

				x_sz>>=1;
				x>>=1;
				sh++;
			}
		}
		return rv;
	}

void BuildTwiddleTables()
{
	for (u32 s=0;s<8;s++)
	{
		u32 x_sz=1024;
		u32 y_sz=8<<s;
		for (u32 i=0;i<x_sz;i++)
		{
			detwiddle[0][s][i]=twiddle_razi(i,0,x_sz,y_sz);
			detwiddle[1][s][i]=twiddle_razi(0,i,y_sz,x_sz);
		}
	}

	//also fill in the texture cvt tables !

	for (int i=0;i<(1<<4);i++)
	{
		unpack_4_to_8_tw[i]=((i)<<4)|(i>>0);
	}

	for (int i=0;i<(1<<5);i++)
	{
		unpack_5_to_8_tw[i]=((i)<<3)|(i>>2);
	}

	for (int i=0;i<(1<<6);i++)
	{
		unpack_6_to_8_tw[i]=((i)<<2)|(i>>4);
	}
}


	#define twop twiddle_fast
	
	//HLIDE
	int32_t ilog2(uint32_t x)
	{
    	return 31 - __builtin_clz(x);
	}

	u8 VramWork[1024*1024*2];
	//hanlder functions
	template<class PixelConvertor>
	void fastcall texture_TW(u8* p_in,u32 Width,u32 Height)
	{
		u8* pb=VramWork;

		unsigned long bcx_ = ilog2(Width),bcy_ = ilog2(Height);

		const u32 bcx=bcx_-3;
		const u32 bcy=bcy_-3;

		const u32 divider=PixelConvertor::xpp*PixelConvertor::ypp;

		for (u32 y=0;y<Height;y+=PixelConvertor::ypp)
		{
			for (u32 x=0;x<Width;x+=PixelConvertor::xpp)
			{
				u8* p = &p_in[(twop(x,y,bcx,bcy)/divider)<<3];
				PixelConvertor::Convert((u16*)pb,Width,p);

				pb+=PixelConvertor::xpp*2;
			}
			pb+=Width*(PixelConvertor::ypp-1)*2;
		}

		memcpy_vfpu(p_in, VramWork, Width*Height*2);
	}


	template<class PixelConvertor>
	void fastcall texture_VQ(u8* p_in,u32 Width,u32 Height,u8* vq_codebook)
	{
		u8* pb=VramWork;

		u16* pal_cb=(u16*)vq_codebook;
		for (u32 palidx=0;palidx<256;palidx++)
		{
			pal_cb[palidx]=PixelConvertor::Convert(&pal_cb[palidx*4]);
		}

		const u32 divider=PixelConvertor::xpp*PixelConvertor::ypp;

		unsigned long bcx_ = ilog2(Width),bcy_ = ilog2(Height);
		const u32 bcx=bcx_-3;
		const u32 bcy=bcy_-3;

		for (u32 y=0;y<Height;y+=PixelConvertor::ypp)
		{
			for (u32 x=0;x<Width;x+=PixelConvertor::xpp)
			{ 
				*pb= p_in[twop(x,y,bcx,bcy)/divider];
				PixelConvertor::Convert((u16*)pb);
				pb+=1;
			}
		}

		//align up to 16 bytes
		u32 p_in_int=(u32)p_in;
		p_in_int&=~15;
		p_in=(u8*)p_in_int;

		memcpy_vfpu(p_in, VramWork, Width*Height/divider);
	}

	template<class PixelConvertor>
	void fastcall texture_PL(u8* p_in,u32 Width,u32 Height)
	{
		//u32 p=0;
		u16* pb=(u16*)VramWork;

		Height/=PixelConvertor::ypp;
		Width/=PixelConvertor::xpp;

		for (u32 y=0;y<Height;y++)
		{
			for (u32 x=0;x<Width;x++)
			{
				u8* p = p_in;
				PixelConvertor::Convert(pb,Width,p);
				p_in+=8;
				pb+=PixelConvertor::xpp*2;
			}
			pb+=Width*(y+1)*2;
		}

		memcpy_vfpu(p_in, VramWork, Width*Height*2);
	}


	void ARGB1555_(u8* praw,u32 w,u32 h)
	{
		u32 sz=w*h;
		u16* ptr=(u16*)praw;

		while(sz--)
			*ptr++=ABGR1555(*ptr);
	}
	void ARGB565_(u8* praw,u32 w,u32 h)
	{
		u32 sz=w*h;
		u16* ptr=(u16*)praw;
		while(sz--)
			*ptr++=ABGR0565(*ptr);
	}
	void ARGB4444_(u8* praw,u32 w,u32 h)
	{
		u32 sz=w*h;
		u16* ptr=(u16*)praw;

		while(sz--)
			*ptr++=ABGR4444(*ptr);
	}
	void ARGBYUV422_(u8* praw,u32 w,u32 h)
	{
		u32 sz=w*h*2;
		u16* ptr=(u16*)praw;

		while(sz)
		{
			s32 Y0 = (ptr[0]>>8) &255; //
			s32 Yu = (ptr[0]>>0) &255; //p_in[0]
			s32 Y1 = (ptr[1]>>8) &255; //p_in[3]
			s32 Yv = (ptr[1]>>0) &255; //p_in[2]

			ptr[0]=YUV422(Y0,Yu,Yv);
			ptr[1]=YUV422(Y1,Yu,Yv);;

			ptr+=2;
			sz-=2;
		}
	}

	u8* GPU_mem;

	char texFormatName[8][30]=
	{
		"1555",
		"565",
		"4444",
		"YUV422",
		"Bump Map",
		"4 BPP Palette",
		"8 BPP Palette",
		"Reserved	, 1555"
	};

	void PrintTextureName(PolyParam* mod)
		{
			printf(texFormatName[mod->tcw.NO_PAL.PixelFmt]);
	
			if (mod->tcw.NO_PAL.VQ_Comp)
				printf(" VQ");

			if (mod->tcw.NO_PAL.ScanOrder==0)
				printf(" TW");

			if (mod->tcw.NO_PAL.MipMapped)
				printf(" MM");

			if (mod->tcw.NO_PAL.StrideSel)
				printf(" Stride");

			if (mod->tcw.NO_PAL.StrideSel)
				printf(" %d[%d]x%d @ 0x%X",(TEXT_CONTROL&31)*32,8<<mod->tsp.TexU,8<<mod->tsp.TexV,mod->tcw.NO_PAL.TexAddr<<3);
			else
				printf(" %dx%d @ 0x%X",8<<mod->tsp.TexU,8<<mod->tsp.TexV,mod->tcw.NO_PAL.TexAddr<<3);
			printf("\n");
		}

	void SetupPaletteForTexture(u32 palette_index,u32 sz)
	{
		u32 fmtpal=PAL_RAM_CTRL&3;

		sceGuClutMode(PalFMT[fmtpal],0,0xFF,0);//or whatever
		sceGuClutLoad(sz/8,&palette_lut[palette_index]);
	}


	inline bool is_fmt_supported(TSP tsp,TCW tcw) {
			const s32 sw = (s32)((u32)(8<<tsp.TexU));
			const s32 sh = (s32)((u32)(8<<tsp.TexV));
			if (( (sw + sh) << 1) > VRAM_SIZE) { //Minimum of 2components
				//printf("Unsupported texture(OUT OF RANGE) w %u h %u fmt %u vq %u\n",8<<tsp.TexU,8<<tsp.TexV,tcw.NO_PAL.VQ_Comp);
				return false;
			}

			switch (tcw.NO_PAL.PixelFmt) {
				case 0:
				case 1:
				case 2:
				case 3:
				case 7:
				if (tcw.NO_PAL.ScanOrder) {
					return (tcw.NO_PAL.VQ_Comp == 0);
				} 
				return true;

				case 4:
					return false;

				case 5:
					return (tcw.NO_PAL.VQ_Comp==0);
				case 6:
					return (tcw.NO_PAL.VQ_Comp==0);
			}
		return false;
	}

	static void SetTextureParams(PolyParam* mod)
	{

		#define twidle_tex(format)\
			if (mod->tcw.NO_PAL.VQ_Comp)\
				{\
				vq_codebook=(u8*)&params.vram[sa];\
				sa+=256*4*2;\
				if (mod->tcw.NO_PAL.MipMapped){ /*int* p=0;*p=4;*/\
				sa+=MipPoint[mod->tsp.TexU];}\
				if (*(u32*)&params.vram[(sa&(~0x3))-0]!=0xDEADC0DE)	\
				{		\
					texture_VQ<conv##format##_VQ>((u8*)&params.vram[sa],w,h,vq_codebook);	\
					*(u32*)&params.vram[(sa&(~0x3))-0]=0xDEADC0DE;\
				}\
				sa&=~15; /* ALIGN UP TO 16 BYTES!*/\
				texVQ=1;\
			}\
			else\
			{\
				if (mod->tcw.NO_PAL.MipMapped)\
				sa+=MipPoint[mod->tsp.TexU]<<3;\
				if (*(u32*)&params.vram[sa-0]!=0xDEADC0DE)	\
				{	\
					texture_TW<conv##format##_TW>/*TW*/((u8*)&params.vram[sa],w,h);\
					*(u32*)&params.vram[(sa&(~0x3))-0]=0xDEADC0DE;\
				}\
			}

		#define normPL_text(format) \
			u32 sr;\
			if (mod->tcw.NO_PAL.StrideSel)\
				{sr=(TEXT_CONTROL&31)*32;}\
				else\
				{sr=w;}\
				if (mod->tcw.NO_PAL.StrideSel || *(u32*)&params.vram[sa-0]!=0xDEADC0DE)\
				{ \
					format((u8*)&params.vram[sa],sr,h); \
					*(u32*)&params.vram[sa-0]=0xDEADC0DE;\
				}

		#define norm_text(format) \
		u32 sr = w; \
		if (mod->tcw.NO_PAL.StrideSel) sr=(TEXT_CONTROL&31)*32;; \
			if (mod->tcw.NO_PAL.StrideSel || *(u32*)&params.vram[sa-0]!=0xDEADC0DE)\
			{	\
			ARGB##format##_((u8*)&params.vram[sa],sr,h);	\
				*(u32*)&params.vram[sa-0]=0xDEADC0DE;\
			}


		if (!is_fmt_supported(mod->tsp,mod->tcw)) {
			return;
		}

		u32 sa=((mod->tcw.NO_PAL.TexAddr<<3) & VRAM_MASK);
		u32 FMT;
		u32 texVQ=0;
		u8* vq_codebook;
		u32 w=8<<mod->tsp.TexU;
		u32 h=8<<mod->tsp.TexV;

		bool Palette_setup_done = false;

		//if (mod->tcw.NO_PAL.PixelFmt != 5) PrintTextureName(mod);

		switch (mod->tcw.NO_PAL.PixelFmt)
		{
		case 0:
		case 7:
			//0	1555 value: 1 bit; RGB values: 5 bits each
			//7	Reserved	Regarded as 1555
			if (mod->tcw.NO_PAL.ScanOrder)
			{
				//verify(tcw.NO_PAL.VQ_Comp==0);
				norm_text(1555);
				//argb1555to8888(&pbt,(u16*)&params.vram[sa],w,h);
			}
			else
			{
				//verify(tsp.TexU==tsp.TexV);
				twidle_tex(1555);
			}
			FMT=GU_PSM_5551;
			break;

			//redo_argb:
			//1	565	 R value: 5 bits; G value: 6 bits; B value: 5 bits
		case 1:
			if (mod->tcw.NO_PAL.ScanOrder)
			{
				//verify(tcw.NO_PAL.VQ_Comp==0);
				norm_text(565);
				//(&pbt,(u16*)&params.vram[sa],w,h);
			}
			else
			{
				//verify(tsp.TexU==tsp.TexV);
				twidle_tex(565);
			}
			FMT=GU_PSM_5650;
			break;


			//2	4444 value: 4 bits; RGB values: 4 bits each
		case 2:
			if (mod->tcw.NO_PAL.ScanOrder)
			{
				//verify(tcw.NO_PAL.VQ_Comp==0);
				//argb4444to8888(&pbt,(u16*)&params.vram[sa],w,h);
				norm_text(4444);
			}
			else
			{
				twidle_tex(4444);
			}
			FMT=GU_PSM_4444;
			break;
			//3	YUV422 32 bits per 2 pixels; YUYV values: 8 bits each
		case 3:
			if (mod->tcw.NO_PAL.ScanOrder)
			{
					normPL_text(texture_PL<convYUV_PL>);
					//norm_text(ANYtoRAW);
			}
			else
			{
				//it cant be VQ , can it ?
				//docs say that yuv can't be VQ ...
				//HW seems to support it ;p
				twidle_tex(YUV422);
			}
			FMT=GU_PSM_5650;//wha?
			break;
			//4	Bump Map	16 bits/pixel; S value: 8 bits; R value: 8 bits
		case 5:
			//5	4 BPP Palette	Palette texture with 4 bits/pixel
			verify(mod->tcw.PAL.VQ_Comp==0);
			if (mod->tcw.NO_PAL.MipMapped)
				sa+=MipPoint[mod->tsp.TexU]<<1;


			palette_index = mod->tcw.PAL.PalSelect<<4;

			normPL_text(texture_TW<convPAL4_TW>)/*
			
			if (*(u32*)&params.vram[sa-0]!=0xDEADC0DE)	
			{	
				texture_TW<convPAL4_TW>((u8*)&params.vram[sa],w,h);
				*(u32*)&params.vram[(sa&(~0x3))-0]=0xDEADC0DE;
			}*/

			FMT=GU_PSM_4444; //PalFMT[PAL_RAM_CTRL&3];

			

			/*sceGuClutMode(GU_PSM_T16,1,0x3F,0);//or whatever
			sceGuClutLoad(8,&palette_lut[palette_index]);

			FMT=GU_PSM_T4;*/

			break;
		case 6:
			{
				//6	8 BPP Palette	Palette texture with 8 bits/pixel
				verify(mod->tcw.PAL.VQ_Comp==0);
				if (mod->tcw.NO_PAL.MipMapped)
					sa+=MipPoint[mod->tsp.TexU]<<2;

				normPL_text(texture_TW<convPAL8_TW>)

				//Palette_setup_done =true;

				//SetupPaletteForTexture(mod->tcw.PAL.PalSelect<<4,256);

				FMT=GU_PSM_4444;//wha? the ? FUCK!
			}
			break;
		default:
		break;
			//printf("Unhandled texture\n");
			//memset(temp_tex_buffer,0xFFEFCFAF,w*h*4);
		}

		if (texVQ && !Palette_setup_done)
		{
			sceGuClutMode(FMT,0,0xFF,0);
			sceGuClutLoad(256/8,vq_codebook);
			FMT=GU_PSM_T8;
			w>>=1;
			h>>=1;
		}

		//sceGuTexScale(w, h);
		sceGuTexMode(FMT,0,0,0);
		sceGuTexImage(0, w>512?512:w, h>512?512:h, w,
			params.vram + sa );

	}
	union _ISP_BACKGND_T_type
	{
		struct
		{
			u32 tag_offset:3;
			u32 tag_address:21;
			u32 skip:3;
			u32 shadow:1;
			u32 cache_bypass:1;
		};
		u32 full;
	};
	union _ISP_BACKGND_D_type
	{
		u32 i;
		f32 f;
	};
	u32 vramlock_ConvOffset32toOffset64(u32 offset32)
	{
		//64b wide bus is archevied by interleaving the banks every 32 bits
		//so bank is Address<<3
		//bits <4 are <<1 to create space for bank num
		//bank 0 is mapped at 400000 (32b offset) and after
		u32 bank=((offset32>>22)&0x1)<<2;//bank will be used as uper offset too
		u32 lv=offset32&0x3; //these will survive
		offset32<<=1;
		//       |inbank offset    |       bank id        | lower 2 bits (not changed)
		u32 rv=  (offset32&(VRAM_MASK-7))|bank                  | lv;

		return rv;
	}
	f32 vrf(u32 addr)
	{
		return *(f32*)&params.vram[vramlock_ConvOffset32toOffset64(addr)];
	}
	u32 vri(u32 addr)
	{
		return *(u32*)&params.vram[vramlock_ConvOffset32toOffset64(addr)];
	}
	static f32 CVT16UV(u32 uv)
	{
		uv<<=16;
		return *(f32*)&uv;
	}
	void decode_pvr_vertex(u32 base,u32 ptr,Vertex* cv)
	{
		//ISP
		//TSP
		//TCW
		ISP_TSP isp;
		TSP tsp;
		TCW tcw;

		isp.full=vri(base);
		tsp.full=vri(base+4);
		tcw.full=vri(base+8);

		//XYZ
		//UV
		//Base Col
		//Offset Col

		//XYZ are _allways_ there :)
		cv->x=vrf(ptr);ptr+=4;
		cv->y=vrf(ptr);ptr+=4;
		cv->z=vrf(ptr);ptr+=4;

		if (isp.Texture)
		{	//Do texture , if any
			if (isp.UV_16b)
			{
				u32 uv=vri(ptr);
				cv->u	=	CVT16UV((u16)uv);
				cv->v	=	CVT16UV((u16)(uv>>16));
				ptr+=4;
			}
			else
			{
				cv->u=vrf(ptr);ptr+=4;
				cv->v=vrf(ptr);ptr+=4;
			}
		}

		//Color
		u32 col=vri(ptr);ptr+=4;
		cv->col=ABGR8888(col);
		if (isp.Offset)
		{
			//Intesity color (can be missing too ;p)
			u32 col=vri(ptr);ptr+=4;
		//	vert_packed_color_(cv->spc,col);
		}
	}

	void reset_vtx_state()
	{

		curVTX=vertices;
		curLST=lists;
		curMod=listModes-1;
		global_regd=false;
		vtx_min_Z=128*1024;//if someone uses more, i realy realy dont care
		vtx_max_Z=0;		//lower than 0 is invalid for pvr .. i wonder if SA knows that.
	}

	ScePspFVector4 Transform(ScePspFMatrix4* mtx,ScePspFVector3* vec)
	{
		ScePspFVector4 rv;
		__asm__ volatile (
			"lv.q    C000, 0  + %1\n"
			"lv.q    C010, 16 + %1\n"
			"lv.q    C020, 32 + %1\n"
			"lv.q    C030, 48 + %1\n"
			"lv.q    C100, %2\n"
			"vtfm4.q C110, M000, C100\n"
			"sv.q    C110, %0\n"
			:"+m"(rv): "m"(*mtx), "m"(*vec));

		return rv;
	}

	#define VTX_TFX(x) (x)
	#define VTX_TFY(y) (y)
	
	//480*(480/272)=847.0588 ~ 847, but we'l use 848.

	const u32 unpack_1_to_8[2]={0,0xFF};

	#define ARGB8888(a,r,g,b) \
			(a|r|g|b)

	#define ARGB1555_PL( word )	\
		ARGB8888(unpack_1_to_8[(word>>15)&1],((word>>10) & 0x1F)<<3,	\
		((word>>5) & 0x1F)<<3,(word&0x1F)<<3)

	#define ARGB565_PL( word )		\
		ARGB8888(0xFF,((word>>11) & 0x1F)<<3,	\
		((word>>5) & 0x3F)<<2,(word&0x1F)<<3)

	#define ARGB4444_PL( word )	\
		ARGB8888( (word&0xF000)>>(12-4),(word&0xF00)>>(8-4),(word&0xF0)>>(4-4),(word&0xF)<<4 )


	#define ARGB1555_TW( word )	 word

	#define ARGB565_TW( word )		\
		ARGB8888(unpack_4_to_8_tw[(word&0xF000)>>(12)],unpack_5_to_8_tw[(word>>11) & 0x1F],	\
		unpack_6_to_8_tw[(word>>5) & 0x3F],unpack_5_to_8_tw[word&0x1F])
	
	#define ARGB4444_TW( word )	\
		ARGB8888( unpack_4_to_8_tw[(word&0xF000)>>(12)],unpack_4_to_8_tw[(word&0xF00)>>(8)],unpack_4_to_8_tw[(word&0xF0)>>(4)],unpack_4_to_8_tw[(word&0xF)] )


	void palette_update()
	{
	
		if (pal_needs_update==false)
			return;

		pal_needs_update=false;
		switch(PAL_RAM_CTRL&3)
		{
			case 0:
				for (int i=0;i<1024;i++)
				{
					palette_lut[i]=ARGB1555_TW(PALETTE_RAM[i]);
				}
			break;

			case 1:
				for (int i=0;i<1024;i++)
				{
					palette_lut[i]=ARGB565_TW(PALETTE_RAM[i]);
				}
			break;

			case 2:
				for (int i=0;i<1024;i++)
				{
					palette_lut[i]=ARGB4444_TW(PALETTE_RAM[i]);
				}
			break;

			case 3:
				for (int i=0;i<1024;i++)
				{
					palette_lut[i]=PALETTE_RAM[i];//argb 8888 :p
				}
			break;
		}
	}


	#ifndef GU_SYNC_WHAT_DONE
		#define 	GU_SYNC_WHAT_DONE   (0)
		#define 	GU_SYNC_WHAT_QUEUED   (1)
		#define 	GU_SYNC_WHAT_DRAW   (2)
		#define 	GU_SYNC_WHAT_STALL   (3)
		#define 	GU_SYNC_WHAT_CANCEL   (4)
	#endif

	Vertex BGTest;
	float old_vtx_max_Z = 0;
	float old_vtx_min_Z = 0;
	//u32 old_VPTR = 0, old_SBASE = 0;

	void DoRender()
	{
		pspDebugScreenSetOffset((int)fbp0);
		pspDebugScreenSetXY(0,0);
		pspDebugScreenPrintf("%s",fps_text);

		//if(!PSP_UC(draw)) return;

		//wait for last frame to end
		sceGuSync(GU_SYNC_FINISH,GU_SYNC_WHAT_DONE);

		fbp0 = sceGuSwapBuffers();

		palette_update();

		//--BG poly
		u32 param_base=PARAM_BASE & 0xF00000;
		_ISP_BACKGND_D_type bg_d;
		_ISP_BACKGND_T_type bg_t;

		bg_d.i=ISP_BACKGND_D & ~(0xF);
		bg_t.full=ISP_BACKGND_T;

		bool PSVM=FPU_SHAD_SCALE&0x100; //double parameters for volumes

		//Get the strip base
		u32 strip_base=param_base + bg_t.tag_address*4;
		//Calculate the vertex size
		u32 strip_vs=3 + bg_t.skip;
		u32 strip_vert_num=bg_t.tag_offset;

		if (PSVM && bg_t.shadow)
		{
			strip_vs+=bg_t.skip;//2x the size needed :p
		}
		strip_vs*=4;
		//Get vertex ptr
		u32 vertex_ptr=strip_vert_num*strip_vs+strip_base +3*4;
		//now , all the info is ready :p

		Vertex BGTest;

		decode_pvr_vertex(strip_base,vertex_ptr,&BGTest);

		sceGuStart(GU_DIRECT,list);

		sceGuClearColor(BGTest.col);
		sceGuClearDepth(0);
		sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);

		//i create the matrix by hand :p
	
		/*Setup the matrix*/

		if (unlikely(changed)){

			//printf("%d\n",(settings.Enhancements.AspectRatioMode%PSP_DC_AR_COUNT));
			
			changed = false;
			curr_mtx.x.x=(2.f/dc_width);
			curr_mtx.y.x= 0;
			curr_mtx.z.x=-(640/dc_width);
			curr_mtx.w.x=0;

			curr_mtx.x.y=0;
			curr_mtx.y.y=(settings.Enhancements.AspectRatioMode%PSP_DC_AR_COUNT) != 5 ? -(2.f/dc_height) : -0.0085f;
			curr_mtx.z.y=(480/dc_height);
			curr_mtx.w.y=0;

			curr_mtx.x.w=0;
			curr_mtx.y.w=0;
			curr_mtx.w.w=0;

			curr_mtx.x.z=0;
			curr_mtx.z.w=1;
			curr_mtx.y.z=0;

			//clear out other matrixes
			sceGumMatrixMode(GU_VIEW);
			sceGumLoadIdentity();

			sceGumMatrixMode(GU_MODEL);
			sceGumLoadIdentity();
		}

		sceGumMatrixMode(GU_PROJECTION);
		

		if (unlikely(vtx_min_Z<=0.001))
			vtx_min_Z=0.001;
		if (unlikely(vtx_max_Z<0 || vtx_max_Z>128*1024))
			vtx_max_Z=1;

		/*if (vtx_max_Z != old_vtx_max_Z)
		{
			curr_mtx.z.z= ((1.f/(vtx_max_Z))* 1.004f);
			old_vtx_max_Z = vtx_max_Z;
		}*/

		curr_mtx.z.z= ((1.f/(vtx_max_Z))* 1.001f);
		curr_mtx.w.z= -vtx_min_Z;
		//curr_mtx.z.w=1.1f;

		/*float normal_max=vtx_max_Z;

		float SCL=-2/(vtx_min_Z * vtx_max_Z);
		curr_mtx.z.z=-((2.f/(vtx_max_Z))* SCL);
		curr_mtx.w.z=SCL;*/

		

		//Load the matrix to gu
		sceGumLoadMatrix(&curr_mtx);

		//push it to the hardware :)
		sceGumUpdateMatrix();

		/*sceGuDisable(GU_SCISSOR_TEST);
		sceGuScissor(0,0,512,271);
		sceGuEnable(GU_SCISSOR_TEST);*/


		Vertex* drawVTX=vertices;
		VertexList* drawLST=lists;
		PolyParam* drawMod=listModes;

		const VertexList* const crLST=curLST;//hint to the compiler that sceGUM cant edit this value !
		
		sceGuDisable(GU_BLEND);
		sceGuDisable(GU_ALPHA_TEST);

		sceKernelDcacheWritebackInvalidateAll();

		for (;drawLST != crLST;drawLST++)
		{
			if (drawLST==TransLST)
			{
				//enable blending
				sceGuEnable(GU_BLEND);
				
				//set blending mode
				sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);

				sceGuEnable(GU_ALPHA_TEST);
				sceGuAlphaFunc(GU_GREATER,0,0xFF);
			}

			s32 count=drawLST->count;
			if (count<0)
			{
				if (drawMod->pcw.Texture)
				{
					sceGuEnable(GU_TEXTURE_2D);
					SetTextureParams(drawMod);

				}
				else
				{
					sceGuDisable(GU_TEXTURE_2D);
				}
				drawMod++;
				count&=0x7FFF;
			}

			sceGuDrawArray(GU_TRIANGLE_STRIP,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,count,0,drawVTX);

			drawVTX+=count;
		}

		reset_vtx_state();
		sceGuFinish();
	}

	void StartRender()
	{
		u32 VtxCnt=curVTX-vertices;

		VertexCount+=VtxCnt;
		
		FrameCount++;

		if (FB_W_SOF1 & 0x1000000)
			return;

		if (!_doRender) { printf("CAPITO'\n"); return;}

		render_end_pending_cycles = (VtxCnt * 60) + 500000 * 3;

		sh4_sched_request(render_end_schid, render_end_pending_cycles);

	    DoRender();
	}
	void EndRender()
	{
	}


	//Vertex Decoding-Converting
	struct VertexDecoder
	{
		//list handling
		__forceinline
		static void StartList(u32 ListType)
		{
			if (ListType==ListType_Translucent)
				TransLST=curLST;
		}
		__forceinline
		static void EndList(u32 ListType)
		{

		}

		static u32 FLCOL(float* col)
		{
			u32 A=col[0]*255;
			u32 R=col[1]*255;
			u32 G=col[2]*255;
			u32 B=col[3]*255;
			if (A>255)
				A=255;
			if (R>255)
				R=255;
			if (G>255)
				G=255;
			if (B>255)
				B=255;

			return (A<<24) | (B<<16) | (G<<8) | R;
		}
		static u32 INTESITY(float inte)
		{
			u32 C=inte*255;
			if (C>255)
				C=255;
			return (0xFF<<24) | (C<<16) | (C<<8) | (C);
		}

		//Polys
#define glob_param_bdc  \
			if ( (curVTX-vertices)>38*1024) reset_vtx_state(); \
			if (!global_regd)	curMod++; \
			global_regd=true;			\
			curMod->pcw=pp->pcw;		\
			curMod->isp=pp->isp;		\
			curMod->tsp=pp->tsp;		\
			curMod->tcw=pp->tcw;		\




		__forceinline
		static void fastcall AppendPolyParam0(TA_PolyParam0* pp)
		{
			glob_param_bdc;
		}
		__forceinline
		static void fastcall AppendPolyParam1(TA_PolyParam1* pp)
		{
			glob_param_bdc;
		}
		__forceinline
		static void fastcall AppendPolyParam2A(TA_PolyParam2A* pp)
		{
			glob_param_bdc;
		}
		__forceinline
		static void fastcall AppendPolyParam2B(TA_PolyParam2B* pp)
		{

		}
		__forceinline
		static void fastcall AppendPolyParam3(TA_PolyParam3* pp)
		{
			glob_param_bdc;
		}
		__forceinline
		static void fastcall AppendPolyParam4A(TA_PolyParam4A* pp)
		{
			glob_param_bdc;
		}
		__forceinline
		static void fastcall AppendPolyParam4B(TA_PolyParam4B* pp)
		{

		}

		//Poly Strip handling
		//UPDATE SPRITES ON EDIT !
		__forceinline
		static void StartPolyStrip()
		{
			curLST->ptr=curVTX;
		}

		__forceinline
		static void EndPolyStrip()
		{
			curLST->count=(curVTX-curLST->ptr);
			if (global_regd)
			{
				curLST->count|=0x80000000;
				global_regd=false;
			}
			curLST++;
		}

#define vert_base(dst,_x,_y,_z) /*VertexCount++;*/ \
		float W=1.0f/_z; \
		curVTX[dst].x=VTX_TFX(_x)*W; \
		curVTX[dst].y=VTX_TFY(_y)*W; \
		if (W<vtx_min_Z)	\
			vtx_min_Z=W;	\
		else if (W>vtx_max_Z)	\
			vtx_max_Z=W;	\
		curVTX[dst].z=W; /*Linearly scaled later*/

		//Poly Vertex handlers
#define vert_cvt_base vert_base(0,vtx->xyz[0],vtx->xyz[1],vtx->xyz[2])


		//(Non-Textured, Packed Color)
		__forceinline
		static void AppendPolyVertex0(TA_Vertex0* vtx)
		{
			vert_cvt_base;
			curVTX->col=ABGR8888(vtx->BaseCol);

			curVTX++;
		}

		//(Non-Textured, Floating Color)
		__forceinline
		static void AppendPolyVertex1(TA_Vertex1* vtx)
		{
			vert_cvt_base;
			curVTX->col=FLCOL(&vtx->BaseA);

			curVTX++;
		}

		//(Non-Textured, Intensity)
		__forceinline
		static void AppendPolyVertex2(TA_Vertex2* vtx)
		{
			vert_cvt_base;
			curVTX->col=INTESITY(vtx->BaseInt);

			curVTX++;
		}

		//(Textured, Packed Color)
		__forceinline
		static void AppendPolyVertex3(TA_Vertex3* vtx)
		{
			vert_cvt_base;
			curVTX->col=ABGR8888(vtx->BaseCol);

			curVTX->u=vtx->u;
			curVTX->v=vtx->v;

			curVTX++;
		}

		//(Textured, Packed Color, 16bit UV)
		__forceinline
		static void AppendPolyVertex4(TA_Vertex4* vtx)
		{
			vert_cvt_base;
			curVTX->col=ABGR8888(vtx->BaseCol);

			curVTX->u=CVT16UV(vtx->u);
			curVTX->v=CVT16UV(vtx->v);

			curVTX++;
		}

		//(Textured, Floating Color)
		__forceinline
		static void AppendPolyVertex5A(TA_Vertex5A* vtx)
		{
			vert_cvt_base;

			curVTX->u=vtx->u;
			curVTX->v=vtx->v;
		}
		__forceinline
		static void AppendPolyVertex5B(TA_Vertex5B* vtx)
		{
			curVTX->col=FLCOL(&vtx->BaseA);
			curVTX++;
		}

		//(Textured, Floating Color, 16bit UV)
		__forceinline
		static void AppendPolyVertex6A(TA_Vertex6A* vtx)
		{
			vert_cvt_base;

			curVTX->u=CVT16UV(vtx->u);
			curVTX->v=CVT16UV(vtx->v);
		}
		__forceinline
		static void AppendPolyVertex6B(TA_Vertex6B* vtx)
		{
			curVTX->col=FLCOL(&vtx->BaseA);
			curVTX++;
		}

		//(Textured, Intensity)
		__forceinline
		static void AppendPolyVertex7(TA_Vertex7* vtx)
		{
			vert_cvt_base;
			curVTX->u=vtx->u;
			curVTX->v=vtx->v;

			curVTX->col=INTESITY(vtx->BaseInt);

			curVTX++;
		}

		//(Textured, Intensity, 16bit UV)
		__forceinline
		static void AppendPolyVertex8(TA_Vertex8* vtx)
		{
			vert_cvt_base;
			curVTX->col=INTESITY(vtx->BaseInt);

			curVTX->u=CVT16UV(vtx->u);
			curVTX->v=CVT16UV(vtx->v);

			curVTX++;
		}

		//(Non-Textured, Packed Color, with Two Volumes)
		__forceinline
		static void AppendPolyVertex9(TA_Vertex9* vtx)
		{
			vert_cvt_base;
			curVTX->col=ABGR8888(vtx->BaseCol0);

			curVTX++;
		}

		//(Non-Textured, Intensity,	with Two Volumes)
		__forceinline
		static void AppendPolyVertex10(TA_Vertex10* vtx)
		{
			vert_cvt_base;
			curVTX->col=INTESITY(vtx->BaseInt0);

			curVTX++;
		}

		//(Textured, Packed Color,	with Two Volumes)
		__forceinline
		static void AppendPolyVertex11A(TA_Vertex11A* vtx)
		{
			vert_cvt_base;

			curVTX->u=vtx->u0;
			curVTX->v=vtx->v0;

			curVTX->col=ABGR8888(vtx->BaseCol0);

		}
		__forceinline
		static void AppendPolyVertex11B(TA_Vertex11B* vtx)
		{
			curVTX++;
		}

		//(Textured, Packed Color, 16bit UV, with Two Volumes)
		__forceinline
		static void AppendPolyVertex12A(TA_Vertex12A* vtx)
		{
			vert_cvt_base;

			curVTX->u=CVT16UV(vtx->u0);
			curVTX->v=CVT16UV(vtx->v0);

			curVTX->col=ABGR8888(vtx->BaseCol0);
		}
		__forceinline
		static void AppendPolyVertex12B(TA_Vertex12B* vtx)
		{
			curVTX++;
		}

		//(Textured, Intensity,	with Two Volumes)
		__forceinline
		static void AppendPolyVertex13A(TA_Vertex13A* vtx)
		{
			vert_cvt_base;
			curVTX->u=vtx->u0;
			curVTX->v=vtx->v0;
			curVTX->col=INTESITY(vtx->BaseInt0);
		}
		__forceinline
		static void AppendPolyVertex13B(TA_Vertex13B* vtx)
		{
			curVTX++;
		}

		//(Textured, Intensity, 16bit UV, with Two Volumes)
		__forceinline
		static void AppendPolyVertex14A(TA_Vertex14A* vtx)
		{
			vert_cvt_base;
			curVTX->u=CVT16UV(vtx->u0);
			curVTX->v=CVT16UV(vtx->v0);
			curVTX->col=INTESITY(vtx->BaseInt0);
		}
		__forceinline
		static void AppendPolyVertex14B(TA_Vertex14B* vtx)
		{
			curVTX++;
		}

		//Sprites
		__forceinline
		static void AppendSpriteParam(TA_SpriteParam* spr)
		{
			TA_SpriteParam* pp=spr;
			glob_param_bdc;
		}

		//Sprite Vertex Handlers
		/*
		__forceinline
		static void AppendSpriteVertex0A(TA_Sprite0A* sv)
		{
		}
		__forceinline
		static void AppendSpriteVertex0B(TA_Sprite0B* sv)
		{
		}
		*/
		#define sprite_uv(indx,u_name,v_name) \
		curVTX[indx].u	=	CVT16UV(sv->u_name);\
		curVTX[indx].v	=	CVT16UV(sv->v_name);
		__forceinline
		static void AppendSpriteVertexA(TA_Sprite1A* sv)
		{

			StartPolyStrip();
			curVTX[0].col=0xFFFFFFFF;
			curVTX[1].col=0xFFFFFFFF;
			curVTX[2].col=0xFFFFFFFF;
			curVTX[3].col=0xFFFFFFFF;

			{
			vert_base(2,sv->x0,sv->y0,sv->z0);
			}
			{
			vert_base(3,sv->x1,sv->y1,sv->z1);
			}

			curVTX[1].x=sv->x2;
		}
		__forceinline
		static void AppendSpriteVertexB(TA_Sprite1B* sv)
		{

			{
			vert_base(1,curVTX[1].x,sv->y2,sv->z2);
			}
			{
			vert_base(0,sv->x3,sv->y3,sv->z2);
			}

			sprite_uv(2, u0,v0);
			sprite_uv(3, u1,v1);
			sprite_uv(1, u2,v2);
			sprite_uv(0, u0,v2);//or sprite_uv(u2,v0); ?

			curVTX+=4;
//			VertexCount+=4;

			//EndPolyStrip();
			curLST->count=4;
			if (global_regd)
			{
				curLST->count|=0x80000000;
				global_regd=false;
			}
			curLST++;
		}

		//ModVolumes
		__forceinline
		static void AppendModVolParam(TA_ModVolParam* modv)
		{

		}

		//ModVol Strip handling
		__forceinline
		static void StartModVol(TA_ModVolParam* param)
		{

		}
		__forceinline
		static void ModVolStripEnd()
		{

		}

		//Mod Volume Vertex handlers
		__forceinline
		static void AppendModVolVertexA(TA_ModVolA* mvv)
		{

		}
		__forceinline
		static void AppendModVolVertexB(TA_ModVolB* mvv)
		{

		}
		__forceinline
		static void SetTileClip(u32 xmin,u32 ymin,u32 xmax,u32 ymax)
		{
		}
		__forceinline
		static void TileClipMode(u32 mode)
		{

		}
		//Misc
		__forceinline
		static void ListCont()
		{
		}
		__forceinline
		static void ListInit()
		{
			//reset_vtx_state();
		}
		__forceinline
		static void SoftReset()
		{
			//reset_vtx_state();
		}
	};
	//Setup related

	//Misc setup
	void SetFpsText(char* text)
	{
		strcpy(fps_text,text);
	}
	bool InitRenderer()
	{
		pspDebugScreenInitEx((void*)0x4000000,GU_PSM_8888,1);
		sceGuInit();

		for (u32 i=0;i<256;i++)
		{
			unkpack_bgp_to_float[i]=i/255.0f;
		}

		BuildTwiddleTables();

		printf("Init gu\n");

		FB_DIRTY = false;

		fbp0 = getStaticVramBuffer(BUF_WIDTH,SCR_HEIGHT,GU_PSM_8888);
		fbp1 = getStaticVramBuffer(BUF_WIDTH,SCR_HEIGHT,GU_PSM_8888);
		zbp = getStaticVramBuffer(BUF_WIDTH,SCR_HEIGHT,GU_PSM_4444);

		sceGuStart(GU_DIRECT,list);
		sceGuDrawBuffer(GU_PSM_8888,fbp0,BUF_WIDTH);
		sceGuDispBuffer(SCR_WIDTH,SCR_HEIGHT,fbp1,BUF_WIDTH);
		sceGuDepthBuffer(zbp,BUF_WIDTH);
		sceGuOffset(2048 - (SCR_WIDTH/2),2048 - (SCR_HEIGHT/2));
		sceGuViewport(2048,2048,SCR_WIDTH,SCR_HEIGHT);
		sceGuDepthRange(65535,0);
		sceGuScissor(0,0,SCR_WIDTH,SCR_HEIGHT);
		sceGuEnable(GU_SCISSOR_TEST);
		sceGuEnable(GU_DEPTH_TEST);
		sceGuDepthMask(GU_FALSE);
		sceGuDepthFunc(GU_GEQUAL);
		sceGuFrontFace(GU_CW);
		sceGuDisable(GU_CULL_FACE);
		sceGuShadeModel(GU_SMOOTH);
		sceGuDisable(GU_TEXTURE_2D);
		sceGuTexFunc( GU_TFX_MODULATE, GU_TCC_RGBA );	// Apply image as a decal (NEW)
		sceGuTexOffset( 0.0f, 0.0f );

		sceGuFinish();
		sceGuSync(0,0);

		sceGuDisplay(1);

		return TileAccel.Init();
	}

	void TermRenderer()
	{
		#if HOST_SYS == SYS_PSP
		sceGuTerm();
		/*if (PSP_720_480)
		{
			pspDveMgrSetVideoOut(0, 0, 480, 272, 1, 15, 0);
		}*/
		#endif
		TileAccel.Term();
	}

	void ResetRenderer(bool Manual)
	{
		TileAccel.Reset(Manual);
		VertexCount=0;
		FrameCount=0;
	}

	bool ThreadStart()
	{
		return true;
	}

	void ThreadEnd()
	{

	}
	void ListCont()
	{
		TileAccel.ListCont();
	}
	void ListInit()
	{
		TileAccel.ListInit();
	}
	void SoftReset()
	{
		TileAccel.SoftReset();
	}

	void VramLockedWrite(vram_block* bl)
	{

	}
}
using namespace NORenderer;

#if HOST_SYS==SYS_PSP
void pspguCrear()
{
	sceGuStart(GU_DIRECT,list);
	sceGuClearColor(0);
	sceGuClearDepth(0);
	sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);
	sceGuFinish();
	sceGuSync(0,0);

	pspDebugScreenSetOffset((int)fbp0);
}
void pspguWaitVblank()
{
	sceDisplayWaitVblankStart();
	fbp0 = sceGuSwapBuffers();
}
#endif
