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



psp_gpr_t alloc_regs[]={psp_s0,psp_s2,psp_s3,psp_s4,psp_s5,psp_s6, psp_s7, psp_s8,(psp_gpr_t)-1};

psp_fpr_t alloc_fpu[]={psp_fr20,psp_fr21,psp_fr22,psp_fr23,psp_fr24,psp_fr25,psp_fr26,psp_fr27,
					psp_fr28,psp_fr29,psp_fr30, (psp_fpr_t)-1};

struct mips_reg_alloc: RegAlloc<psp_gpr_t,psp_fpr_t, false>
{

	virtual psp_fpr_t FpuMap(u32 reg)
	{
		if (reg>=reg_fr_0 && reg<=reg_fr_15)
		{
			return alloc_fpu[reg-reg_fr_0];
		}
		else
			return (psp_fpr_t)-1;
	}

	virtual void Preload(u32 reg,psp_gpr_t nreg)
	{
		verify(reg!=reg_pc_dyn);
		emit_sh_load(nreg,reg);
	}
	virtual void Writeback(u32 reg,psp_gpr_t nreg)
	{
		if (reg==reg_pc_dyn)
			;//MOV(r4,nreg);
		else
			emit_sh_store(nreg,reg);
	}

	virtual void Preload_FPU(u32 reg,psp_fpr_t nreg)
	{
		emit_sh_lwc1(nreg, reg);
	}
	virtual void Writeback_FPU(u32 reg,psp_fpr_t nreg)
	{
		emit_sh_swc1(nreg, reg);
	}
	/*
	psp_fpr_t fd0_to_fs(eFDReg fd0)
	{
		psp_fpr_t rv=(psp_fpr_t)(fd0*2);
		verify(rv<32);
		return rv;
	}
	*/
	psp_fpr_t mapfs(const shil_param& prm)
	{
		return mapf(prm);
	}

};


mips_reg_alloc reg;

