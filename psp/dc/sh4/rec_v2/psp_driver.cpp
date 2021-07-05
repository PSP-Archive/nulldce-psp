#define FULL_VFPU 0
#define HALF_VFPU 1

#define likely(x) __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)

#include "types.h"
#include "dc\sh4\sh4_opcode_list.h"

#include "dc\sh4\sh4_registers.h"
#include "dc\sh4\sh4_if.h"
#include "dc\sh4\ccn.h"
#include "dc\sh4\rec_v2\ngen.h"
#include "dc\mem\sh4_mem.h"
#include "psp\psp_emit.h"

#include "ssa_regalloc.h"

void __debugbreak() { fflush(stdout); *(int*)0=1;}

void make_address_range_executable(u32 address_start, u32 address_end)
{
	address_start = (address_start + 0) & -64;
	address_end   = (address_end   +63) & -64;

	for (; address_start < address_end; address_start += 64)
	{
		__builtin_allegrex_cache(0x1a, address_start);
		__builtin_allegrex_cache(0x1a, address_start);
		__builtin_allegrex_cache(0x08, address_start);
		__builtin_allegrex_cache(0x08, address_start);
	}
}

#if 1
#define REGISTER_BIT1(b1) (1<<b1)
#define REGISTER_BIT2(b1,b2) REGISTER_BIT1(b1)|REGISTER_BIT1(b2)
#define REGISTER_BIT4(b1,b2,b3,b4) REGISTER_BIT2(b1,b2)|REGISTER_BIT2(b3,b4)
#define REGISTER_BIT8(b1,b2,b3,b4,b5,b6,b7,b8) REGISTER_BIT4(b1,b2,b3,b4)|REGISTER_BIT4(b5,b6,b7,b8)

#define CALLEESAVED_REGISTERS REGISTER_BIT1(host_s8)|REGISTER_BIT8(host_s7, host_s6, host_s5, host_s4, host_s3, host_s2, host_s1, host_s0)
#define CALLERSAVED_REGISTERS REGISTER_BIT2(host_t9, host_t8)|REGISTER_BIT4(host_t7, host_t6, host_t5, host_t4)

#define OREGS_NUM (9 /*s0-s8 */ /* + 6 t4-t9 */)

#define NGPR_NUM 32

#define NREG_PRELOAD 1
#define NREG_REMAP   2
#define NREG_LOCK    4

#define OREGS_NUM (9/*+6*/)

u8 oregs[OREGS_NUM] =
{
#define SAFE_NREGS 0
	// those registers are saved-callee, so there is no need to spill them before calling a C function
	psp_s0, psp_s1, psp_s2, psp_s3, psp_s4, psp_s5, psp_s6, psp_s7, psp_s8,

#define TEMP_NREGS 8
	// those registers are saved-caller, so you need to spill them before calling a C function
	//psp_t4, psp_t5, psp_t6, psp_t7, psp_t8, psp_t9
};

#define EREGS_NUM sh4_reg_count

struct eregs_s
{
	u32 nreg : 8;
} eregs[EREGS_NUM]; // emulated registers

struct ngpr_s
{
	u32 ereg        : 8;
	u32 locked      : 1; // reserve it permanently or temporally so it cannot be mapped or spilled
	u32 mapped      : 1; // an emulated register is associated
	u32 dirty       : 1; // it will be spilled
} ngpr[NGPR_NUM]; // native registers

struct nhilo_s
{
	u16 mapped      : 1; // an emulated register is associated
	u16 dirty       : 1; // it will be spilled
} nhilo[2]; // native registers

void regcache_reset();

void spill_ngpr(u32 n = OREGS_NUM);

u32 map_ngpr(u32 ereg, u32 action);
u32 unmap_ngpr(u32 ereg, bool update = true);

void lock_ngpr(u32 ereg);
void unlock_ngpr(u32 ereg);

void set_ngpr_dirty(u32 nreg) { ngpr[nreg].dirty = 1; }

void spill_hilo();

void map_hilo(u32 hilo);

bool is_hilo_mapped(u32 hilo);
#endif

void emit_insn(psp_insn_t insn)
{
	emit_Write32(insn.word);
}

u32* GetRegPtr(u32 reg)
{
	return Sh4_int_GetRegisterPtr((Sh4RegType)reg);
}

/*
	psp static reg alloc
	s0 - pointer to the sh4 context
	k0 - cycle counter
	a0 - next pc
*/
const psp_gpr_t psp_ctx_reg = psp_gp;
const psp_gpr_t psp_mem_lut = psp_s1;
const psp_gpr_t psp_cycle_reg = psp_k0;
const psp_gpr_t psp_next_pc_reg = psp_a0;

#include <stdarg.h>

u32 regMapping[9] {255};
u8 regLeft = 1;
bool blocked = false;


u8 GetMappedRegister(u32 sh_reg){
	if (!blocked) return 255;
	
	for (int regNum = 0; regNum < 8; regNum++) {
		if (regMapping[regNum] == sh_reg) return 15+regNum;
	}
	return 255;
}

u8 AllocateSingleReg(u32 reg){

	for (int regNum = 0; regNum < 8; regNum++) {
		if (regMapping[regNum] == reg) return regNum+15;
		if (regMapping[regNum] != 255){
			regMapping[regNum] = reg;
			--regLeft;
			return 15+regNum;
		}
	}
	return 255;
}

//1+n opcodes
void emit_mpush(u32 n, ...)
{
	va_list ap;
	va_start(ap, n);
	emit_addiu(psp_sp, psp_sp, u16(-4*n));
	while (n--)
	{
		u32 reg = va_arg(ap, u32);
#if 0
		if (reg < reg_fpr)
			emit_sw(reg-reg_gpr,psp_sp,u16(4*n));
		else
			emit_swc1(reg-reg_fpr,psp_sp,u16(4*n));
#else
		emit_sw(reg,psp_sp,u16(4*n));
#endif
	}
	va_end(ap);
}
//1+n opcodes
void emit_mpop(u32 n, ...)
{
    va_list ap;
    va_start(ap, n);
    for (u32 i = n; i--> 0;)
    {
        u32 reg = va_arg(ap, u32);
#if 0
        if (reg < reg_fpr)
            emit_lw(reg-reg_gpr,psp_sp,u16(4*i));
        else
            emit_lwc1(reg-reg_fpr,psp_sp,u16(4*i));
#else
        emit_lw(reg,psp_sp,u16(4*i));
#endif
    }
    va_end(ap);
    emit_addiu(psp_sp, psp_sp, u16(+4*n));
}
//2 opcodes
void emit_li(u32 reg,u32 data,u32 sz=0)
{
	if (is_u16(data) && sz!=2)
	{
		emit_ori(reg,psp_zero,data&0xFFFF);
	}
	else if (is_s16(data) && sz!=2)
	{
		emit_addiu(reg,psp_zero,data&0xFFFF);
	}
	else
	{
		emit_lui(reg,data>>16);
		if ((sz==2) || (data&0xFFFF))
		{
			verify(sz!=1);
			emit_ori(reg,reg,data&0xFFFF);
		}
	}
}
//load upper base
//1 opcode
u32 emit_lub(u32 reg,u32 data)
{
	u32 hi = data>>16;
	u32 lo = data&0xFFFF;

	if (lo & 0x8000)
	{
		hi++;
		lo = data - (hi<<16); 
	}
	emit_lui(reg,u16(hi));
	return lo;
}
//load upper address base
//1 opcode
u32 emit_luab(u32 reg,void*data) { return emit_lub(reg,(u32)data); }
//2 opcodes
void emit_la(u32 reg,void* data)
{
	emit_li(reg,(u32)data);
}
//2 opcodes
void emit_lba(u32 reg,void* data)
{
	u32 lo = emit_lub(reg,(u32)data);
	emit_lb(reg,reg,lo);
}
//2 opcodes
void emit_lha(u32 reg,void* data)
{
	u32 lo = emit_lub(reg,(u32)data);
	emit_lh(reg,reg,lo);
}
//2 opcodes
void emit_lwa(u32 reg,void* data)
{
	u32 lo = emit_lub(reg,(u32)data);
	emit_lw(reg,reg,lo);
}
//2 opcodes
void emit_swa(u32 reg1,u32 reg2,void* data)
{
	u32 lo = emit_lub(reg2,(u32)data);
	emit_sw(reg1,reg2,lo);
}
//1 opcode
void emit_sh_load(u32 rt,u32 sh4_reg)
{
	emit_lw(rt,psp_ctx_reg,Sh4cntx.offset(sh4_reg));
}
void emit_sh_load(u32 rt,shil_param prm)
{
	emit_sh_load(rt,prm._reg);
}
void emit_sh_load_hu(u32 rt,u32 sh4_reg)
{
	emit_lhu(rt,psp_ctx_reg,Sh4cntx.offset(sh4_reg));
}
void emit_sh_load_hu(u32 rt,shil_param prm)
{
	verify(prm.is_reg());
	emit_sh_load_hu(rt,prm._reg);
}
//1 opcode
void emit_sh_addr(u32 rt,u32 sh4_reg)
{
	emit_addiu(rt,psp_ctx_reg,Sh4cntx.offset(sh4_reg));
}
void emit_sh_addr(u32 rt,shil_param prm)
{
	verify(prm.is_reg());
	emit_sh_addr(rt,prm._reg);
}
//1 opcode
void emit_sh_store(u32 rt,u32 sh4_reg)
{
	emit_sw(rt,psp_ctx_reg,Sh4cntx.offset(sh4_reg));
}
void emit_sh_store(u32 rt,shil_param prm)
{
	emit_sh_store(rt,prm._reg);
}
//1 opcode
void emit_sh_lvq(u32 vt,u32 sh4_reg)
{
	emit_lvq(vt,psp_ctx_reg,Sh4cntx.offset(sh4_reg));
}
void emit_sh_lvq(u32 vt,shil_param prm)
{
	verify(prm.is_r32fv()==4);
	emit_sh_lvq(vt,prm._reg);
}
//1 opcode
void emit_sh_svq(u32 vt,u32 sh4_reg)
{
	emit_svq(vt,psp_ctx_reg,Sh4cntx.offset(sh4_reg));
}
void emit_sh_svq(u32 vt,shil_param prm)
{
	verify(prm.is_r32fv()==4);
	emit_sh_svq(vt,prm._reg);
}
//1 opcode
void emit_sh_lvs(u32 vt,u32 sh4_reg)
{
	emit_lvs(vt,psp_ctx_reg,Sh4cntx.offset(sh4_reg));
}
void emit_sh_lvs(u32 vt,shil_param prm)
{
	//verify(prm.is_r32f());
	emit_sh_lvs(vt,prm._reg);
}
//1 opcode
void emit_sh_svs(u32 vt,u32 sh4_reg)
{
	emit_svs(vt,psp_ctx_reg,Sh4cntx.offset(sh4_reg));
}
void emit_sh_svs(u32 vt,shil_param prm)
{
	//verify(prm.is_r32f());
	emit_sh_svs(vt,prm._reg);
}

