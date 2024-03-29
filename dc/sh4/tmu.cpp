/*
	Lovely timers, its amazing how many times this module was bugged
*/

#include "types.h"
#include "tmu.h"
#include "intc.h"
#include "dc/mem/sh4_internal_reg.h"
#include "dc/sh4/sh4_sched.h"


#define tmu_underflow	0x0100
#define tmu_UNIE		0x0020

u32 tmu_regs_CNT[3];
u32 tmu_regs_COR[3];
u16 tmu_regs_CR[3];
u32 old_mode[3] = {0xFFFF,0xFFFF,0xFFFF};
u8 TMU_TOCR,TMU_TSTR;

u32 tmu_ch_base[3];
u64 tmu_ch_base64[3];

u32 tmu_shift[3];
u32 tmu_mask[3];
u64 tmu_mask64[3];


const InterruptID tmu_intID[3]={sh4_TMU0_TUNI0,sh4_TMU1_TUNI1,sh4_TMU2_TUNI2};
static int tmu_sched[3];

#define likely(x) __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)

u32 read_TMU_TCNTch(u32 ch)
{
	return tmu_ch_base[ch] - ((sh4_sched_now64() >> tmu_shift[ch])& tmu_mask[ch]);
}

s64 read_TMU_TCNTch64(u32 ch)
{
	return tmu_ch_base64[ch] - ((sh4_sched_now64() >> tmu_shift[ch])& tmu_mask64[ch]);
}

void sched_chan_tick(int ch)
{
	//schedule next interrupt
	//return TMU_TCOR(ch) << tmu_shift[ch];

	u32 togo = read_TMU_TCNTch(ch);

	if (togo > SH4_CLOCK)
		togo = SH4_CLOCK;

	u32 cycles = togo << tmu_shift[ch];

	if (cycles > SH4_CLOCK)
		cycles = SH4_CLOCK;

	if (tmu_mask[ch])
		sh4_sched_request(tmu_sched[ch], cycles);
	else
		sh4_sched_request(tmu_sched[ch], -1);
}

void write_TMU_TCNTch(u32 ch, u32 data)
{
	//u32 TCNT=read_TMU_TCNTch(ch);
	tmu_ch_base[ch] = data + ((sh4_sched_now64() >> tmu_shift[ch])& tmu_mask[ch]);
	tmu_ch_base64[ch] = data + ((sh4_sched_now64() >> tmu_shift[ch])& tmu_mask64[ch]);

	sched_chan_tick(ch);
}

int sched_tmu_cb(int ch, int sch_cycl, int jitter)
{
	if (tmu_mask[ch]) {

		u32 tcnt = read_TMU_TCNTch(ch);

		s64 tcnt64 = (s64)read_TMU_TCNTch64(ch);

		//u32 cycles = tcor << tmu_shift[ch];

		//64 bit maths to differentiate big values from overflows
		if (tcnt64 <= jitter) {
			//raise interrupt, timer counted down
			tmu_regs_CR[ch] |= tmu_underflow;
			InterruptPend(tmu_intID[ch], 1);

			//printf("Interrupt for %d, %d cycles\n", ch, sch_cycl);

			//schedule next trigger by writing the TCNT register
			u32 tcor = tmu_regs_COR[ch];
			write_TMU_TCNTch(ch, tcor + tcnt);
		}
		else {

			//schedule next trigger by writing the TCNT register
			write_TMU_TCNTch(ch, tcnt);
		}

		return 0;	//has already been scheduled by TCNT write
	}
	else {
		return 0;	//this channel is disabled, no need to schedule next event
	}
}

//Update internal counter registers
void UpdateTMUCounts(u32 reg)
{
	InterruptPend(tmu_intID[reg], tmu_regs_CR[reg] & tmu_underflow);
	InterruptMask(tmu_intID[reg], tmu_regs_CR[reg] & tmu_UNIE);

	if (old_mode[reg] == (tmu_regs_CR[reg] & 0x7))
		return;
	else
		old_mode[reg] = (tmu_regs_CR[reg] & 0x7);

	u32 TCNT = read_TMU_TCNTch(reg);
	switch (tmu_regs_CR[reg] & 0x7)
	{
	case 0: //4
		tmu_shift[reg] = 2;
		break;

	case 1: //16
		tmu_shift[reg] = 4;
		break;

	case 2: //64
		tmu_shift[reg] = 6;
		break;

	case 3: //256
		tmu_shift[reg] = 8;
		break;

	case 4: //1024
		tmu_shift[reg] = 10;
		break;

	case 5: //reserved
		printf("TMU ch%d - TCR%d mode is reserved (5)", reg, reg);
		break;

	case 6: //RTC
		printf("TMU ch%d - TCR%d mode is RTC (6), can't be used on Dreamcast", reg, reg);
		break;

	case 7: //external
		printf("TMU ch%d - TCR%d mode is External (7), can't be used on Dreamcast", reg, reg);
		break;
	}
	tmu_shift[reg] += 2;
	write_TMU_TCNTch(reg, TCNT);
	sched_chan_tick(reg);
}