void SaveAllocatedReg(){
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


u32 last_block;

void ngen_Begin(DecodedBlock* block,bool force_checks)
{
	block->has_jcond = false;

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
			if (!block->has_jcond)
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

psp_gpr_t GenMemAddr(shil_opcode* op, psp_gpr_t raddr=psp_a0)
{
	if (op->rs3.is_imm())
	{
		if(is_s16(op->rs3._imm))
		{
			emit_addiu(raddr,reg.mapg(op->rs1),op->rs3._imm);
		}
		else 
		{
			emit_li(psp_at,op->rs3._imm);
			emit_addiu(raddr,reg.mapg(op->rs1),psp_at);
		}
	}
	else if (op->rs3.is_r32i())
	{
		emit_addu(raddr,reg.mapg(op->rs1),reg.mapg(op->rs3));
	}
	else if (op->rs1.is_imm())
	{
		emit_li(raddr, op->rs1._imm);
	}
	else
	{
		emit_move(raddr, reg.mapg(op->rs1));
	}

	return raddr;
}

bool ngen_writemem_immediate_static(shil_opcode* op)
{
	if (!op->rs1.is_imm())
		return false;

	mem_op_type optp = memop_type(op);
	bool isram = false;
	void* ptr = _vmem_write_const(op->rs1._imm, isram, op->flags);

	psp_gpr_t rs2 = psp_a1;
	psp_fpr_t rs2f = psp_fr1;
	if (op->rs2.is_imm())
		emit_li(rs2, op->rs2._imm);
	else if (optp == SZ_32F)
		rs2f = reg.mapf(op->rs2);
	else if (optp != SZ_64F)
		rs2 = reg.mapg(op->rs2);

	if (isram){
		u32 offs=emit_luab(psp_a0,ptr);
		switch(optp)
		{
		case SZ_8:
			emit_sb(rs2,psp_a0,offs);
			break;

		case SZ_16:
			emit_sh(rs2,psp_a0,offs);
			break;

		case SZ_32I:
			emit_sw(rs2,psp_a0,offs);
			break;

		case SZ_32F:
			emit_swc1(rs2f,psp_a0,offs);
			break;

		case SZ_64F:
			emit_sh_load(psp_v0, op->rs2._reg+0);
			emit_sh_load(psp_v1, op->rs2._reg+1);
			emit_sw(psp_v0,psp_a0,offs);
			emit_sw(psp_v1,psp_a0,offs+4);
			break;

		default:
			die("Invalid size");
			break;
		}
	}

	return true;
}

u32 emit_SlideDelay() { emit_Skip(-4); u32 rv=*(u32*)emit_GetCCPtr();  return rv;}
u32 emit_SlideDelay(u8 sz) { emit_Skip(-sz); u32 rv=*(u32*)emit_GetCCPtr();  return rv;}


extern "C" void asm_read08();
extern "C" void asm_read16();
extern "C" void asm_read32();

extern "C" void asm_write32();
extern "C" void asm_write16();
extern "C" void asm_write08();

void FASTCALL do_sqw_mmu(u32 dst);

void ngen_CC_Start(shil_opcode* op) { die("Unsuported for psp.."); }
void ngen_CC_Param(shil_opcode* op,shil_param* par,CanonicalParamType tp) { die("Unsuported for psp.."); }
void ngen_CC_Call(shil_opcode*op,void* function) { die("Unsuported for psp.."); }
void ngen_CC_Finish(shil_opcode* op) { die("Unsuported for psp.."); }

void shil_param_to_host_reg(const shil_param& param, const u8 _reg)
	{
		if (param.is_imm())
		{
			emit_li(_reg, param._imm);
		}
		else if (param.is_reg())
		{
			if (param.is_r64f()){
				emit_sh_lwc1(_reg, param._reg);
				emit_sh_lwc1(_reg + 1, param._reg+1);
			}
			else if (param.is_r32f())
			{
				if (reg.IsAllocf(param))
					emit_movs(_reg, reg.mapf(param));
				else
					emit_sh_lwc1(_reg, param._reg);
			}
			else
			{
				if (reg.IsAllocg(param))
					emit_move(_reg, reg.mapg(param));
				else
					emit_sh_load(_reg, param._reg);
			}
		}
		else
		{
			verify(param.is_null());
		}
	}

#define emit_Fop(_op) 																		\
{ 																							\
	psp_fpr_t reg1 = psp_fr1;																\
	psp_fpr_t reg2 = psp_fr2;																\
																							\
	if (op->rs1.is_imm()){    																\
			emit_li(psp_at, op->rs1._imm);													\
			emit_mtc1(psp_at, reg1);														\
	}else                                                                                   \
		reg1 = reg.mapf(op->rs1);													        \
																							\
	if (op->rs2.is_imm()){    																\
			emit_li(psp_at, op->rs2._imm);													\
			emit_mtc1(psp_at, reg2);														\
	}else 																					\
		reg2 = reg.mapf(op->rs2);															\
																							\
	emit_##_op##s(reg.mapf(op->rd),reg1, reg2);												\
}	

#define emit_op(_op, _op_imm, sign) 														\
{ 																							\
	if (op->rs2.is_imm())    																\
	{																						\
		if (op->rs2.is_imm_##sign##16())													\
		{																					\
			emit_##_op_imm(reg.mapg(op->rd), reg.mapg(op->rs1), op->rs2._imm&0xFFFF);		\
		}																					\
		else																				\
		{																					\
			emit_li(psp_at, op->rs2._imm);													\
			emit_##_op(reg.mapg(op->rd),reg.mapg(op->rs1), psp_at);							\
		}																					\
	}																						\
	else if (op->rs2.is_r32i())																\
	{																						\
		emit_##_op(reg.mapg(op->rd),reg.mapg(op->rs1), reg.mapg(op->rs2));					\
	}																						\
}	

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

DynarecCodeEntry* ngen_Compile(DecodedBlock* block,bool force_checks, bool opt)
{
	if (unlikely(emit_FreeSpace()<16*1024)){
		printf("OUT OF MEM	\n");
		return 0;
	}

	DynarecCodeEntry* rv=(DynarecCodeEntry*)emit_GetCCPtr();

	reg.DoAlloc(block, alloc_regs, alloc_fpu); 

	//pre-load the first reg alloc operations, for better efficiency ..
	reg.OpBegin(&block->oplist[0],0);

	ngen_Begin(block,force_checks);

	//StartCodeDump();

	bool _save = false;

	for (size_t i=0;i<block->oplist.size();i++)
	{
		shil_opcode* op=&block->oplist[i];

		if (i!=0)
			reg.OpBegin(op,i);

		//printf("OP: %d\n", op->op);
 
		switch(op->op)
		{
		
		case shop_nop: break;
		case shop_ifb:
			{
				if (op->rs1._imm)
				{
					emit_li(psp_a0,op->rs2._imm);
					emit_sh_store(psp_a0,reg_nextpc);
				}
				emit_jal(OpPtr[op->rs3._imm]);
				emit_movi(psp_a0,op->rs3._imm&0xFFFF);
			}
			break;

		case shop_jcond:
			block->has_jcond=true;
			emit_move(djump_temp, reg.mapg(op->rs1));
		break;

		case shop_jdyn:
			{
				if (op->rs2.is_imm())
				{
					if (is_s16(op->rs2._imm))
					{
						emit_addiu(djump_temp,reg.mapg(op->rs1),op->rs2._imm);
					}
					else
					{
						emit_li(psp_a1,op->rs2._imm);
						emit_addu(djump_temp,reg.mapg(op->rs1),psp_a1);
					}
				}else
					emit_move(djump_temp, reg.mapg(op->rs1));
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

		case shop_mov32:
			{
				int _reg = reg.ManualWriteBack(op->rd._reg, i);

				if (op->rs1.is_imm())
				{
					if (op->rd.is_r32i())
					{
						if (op->rs1._imm==0)
						{
							if (_reg != -1) emit_sh_store(psp_zero, op->rd);
							else 			emit_move(reg.mapg(op->rd), psp_zero);
						}
						else
						{
							emit_li(reg.mapg(op->rd),op->rs1._imm);
							if (_reg != -1) emit_sh_store(reg.mapg(op->rd),op->rd);
						}	
					}
					else
					{
					
						emit_li(psp_at, op->rs1._imm);
						if (_reg != -1) emit_sh_store(psp_at, op->rd);
						else 			emit_mtc1(psp_at, reg.mapfs(op->rd));						
					}
				}
				else if (op->rs1.is_r32())
				{
					u32 type=0;

					if (reg.IsAllocf(op->rd))
						type|=1;
					
					if (reg.IsAllocf(op->rs1))
						type|=2;

					switch(type)
					{
					case 0: //reg=reg
					{	
						/*if (reg.IsAllocg(op->rs1) && reg.IsAllocg(op->rd) && reg.IsEnding(op->rs1._reg, i)){
								reg.ReplaceSpan(reg.mapg(op->rd), reg.mapg(op->rs1));
						}else*/
						if (reg.mapg(op->rd)!=reg.mapg(op->rs1)){
							if (_reg != -1) emit_sh_store(reg.mapg(op->rs1), op->rd);
							else 			emit_move(reg.mapg(op->rd),reg.mapg(op->rs1));
						}
						break;
					}
					case 1: //vfp=reg
						if (_reg != -1) emit_sh_store(reg.mapg(op->rs1), op->rd);
						else 			emit_mtc1(reg.mapg(op->rs1), reg.mapfs(op->rd));
						break;

					case 2: //reg=vfp
						if (_reg != -1) emit_sh_swc1(reg.mapfs(op->rs1), op->rd);
						else 			emit_mfc1(reg.mapfs(op->rs1), reg.mapg(op->rd));
						break;

					case 3: //vfp=vfp
						if (_reg != -1) emit_sh_swc1(reg.mapfs(op->rs1), op->rd);
						else 			emit_movs(reg.mapfs(op->rd),reg.mapfs(op->rs1));
						break;
					}
				}
				else
				{
					goto defaulty;
				}

			}
			break;

		case shop_readm:
			{
				//die("shop_readm");

				mem_op_type optp=memop_type(op);

				if (op->rs1.is_imm())
				{
					bool wasram=false;
					void* ptr=_vmem_read_const(op->rs1._imm,wasram,op->flags);	

					if (likely(wasram))
					{
						u32 offs=emit_luab(psp_a0,ptr);

						switch(optp)
						{
						case SZ_8:
							{
								emit_lb(reg.mapg(op->rd),psp_a0,offs);
							} 
							break;

						case SZ_16:
							{
								emit_lh(reg.mapg(op->rd),psp_a0,offs);
							} 
							break;

						case SZ_32I:
							{
								if (op->flags & 0x40000000)	{
									emit_li(reg.mapg(op->rd),*(u32*)ptr);
								}else
									emit_lw(reg.mapg(op->rd),psp_a0,offs);
							}
							break;

						case SZ_32F:
							{
								emit_lwc1(reg.mapfs(op->rd), psp_a0, offs);
							}
							break;
						}

					}
					else
					{
						emit_li(psp_a0,op->rs1._imm);

						u32 _opcode = emit_SlideDelay();

						switch(optp)
						{
							case SZ_8: 
								emit_jal(ptr);
								emit_Write32(_opcode);
								emit_seb(reg.mapg(op->rd),psp_v0); 
							break;

							case SZ_16: 
								emit_jal(ptr);
								emit_Write32(_opcode);
								emit_seh(reg.mapg(op->rd),psp_v0);
							break;

							case SZ_32I:
							case SZ_32F: 
								emit_jal(ptr);
								emit_Write32(_opcode);
							
							if (reg.IsAllocg(op->rd))
								emit_move(reg.mapg(op->rd),psp_v0);
							else
								emit_mtc1(psp_v0, reg.mapfs(op->rd));
							
							break;
						}
					}
				}else
				{

					psp_gpr_t raddr=GenMemAddr(op);

					u32 delay=emit_SlideDelay();
					switch(optp)
					{
						case SZ_8:
							emit_jal(asm_read08);	
							emit_Write32(delay);	//dslot
							emit_seb(reg.mapg(op->rd),psp_v0);
							break;
						case SZ_16:
							emit_jal(asm_read16);	
							emit_Write32(delay);	//dslot
							emit_seh(reg.mapg(op->rd),psp_v0);
							break;
						case SZ_32I:
						    emit_jal(asm_read32);	
							emit_Write32(delay);	//dslot
							emit_move(reg.mapg(op->rd), psp_v0);
							break;
						case SZ_32F:
							emit_jal(asm_read32);	
							emit_Write32(delay);	//dslot
							emit_mtc1(psp_v0, reg.mapf(op->rd));
							break;
						case SZ_64F:
							emit_jal(ReadMem64);
							emit_Write32(delay);	//dslot
							emit_sh_store(psp_v0,op->rd._reg+0);
							emit_sh_store(psp_v1,op->rd._reg+1);
							break;
					}
				}
			}
			break;

		case shop_writem:
			{
				if (!ngen_writemem_immediate_static(op))  {

					mem_op_type optp=memop_type(op);

					psp_gpr_t raddr=GenMemAddr(op);
					
					if (optp == SZ_64F){
						if (op->flags2==0x1337) {
							emit_sh_load(psp_a2,op->rs2._reg);
							emit_andi(psp_at, psp_a0, 0x3f);
							emit_sh_load(psp_a1,op->rs2._reg+1);
							emit_addu(psp_at, psp_at, psp_ctx_reg);
							emit_sw(psp_a2, psp_at, GET_VAR_OFF(Sh4cntx.sq_buffer));
							emit_sw(psp_a1, psp_at, GET_VAR_OFF(Sh4cntx.sq_buffer) + 4);
						}else{
							emit_sh_load(psp_a3,op->rs2._reg + 1);
							emit_jal(&WriteMem64);
							emit_sh_load(psp_a2,op->rs2);
						}
					}else{

						if (op->rs2.is_imm())
						{
							emit_li(psp_a1, op->rs2._imm);
						}
						else
						{
							if (optp == SZ_32F)
								emit_mfc1(reg.mapf(op->rs2), psp_a1);
							else
								emit_move(psp_a1, reg.mapg(op->rs2));
						}

						u32 delay=emit_SlideDelay();

						switch(optp)
						{
							case SZ_8:
								emit_jal(asm_write08);
								emit_Write32(delay);	
							break;
							case SZ_16:
								emit_jal(asm_write16);	
								emit_Write32(delay);
							break;
							case SZ_32I:
							case SZ_32F:
							if (op->flags2==0x1337) {
								emit_andi(psp_at, psp_a0, 0x3f);
								emit_addu(psp_at, psp_at, psp_ctx_reg);
								if (optp == SZ_32F)
									emit_swc1((op->rs2.is_imm() ? psp_a1 : reg.mapf(op->rs2)), psp_at, GET_VAR_OFF(Sh4cntx.sq_buffer));
								else
									emit_sw((op->rs2.is_imm() ? psp_a1 : reg.mapg(op->rs2)), psp_at, GET_VAR_OFF(Sh4cntx.sq_buffer));
							}else{
								emit_jal(asm_write32);
								emit_Write32(delay);
							}
							break;

							default:
								verify(false);
						}		
					}
				}		
			}
			break;

		case shop_rocr:
			{
				psp_gpr_t reg1;
				psp_gpr_t reg2;

				if (op->rs1.is_imm())
				{
					emit_li(psp_a0, op->rs1._imm);
					reg1 = psp_a0;
				}
				else
				{
					reg1 = reg.mapg(op->rs1);
				}
				if (op->rs2.is_imm())
				{
					emit_li(psp_a1, op->rs2._imm);
					reg2 = psp_a1;
				}
				else
				{
					reg2 = reg.mapg(op->rs2);
				}

				emit_andi(reg2,reg1,1);		
				emit_rotr(reg.mapg(op->rd),reg1,1);		
			}
			break;
			
		case shop_rocl:
			{
				psp_gpr_t reg1;
				psp_gpr_t reg2;

				if (op->rs1.is_imm())
				{
					emit_li(psp_a0, op->rs1._imm);
					reg1 = psp_a0;
				}
				else
				{
					reg1 = reg.mapg(op->rs1);
				}
				if (op->rs2.is_imm())
				{
					emit_li(psp_a1, op->rs2._imm);
					reg2 = psp_a1;
				}
				else
				{
					reg2 = reg.mapg(op->rs2);
				}

				emit_sll(psp_a2,reg.mapg(op->rs1),1);
				emit_or(reg.mapg(op->rd),psp_a2,reg.mapg(op->rs2));

				emit_srl(reg.mapg(op->rd2),reg.mapg(op->rs1),31);
			}
			break;
 
		case shop_sync_sr:
			{
				u32 delay=emit_SlideDelay();
				emit_jal(UpdateSR);
				emit_Write32(delay);
			}
			break;
		
		case shop_shl:
			emit_op(sllv, sll, u)
		break;
		case shop_shr:
			emit_op(srlv, srl, u)
		break;
		case shop_sar:
			emit_op(srav, sra, u)
		break;
		case shop_ror:
			emit_op(rotrv, rotr, u)
		break;

		case shop_shad:
		case shop_shld:
		{
			psp_gpr_t val=psp_t0,shft=reg.mapg(op->rs2),negshft=psp_a2,shft_1f=psp_a3;
			psp_gpr_t sh_l=reg.mapg(op->rd),sh_r=psp_t1,sh_z=psp_t2,isltz=psp_t3;

			if (op->rs1.is_imm()) 	emit_li(psp_t0, op->rs1._imm);
			else					emit_move(psp_t0, reg.mapg(op->rs1));

			emit_negu(negshft,shft);
			emit_andi(shft_1f,shft,0x1F);

			emit_slt(isltz,shft,psp_zero);

			emit_sllv(sh_l,val,shft);

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
			emit_movn(sh_l,sh_r,isltz);		//if (shft<0) sh_l=sh_r;//if (isltz!=0) sh_l=sh_r
		}
		break;

		case shop_and:
			emit_op(and, andi, u)
		break;
		case shop_or:
			emit_op(or, ori, u)
		break;
		case shop_xor:
			emit_op(xor, xori, u)
		break;

		case shop_add:
			emit_op(addu,addiu, s)
		break;
		case shop_sub:
			emit_op(subu,subiu, s)
		break;
		case shop_neg:
			emit_negu(reg.mapg(op->rd),reg.mapg(op->rs1));
			break;
			
		case shop_not:
			emit_not(reg.mapg(op->rd),reg.mapg(op->rs1));
		break;

		case shop_test:
			emit_op(and, andi, u)
			emit_sltiu(reg.mapg(op->rd),reg.mapg(op->rd),1);//if a0==0 -> a0=1 else a0=0
		break;

		case shop_swaplb:
			emit_andi(psp_a1, reg.mapg(op->rs1), 0xffff);
			emit_wsbh(psp_a1, psp_a1);
			emit_ins(reg.mapg(op->rd),psp_a1, 16, 0);
		break;

		case shop_setpeq:
		{
			psp_gpr_t reg1;
			psp_gpr_t reg2;

			if (op->rs1.is_imm())
			{
				emit_li(psp_a0, op->rs1._imm);
				reg1 = psp_a0;
			}
			else
			{
				reg1 = reg.mapg(op->rs1);
			}
			if (op->rs2.is_imm())
			{
				emit_li(psp_a1, op->rs2._imm);
				reg2 = psp_a1;
			}
			else
			{
				reg2 = reg.mapg(op->rs2);
			}

			emit_xor(psp_at, reg1, reg2);

			emit_andi(psp_t0, psp_at, 0x00ff);
			emit_andi(psp_t1, psp_at, 0xff00);
			
			emit_wsbw(psp_at, psp_at);

			emit_sltiu(psp_t0, psp_t0, 1);
			emit_sltiu(psp_t1, psp_t1, 1);

			emit_andi(psp_t2, psp_at, 0x00ff);
			emit_or(psp_t0, psp_t0, psp_t1);
			emit_andi(psp_t3, psp_at, 0xff00);

			emit_sltiu(psp_t2, psp_t2, 1);
			emit_sltiu(psp_t3, psp_t3, 1);

			emit_or(psp_t1, psp_t2, psp_t3);
			emit_or(reg.mapg(op->rd), psp_t1, psp_t0);
		}
		break;
			
		case shop_seteq:
			{
				bool direct = false;

				if (op->rs2.is_r32i())
				{
					emit_xor(reg.mapg(op->rd),reg.mapg(op->rs1),reg.mapg(op->rs2));
				}
				else
				{
					if (op->rs2._imm!=0)
					{
						emit_subiu(reg.mapg(op->rd),reg.mapg(op->rs1),op->rs2._imm);
					}else{
						emit_sltiu(reg.mapg(op->rd),reg.mapg(op->rs1),1);
						direct = true;
					}
				}
				
				//if rf ==0 -> at=1; else at=0
				if (!direct) emit_sltiu(reg.mapg(op->rd),reg.mapg(op->rd),1);
			}
			break;
		
		case shop_xtrct:
			if (op->rs1._reg == op->rd._reg)
			{
				emit_srl(reg.mapg(op->rd),reg.mapg(op->rs1),16);
				emit_sll(psp_at,reg.mapg(op->rs2),16);
			}else{
				emit_sll(reg.mapg(op->rd),reg.mapg(op->rs2),16);
				emit_srl(psp_at,reg.mapg(op->rs1),16);
			}
			emit_or(reg.mapg(op->rd),reg.mapg(op->rd), psp_at);
		break;

		case shop_setge:
		case shop_setgt:
		case shop_setae:
		case shop_setab:
			{
				bool flip=op->op==shop_setge || op->op==shop_setae;
				bool usgnd=op->op>=shop_setae;

				u32 rs1=reg.mapg(op->rs1);
				u32 rs2=psp_zero;

				if (!op->rs2.is_imm())
					rs2 = reg.mapg(op->rs2);
				/*else 
					emit_li(rs2, op->rs2._imm);*/

				if (flip) 
				{	//swap operands !
					u32 t=rs1;
					rs1=rs2;
					rs2=t;
				}

				if (usgnd)
				{	//unsigned
					emit_sltu(reg.mapg(op->rd),rs2,rs1);
				}
				else
				{	//signed
					emit_slt(reg.mapg(op->rd),rs2,rs1);
				}

				if (flip)
				{
					emit_xori(reg.mapg(op->rd),reg.mapg(op->rd),1);
				}
			}
			break;

		case shop_mul_u16:
		{
			if (op->rs2.is_imm()) {
				emit_li(psp_a1,op->rs2._imm&0xffff);
				emit_multu(reg.mapg(op->rs1),psp_a1);
			}else
				emit_multu(reg.mapg(op->rs1),reg.mapg(op->rs2));

			emit_mflo(reg.mapg(op->rd));
		}
		break;

		case shop_mul_s16:
		{
			if (op->rs2.is_imm()) {
				emit_li(psp_a1,op->rs2._imm&0xffff);
				emit_mult(reg.mapg(op->rs1),psp_a1);
			}else
				emit_mult(reg.mapg(op->rs1),reg.mapg(op->rs2));

			emit_mflo(reg.mapg(op->rd));
		}
		break;

		case shop_mul_i32:
		{
			if (op->rs2.is_imm()) {
				emit_li(psp_a1,op->rs2._imm);
				emit_mult(reg.mapg(op->rs1),psp_a1);
			}else
				emit_mult(reg.mapg(op->rs1),reg.mapg(op->rs2));

			emit_mflo(reg.mapg(op->rd));
		}
		break;

		case shop_mul_u64:
		{
			if (op->rs2.is_imm()) {
				emit_li(psp_a1,op->rs2._imm);
				emit_multu(reg.mapg(op->rs1),psp_a1);
			}else
				emit_multu(reg.mapg(op->rs1),reg.mapg(op->rs2));

			emit_mflo(reg.mapg(op->rd));
			emit_mfhi(reg.mapg(op->rd2));
		}

		break;
		case shop_mul_s64:
		{
			if (op->rs2.is_imm()) {
				emit_li(psp_a1,op->rs2._imm);
				emit_mult(reg.mapg(op->rs1),psp_a1);
			}else
				emit_mult(reg.mapg(op->rs1),reg.mapg(op->rs2));

			emit_mflo(reg.mapg(op->rd));
			emit_mfhi(reg.mapg(op->rd2));
		}

		break;

		case shop_div1:
		break;


		case shop_div32u:
		{
			emit_divu(reg.mapg(op->rs1),reg.mapg(op->rs2));

			emit_mflo(reg.mapg(op->rd));
			emit_mfhi(reg.mapg(op->rd2));
		}
		break;

		case shop_div32s:
		{
			emit_div(reg.mapg(op->rs1),reg.mapg(op->rs2));

			emit_mflo(reg.mapg(op->rd));
			emit_mfhi(reg.mapg(op->rd2));
		}
		break;

		case shop_div32p2:
		{
			emit_subu(psp_at,reg.mapg(op->rs1),reg.mapg(op->rs2));
			emit_movz(reg.mapg(op->rd), psp_at, reg.mapg(op->rs3));
		}
		break;


		case shop_adc:
		{
			psp_gpr_t reg1;

			if (op->rs1.is_imm())
			{
				emit_li(psp_a0, op->rs1._imm);
				reg1 = psp_a0;
			}
			else
			{
				reg1 = reg.mapg(op->rs1);
			}

			if (op->rs2.is_imm() && op->rs2.is_imm_s16()){
				emit_addiu(psp_at, reg1, op->rs2._imm);
			}else if (op->rs2.is_imm()){
				emit_li(psp_a1, op->rs2._imm);
				emit_addiu(psp_at, reg1, psp_a1);
			}
			else
			{
				emit_addu(psp_at, reg1, reg.mapg(op->rs2));
			}

			if (op->rs3.is_imm())
				emit_addiu(reg.mapg(op->rd), psp_at, op->rs3._imm);
			else
				emit_addu(reg.mapg(op->rd), psp_at, reg.mapg(op->rs3));
		}
		break;
		case shop_sbc:
		{
			psp_gpr_t reg1;

			if (op->rs1.is_imm())
			{
				emit_li(psp_a0, op->rs1._imm);
				reg1 = psp_a0;
			}
			else
			{
				reg1 = reg.mapg(op->rs1);
			}

			if (op->rs2.is_imm() && op->rs2.is_imm_s16()){
				emit_subiu(psp_at, reg1, op->rs2._imm);
			}else if (op->rs2.is_imm()){
				emit_li(psp_a1, op->rs2._imm);
				emit_subiu(psp_at, reg1, psp_a1);
			}
			else
			{
				emit_subu(psp_at, reg1, reg.mapg(op->rs2));
			}

			if (op->rs3.is_imm())
				emit_subiu(reg.mapg(op->rd), psp_at, op->rs3._imm);
			else
				emit_subu(reg.mapg(op->rd), psp_at, reg.mapg(op->rs3));
		}
		break;
				//fpu
		case shop_fadd:
			emit_Fop(add)
		break;
		case shop_fsub:
			emit_Fop(sub)
		break;
		case shop_fmul:
			emit_Fop(mul)
		break;
		case shop_fdiv:
			emit_Fop(div)
		break;
			
		case shop_fabs:
			emit_abss(reg.mapfs(op->rd),reg.mapfs(op->rs1));
		break;

		case shop_fneg:
			emit_negs(reg.mapfs(op->rd),reg.mapfs(op->rs1));
		break;

		case shop_fsca:
			emit_andi(psp_a1, reg.mapg(op->rs1), 0xffff);
			emit_jal(r_fsca);
			emit_sh_addr(psp_a0,op->rd);
		break;

		case shop_fipr:
			emit_sh_lvq(0, op->rs1);

			if (op->rs1._reg != op->rs2._reg) {
				emit_sh_lvq(1, op->rs2);
				emit_vdotq(0, 1, 0);
			}else{
				emit_vdotq(0, 0, 0);
			}

			emit_mfv(psp_at, 0);
			emit_mtc1(psp_at, reg.mapfs(op->rd));
		break;

		case shop_fsqrt:
			emit_sqrts(reg.mapfs(op->rd),reg.mapfs(op->rs1));
		break;

		case shop_ftrv:
			emit_sh_lvq(4, op->rs2._reg+0);		//4=c100
			emit_sh_lvq(5, op->rs2._reg+4);		//5=c110
			emit_sh_lvq(6, op->rs2._reg+8);		//6=c120
			emit_sh_lvq(7, op->rs2._reg+12);		//7=c130
			emit_sh_lvq(8, op->rs1);		//8=c200
			emit_vtfm4q(36, 8, 0);			//36 is e100, 8 is c200, 0 is c000
			emit_sh_svq(0, op->rd);			//0 is c000
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
				emit_muls(1,reg.mapfs(op->rs3),reg.mapfs(op->rs2));
				emit_adds(reg.mapfs(op->rd),1,reg.mapfs(op->rs1));
			}
			break;

		case shop_fsrra:
			{
				emit_lui(psp_a0,0x3F80);
				emit_mtc1(psp_a0, 2);
				emit_sqrts(1,reg.mapfs(op->rs1));
				emit_divs(reg.mapfs(op->rd),2, 1);
			}
			break;

		case shop_fseteq:
		case shop_fsetgt:
			{			
				if (op->op==shop_fsetgt) emit_clts(reg.mapfs(op->rs2),reg.mapfs(op->rs1));
				else 					 emit_cseqs(reg.mapfs(op->rs2),reg.mapfs(op->rs1));
				emit_cfc1(psp_c1cr31, psp_a1);
				emit_ext(reg.mapg(op->rd),psp_a1,23,23);
			}
			break;

		case shop_ext_s8:
		case shop_ext_s16:
			{
				if (op->op==shop_ext_s8)
					emit_seb(reg.mapg(op->rd), reg.mapg(op->rs1));
				else
					emit_seh(reg.mapg(op->rd), reg.mapg(op->rs1));
			}
			break;

		case shop_cvt_f2i_t:
			{
				emit_truncws(1,reg.mapfs(op->rs1));
				emit_mfc1(1, reg.mapg(op->rd));
			}
			break;

			//i hope that the round mode bit is set properly here :p
		case shop_cvt_i2f_n:
		case shop_cvt_i2f_z:
			{
				emit_mtc1(reg.mapg(op->rs1), 1);
				emit_cvtsw(reg.mapfs(op->rd), 1);
			}
			break;

		case shop_pref:
			{

				if (op->rs1.is_imm())
					emit_li(psp_a0, op->rs1._imm);
				else if (op->flags != 0x1337)
				{
					emit_srl(psp_a1,reg.mapg(op->rs1),26);
					emit_subiu(psp_a2,psp_a1,0x38);

					u32* target=(u32*)emit_GetCCPtr();
					target+=4;

					emit_bltz(psp_a2,target);
					emit_nop();
				}

				if (CCN_MMUCR.AT)
					emit_jal(do_sqw_mmu);
				else{
					emit_jal(*do_sqw_nommu);
				}

				if (!op->rs1.is_imm()) emit_move(psp_a0, reg.mapg(op->rs1));
				else emit_nop();
			}
			break;
	defaulty:
		default:
			printf("OH CRAP %d\n",op->op);
			die("Recompiled doesn't know about that opcode");
		}


		reg.OpEnd(op); 
	}


    //if (block->oplist.size() > 20) SaveAllocatedReg();

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
			emit_mpush(23,
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
						reg_gpr+psp_s8,
						reg_gpr+psp_fr20,
						reg_gpr+psp_fr21,
						reg_gpr+psp_fr22,
						reg_gpr+psp_fr23,
						reg_gpr+psp_fr24,
						reg_gpr+psp_fr25,
						reg_gpr+psp_fr26,
						reg_gpr+psp_fr27,
						reg_gpr+psp_fr28,
						reg_gpr+psp_fr29,
						reg_gpr+psp_fr30);

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
	emit_nop();
	
	emit_lba(psp_at,(void*)&sh4_int_bCpuRun);
	emit_bgtz(psp_at,loop_no_update);
	emit_sh_load(psp_next_pc_reg,reg_nextpc);

	emit_mpop(23,
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
			reg_gpr+psp_s8,
			reg_gpr+psp_fr20,
			reg_gpr+psp_fr21,
			reg_gpr+psp_fr22,
			reg_gpr+psp_fr23,
			reg_gpr+psp_fr24,
			reg_gpr+psp_fr25,
			reg_gpr+psp_fr26,
			reg_gpr+psp_fr27,
			reg_gpr+psp_fr28,
			reg_gpr+psp_fr29,
			reg_gpr+psp_fr30);

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

void ngen_GetFeatures(ngen_features* dst)
{
	dst->InterpreterFallback=false;
	dst->OnlyDynamicEnds=false;
}