#define GET_REG_OFF(reg) ((u32)GetRegPtr(reg) - (u32)&Sh4cntx)
#define GET_VAR_OFF(var) ((u32)&var - (u32)&Sh4cntx)

void emit_sh_lwc1(u32 vt,u32 sh4_reg)
{
	//verify(prm.is_r32f());
	emit_lwc1(vt,psp_ctx_reg,GET_REG_OFF(sh4_reg));
}

void emit_sh_lwc1(u32 vt,shil_param prm)
{
	//verify(prm.is_r32f());
	emit_sh_lwc1(vt,prm._reg);
}

void emit_sh_swc1(u32 vt,u32 sh4_reg)
{
	//verify(prm.is_r32f());
	emit_swc1(vt,psp_ctx_reg,GET_REG_OFF(sh4_reg));
}

void emit_sh_swc1(u32 vt,shil_param prm)
{
	//verify(prm.is_r32f());
	emit_sh_swc1(vt,prm._reg);
}

void AllocateStaticRegisters(DecodedBlock* block){
	/*for (int regNum = 0; regNum < 8; ++regNum) {
		if (block->_regUsed[regNum] == 255) break;
		regMapping[regNum] = block->_regUsed[regNum];
		emit_sh_load(psp_s0+regNum, block->_regUsed[regNum]);
	}*/

	blocked = true;
}

struct mips_reg_alloc: RegAlloc<int, int>
{
	/*void Preload(u32 reg, int nreg) override
	{
		emit_sh_load(Register(nreg), reg);
	}
	void Writeback(u32 reg, int nreg) override
	{
		if (reg == reg_pc_dyn)
			// reg_pc_dyn has been stored in r4 by the jdyn op implementation
			// No need to write it back since it won't be used past the end of the block
			; //ass.Mov(r4, Register(nreg));
		else
			storeSh4Reg(Register(nreg), reg);
	}

	void Preload_FPU(u32 reg, int nreg) override
	{
		const s32 shRegOffs = (u8*)GetRegPtr(reg) - sh4_dyna_context;

		ass.Vldr(SRegister(nreg), MemOperand(r8, shRegOffs));
	}
	void Writeback_FPU(u32 reg, int nreg) override
	{
		const s32 shRegOffs = (u8*)GetRegPtr(reg) - sh4_dyna_context;

		ass.Vstr(SRegister(nreg), MemOperand(r8, shRegOffs));
	}

	SRegister mapFReg(const shil_param& prm)
	{
		return SRegister(mapf(prm));
	}
	Register mapReg(const shil_param& prm)
	{
		return Register(mapg(prm));
	}*/
};

//static mips_reg_alloc reg;

void SaveAllocatedReg(){
	blocked = false;

#if 0
	/*for (int regNum = 0; regNum < 8; regNum++) {
		if (regMapping[regNum] == 255) { /*CodeDump("code.bin"); printf("%d\n",regNum); die("SAVE");*/ break;}
	/*	emit_sw(psp_s0+regNum,psp_ctx_reg,Sh4cntx.offset(regMapping[regNum]));
		regMapping[regNum] = 255;
	}

	regLeft = 1;*/
#endif
	CodeDump("code.bin"); die("SAVE");
}

const u32 djump_temp=psp_k1;	//djump_reg or djump_cond

void* loop_no_update;
void* ngen_LinkBlock_Static_stub;
void* ngen_LinkBlock_Dynamic_1st_stub;
void* ngen_LinkBlock_Dynamic_2nd_stub;
void* ngen_BlockCheckFail_stub;
void* loop_do_update_write;
void (*loop_code)() ;
void (*ngen_FailedToFindBlock)();

struct
{
	bool has_jcond;

	void Reset()
	{
		has_jcond=false;
	}
} compile_state;
u32 last_block;

void ngen_Begin(DecodedBlock* block,bool force_checks)
{
	compile_state.Reset();

	if (force_checks)
	{
		u8* ptr = GetMemPtr(block->start, 4);
		if (ptr == NULL)
			return;

		emit_li(psp_a0,*ptr);

		u32 low=emit_luab(psp_a1,ptr);
		emit_lw(psp_a1,psp_a1,low);

		u32* jdst=(u32*)emit_GetCCPtr();
		jdst+=4;
		emit_beq(psp_a0,psp_a1,jdst);		//same ?
		low=emit_lub(psp_a0,block->start);	//delay slot, first step of loading block->start

		emit_j(ngen_BlockCheckFail_stub);		//jump
		emit_addiu(psp_a0,psp_a0,low);		//delay slot, 2nd step of loading block->start
		//jump dst is here
	}
	

	u32* jdst=(u32*)emit_GetCCPtr();
	jdst+=6;
	emit_bgez(psp_cycle_reg,jdst);	//if cycles <0 break the chain, esle skip 5 opcodes
	emit_addiu(psp_cycle_reg,psp_cycle_reg,-block->cycles);	//sub cycles		| 1
	
	emit_li(psp_next_pc_reg,block->start,2);				//load pc			| 2& 3
	emit_j(loop_do_update_write);							//jumpity jump		| 4
	emit_addiu(psp_cycle_reg,psp_cycle_reg,block->cycles);	//re-add cycles		| 5
}

//static !
void DoStatic(u32 target)
{
	emit_lui(psp_a0,target>>16);
	emit_jal(ngen_LinkBlock_Static_stub);
	emit_ori(psp_a0,psp_a0,target&0xFFFF);
}
void* FASTCALL ngen_LinkBlock_Static(u32 pc,u32* patch)
{
	next_pc=pc;

	DynarecCodeEntry* rv=rdv_FindOrCompile();
	
	emit_ptr=patch;
	{
		emit_j(rv);
		emit_nop();
	}
	emit_ptr=0;
	make_address_range_executable((u32)patch, (u32)(patch+2));

	return (void*)rv;
}

//Dynamic block: 1st time -> call stub & modify to compare
//On compare fail -> jump directly
void DoDynamic(u32 target)
{
	emit_jal(ngen_LinkBlock_Dynamic_1st_stub);
	emit_move(psp_a0,djump_temp);

	emit_Skip(24);

	/*
	//guess mode:
	li(psp_a0,expect_addr);		//2
	bne djump_temp,psp_a0,fail	//1
	nop();						//1
	j expect_dst				//1
	nop();						//1
	fail:	
	jal DynamicFallbackStub		//1
	nop()						//1

	total: 8 opcodes

	//fallback:
	emit_j(loop_no_update);
	emit_move(psp_a0,djump_temp);
	*/
}

void* FASTCALL ngen_LinkBlock_Dynamic_1st(u32 pc,u32* patch)
{
	next_pc=pc;
	
	DynarecCodeEntry* rv=rdv_FindOrCompile();
	
	emit_ptr=patch;
	{
		emit_li(psp_a0,pc);		//2
		u32* dst=(u32*)emit_GetCCPtr()+2;
		dst+=2;
		emit_bne(djump_temp,psp_a0,dst);	//1
		emit_nop();							//1
		emit_j(rv);							//1
		emit_nop();							//1
		//fail:	
		emit_jal(ngen_LinkBlock_Dynamic_2nd_stub);		//1
		emit_nop();						//1
	}
	make_address_range_executable((u32)patch, (u32)emit_ptr);
	emit_ptr=0;

	return (void*)rv;
}

void* FASTCALL ngen_LinkBlock_Dynamic_2nd(u32 pc,u32* patch)
{
	next_pc=pc;
	
	DynarecCodeEntry* rv=rdv_FindOrCompile();
	
	emit_ptr=patch;
	{
		emit_j(loop_no_update);
		emit_move(psp_a0,djump_temp);
	}
	make_address_range_executable((u32)patch, (u32)emit_ptr);
	emit_ptr=0;

	return (void*)rv;
}

#define BET_GET_CLS(x) (x>>3)

void ngen_End(DecodedBlock* block)
{
	switch(block->BlockType)
	{
	case BET_Cond_0:
	case BET_Cond_1:
		{
			//printf("COND %d\n",block->BlockType&1);

			u32 reg = djump_temp;
			if (!compile_state.has_jcond)
			{
				reg=psp_a0;
				emit_sh_load(psp_a0,reg_sr_T);
			}

			u32* target=(u32*)emit_GetCCPtr();
			target+=2; // for the branches
			target+=3;	//skip 3 opcodes

			if (block->BlockType&1)
			{	//==1 -> branch block else skip to next block
				emit_beq(reg,psp_zero,target);
			}
			else
			{	//==0 -> branch block else skip to next block
				emit_bne(reg,psp_zero,target);
			}
			
			emit_nop();	//delay slot

			DoStatic(block->BranchBlock);

			DoStatic(block->NextBlock);
		}
		break;

	case BET_DynamicCall:
	case BET_DynamicJump:
	case BET_DynamicRet:
		//printf("Dynamic !\n");
		DoDynamic(block->NextBlock);
		break;

	case BET_StaticIntr:
	case BET_DynamicIntr:
		u32 reg;
		//printf("Interrupt !\n");
		if (block->BlockType==BET_StaticIntr)
		{
			emit_li(psp_a0,block->BranchBlock);
			reg=psp_a0;
		}
		else
		{
			reg=djump_temp;
		}
		emit_jal(UpdateINTC);
		emit_sh_store(reg,reg_nextpc);	//delay slot

		emit_j(loop_no_update);
		emit_sh_load(psp_next_pc_reg,reg_nextpc);	//deay slot

		break;

	case BET_StaticCall:
	case BET_StaticJump:
		//printf("Static 0x%08X!\n",block->BranchBlock);
		DoStatic(block->BranchBlock);
		break;
		
	default:
		die("invalid end mode");
	}
}