//Write to status registers
void TMU_TCR0_write(u32 data)
{
	tmu_regs_CR[0]=(u16)data;
	UpdateTMUCounts(0);
}
void TMU_TCR1_write(u32 data)
{
	tmu_regs_CR[1]=(u16)data;
	UpdateTMUCounts(1);
}
void TMU_TCR2_write(u32 data)
{
	tmu_regs_CR[2]=(u16)data;
	UpdateTMUCounts(2);
}


void turn_on_off_ch(u32 ch, bool on)
{
	u32 TCNT = read_TMU_TCNTch(ch);
	tmu_mask[ch] = on ? 0xFFFFFFFF : 0x00000000;
	tmu_mask64[ch] = on ? 0xFFFFFFFFFFFFFFFF : 0x0000000000000000;
	write_TMU_TCNTch(ch, TCNT);

	sched_chan_tick(ch);
}

void write_TMU_TSTR(u32 data)
{
	TMU_TSTR = data;

	for (int i = 0; i < 3; i++)
		turn_on_off_ch(i, data & (1 << i));
}

template<u32 ch>
void write_TMU_TCNT(u32 data)
{
	write_TMU_TCNTch(ch, data);
}

template<u32 ch>
u32 read_TMU_TCNT()
{
	return read_TMU_TCNTch(ch);
}

//Chan 2 not used functions
u32 TMU_TCPR2_read()
{
	EMUERROR("Read from TMU_TCPR2  , this regiser should be not used on dreamcast according to docs");
	return 0;
}

void TMU_TCPR2_write(u32 data)
{
	EMUERROR2("Write to TMU_TCPR2  , this regiser should be not used on dreamcast according to docs , data=%d",data);
}