#define ngen_Bin_nostore(bin_op,bin_opi,is,tf)	\
{	bool short_form=false;\
	bool rs2_loaded=false;\
	if (op->SkipLoadReg2) { rs2_loaded = true; } \
	if (op->loadReg) emit_sh_load(psp_a0,op->rs1);	\
	if (op->rs2._reg == op->rs1._reg && !op->rs2.is_imm()){ \
	 \
	 	emit_##bin_op(return_reg,psp_a0,psp_a0);	\
		short_form = true;\
		rs2_loaded = true; \
	} \
	\
	if (op->rs2.is_imm() && !short_form)	\
	{	\
		if (is_##is##16(tf op->rs2._imm))	\
		{	\
			short_form=true;	\
			emit_##bin_opi(return_reg,psp_a0,tf op->rs2._imm);	\
		}	\
		else	\
		{	\
			emit_li(psp_a1,op->rs2._imm);	\
		}	\
	}	\
	else if (op->rs2.is_r32i() && !rs2_loaded)	\
	{	\
		emit_sh_load(psp_a1,op->rs2._reg);	\
	}	\
	else if (!short_form && !rs2_loaded)	\
	{	\
		printf("%d \n",op->rs2.type);	\
		verify(false);	\
	}	\
	\
	if (!short_form) emit_##bin_op(return_reg,psp_a0,psp_a1);	\
}


#define ngen_Bin(bin_op,bin_opi,is,tf)	\
{	\
	u32 return_reg = (op->SwapSaveReg ? psp_a1 : psp_a0);\
	return_reg = (op->UseCustomReg ? op->customReg : return_reg);\
	ngen_Bin_nostore(bin_op,bin_opi,is,tf) \
	if (op->SaveReg) emit_sh_store(return_reg,op->rd._reg);	\
}

#define ngen_Unary(un_op)	\
{	\
	u32 return_reg = (op->SwapSaveReg ? psp_a1 : psp_a0); \
	if (op->loadReg) emit_sh_load(psp_a0,op->rs1);	\
	emit_##un_op(return_reg,psp_a0);	\
	if (op->SaveReg) emit_sh_store(return_reg,op->rd);	\
}

void* _vmem_read_const(u32 addr,bool& ismem,u32 sz);

enum mem_op_type
{
	SZ_8,
	SZ_16,
	SZ_32I,
	SZ_32F,
	SZ_64F,
};

u32 memop_bytes(mem_op_type tp)
{
	const u32 rv[] = { 1,2,4,4,8};

	return rv[tp];
}

mem_op_type memop_type(shil_opcode* op)
{

	int Lsz=-1;
	int sz=op->flags&0x7f;

	bool fp32=op->rs2.is_r32f() || op->rd.is_r32f();

	if (sz==1) Lsz=SZ_8;
	if (sz==2) Lsz=SZ_16;
	if (sz==4 && !fp32) Lsz=SZ_32I;
	if (sz==4 && fp32) Lsz=SZ_32F;
	if (sz==8) Lsz=SZ_64F;

	verify(Lsz!=-1);
	
	return (mem_op_type)Lsz;
}

bool ngen_writemem_immediate(shil_opcode* op)
{
	if (!op->rs1.is_imm())
		return false;

	//mem_op_type optp = memop_type(op);
	bool isram = false;
	void* ptr = _vmem_write_const(op->rs1._imm, isram, op->flags);

	if (isram){
		u32 offs=emit_luab(psp_a0,ptr);
		emit_sh_load(psp_a1,op->rs2);
		if (op->flags==1)
		{
			//x86e->Emit(op_movsx8to32,EAX,ptr);
			emit_sb(psp_a1,psp_a0,offs);
		}
		else if (op->flags==2)
		{
			//x86e->Emit(op_movsx16to32,EAX,ptr);
			emit_sh(psp_a1,psp_a0,offs);
		}
		else if (op->flags==4)
		{
			//x86e->Emit(op_mov32,EAX,ptr);
			emit_sw(psp_a1,psp_a0,offs);
		}
	}

	return true;
}

bool readwriteparams(shil_opcode* op,bool& wasram,void*& ptr)
{
	if (op->rs1.is_imm())
	{
		verify(op->op==shop_readm);
		ptr=_vmem_read_const(op->rs1._imm,wasram,op->flags);
		return true;
	}
	else
	{
		if (op->loadReg) emit_sh_load(psp_a0,op->rs1);
		//emit_andi(psp_a0,psp_s1,0x1f);
		//02002021 -> move a0, s0
		//emit_Write32(0x02002021);
		//emit_move(psp_a0,psp_s0);
        
		if (!op->rs3.is_null())
		{
			if (op->rs3.is_imm())
			{
				if (op->rs3._imm==0)
				{
					//printf("op->rs3 == 0:: wtf?\n");
				}
				else if (is_s16(op->rs3._imm))
				{
					emit_addiu(psp_a0,psp_a0,op->rs3._imm);
				}
				else
				{
					emit_li(psp_a3,op->rs3._imm);
					emit_addiu(psp_a0,psp_a0,psp_a3);
				}
			}
			else if (op->rs3.is_r32i())
			{
				if(!op->SkipLoadReg2) emit_sh_load(psp_a3,op->rs3);
				emit_addu(psp_a0,psp_a0,psp_a3);
			}
			else
			{
				die("invalid rs3");
			}			
		}
		return false;
	}

}
u32 emit_SlideDelay() { emit_Skip(-4); u32 rv=*(u32*)emit_GetCCPtr();  return rv;}
u32 emit_SlideDelay(u8 sz) { emit_Skip(-sz); u32 rv=*(u32*)emit_GetCCPtr();  return rv;}
/*
	fpu helper stuff
*/
void r_fadd(f32* fn,f32* fm)	{ *fn += *fm;	}
void r_fsub(f32* fn,f32* fm)	{ *fn -= *fm;	}
void r_fmul(f32* fn,f32* fm)	{ *fn *= *fm;	}
void r_fdiv(f32* fn,f32* fm)	{ *fn /= *fm;	}
u32 r_fcmp_gt(f32* fn,f32* fm)	{ return *fn > *fm ? 1:0; }
u32 r_fcmp_eq(f32* fn,f32* fm)	{ return *fn == *fm ? 1:0; }

void r_fsca(f32* fn,u32 pi_index)
{
	__asm__ volatile (
		"mtv	%2, S100\n"
		"vi2f.s S100, S100, 14\n"
		"vrot.p C000, S100, [s,c]\n"
		"sv.s	S000, %0\n"
		"sv.s	S001, %1\n"
	: "=m" ( *fn ),"=m" ( *(fn+1) ) : "r" ( pi_index ) );
}

void r_fsrra(f32* fn)
{
	__asm__ 
		(
	".set			push\n"
	".set			noreorder\n"
	"lv.s			s000,  %1\n"
	"vrsq.s			s000, s000\n"
	"sv.s			s000, %0\n"
	".set			pop\n"
	: "=m"(*fn)
	: "m"(*fn)
		);
}

void r_fipr(f32* fn,f32* fm)
{
	__asm__ 
		(
	".set			push\n"
	".set			noreorder\n"
	"lv.q			c110,  %1\n"
	"lv.q			c120,  %2\n"
	"vdot.q			s000, c110, c120\n"
	"sv.s			s000, %0\n"
	".set			pop\n"
	: "=m"(*(fn+3))
	: "m"(*fn),"m"(*fm)
		);
}

void r_fsqrt(f32* fn)
{
	__asm__ 
		(
		".set			push\n"
		".set			noreorder\n"
		"lv.s			s000,  %1\n"
		"vsqrt.s		s000, s000\n"
		"sv.s			s000, %0\n"
		".set			pop\n"
		: "=m"(*fn)
		: "m"(*fn)
		);
}

//rs1+rs2*rs3
void r_fmac(f32* fn,f32* f0,f32* fm)
{
	__asm__ 
		(
		".set			push\n"
		".set			noreorder\n"
		"lv.s			s001,  %0\n"
		"lv.s			s000,  %1\n"
		"lv.s			s010,  %2\n"
		"vhdp.p			s000, c010, c000\n"
		"sv.s			s000, %0\n"
		".set			pop\n"
		: "+m"(*fn)
		: "m"(*f0), "m"(*fm)
		);
	//*fn =*fn+*f0* *fm;
}

int r_f2i_t(f32* fn)
{
	return (int)*fn;
}
void r_i2f_z(int src, f32* dst)
{
	*dst=src;
}
void r_i2f_n(int src, f32* dst)
{
	*dst=src;
}

void r_ftrv(f32* fn,f32* mtrx)
{
	__asm__ 
		(
	".set			push\n"
	".set			noreorder\n"
	"lv.q			c100,  %1\n"
	"lv.q			c110,  %2\n"
	"lv.q			c120,  %3\n"
	"lv.q			c130,  %4\n"
	"lv.q			c200,  %5\n"
	"vtfm4.q		c000, e100, c200\n"
	"sv.q			c000, %0\n"
	".set			pop\n"
	: "=m"(*fn)
	: "m"(*mtrx),"m"(*(mtrx+4)),"m"(*(mtrx+8)),"m"(*(mtrx+12)), "m"(*fn)
		);
}

void PRINTREG(u32 val, u32 val2, u32 val3){
	printf("A0: %x A1: %x   %x\n",val,val2,val3);
}

extern "C" void asm_read08();
extern "C" void asm_read16();
extern "C" void asm_read32();

extern "C" void asm_write32();
extern "C" void asm_write16();
extern "C" void asm_write08();

bool OptmizedReading = false;
bool OptmizedWriting = false;

void GenWrite(shil_opcode* op){

	if (op->flags==8){
	
		emit_sh_load(psp_a3,op->rs2._reg+1);
		emit_jal(&WriteMem64);
		emit_sh_load(psp_a2,op->rs2._reg);
		return;
	}

	u32 _opcode = emit_SlideDelay();

	if (!op->UseCustomReg) emit_sh_load(psp_a1,op->rs2);

	switch(op->flags)
	{
		case 1:
			if (OptmizedWriting) emit_jal(asm_write08);	
			else				 emit_jal(&WriteMem8);	
			break;
		case 2:
			if (OptmizedWriting) emit_jal(asm_write16);	
			else				 emit_jal(&WriteMem16);
			break;
		case 4:
			if (OptmizedWriting) emit_jal(asm_write32);	
			else				 emit_jal(&WriteMem32);
			break;
		default:
			verify(false);
	}

	emit_Write32(_opcode);
}

void FASTCALL do_sqw_mmu(u32 dst);

void ngen_CC_Start(shil_opcode* op) { die("Unsuported for psp.."); }
void ngen_CC_Param(shil_opcode* op,shil_param* par,CanonicalParamType tp) { die("Unsuported for psp.."); }
void ngen_CC_Call(shil_opcode*op,void* function) { die("Unsuported for psp.."); }
void ngen_CC_Finish(shil_opcode* op) { die("Unsuported for psp.."); }



DynarecCodeEntry* ngen_Compile(DecodedBlock* block,bool force_checks)
{
	if (unlikely(emit_FreeSpace()<16*1024))
		return 0;
	
	DynarecCodeEntry* rv=(DynarecCodeEntry*)emit_GetCCPtr();
	
	ngen_Begin(block,force_checks);

	/*AllocateStaticRegisters(block);*/
	StartCodeDump();

	bool save_block = false;

	for (size_t i=0;i<block->oplist.size();i++)
	{
		shil_opcode* op=&block->oplist[i];
		switch(op->op)
		{
		case shop_ifb:
			{
				if (op->rs1._imm)
				{
					emit_li(psp_a0,op->rs2._imm);
					emit_sh_store(psp_a0,reg_nextpc);
				}
				emit_jal(OpPtr[op->rs3._imm]);
				emit_addiu(psp_a0,psp_zero,op->rs3._imm&0xFFFF);
			}
			break;

		case shop_jcond:
			compile_state.has_jcond=true;
		case shop_jdyn:
			{
				emit_sh_load(djump_temp,op->rs1);
				if (op->rs2.is_imm())
				{
					if (is_s16(op->rs2._imm))
					{
						emit_addiu(djump_temp,djump_temp,op->rs2._imm);
					}
					else
					{
						emit_li(psp_a1,op->rs2._imm);
						emit_addu(djump_temp,djump_temp,psp_a1);
					}
				}
			}
			break;

		case shop_mov64:
			{
			    emit_sh_load(psp_a0,op->rs1._reg);
				emit_sh_load(psp_a1,op->rs1._reg+1);

				emit_sh_store(psp_a0,op->rd._reg);
				emit_sh_store(psp_a1,op->rd._reg+1);
			}
			break;

		case shop_mov32f:
		{
			if (op->loadReg) emit_sh_lwc1(1,op->rs1);
			if (op->SaveReg) emit_sh_swc1(1,op->rd);	
		}
		break;

		case shop_mov32:
			{
	
				if (op->rs1.is_imm())
				{

					if (op->rs1._imm==0)
					{
						if (op->UseMemReg2) emit_move(psp_a3,psp_zero);
						emit_sh_store(psp_zero,op->rd);
					}
					else
					{
						if (op->UseCustomReg){
							emit_li(op->customReg,op->rs1._imm);
							if (op->SaveReg) emit_sh_store(op->customReg,op->rd);
						}
						else if (op->UseMemReg2) {
							emit_li(psp_a3,op->rs1._imm);
							if (op->SaveReg) emit_sh_store(psp_a3,op->rd);
						}
						else
						{
							emit_li((op->SwapSaveReg ? psp_a1 : psp_a0),op->rs1._imm);
							if (op->SaveReg) emit_sh_store((op->SwapSaveReg ? psp_a1 : psp_a0),op->rd);
						}
					}				
				}
				else if (likely(op->rs1.is_r32()))
				{
					if (op->UseCustomReg){
						emit_sh_load(op->customReg,op->rs1);
						if (op->SaveReg) emit_sh_store(op->customReg,op->rd);
					}else{
						if (op->loadReg)	emit_sh_load(psp_a0,op->rs1);
						if (op->SaveReg)    emit_sh_store(psp_a0,op->rd);
					}
				}
				else
				{
					goto defaulty;
				}

			}
			break;

		case shop_nop:
		break;

		case shop_readm:
			{
				//die("shop_readm");
				bool wasram=false;
				void* ptr;
				bool _ptr_ = false;

				if (readwriteparams(op,wasram,ptr))
				{
					if (likely(wasram))
					{
						u8 dst_reg = (op->SwapReg ? psp_a0 : psp_v0);
						   dst_reg = (op->SwapSaveReg ? psp_a1 : dst_reg);

						u32 offs=emit_luab(psp_a0,ptr);
						if (op->flags==1)
						{
							//x86e->Emit(op_movsx8to32,EAX,ptr);
							emit_lb(dst_reg,psp_a0,offs);
						}
						else if (op->flags==2)
						{
							//x86e->Emit(op_movsx16to32,EAX,ptr);
							emit_lh(dst_reg,psp_a0,offs);
						}
						else if (op->flags==4)
						{
							//x86e->Emit(op_mov32,EAX,ptr);
							emit_lw(dst_reg,psp_a0,offs);
						}
						else
						{
							die("Invalid mem read size");
						}

						if (op->SaveReg) emit_sh_store(dst_reg,op->rd._reg);
					}
					else
					{
						wasram = true;

						u32 target=(u32)emit_GetCCPtr();

						//printf("%0x\n",target);

						emit_li(psp_a0,op->rs1._imm);
						
						u32 delay=emit_SlideDelay();
						switch(op->flags)
						{
							case 1:
								emit_jal(ptr);
								emit_Write32(delay);	//dslot
								emit_seb(psp_v0,psp_v0);
								break;
							case 2:
								emit_jal(ptr);
								emit_Write32(delay);	//dslot
								emit_seh(psp_v0,psp_v0);
								break;
							case 4:
								emit_jal(ptr);
								emit_Write32(delay);	//dslot
								break;
						}

						if (op->SwapReg) emit_move((op->SwapSaveReg ? psp_a1 : psp_a0),psp_v0);
						if (op->SaveReg) emit_sh_store(psp_v0,op->rd._reg);
					}
				}
				
				if (!wasram)
				{
					u32 delay=emit_SlideDelay();
					switch(op->flags)
					{
						case 1:
							if (OptmizedReading) emit_jal(asm_read08);	
							else				 emit_jal(ReadMem8);

							emit_Write32(delay);	//dslot
							emit_seb(psp_v0,psp_v0);
							break;
						case 2:
							if (OptmizedReading) emit_jal(asm_read16);	
							else				 emit_jal(ReadMem16);
							emit_Write32(delay);	//dslot
							emit_seh(psp_v0,psp_v0);
							break;
						case 4:
							if (OptmizedReading) emit_jal(asm_read32);	
							else				 emit_jal(ReadMem32);
							emit_Write32(delay);	//dslot
							break;
						case 8:
							emit_jal(ReadMem64);
							emit_Write32(delay);	//dslot
							break;
					}

					if (op->SwapReg) emit_move((op->SwapSaveReg ? psp_a1 : psp_a0),psp_v0);
					if (op->SaveReg) emit_sh_store(psp_v0,op->rd._reg);
					if (op->SwapWFloatR) emit_mtc1(psp_v0, psp_fr1);
				}

				if (op->flags==8)
					emit_sh_store(psp_v1,op->rd._reg+1);
			}
			break;

		case shop_writem:
			{
			
				if (ngen_writemem_immediate(op)) continue;

				if (op->loadReg)  emit_sh_load(psp_a0,op->rs1);

				if (op->SkipLoadReg2){
					emit_addu(psp_a0,psp_a0,psp_a3);
				}else if (!op->rs3.is_null())
				{
					
					if (op->rs3.is_imm())
					{
						if (is_s16(op->rs3._imm))
						{
							emit_addiu(psp_a0,psp_a0,op->rs3._imm);
						}
						else
						{
							emit_li(psp_a3,op->rs3._imm);
							emit_addiu(psp_a0,psp_a0,psp_a3);
						}
					}
					else if (op->rs3.is_r32i())
					{
						emit_sh_load(psp_a3,op->rs3);
						emit_addu(psp_a0,psp_a0,psp_a3);
					}
					else
					{
						die("invalid rs3");
					}			
				}

				GenWrite(op);
				
			}
			break;

		case shop_rocr:
			{
				if (op->loadReg) emit_sh_load(psp_a0,op->rs1);

				u32 return_reg = (op->SwapSaveReg ? psp_a1 : psp_a0);

				emit_andi(psp_a2,psp_a0,1);
				
				//It works but needs more tests..			
				emit_rotr(return_reg,psp_a0,1);

				emit_sh_store(psp_a2,op->rd2);
				
				if (op->SaveReg) emit_sh_store(return_reg,op->rd);				
			}
			break;
			
		case shop_rocl:
			{
				u32 return_reg = (op->SwapSaveReg ? psp_a1 : psp_a0);

				if (!op->SkipLoadReg2) emit_sh_load(psp_a1,op->rs2);
				if (op->loadReg) emit_sh_load(psp_a0,op->rs1);
				
				emit_sll(psp_a2,psp_a0,1);
				emit_or(psp_a2,psp_a2,psp_a1);
				emit_sh_store(psp_a2,op->rd);

				emit_srl(psp_a0,psp_a0,31);
				emit_sh_store(psp_a0,op->rd2);
			}
			break;

		case shop_sync_sr:
			{
				u32 delay=emit_SlideDelay();
				emit_jal(UpdateSR);
				emit_Write32(delay);
			}
			break;
		
		/*case shop_sync_fpscr:
			{
				emit_jal(UpdateFPSCR);
				emit_nop();
			}
			break;*/

		case shop_shl:
			ngen_Bin(sllv,sll,u,+);
			break;
		case shop_shr:
			ngen_Bin(srlv,srl,u,+);
			break;
		case shop_sar:
			ngen_Bin(srav,sra,u,+);
			break;
		case shop_ror:
			ngen_Bin(rotrv,rotr,u,+);
			break;

		case shop_shad:
		case shop_shld:
			{
				psp_gpr_t val=psp_a0,shft=psp_a1,negshft=psp_a2,shft_1f=psp_a3;
				psp_gpr_t sh_l=psp_t0,sh_r=psp_t1,sh_z=psp_t2,isltz=psp_t3;
				
				//emit_sh_load(shft,op->rs2);
				if(!op->SkipLoadReg2) emit_sh_load(psp_a1,op->rs2);
				if (op->loadReg) emit_sh_load(val,op->rs1);

				emit_negu(negshft,shft);
				emit_andi(shft_1f,shft,0x1F);

				emit_slt(isltz,shft,psp_zero);

				u32 return_reg = (op->SwapSaveReg ? psp_a1 : sh_l);	

				emit_sllv(return_reg,val,shft);

				if (op->op==shop_shad)
				{
					emit_srav(sh_r,val,negshft);
					emit_sra(sh_z,val,31);
				}
				else
				{
					emit_srlv(sh_r,val,negshft);
					sh_z=psp_zero;
				}

				emit_slti(shft_1f,shft_1f,1);	//shft_1f=shft_1f<1?1:0	//for some odd reason using just movz doesn't work ...
				emit_movn(sh_r,sh_z,shft_1f);	//if (shft_1f==0) sh_r=sh_z;
				
				//s1=b!=0?s23:s1;
				emit_movn(return_reg,sh_r,isltz);		//if (shft<0) sh_l=sh_r;//if (isltz!=0) sh_l=sh_r

				if (op->SaveReg) emit_sh_store(return_reg,op->rd);
			}
			break;

		case shop_and:
			ngen_Bin(and,andi,u,+);
			break;

		case shop_or:
			ngen_Bin(or,ori,u,+);
			break;

		case shop_xor:
			ngen_Bin(xor,xori,u,+);
			break;

		case shop_add:
			ngen_Bin(addu,addiu,s,+);
			break;
		case shop_sub:
			ngen_Bin(subu,addiu,s,-);
			break;

		case shop_neg:
			ngen_Unary(negu);
			break;
			
		case shop_not:
			ngen_Unary(not);
			break;

		case shop_test:
			{
				u32 return_reg = (op->SwapSaveReg ? psp_a1 : psp_a0);	
				ngen_Bin_nostore(and,andi,u,+);
				emit_sltiu(return_reg,return_reg,1);//if a0==0 -> a0=1 else a0=0
				if (op->SaveReg) emit_sh_store(return_reg,op->rd);
			}
			break;

		case shop_swaplb:
			if (op->loadReg)       emit_sh_load(psp_a0,op->rs1);
			//emit_sll(psp_a1,psp_a0,8);
			//emit_srl(psp_a2,psp_a0,8);
			//emit_andi(psp_a3, psp_a1, 0xffff);
			//emit_andi(psp_a1,psp_a2,0xff);
			emit_ext(psp_a3, psp_a0, 24, 8);
			emit_ext(psp_a1, psp_a0, 16, 8);
			emit_li(psp_a2,-65536);
			emit_or(psp_a1,psp_a3,psp_a1);
			emit_and(psp_a0,psp_a0,psp_a2);
			emit_or(psp_a0,psp_a0,psp_a1);
			if (op->SaveReg)        emit_sh_store(psp_a0,op->rd);
		break;

		case shop_setpeq:
		{
			emit_sh_load(psp_a1,op->rs2);
			if (op->loadReg) emit_sh_load(psp_a0,op->rs1);
			
			emit_xor(psp_at,psp_a0,psp_a1);

			u32* exit=(u32*)emit_GetCCPtr() + 11;
			
			emit_ext(psp_a0,psp_at,31,24);
			emit_beql(psp_a0,0,exit);
			emit_sltiu(psp_a0,psp_a0,1);

			emit_ext(psp_a0,psp_at,23,16);
			emit_beql(psp_a0,0,exit);
			emit_sltiu(psp_a0,psp_a0,1);

			emit_ext(psp_a0,psp_at,15,8);
			emit_beql(psp_a0,0,exit);
			emit_sltiu(psp_a0,psp_a0,1);

			emit_ext(psp_a0,psp_at,7,0);
			emit_sltiu(psp_a0,psp_a0,1);

			if (op->SaveReg) emit_sh_store(psp_a0,op->rd);
		}
		break;

		case shop_cmp_set:
		{
			u32 return_reg = (op->SwapSaveReg ? psp_a1 : psp_a0);

			ngen_Bin_nostore(subu,addiu,s,-);

			emit_sh_store(return_reg,op->rd);

			u8 rs1 = return_reg, rs2 = psp_zero;

			const u8 flag = op->flags&0xff;
			const u16 rd_mov32 = op->flags>>20;

			if (flag == shop_seteq){

				if (op->rs2.is_r32i()) {
					rs2 = psp_a1;
					emit_sh_load(rs2,op->rs3);
					
					emit_xor(return_reg,rs1,rs2);
				}else
				if (op->rs2._imm==0)
				{
					rs2=psp_a0;
				}
				else
				{
					emit_addiu(return_reg,rs1,-op->rs3._imm);
				}
				
				emit_sltiu(return_reg,rs1,1);
			}else{
								
				bool flip=flag==shop_setge || flag==shop_setae;
				bool usgnd=flag>=shop_setae;

				if (!op->rs3.is_imm()){
					rs2 = psp_a1;
					emit_sh_load(rs2,op->rs3);
				}

				if (flip) 
				{	//swap operands !
					u32 t=rs1;
					rs1=rs2;
					rs2=t;
				}

				if (usgnd)	emit_sltu(return_reg,rs2,rs1);
				else        emit_slt(return_reg,rs2,rs1);

				if (flip)   emit_xori(return_reg,return_reg,1);
			}

			if (op->SaveReg) emit_sh_store(return_reg,op->rd2);

			if (rd_mov32 != 0) emit_sh_store(return_reg,rd_mov32);
		}
		break;
			
		case shop_seteq:
			{
				/*verify(op->rs1.is_r32i());
				verify(op->rs2.is_r32i() || (op->rs2.is_imm() && is_s16(-op->rs2._imm)));*/

				u32 return_reg = (op->SwapSaveReg ? psp_a1 : psp_a0);

				u32 rs1,rs2,rf;
				rs1=psp_a0;
				rs2=psp_a1;
				rf=psp_at;				

				if (op->rs2.is_r32i())
				{
					if (!op->SkipLoadReg2) emit_sh_load(rs2,op->rs2);

					if (op->loadReg) emit_sh_load(rs1,op->rs1);

					emit_xor(rf,rs1,rs2);
				}
				else
				{
					if (op->loadReg) emit_sh_load(rs1,op->rs1);

					if (op->rs2._imm==0)
					{
						rf=psp_a0;
					}
					else
					{
						emit_addiu(rf,rs1,-op->rs2._imm);
					}
				}
				
				//if rf ==0 -> at=1; else at=0
				emit_sltiu(return_reg,rf,1);
				if (op->SaveReg) emit_sh_store(return_reg,op->rd);
			}
			break;
		
		case shop_xtrct:
			if (op->loadReg) emit_sh_load(psp_a0,op->rs1);
			emit_sh_load(psp_a1,op->rs2);

			emit_srl(psp_a0,psp_a0,16);
			emit_sll(psp_a1,psp_a1,16);
			emit_or(psp_a0,psp_a1,16);

			if (op->SaveReg) emit_sh_store(psp_a0,op->rd);
		break;

		case shop_setge:
		case shop_setgt:
		case shop_setae:
		case shop_setab:
			{
				/*verify(op->rs1.is_r32i());
				verify(op->rs2.is_r32i() || (op->rs2.is_imm() && op->rs2._imm==0));*/

				bool flip=op->op==shop_setge || op->op==shop_setae;
				bool usgnd=op->op>=shop_setae;

				u32 return_reg = (op->SwapSaveReg ? psp_a1 : psp_a0);

				u32 rs1=psp_a0;
				u32 rs2=psp_a1;

				if (op->rs2.is_imm())
					rs2=psp_zero;
				else if (!op->SkipLoadReg2)
					emit_sh_load(rs2,op->rs2);

				if (op->loadReg) emit_sh_load(rs1,op->rs1);

				if (flip) 
				{	//swap operands !
					u32 t=rs1;
					rs1=rs2;
					rs2=t;
				}

				if (usgnd)
				{	//unsigned
					emit_sltu(return_reg,rs2,rs1);
				}
				else
				{	//signed
					emit_slt(return_reg,rs2,rs1);
				}

				if (flip)
				{
					emit_xori(return_reg,return_reg,1);
				}

				if (op->SaveReg) emit_sh_store(return_reg,op->rd);
			}
			break;

		case shop_mul_u16:
		{
			verify(!op->rs1.is_null() && !op->rs2.is_null() && !op->rd.is_null());
			verify(op->rs1.is_reg());

			u32 return_reg = (op->SwapSaveReg ? psp_a1 : psp_a0);

			if (op->rs2.is_imm())	emit_li(psp_a1,op->rs2._imm&0xffff);
			else if (op->rs2.is_reg() && !op->SkipLoadReg2)	emit_sh_load(psp_a1,op->rs2);
		
			if (op->loadReg)		emit_sh_load(psp_a0,op->rs1);

			emit_multu(psp_a0,psp_a1);
			
			emit_mflo(return_reg);
			if (op->SaveReg) emit_sh_store(return_reg,op->rd._reg);
		}
		break;

		case shop_mul_s16:
		{
			verify(!op->rs1.is_null() && !op->rs2.is_null() && !op->rd.is_null());
			verify(op->rs1.is_reg());

			u32 return_reg = (op->SwapSaveReg ? psp_a1 : psp_a0);

			if (op->rs2.is_imm())	                        emit_li(psp_a1,op->rs2._imm&0xffff);
			else if (op->rs2.is_reg() && !op->SkipLoadReg2)	emit_sh_load(psp_a1,op->rs2);

			if (op->loadReg) 		emit_sh_load(psp_a0,op->rs1);

			emit_mult(psp_a0,psp_a1);
			
			emit_mflo(return_reg);
			if (op->SaveReg) emit_sh_store(return_reg,op->rd._reg);
		}
		break;

		case shop_mul_i32:
		{
			verify(!op->rs1.is_null() && !op->rs2.is_null() && !op->rd.is_null());
			verify(op->rs1.is_reg());

			u32 return_reg = (op->SwapSaveReg ? psp_a1 : psp_a0);

			if (op->rs2.is_imm())	emit_li(psp_a1,op->rs2._imm);
			else if (op->rs2.is_reg() && !op->SkipLoadReg2)	emit_sh_load(psp_a1,op->rs2);

			if (op->loadReg)		emit_sh_load(psp_a0,op->rs1);

			emit_mult(psp_a0,psp_a1);
			
			emit_mflo(return_reg);
			if (op->SaveReg) emit_sh_store(return_reg,op->rd._reg);
		}
		break;

		case shop_mul_u64:
		{
			verify(!op->rs1.is_null() && !op->rs2.is_null() && !op->rd.is_null());
			verify(op->rs1.is_reg());

			if (op->SwapReg) emit_move(psp_a1,psp_a0);
			else if (op->rs2.is_imm())	emit_li(psp_a1,op->rs2._imm);
			else if (op->rs2.is_reg())	emit_sh_load(psp_a1,op->rs2);

			if (op->loadReg) emit_sh_load(psp_a0,op->rs1);

			emit_multu(psp_a0,psp_a1);
			
			emit_mflo(psp_a0);
			emit_sh_store(psp_a0,op->rd._reg);
			emit_mfhi(psp_a0);
			emit_sh_store(psp_a0,op->rd2._reg);
		}

		break;
		case shop_mul_s64:
		{
			verify(!op->rs1.is_null() && !op->rs2.is_null() && !op->rd.is_null());
			verify(op->rs1.is_reg());

			if (op->SwapReg) emit_move(psp_a1,psp_a0);
			else if (op->rs2.is_imm())	emit_li(psp_a1,op->rs2._imm);
			else if (op->rs2.is_reg())	emit_sh_load(psp_a1,op->rs2);
		
			if (op->loadReg) 		emit_sh_load(psp_a0,op->rs1);

			emit_mult(psp_a0,psp_a1);
			
			emit_mflo(psp_a0);
			emit_sh_store(psp_a0,op->rd._reg);

			emit_mfhi(psp_a0);
			emit_sh_store(psp_a0,op->rd2._reg);
		}

		break;

		case shop_div1:
		{
			//Incomplete

			/*emit_sh_load(psp_t5,op->rd2);  //reg_sr_status
			
			emit_sh_load(psp_a1,op->rs2);  //N

			emit_ext(psp_t1,psp_t5,8, 8);  //Q

			emit_lui(psp_at,-512);    //0x80000000
			emit_move(psp_t2,psp_t1); //old_Q = Q

			emit_and(psp_t3,psp_a1,psp_at); //(u8)(0x80000000 & r[n])

			emit_sll(psp_a1,psp_a1,1);    //r[n] <<= 1;

			emit_sh_load(psp_t0,op->rd);   //T

			emit_sltiu(psp_t1,psp_t3,1);   //(u8)((0x80000000 & r[n]) !=0);

			emit_or(psp_a1,psp_a1,psp_t0); //r[n] |= (unsigned long)sr.T;

			emit_sh_load(psp_a0,op->rs1); //M

			emit_sh_store(psp_t3,op->rd);   //T

			printf("DIV1\n");*/
			

		}
		break;


		case shop_div32u:
		{
			if (op->loadReg) emit_sh_load(psp_a0,op->rs1);
			if (!op->SkipLoadReg2) emit_sh_load(psp_a1,op->rs2);

			emit_divu(psp_a0,psp_a1);

			emit_mflo(psp_a0);
			emit_mfhi(psp_a1);

			if (op->SaveReg) emit_sh_store(psp_a0,op->rd);
			emit_sh_store(psp_a1,op->rd2);
		}
		break;

		case shop_div32s:
		{
			if (op->loadReg) emit_sh_load(psp_a0,op->rs1);
			if (!op->SkipLoadReg2) emit_sh_load(psp_a1,op->rs2);

			emit_div(psp_a0,psp_a1);

			emit_mflo(psp_a0);
			emit_mfhi(psp_a1);

			if (op->SaveReg) emit_sh_store(psp_a0,op->rd);
			emit_sh_store(psp_a1,op->rd2);
		}
		break;

		case shop_div32p2:
		{ 
			emit_sh_load(psp_a2,op->rs3);	

			u32* target=(u32*)emit_GetCCPtr();

			emit_bne(psp_a2,psp_zero,target+24);
			emit_nop();
			if (op->loadReg) emit_sh_load(psp_a0,op->rs1);
			if (!op->SkipLoadReg2)emit_sh_load(psp_a1,op->rs2);
			emit_subu(psp_a0,psp_a0,psp_a1);
			if (op->SaveReg) emit_sh_store(psp_a0,op->rd);
		}
		break;


		case shop_adc:
		{
			if (op->loadReg)       emit_sh_load(psp_a0,op->rs1);
			if (!op->SkipLoadReg2) emit_sh_load(psp_a1,op->rs2);

			emit_sh_load(psp_a2,op->rs3);

			emit_addu(psp_a0,psp_a0,psp_a1);
			emit_addu(psp_a0,psp_a0,psp_a2);
			
			if (op->SaveReg) emit_sh_store(psp_a0,op->rd);
		}
		break;
		case shop_sbc:
		{
			if (op->loadReg)       emit_sh_load(psp_a0,op->rs1);
			if (!op->SkipLoadReg2) emit_sh_load(psp_a1,op->rs2);

			emit_sh_load(psp_a2,op->rs3);

			emit_subu(psp_a0,psp_a0,psp_a1);
			emit_subu(psp_a0,psp_a0,psp_a2);

			if (op->SaveReg) emit_sh_store(psp_a0,op->rd);
		}
		break;
				//fpu
		case shop_fadd:
			{
				verify(op->rs1._reg==op->rd._reg);
#if FULL_VFPU
				u32 vd = op->rd; vd = (vd&3)*32|(vd&12)>>2;
				u32 vt = op->rs2; vt = (vt&3)*32|(vt&12)>>2;
				emit_vadds(vd, vt, vd);
#elif HALF_VFPU
				//printf("0%x\n",(u32)emit_GetCCPtr());
				if (op->loadReg) emit_sh_lwc1(1,op->rd);

				u8 dest_reg = (op->SwapSaveReg ? 2 : 1);

				if (op->rd._reg != op->rs2._reg){
					if (!op->SkipLoadReg2) emit_sh_lwc1(2,op->rs2);
					emit_adds(dest_reg,1,2);
				}else{
					emit_adds(dest_reg,1,1);
				}

				if (op->SaveReg) emit_sh_swc1(dest_reg,op->rd);
#else
				emit_sh_addr(psp_a0,op->rd);
				emit_jal(r_fadd);
				emit_sh_addr(psp_a1,op->rs2);
#endif 
			}
			break;
		case shop_fsub:
			{
				verify(op->rs1._reg==op->rd._reg);
#if FULL_VFPU
				u32 vd = op->rd; vd = (vd&3)*32|(vd&12)>>2;
				u32 vt = op->rs2; vt = (vt&3)*32|(vt&12)>>2;
				emit_vsubs(vd, vt, vd);
#elif HALF_VFPU
				if (op->loadReg) emit_sh_lwc1(1,op->rd);

				u8 dest_reg = (op->SwapSaveReg ? 2 : 1);

				if (op->rd._reg != op->rs2._reg){
					if (!op->SkipLoadReg2)	emit_sh_lwc1(2,op->rs2);
					emit_subs(dest_reg,1,2);
				}else{
					emit_subs(dest_reg,1,1);
				}
			
				if (op->SaveReg) emit_sh_swc1(dest_reg,op->rd);
#else
				emit_sh_addr(psp_a0,op->rd);
				emit_jal(r_fsub);
				emit_sh_addr(psp_a1,op->rs2);
#endif
			}
			break;
		case shop_fmul:
			{
				verify(op->rs1._reg==op->rd._reg);
#if FULL_VFPU
				u32 vd = op->rd; vd = (vd&3)*32|(vd&12)>>2;
				u32 vt = op->rs2; vt = (vt&3)*32|(vt&12)>>2;
				emit_vmuls(vd, vt, vd);
#elif HALF_VFPU
				if (op->loadReg) emit_sh_lwc1(1,op->rd);

				u8 dest_reg = (op->SwapSaveReg ? 2 : 1);

				if (op->rd._reg != op->rs2._reg){
					if (!op->SkipLoadReg2) emit_sh_lwc1(2,op->rs2);
					emit_muls(dest_reg,1,2);
				}else{
					emit_muls(dest_reg,1,1);
				}
				
				if (op->SaveReg) emit_sh_swc1(dest_reg,op->rd);
#else
				emit_sh_addr(psp_a0,op->rd);
				emit_jal(r_fmul);
				emit_sh_addr(psp_a1,op->rs2);
#endif
			}
			break;
		case shop_fdiv:
			{
				verify(op->rs1._reg==op->rd._reg);
#if FULL_VFPU
				u32 vd = op->rd; vd = (vd&3)*32|(vd&12)>>2;
				u32 vt = op->rs2; vt = (vt&3)*32|(vt&12)>>2;
				emit_vrcps(vt, 31);
				emit_vmuls(vd, 31, vd);
#elif HALF_VFPU
				if (op->loadReg) emit_sh_lwc1(1,op->rd);

				u8 dest_reg = (op->SwapSaveReg ? 2 : 1);
				
				if (op->rd._reg != op->rs2._reg){
					emit_sh_lwc1(2,op->rs2);
					emit_divs(dest_reg,1,2);
				}else{
					emit_divs(dest_reg,1,1);
				}

				if (op->SaveReg) emit_sh_swc1(dest_reg,op->rd);
#else
				emit_sh_addr(psp_a0,op->rd);
				emit_jal(r_fdiv);
				emit_sh_addr(psp_a1,op->rs2);
#endif
			}
			break;
			
		case shop_fabs:
			{
				//die("shop_fabs");
#if FULL_VFPU
				u32 vd = op->rd; vd = (vd&3)*32|(vd&12)>>2;
				u32 vs = op->rs1; vs = (vs&3)*32|(vs&12)>>2;
				emit_vabss(vs, vd);
#elif HALF_VFPU
				u8 dest_reg = (op->SwapSaveReg ? 2 : 1);

				if (op->loadReg) emit_sh_lwc1(1,op->rs1);
				emit_abss(dest_reg,1);
				if (op->SaveReg) emit_sh_swc1(dest_reg, op->rd);
#else
				if (op->loadReg) emit_sh_load(psp_a0,op->rs1);
				emit_li(psp_at,0x7FFFFFFF);
				emit_and(psp_a0,psp_a0,psp_at);
				emit_sh_store(psp_a0,op->rd);
#endif
			}
			break;

		case shop_fneg:
			{
				//die("shop_fneg");
#if FULL_VFPU
				u32 vd = op->rd; vd = (vd&3)*32|(vd&12)>>2;
				u32 vs = op->rs1; vs = (vs&3)*32|(vs&12)>>2;
				emit_vnegs(vs, vd);
#elif HALF_VFPU
				u8 dest_reg = (op->SwapSaveReg ? 2 : 1);

				if (op->loadReg) emit_sh_lwc1(1,op->rs1);
				emit_negs(dest_reg,1);
				if (op->SaveReg) emit_sh_swc1(dest_reg, op->rd);
#else
				if (op->loadReg)  emit_sh_load(psp_a0,op->rs1);
				emit_li(psp_at,0x80000000);
				emit_xor(psp_a0,psp_a0,psp_at);
				emit_sh_store(psp_a0,op->rd);
#endif
			}
			break;

		case shop_fsca:
			{
				//die("shop_fsca");
#if FULL_VFPU
				u32 vd = op->rd; vd = (vd&3)*32|(vd&12)>>2;
				u32 vs = op->rs1; vs = (vs&3)*32|(vs&12)>>2;
				emit_vi2fs(vs, 31, 14);
				emit_vrotp_sc(31, vd);
#elif HALF_VFPU && 0
				u32 p=(u32)emit_GetCCPtr();
				printf("0%x\n");
				emit_sh_lvs(0, op->rs1);
				emit_vi2fs(0, 4, 14);
				emit_vrotp_sc(4, 0);
				emit_sh_svs(0, op->rd);
#else

				/*emit_sh_load_hu(psp_a1,op->rs1);
				emit_jal(r_fsca);
				emit_sh_addr(psp_a0,op->rd);*/

				/*emit_sh_load_hu(psp_a0,op->rs1);

				emit_sll(psp_a0,psp_a0,3);
				emit_addu(psp_a1,psp_pi_array,psp_a0);

				emit_lvs(0,psp_a1,0);
				emit_lvs(1,psp_a1,4);
				emit_sh_svs(0,op->rd);
				emit_sh_svs(1,op->rd._reg+1);*/

				emit_sh_load_hu(psp_a1,op->rs1);
				emit_jal(r_fsca);
				emit_sh_addr(psp_a0,op->rd);

				/*emit_lw(psp_a2,psp_a1,0);
				emit_lw(psp_a3,psp_a1,4);
				emit_sh_store(psp_a2,op->rd);
				emit_sh_store(psp_a3,op->rd._reg+1);*/
#endif
			}
			break;

		case shop_fipr:
			{
				//die("shop_fipr");
				verify(op->rs1._reg+3==op->rd._reg);
#if FULL_VFPU
				//emit_vdotq(vs, vt, vd);
#elif HALF_VFPU
				/*
				"lv.q			c110,  %1\n"
				"lv.q			c120,  %2\n"
				"vdot.q			s000, c110, c120\n"
				"sv.s			s000, %0\n"
				*/
				emit_sh_lvq(0, op->rs1);

				if (op->rs1._reg != op->rs2._reg) {
					emit_sh_lvq(1, op->rs2);
					emit_vdotq(0, 1, 0);
				}else{
					emit_vdotq(0, 0, 0);
				}
				
				emit_sh_svs(0, op->rd);
#else
				emit_sh_addr(psp_a0,op->rs1);
				emit_jal(r_fipr);
				emit_sh_addr(psp_a1,op->rs2);
#endif
			}
			break;

		case shop_fsqrt:
			{
				//die("shop_fsqrt");
				verify(op->rs1._reg==op->rd._reg);
#if FULL_VFPU
				u32 vs = op->rs1; vs = (vs&3)*32|(vs&12)>>2;
				u32 vd = op->rd;  vd = (vd&3)*32|(vd&12)>>2;
				emit_vsqrts(vs, vd);
#elif HALF_VFPU 
//D00D0000
//D0070000 
				u8 dest_reg = (op->SwapSaveReg ? 2 : 1);

				if (op->loadReg) emit_sh_lwc1(1,op->rs1);
				emit_sqrts(dest_reg,1);
				if (op->SaveReg) emit_sh_swc1(dest_reg, op->rd);
#else
				emit_jal(r_fsqrt);
				emit_sh_addr(psp_a0,op->rd);
#endif
			}
			break;

		case shop_ftrv:
			{
				//die("shop_ftrv");
				verify(op->rs1._reg==op->rd._reg);
#if FULL_VFPU
#elif HALF_VFPU
				/*
				"lv.q			c100,  %1\n"
				"lv.q			c110,  %2\n"
				"lv.q			c120,  %3\n"
				"lv.q			c130,  %4\n"
				"lv.q			c200,  %5\n"
				"vtfm4.q		c000, e100, c200\n"
				"sv.q			c000, %0\n"
				*/

				emit_sh_lvq(4, op->rs2._reg+0);		//4=c100
				emit_sh_lvq(5, op->rs2._reg+4);		//5=c110
				emit_sh_lvq(6, op->rs2._reg+8);		//6=c120
				emit_sh_lvq(7, op->rs2._reg+12);		//7=c130
				emit_sh_lvq(8, op->rs1);		//8=c200
				emit_vtfm4q(36, 8, 0);			//36 is e100, 8 is c200, 0 is c000
				emit_sh_svq(0, op->rd);			//0 is c000

				//printf("%08X %08X %08X %08X %08X %08X %08X",p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7]);
#else
				emit_sh_addr(psp_a0,op->rd);
				emit_jal(r_ftrv);
				emit_sh_addr(psp_a1,op->rs2);
#endif
			}
			break;

		case shop_frswap:

			for (int i=0;i<4;i++)
				{
					emit_sh_lvq(0, op->rs1._reg + (i*4));			
					emit_sh_lvq(5, op->rs2._reg + (i*4));	

					emit_sh_svq(0, op->rd._reg + (i*4));
					emit_sh_svq(5, op->rd2._reg + (i*4));
				}

		break;

		case shop_fmac:
			{
				//die("shop_fmac");
				verify(op->rs1._reg==op->rd._reg);
#if FULL_VFPU
#elif HALF_VFPU
				/*emit_sh_lvs(0, op->rs2); // f0
				emit_sh_lvs(1, op->rs3); // fm
				emit_sh_lvs(2, op->rs1); // fn
				emit_vmuls(0, 1, 0);      // fm * f0 
				emit_vadds(0, 2, 0);      // fm * f0 + fn 
				emit_sh_svs(0, op->rd);*/

				if (op->loadReg) emit_sh_lwc1(1,op->rs1);
				emit_sh_lwc1(2,op->rs2);
				emit_sh_lwc1(3,op->rs3);

				u8 dest_reg = (op->SwapSaveReg ? 2 : 1);

				emit_muls(3,3,2);
				emit_adds(dest_reg,3,1);

				if (op->SwapWFloatR) emit_mfc1(psp_a1,dest_reg);
				if (op->SaveReg) emit_sh_swc1(dest_reg, op->rd);

#else
				emit_sh_addr(psp_a0,op->rd);
				emit_sh_addr(psp_a1,op->rs2);
				emit_jal(r_fmac);
				emit_sh_addr(psp_a2,op->rs3);
#endif
			}
			break;

		case shop_fsrra:
			{
				//die("shop_fsrra");
				verify(op->rs1._reg==op->rd._reg);
#if FULL_VFPU
#elif HALF_VFPU && 0
				emit_sh_lvs(0, op->rd);
				emit_vrsqs(0, 0);
				emit_sh_svs(0, op->rd);
#else
				if (op->loadReg) emit_sh_lwc1(1,op->rs1);

				u8 dest_reg = (op->SwapSaveReg ? 2 : 1);

				emit_lui(psp_a0,0x3F80);
				emit_mtc1(2,psp_a0);
				emit_sqrts(1,1);
				emit_divs(dest_reg,2,1);
				if (op->SaveReg) emit_sh_swc1(dest_reg, op->rd);
#endif
			}
			break;

		case shop_fseteq:
		case shop_fsetgt:
			{
				if (op->loadReg) emit_sh_lwc1(psp_fr1,op->rs1);
				emit_sh_lwc1(psp_fr2,op->rs2);
				if (op->op==shop_fsetgt) emit_clts(psp_fr2,psp_fr1);
				else 					 emit_cseqs(psp_fr2,psp_fr1);
				emit_cfc1(psp_c1cr31,psp_a1);
				emit_ext(psp_a0,psp_a1,23,23);
				if (op->SaveReg) emit_sh_store(psp_a0,op->rd);	
			}
			break;

		case shop_ext_s8:
		case shop_ext_s16:
			{
				//verify(op->rd.is_r32i());
				//verify(op->rs1.is_r32i());

				if (op->loadReg) emit_sh_load(psp_a0,op->rs1);
				
				if (op->op==shop_ext_s8)
					emit_seb(psp_a0,psp_a0);
				else
					emit_seh(psp_a0,psp_a0);
				
				if (op->SaveReg)
					emit_sh_store(psp_a0,op->rd);
			}
			break;

		case shop_cvt_f2i_t:
			{
				verify(op->rd.is_r32i());
				verify(op->rs1.is_r32f());

				u8 dest_reg = (op->SwapSaveReg ? 2 : 1);

				if (op->loadReg) emit_sh_lwc1(1,op->rs1);
				emit_truncws(dest_reg,1);
				if (op->SaveReg) emit_sh_swc1(dest_reg, op->rd); 

				/*emit_jal(r_f2i_t);
				emit_sh_addr(psp_a0,op->rs1);
				emit_sh_store(psp_v0,op->rd);*/
			}
			break;

			//i hope that the round mode bit is set properly here :p
		case shop_cvt_i2f_n:
		case shop_cvt_i2f_z:
			{
				verify(op->rd.is_r32f());
				verify(op->rs1.is_r32i());

				u8 dest_reg = (op->SwapSaveReg ? 2 : 1);

				if (op->loadReg) emit_sh_lwc1(1,op->rs1);
				emit_cvtsw(dest_reg,1);
				if (op->SaveReg) emit_sh_swc1(dest_reg, op->rd);

			}
			break;

		case shop_pref:
			{
				verify(op->rs1.is_r32i());
				//x86e->Emit(op_mov32 ,ECX,GetRegPtr(op->rs1));
				if (op->flags != 0x1337)
				{
					emit_sh_load(psp_a0,op->rs1);
					emit_srl(psp_a1,psp_a0,26);
					emit_movi(psp_a2,0x38);

					u32* target=(u32*)emit_GetCCPtr();
					target+=4;

					emit_bne(psp_a1,psp_a2,target);
					emit_nop();
				}

				if (CCN_MMUCR.AT)
					emit_jal(do_sqw_mmu);
				else{
					emit_jal(*do_sqw_nommu);
				}

				if (op->flags == 0x1337) emit_sh_load(psp_a0,op->rs1);
				else   					 emit_nop();

			}
			break;
defaulty:
		default:
			printf("OH CRAP %d\n",op->op);
			die("Recompiled doesn't know about that opcode");
			//shil_chf[op->op](op);
		}
	}

	//if (block->oplist.size() > 60 && block->contains_fpu_op) CodeDump("code.bin");
	//if (save_block) CodeDump("code.bin");

    //if (save_block) SaveAllocatedReg();

	//SaveAllocatedReg();

	ngen_End(block);

	make_address_range_executable((u32)rv, (u32)emit_GetCCPtr());
	return rv;
}



void ngen_ResetBlocks()
{
}

u16 CUSTOM_SH4_TIMESLICE = 448;

void ngen_mainloop()
{
	if (unlikely(loop_code==0))
	{

		//Sh4cntx.is_runnning = &sh4_int_bCpuRun;

		loop_code=(void(*)())emit_GetCCPtr();
		{
			emit_mpush(12,
				reg_gpr+psp_gp,
				reg_gpr+psp_k0,
				reg_gpr+psp_ra,
				reg_gpr+psp_s0,
				reg_gpr+psp_s1,
				reg_gpr+psp_s2,
				reg_gpr+psp_s3,
				reg_gpr+psp_s4,
				reg_gpr+psp_s5,
				reg_gpr+psp_s6,
				reg_gpr+psp_s7,
				reg_gpr+psp_s8);

	//cntx base
	emit_la(psp_ctx_reg,&Sh4cntx);

	emit_li(psp_mem_lut,(u32)_vmem_MemInfo_ptr,2);

	//load pc
	emit_sh_load(psp_next_pc_reg,reg_nextpc);
	
	//and cycles
	emit_li(psp_cycle_reg, CUSTOM_SH4_TIMESLICE);
			
	//next_pc _MUST_ be on ecx
	//no_update
	loop_no_update=emit_GetCCPtr();

	emit_andi(psp_a1,psp_a0,BM_BLOCKLIST_MASK<<(BM_BLOCKLIST_SHIFT));
	//a1: idx offset
	u32 offs=emit_luab(psp_a2,cache);	//load base top 16 to a2
	emit_addu(psp_a1,psp_a1,psp_a2);	//add em (a1+a2)
	emit_lw(psp_a1,psp_a1,offs);		//add lower 16 and load pointer to a1
	
	//a1: block ptr
	emit_lw(psp_a3,psp_a1,4);	//pc addr
	u32* dst=(u32*)emit_GetCCPtr()+2;
	dst+=4;
	emit_bne(psp_a3,psp_a0,dst);
	emit_lw(psp_a2,psp_a1,0);	//code ptr

	emit_lw(psp_a3,psp_a1,8);		//load count
	emit_addiu(psp_a3,psp_a3,1);	//inc

	emit_sw(psp_a3,psp_a1,8);		//store count
	emit_jr(psp_a2);
	emit_nop();

	emit_jal(&bm_GetCode);
	emit_nop();		//delayslot

	emit_jr(psp_v0);
	emit_nop();

	//do_update_write
	loop_do_update_write=emit_GetCCPtr();

	emit_jal(UpdateSystem);
	emit_sh_store(psp_next_pc_reg,reg_nextpc);

	emit_blez(psp_v0,((u32)emit_GetCCPtr() + 12));
	emit_addiu(psp_cycle_reg,psp_cycle_reg,CUSTOM_SH4_TIMESLICE);	//delayslot

	emit_jal(UpdateINTC);
	
	emit_lba(psp_s7,(void*)&sh4_int_bCpuRun);
	emit_bgtz(psp_s7,loop_no_update);
	emit_sh_load(psp_next_pc_reg,reg_nextpc);

	emit_mpop(12,
			reg_gpr+psp_gp,
			reg_gpr+psp_k0,
			reg_gpr+psp_ra,
			reg_gpr+psp_s0,
			reg_gpr+psp_s1,
			reg_gpr+psp_s2,
			reg_gpr+psp_s3,
			reg_gpr+psp_s4,
			reg_gpr+psp_s5,
			reg_gpr+psp_s6,
			reg_gpr+psp_s7,
			reg_gpr+psp_s8);

	//cleanup
	emit_jra();
	emit_nop();
		
	} //that was mainloop


		//ngen_FailedToFindBlock
		ngen_FailedToFindBlock=(void(*)())emit_GetCCPtr();
		{
			emit_jal(&rdv_FailedToFindBlock);	
			emit_nop();		//delay slot

			emit_jr(psp_v0);
			emit_nop();		//delay slot
		}

		ngen_LinkBlock_Static_stub=emit_GetCCPtr();
		{
			emit_addiu(psp_a1,psp_ra,(u16)-12);
			emit_jal(&ngen_LinkBlock_Static);	
			emit_nop();		//delay slot

			emit_jr(psp_v0);
			emit_nop();		//delay slot
		}

		ngen_LinkBlock_Dynamic_1st_stub=emit_GetCCPtr();
		{
			emit_addiu(psp_a1,psp_ra,(u16)-8);
			emit_jal(&ngen_LinkBlock_Dynamic_1st);	
			emit_nop();		//delay slot

			emit_jr(psp_v0);
			emit_nop();		//delay slot
		}

		ngen_LinkBlock_Dynamic_2nd_stub=emit_GetCCPtr();
		{
			emit_addiu(psp_a1,psp_ra,(u16)-8);
			emit_jal(&ngen_LinkBlock_Dynamic_2nd);	
			emit_move(psp_a0,djump_temp);		//delay slot

			emit_jr(psp_v0);
			emit_nop();		//delay slot
		}

		ngen_BlockCheckFail_stub=emit_GetCCPtr();
		{
			emit_jal(&rdv_BlockCheckFail);	
			emit_nop();		//delay slot

			emit_jr(psp_v0);
			emit_nop();		//delay slot
		}

		//Make _SURE_ this code is not overwriten !
		emit_SetBaseAddr();

		/*char file[512];
		sprintf(file,"dynarec_%08X.bin",loop_code);
		FILE* f=fopen(file,"wb");
		if (!f) { sprintf(file,"dynarec_%08X.bin",loop_code); f=fopen(file,"wb"); }
		fwrite((void*)loop_code,1,CODE_SIZE-emit_FreeSpace(),f);
		fclose(f);*/

		make_address_range_executable((u32)loop_code, (u32)emit_GetCCPtr());
	}

	loop_code();
}

#if 1
void regcache_reset()
{

	for (int i = 0; i < EREGS_NUM; ++i)
	{
		eregs[i].nreg = 255;
	}

	for (int i = 0; i < NGPR_NUM; ++i)
	{
		ngpr[i].locked = 1;
		ngpr[i].mapped = 0;
		ngpr[i].dirty  = 0;
		ngpr[i].ereg   = 255;
	}

	for (int i = 0; i < 2; ++i)
	{
		nhilo[i].mapped = 0;
		nhilo[i].dirty  = 0;
	}

	for (int i = 0; i < OREGS_NUM; ++i)
	{
		u32 nreg = oregs[i];

		ngpr[nreg].locked = 0;
	}
}

void spill_ngpr(u32 n)
{
	u32 ereg, nreg;

	for (u32 i = 0; i < OREGS_NUM; ++i)
	{
		nreg = oregs[i];
		ereg = ngpr[nreg].ereg;

		if (!ngpr[nreg].locked && ngpr[nreg].mapped && ereg != 255)
		{
			if (ngpr[nreg].dirty)
				emit_sh_store(nreg,ereg);

			eregs[ereg].nreg   = 255;
			ngpr[nreg].ereg   = 255;
			ngpr[nreg].dirty  = 0;
			ngpr[nreg].mapped = 0;
			n--;
			if (!n) break;
		}
	}
}

static u32 __ngpr = psp_a0;

u32 map_ngpr(u32 ereg, u32 action)
{
	u32 nreg, old_nreg = 255, i;

	nreg = eregs[ereg].nreg;

	// already mapped ?
	if (nreg != 255)
	{
		if (ngpr[nreg].mapped && (ngpr[nreg].ereg != 255))
		{
			// if no remap, just return the already associated native register
			if (!(action & NREG_REMAP))
				return nreg;

			ngpr[nreg].locked = 1;

			old_nreg = nreg;
		}
	}

redo:
	nreg = 255;

	for (i = 0; i < OREGS_NUM; ++i)
	{
		nreg = oregs[i];

		// new native register should be not locked or mapped to be a candidate
		if (!ngpr[nreg].locked && !ngpr[nreg].mapped && (ngpr[nreg].ereg == 255))
			break;
	}

	// no candidate ? so we need to spill a native register
	if (i == OREGS_NUM)
	{
		spill_ngpr(1);
		goto redo;
	}

	ngpr[nreg].ereg   = ereg;
	ngpr[nreg].mapped = 1;
	ngpr[nreg].locked = 0;
	ngpr[nreg].dirty  = 0;
	eregs[ereg].nreg   = nreg;

	if (action & NREG_PRELOAD)
		emit_sh_load(nreg,ereg);

	// unlock and free the old native register
	if (old_nreg != 255)
	{
		ngpr[old_nreg].locked = 0;
		ngpr[old_nreg].mapped = 0;
		ngpr[old_nreg].dirty  = 0;
		ngpr[old_nreg].ereg   = 255;
	}

	return nreg;
}

u32 unmap_ngpr(u32 ereg, bool update)
{
	u32 nreg = eregs[ereg].nreg;

	if (nreg < NGPR_NUM)
	{
		if (ngpr[nreg].mapped)
		{
			if (update && ngpr[nreg].dirty)
				emit_sh_store(nreg,ereg);

			ngpr[nreg].ereg   = 255;
			ngpr[nreg].dirty  = 0;
			ngpr[nreg].mapped = 0;
			if (eregs[ereg].nreg == nreg)
				eregs[ereg].nreg = 255;
		}
	}

	return nreg;
}

void lock_ngpr(u32 ereg)
{
	u32 nreg = eregs[ereg].nreg;

	if (nreg < NGPR_NUM)
		ngpr[nreg].locked = 1;
}

void unlock_ngpr(u32 ereg)
{
	u32 nreg = eregs[ereg].nreg;

	if (nreg < NGPR_NUM)
		ngpr[nreg].locked = 0;
}

void spill_hilo()
{
	return;
	for (u32 i = 0; i < 2; ++i)
	{
		if (nhilo[i].mapped)
		{
			if (nhilo[i].dirty)
			{
				nhilo[i].dirty  = 0;
				if (i)
					emit_mfhi(psp_at);
				else
					emit_mflo(psp_at);
				emit_sh_store(psp_at,reg_macl-i);
			}

			nhilo[i].mapped = 0;
		}
	}
}

void map_hilo(u32 hilo)
{
	return;
	nhilo[hilo].mapped = 1;
}

bool is_hilo_mapped(u32 hilo)
{
	return false;
	return nhilo[hilo].mapped;
}
#endif

void ngen_GetFeatures(ngen_features* dst)
{
	dst->InterpreterFallback=false;
	dst->OnlyDynamicEnds=false;
}