//Init/Res/Term
void tmu_Init()
{
	//TMU TOCR 0xFFD80000 0x1FD80000 8 0x00 0x00 Held Held Pclk
	TMU[(TMU_TOCR_addr&0xFF)>>2].flags=REG_8BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	TMU[(TMU_TOCR_addr&0xFF)>>2].readFunction=0;
	TMU[(TMU_TOCR_addr&0xFF)>>2].writeFunction=0;
	TMU[(TMU_TOCR_addr&0xFF)>>2].data8=&TMU_TOCR;

	//TMU TSTR 0xFFD80004 0x1FD80004 8 0x00 0x00 Held 0x00 Pclk
	TMU[(TMU_TSTR_addr&0xFF)>>2].flags=REG_8BIT_READWRITE | REG_READ_DATA;
	TMU[(TMU_TSTR_addr&0xFF)>>2].readFunction=0;
	TMU[(TMU_TSTR_addr&0xFF)>>2].writeFunction=write_TMU_TSTR;
	TMU[(TMU_TSTR_addr&0xFF)>>2].data8=&TMU_TSTR;
	/*TMU[(TMU_TSTR_addr&0xFF)>>2].flags=REG_8BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	TMU[(TMU_TSTR_addr&0xFF)>>2].readFunction=0;
	TMU[(TMU_TSTR_addr&0xFF)>>2].writeFunction=0;
	TMU[(TMU_TSTR_addr&0xFF)>>2].data8=&TMU_TSTR;*/

	//TMU TCOR0 0xFFD80008 0x1FD80008 32 0xFFFFFFFF 0xFFFFFFFF Held Held Pclk
	TMU[(TMU_TCOR0_addr&0xFF)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	TMU[(TMU_TCOR0_addr&0xFF)>>2].readFunction=0;
	TMU[(TMU_TCOR0_addr&0xFF)>>2].writeFunction=0;
	TMU[(TMU_TCOR0_addr&0xFF)>>2].data32=&tmu_regs_COR[0];

	//TMU TCNT0 0xFFD8000C 0x1FD8000C 32 0xFFFFFFFF 0xFFFFFFFF Held Held Pclk
	TMU[(TMU_TCNT0_addr&0xFF)>>2].flags=REG_32BIT_READWRITE;
	TMU[(TMU_TCNT0_addr&0xFF)>>2].readFunction=read_TMU_TCNT<0>;
	TMU[(TMU_TCNT0_addr&0xFF)>>2].writeFunction=write_TMU_TCNT<0>;
	TMU[(TMU_TCNT0_addr&0xFF)>>2].data32=0;

	//TMU TCR0 0xFFD80010 0x1FD80010 16 0x0000 0x0000 Held Held Pclk
	TMU[(TMU_TCR0_addr&0xFF)>>2].flags=REG_16BIT_READWRITE | REG_READ_DATA;
	TMU[(TMU_TCR0_addr&0xFF)>>2].readFunction=0;
	TMU[(TMU_TCR0_addr&0xFF)>>2].writeFunction=TMU_TCR0_write;
	TMU[(TMU_TCR0_addr&0xFF)>>2].data16=&tmu_regs_CR[0];

	//TMU TCOR1 0xFFD80014 0x1FD80014 32 0xFFFFFFFF 0xFFFFFFFF Held Held Pclk
	TMU[(TMU_TCOR1_addr&0xFF)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	TMU[(TMU_TCOR1_addr&0xFF)>>2].readFunction=0;
	TMU[(TMU_TCOR1_addr&0xFF)>>2].writeFunction=0;
	TMU[(TMU_TCOR1_addr&0xFF)>>2].data32=&tmu_regs_COR[1];

	//TMU TCNT1 0xFFD80018 0x1FD80018 32 0xFFFFFFFF 0xFFFFFFFF Held Held Pclk
	TMU[(TMU_TCNT1_addr&0xFF)>>2].flags=REG_32BIT_READWRITE;
	TMU[(TMU_TCNT1_addr&0xFF)>>2].readFunction=read_TMU_TCNT<1>;
	TMU[(TMU_TCNT1_addr&0xFF)>>2].writeFunction=write_TMU_TCNT<1>;
	TMU[(TMU_TCNT1_addr&0xFF)>>2].data32=0;

	//TMU TCR1 0xFFD8001C 0x1FD8001C 16 0x0000 0x0000 Held Held Pclk
	TMU[(TMU_TCR1_addr&0xFF)>>2].flags=REG_16BIT_READWRITE | REG_READ_DATA;
	TMU[(TMU_TCR1_addr&0xFF)>>2].readFunction=0;
	TMU[(TMU_TCR1_addr&0xFF)>>2].writeFunction=TMU_TCR1_write;
	TMU[(TMU_TCR1_addr&0xFF)>>2].data16=&tmu_regs_CR[1];

	//TMU TCOR2 0xFFD80020 0x1FD80020 32 0xFFFFFFFF 0xFFFFFFFF Held Held Pclk
	TMU[(TMU_TCOR2_addr&0xFF)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	TMU[(TMU_TCOR2_addr&0xFF)>>2].readFunction=0;
	TMU[(TMU_TCOR2_addr&0xFF)>>2].writeFunction=0;
	TMU[(TMU_TCOR2_addr&0xFF)>>2].data32=&tmu_regs_COR[2];

	//TMU TCNT2 0xFFD80024 0x1FD80024 32 0xFFFFFFFF 0xFFFFFFFF Held Held Pclk
	TMU[(TMU_TCNT2_addr&0xFF)>>2].flags=REG_32BIT_READWRITE;
	TMU[(TMU_TCNT2_addr&0xFF)>>2].readFunction=read_TMU_TCNT<2>;
	TMU[(TMU_TCNT2_addr&0xFF)>>2].writeFunction=write_TMU_TCNT<2>;
	TMU[(TMU_TCNT2_addr&0xFF)>>2].data32=0;

	//TMU TCR2 0xFFD80028 0x1FD80028 16 0x0000 0x0000 Held Held Pclk
	TMU[(TMU_TCR2_addr&0xFF)>>2].flags=REG_16BIT_READWRITE | REG_READ_DATA;
	TMU[(TMU_TCR2_addr&0xFF)>>2].readFunction=0;
	TMU[(TMU_TCR2_addr&0xFF)>>2].writeFunction=TMU_TCR2_write;
	TMU[(TMU_TCR2_addr&0xFF)>>2].data16=&tmu_regs_CR[2];

	//TMU TCPR2 0xFFD8002C 0x1FD8002C 32 Held Held Held Held Pclk
	TMU[(TMU_TCPR2_addr&0xFF)>>2].flags=REG_32BIT_READWRITE;
	TMU[(TMU_TCPR2_addr&0xFF)>>2].readFunction=TMU_TCPR2_read;
	TMU[(TMU_TCPR2_addr&0xFF)>>2].writeFunction=TMU_TCPR2_write;
	TMU[(TMU_TCPR2_addr&0xFF)>>2].data32=0;

	for (int i = 0; i < 3; i++) {
		tmu_sched[i] = sh4_sched_register(i, sched_tmu_cb);
		sh4_sched_request(tmu_sched[i], -1);
	}
}

void tmu_Reset(bool Manual)
{
	TMU_TOCR=TMU_TSTR=0;
	tmu_regs_COR[0] = tmu_regs_COR[1] = tmu_regs_COR[2] = 0xffffffff;
	tmu_regs_CNT[0] = tmu_regs_CNT[1] = tmu_regs_CNT[2] = 0xffffffff;	
	tmu_regs_CR[0] = tmu_regs_CR[1] = tmu_regs_CR[2] = 0;

	UpdateTMUCounts(0);
	UpdateTMUCounts(1);
	UpdateTMUCounts(2);
}

void tmu_Term()
{
}