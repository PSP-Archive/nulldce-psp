/*
	Some WIP optimisation stuff and maby helper functions for shil
*/

#include "types.h"
#include "shil.h"
#include "decoder.h"
#include <map>

u32 RegisterWrite[sh4_reg_count];
u32 RegisterRead[sh4_reg_count];

void RegReadInfo(shil_param p,size_t ord)
{
	if (p.is_reg())
	{
		for (u32 i=0; i<p.count(); i++)
			RegisterRead[p._reg+i]=(u32)ord;
	}
}

void RegWriteInfo(shil_opcode* ops, shil_param p,size_t ord)
{
	if (p.is_reg())
	{
		for (u32 i=0; i<p.count(); i++)
		{
			if (RegisterWrite[p._reg+i]>=RegisterRead[p._reg+i] && RegisterWrite[p._reg+i]!=0xFFFFFFFF)	//if last read was before last write, and there was a last write
			{
				printf("DEAD OPCODE %d %zd!\n",RegisterWrite[p._reg+i],ord);
				ops[RegisterWrite[p._reg+i]].Flow=1; //the last write was unused
			}
			RegisterWrite[p._reg+i]=(u32)ord;
		}
	}
}


void ReplaceByMov32(shil_opcode& op)
{
	//verify(op.rd2.is_null());
	op.op = shop_nop;
	op.rs2.type = FMT_NULL;
	op.rs3.type = FMT_NULL;
}

void ReplaceByMov32(shil_opcode& op, u32 v)
{
	//verify(op.rd2.is_null());
	op.op = shop_mov32;
	op.rs1 = shil_param(FMT_IMM, v);
	op.rs2.type = FMT_NULL;
	op.rs3.type = FMT_NULL;
}


u32 fallback_blocks;
u32 total_blocks;
u32 REMOVED_OPS;

bool supportedOP (shil_opcode op){
	if ((int)op.op >= shop_and && (int)op.op < shop_ror) return true;
	if (   op.op == shop_mul_i32
		|| op.op == shop_mul_s16 || op.op == shop_mul_u16  || op.op == shop_writem
		|| op.op == shop_setge || op.op == shop_setgt || op.op == shop_setae || op.op == shop_setgt || op.op == shop_seteq
		|| op.op == shop_test  || op.op == shop_ext_s16 || op.op == shop_ext_s8 || op.op == shop_rocr)
	return true;

	return false;
}

bool supportedFPU_OP (shil_opcode op){

	if ((int)op.op == shop_fsca || (int)op.op == shop_fipr || (int)op.op == shop_ftrv) return false;
	if ((int)op.op >= shop_fadd && (int)op.op < shop_fsrra+1) return true;
	if ((int)op.op >= shop_cvt_f2i_t && (int)op.op <= shop_cvt_i2f_z) return true;

	return false;
}

bool supportednoSave (shil_opcode op){
	if ((int)op.op >= shop_and && (int)op.op < shop_ror) return true;
	if (op.op == shop_mov32) return true;
	return false;
}

bool supportedSwapRs2 (shil_opcode op){
	if ((int)op.op >= shop_and && (int)op.op < shop_add) return true;
	if ((int)op.op >= shop_mul_u16 && (int)op.op < shop_div32u) return true;
	return false;
}

#define N_REG_MIPS 9

enum psp_static_regs{
	
	psp_a0 = 4,
	psp_a1,
	psp_a2,

	psp_s0 = 16,
	psp_s1,
	psp_s2,
	psp_s3,
	psp_s4,
	psp_s5,
	psp_s6,
	psp_s7,

	psp_s8 = 30
};

u32 	   sh4_mapped_reg[15] {0};
bool   	   dirtyMipsRegs[N_REG_MIPS] {false};
const u32  mips_reg[N_REG_MIPS] {psp_s0, psp_s1, psp_s2, psp_s3, psp_s4, psp_s5, psp_s6, psp_s7, psp_s8};

//constprop
void constprop(DecodedBlock* blk)
{
	u32 rv[16];
	bool isi[16]={0};

	for (size_t i=0;i<blk->oplist.size();i++)
	{
		shil_opcode* op=&blk->oplist[i];

		if (op->rs1.is_r32i() && op->rs1._reg<16 && isi[op->rs1._reg])
		{
			if ((op->op==shop_writem || op->op == shop_readm) && (op->flags&0x7F)==4)
			{
				op->rs1.type=FMT_IMM;
				op->rs1._imm=rv[op->rs1._reg];

				if (op->rs3.is_imm())
				{
					op->rs1._imm+=op->rs3._imm;
					op->rs3.type=FMT_NULL;
				}
				//printf("%s promotion: %08X\n",shop_readm==op->op?"shop_readm":"shop_writem",op->rs1._imm);
			}
		    else if (op->op==shop_jdyn)
			{
				if (blk->BlockType==BET_DynamicJump || blk->BlockType==BET_DynamicCall)
				{
					blk->BranchBlock=rv[op->rs1._reg];
					if (op->rs2.is_imm())	
						blk->BranchBlock+=op->rs2._imm;;

					blk->BlockType=blk->BlockType==BET_DynamicJump?BET_StaticJump:BET_StaticCall;
					blk->oplist.erase(blk->oplist.begin()+i);
					i--;
					printf("SBP: %08X!\n",blk->BranchBlock);
					continue;
				}
			}
			else if (op->op==shop_add || op->op==shop_sub)
			{
									
				if (op->rs2.is_imm())
				{
					op->rs1.type=1;
					op->rs1._imm= op->op==shop_add ? 
						(rv[op->rs1._reg]+op->rs2._imm):
						(rv[op->rs1._reg]-op->rs2._imm);
					op->rs2.type=0;
					//printf("%s -> mov32!\n",op->op==shop_add?"shop_add":"shop_sub");
					op->op=shop_mov32;
				}
				
				else if (op->op==shop_add && !op->rs2.is_imm())
				{
					u32 immy=rv[op->rs1._reg];
					op->rs1=op->rs2;
					op->rs2.type=1;
					op->rs2._imm=immy;
					//printf("%s -> imm prm (%08X)!\n",op->op==shop_add?"shop_add":"shop_sub",immy);
				}
			}
		}

		if (op->rd.is_r32i() && op->rd._reg<16) isi[op->rd._reg]=false;
		if (op->rd2.is_r32i() && op->rd2._reg<16) isi[op->rd._reg]=false;

		if (op->op==shop_mov32 && op->rs1.is_imm() && op->rd.is_r32i() && op->rd._reg<16)
		{
			isi[op->rd._reg]=true;
			rv[op->rd._reg]=op->rs1._imm;
		}
	}
}

void sq_pref(DecodedBlock* blk, int i, Sh4RegType rt, bool mark)
{
	u32 data=0;
	for (int c=i-1;c>0;c--)
	{
		if (blk->oplist[c].op==shop_writem && blk->oplist[c].rs1._reg==rt)
		{
			if (blk->oplist[c].rs2.is_r32i() ||  blk->oplist[c].rs2.is_r32f() || blk->oplist[c].rs2.is_r64f() || blk->oplist[c].rs2.is_r32fv())
			{
				data+=blk->oplist[c].flags;
			}
			else
				break;
		}

		if (blk->oplist[c].op==shop_pref || (blk->oplist[c].rd.is_reg() && blk->oplist[c].rd._reg==rt && blk->oplist[c].op!= shop_sub))
		{
			break;
		}

		if (data==32)
			break;
	}

	if (mark) return;

	if (data>=8)
	{
		blk->oplist[i].flags =0x1337;
		sq_pref(blk,i,rt,true);
		//printf("SQW-WM match %d !\n",data);
	}
	else if (data)
	{
		//printf("SQW-WM FAIL %d !\n",data);
	}
}

void sq_pref(DecodedBlock* blk)
{
	for (int i=0;i<blk->oplist.size();i++)
	{
		//blk->oplist[i].flags2=0;
		if (blk->oplist[i].op==shop_pref)
			sq_pref(blk,i,blk->oplist[i].rs1._reg,false);
	}
}

void dejcond(DecodedBlock* blk)
{
	//if (!blk->has_jcond) return;

	bool found=false;
	u32 jcondp=0;

	for (size_t i=0;i<blk->oplist.size();i++)
	{
		shil_opcode* op=&blk->oplist[i];

		if (found)
		{
			if ((op->rd.is_reg() && op->rd._reg==reg_sr_T) ||  op->op==shop_ifb)
			{
				found=false;
			}
		}

		if (op->op==shop_jcond)
		{
			found=true;
			jcondp=(u32)i;
		}
	}

	if (found)
	{
		//blk->has_jcond=false;
		blk->oplist.erase(blk->oplist.begin()+jcondp);
	}
}

void srt_waw(DecodedBlock* blk)
{
	bool found=false;
	u32 srtw=0;

	for (size_t i=0;i<blk->oplist.size();i++)
	{
		shil_opcode* op=&blk->oplist[i];

		if (found)
		{
			if ((op->rs1.is_reg() && op->rs1._reg==reg_sr_T)
				|| (op->rs2.is_reg() && op->rs2._reg==reg_sr_T)
				|| (op->rs3.is_reg() && op->rs3._reg==reg_sr_T)
				|| op->op==shop_ifb)
			{
				found=false;
			}
		}

		if (op->rd.is_reg() && op->rd._reg==reg_sr_T && op->rd2.is_null())
		{
			if (found)
			{
				blk->oplist.erase(blk->oplist.begin()+srtw);
				i--;
			}

			found=true;
			srtw=(u32)i;
		}
	}
}

std::map<RegValue, u32> constprop_values;	// (reg num, version) -> value
u32 reg_versions[sh4_reg_count];

void AddVersionToOperand(shil_param& param, bool define)
{
	if (param.is_reg())
	{
		if (define)
		{
			for (u32 i = 0; i < param.count(); i++)
				reg_versions[param._reg + i]++;
		}
		for (u32 i = 0; i < param.count(); i++)
			param.version[i] = reg_versions[param._reg + i];
	}
}

void ConstPropOperand(shil_param& param)
	{
		if (param.is_r32())
		{
			auto it = constprop_values.find(RegValue(param));
			if (it != constprop_values.end())
			{
				param.type = FMT_IMM;
				param._imm = it->second;
			}
		}
	}

bool CheckConst(shil_param& p1)
	{
		if (p1.is_r32())
		{
			auto _1 = constprop_values.find(RegValue(p1));
			return (_1 != constprop_values.end());
		}
		return false;
	}

void ConstPropPass(DecodedBlock* block)
	{
		for (int opnum = 0; opnum < (int)block->oplist.size(); opnum++)
		{
			shil_opcode& op = block->oplist[opnum];

			/*if (op.op == shop_add){
				if(CheckConst(op.rs1) && (CheckConst(op.rs2))){
					u32 val1 = constprop_values.find(RegValue(op.rs1))->second;
					u32 val2 = constprop_values.find(RegValue(op.rs2))->second;

					ReplaceByMov32(op,val1 + val2);
					printf("HJKHKJHKHKHK\n");
				}*/

			// TODO do shop_sub and others
			/*if (op.op != shop_setab && op.op != shop_setae && op.op != shop_setgt && op.op != shop_setge && op.op != shop_sub && op.op != shop_fsetgt
					 && op.op != shop_fseteq && op.op != shop_fdiv && op.op != shop_fsub && op.op != shop_fmac)
				ConstPropOperand(op.rs1);
			if (op.op != shop_rocr && op.op != shop_rocl && op.op != shop_fsetgt && op.op != shop_fseteq && op.op != shop_fmac)
				ConstPropOperand(op.rs2);
			if (op.op != shop_fmac && op.op != shop_adc)
				ConstPropOperand(op.rs3);*/
				
			if (op.op == shop_ifb)
			{
				constprop_values.clear();
			}
			else if (op.op == shop_sync_sr)
			{
				for (auto it = constprop_values.begin(); it != constprop_values.end(); )
				{
					Sh4RegType reg = it->first.get_reg();
					if (reg == reg_sr_status  || (reg >= reg_r0 && reg <= reg_r7)
							|| (reg >= reg_r0_Bank && reg <= reg_r7_Bank))
						it = constprop_values.erase(it);
					else
						it++;
				}
			}
			else if (op.op == shop_sync_fpscr)
			{
				for (auto it = constprop_values.begin(); it != constprop_values.end(); )
				{
					Sh4RegType reg = it->first.get_reg();
					if (reg == reg_fpscr || reg == reg_old_fpscr || (reg >= reg_fr_0 && reg <= reg_xf_15))
						it = constprop_values.erase(it);
					else
						it++;
				}
			}
			else if (op.op == shop_readm || op.op == shop_writem)
			{
				if (op.rs1.is_imm())
				{
					if (op.rs3.is_imm())
					{
						// Merge base addr and offset
						op.rs1._imm += op.rs3._imm;
						op.rs3.type = FMT_NULL;
					}
					else if (op.rs3.is_reg())
					{
						// Swap rs1 and rs3 so that rs1 is never an immediate operand
						shil_param t = op.rs1;
						op.rs1 = op.rs3;
						op.rs3 = t;
					}

					else if(op.rs1.is_imm() && op.op == shop_readm 
						&& (op.rs1._imm >> 12) >= (block->start >> 12)
						&& (op.rs1._imm >> 12) <= ((block->start + block->sh4_code_size - 1) >> 12)
						&& (op.flags & 0x7f) <= 4){
								if (IsOnRam(op.rs1._imm))
								{
									u32 v;
									switch (op.flags & 0x7f)
									{
									case 1:
										v = (s32)(::s8)ReadMem8(op.rs1._imm);
										ReplaceByMov32(op, v);
										break;
									case 2:
										v = (s32)(::s16)ReadMem16(op.rs1._imm);
										ReplaceByMov32(op, v);
										break;
									case 4:
										v = ReadMem32(op.rs1._imm);
										break;
									default:
										die("invalid size");
										v = 0;
										break;
									}
									//if ((op.flags&0x7f) == 4 && !is_s8(v)) continue;
									
									constprop_values[RegValue(op.rd)] = v;
								}	
					}
				}
			}
			else if (op.op == shop_and || op.op == shop_or || op.op == shop_xor || op.op == shop_add || op.op == shop_mul_s16 || op.op == shop_mul_u16
					  || op.op == shop_mul_i32 || op.op == shop_test || op.op == shop_seteq || op.op == shop_fseteq || op.op == shop_fadd || op.op == shop_fmul
					  || op.op == shop_mul_u64 || op.op == shop_mul_s64 || op.op == shop_adc || op.op == shop_setpeq)
			{
				if (op.rs1.is_imm() && op.rs2.is_reg())
				{
					// Swap rs1 and rs2 so that rs1 is never an immediate operand
					shil_param t = op.rs1;
					op.rs1 = op.rs2;
					op.rs2 = t;
				}
			}
			else if ((op.op == shop_shld || op.op == shop_shad) && op.rs2.is_imm())
			{
				// Replace shld/shad with shl/shr/sar
				u32 r2 = op.rs2._imm;
				if ((r2 & 0x80000000) == 0)
				{
					// rd = r1 << (r2 & 0x1F)
					op.op = shop_shl;
					op.rs2._imm = r2 & 0x1F;
				}
				else if ((r2 & 0x1F) == 0)
				{
					if (op.op == shop_shl)
						// rd = 0
						ReplaceByMov32(op, 0);
					else
					{
						// rd = r1 >> 31;
						op.op = shop_sar;
						op.rs2._imm = 31;
					}
				}
				else
				{
					// rd = r1 >> ((~r2 & 0x1F) + 1)
					op.op = op.op == shop_shad ? shop_sar : shop_shr;
					op.rs2._imm = (~r2 & 0x1F) + 1;
				}
			}
		}
	}

void SimplifyExpressionPass(DecodedBlock* blk)
{
	for (int opnum = 0; opnum < blk->oplist.size(); opnum++)
	{
		shil_opcode& op = blk->oplist[opnum];


		if (op.rs2.is_imm())
		{
			if (op.rs2._imm == 0)
			{
				// a & 0 == 0
				// a * 0 == 0
				// Not true for FPU ops because of Inf and NaN
				if (op.op == shop_and || op.op == shop_mul_i32 || op.op == shop_mul_s16 || op.op == shop_mul_u16)
				{
					//printf("%08x ZERO %s\n", block->vaddr + op.guest_offs, op.dissasm().c_str());
					ReplaceByMov32(op, 0);
				}
				
				// a + 0 == a
				// a - 0 == a
				// a | 0 == a
				// a ^ 0 == a
				// a >> 0 == a
				// a << 0 == a
				// Not true for FPU ops because of Inf and NaN
				else if (op.op == shop_add || op.op == shop_sub || op.op == shop_or || op.op == shop_xor
						|| op.op == shop_shl || op.op == shop_shr || op.op == shop_sar || op.op == shop_shad || op.op == shop_shld)
				{
					//printf("%08x IDEN %s\n", block->vaddr + op.guest_offs, op.dissasm().c_str());
					ReplaceByMov32(op);
				}
			}
			// a * 1 == a
			else if (op.rs2._imm == 1
					&& (op.op == shop_mul_i32 || op.op == shop_mul_s16 || op.op == shop_mul_u16))
			{
				//printf("%08x IDEN %s\n", block->vaddr + op.guest_offs, op.dissasm().c_str());
				ReplaceByMov32(op);
				continue;
			}
		}
		// Not sure it's worth the trouble, except for the 'and' and 'xor'
		else if (op.rs1.is_r32i() && op.rs1._reg == op.rs2._reg)
		{
			
			// a + a == a * 2 == a << 1
			if (op.op == shop_add)
			{
				// There's quite a few of these
				//printf("%08x +t<< %s\n", block->vaddr + op.guest_offs, op.dissasm().c_str());
				op.op = shop_shl;
				op.rs2 = shil_param(FMT_IMM, 1);
			}
			// a ^ a == 0
			// a - a == 0
			else if (op.op == shop_xor || op.op == shop_sub)
			{
				//printf("%08x ZERO %s\n", block->vaddr + op.guest_offs, op.dissasm().c_str());
				ReplaceByMov32(op, 0);
			}
			// SBC a, a == SBC 0,0
			else if (op.op == shop_sbc)
			{
				//printf("%08x ZERO %s\n", block->vaddr + op.guest_offs, op.dissasm().c_str());
				op.rs1 = shil_param(FMT_IMM, 0);
				op.rs2 = shil_param(FMT_IMM, 0);
			}
			// a & a == a
			// a | a == a
			else if (op.op == shop_and || op.op == shop_or)
			{
				//printf("%08x IDEN %s\n", block->vaddr + op.guest_offs, op.dissasm().c_str());
				ReplaceByMov32(op);
			}
		}
	}
}


void DOPT(DecodedBlock* blk)
{
	if (blk->contains_fpu_op) return;
	
	memset(RegisterWrite,-1,sizeof(RegisterWrite));
	memset(RegisterRead,-1,sizeof(RegisterRead));

	total_blocks++;
	for (size_t i=0;i<blk->oplist.size();i++)
	{
		shil_opcode* op=&blk->oplist[i];
		op->Flow=0;

		if (op->op==shop_ifb)
		{
			fallback_blocks++;
			return;
		}

		RegReadInfo(op->rs1,i);
		RegReadInfo(op->rs2,i);
		RegReadInfo(op->rs3,i);

		RegWriteInfo(&blk->oplist[0],op->rd,i);
		RegWriteInfo(&blk->oplist[0],op->rd2,i);
	}

	for (size_t i=0;i<blk->oplist.size();i++)
	{
		if (blk->oplist[i].Flow)
		{
			blk->oplist.erase(blk->oplist.begin()+i);
			REMOVED_OPS++;
			i--;
		}
	}
	//printf("<> %d\n",affregs);

	//printf("%d FB, %d native, %.2f%% || %d removed ops!\n",fallback_blocks,total_blocks-fallback_blocks,fallback_blocks*100.f/total_blocks,REMOVED_OPS);
	printf("\nBlock: %d affecter regs %d c\n",REMOVED_OPS,blk->cycles);
}

u8 reg[16] {0};
u8 otherReg = 0;
void UpdateRegStatus(u32 _reg){
	if (_reg >= 0 && _reg < 16){
		reg[_reg]++;
	}else otherReg++;
}

void PrintRegStatus(){
	printf("REG Usage:\n");
	for (int i = 0; i < 16; i++){
		printf("R%d: %d\n",i,reg[i]);
		reg[i] = 0;
	}
	printf("Other reg: %d\n",otherReg);
	otherReg = 0;
}



bool UsedStaticRegister = false;
u8   regs_used = 0;

u8 GetmappedReg(u8 _reg)
{
	if (sh4_mapped_reg[_reg]){
		UsedStaticRegister = true;
		return sh4_mapped_reg[_reg];
	}

	return 0;
}

bool MapRegister(DecodedBlock* blk, u32 sh4_reg){

	if (sh4_reg   > 15) return 0;
	if (regs_used >= 9) return 0;

	sh4_mapped_reg[sh4_reg]  = mips_reg[regs_used];

	return GetmappedReg(sh4_reg) != 0;
}

void makeDirty(u8 _reg){

}

bool reg_optimizzation = false;

//Simplistic Write after Write without read pass to remove (a few) dead opcodes
//Seems to be working
void AnalyseBlock(DecodedBlock* blk)
{
	sq_pref(blk);

	if (!reg_optimizzation || blk->oplist.size() < 2) return;

	constprop(blk);
	
	memset(reg_versions, 0, sizeof(reg_versions));
	//memset(dirtyMipsRegs, 0, sizeof(dirtyMipsRegs));

	for (shil_opcode& op : blk->oplist)
	{
		AddVersionToOperand(op.rs1, false);
		AddVersionToOperand(op.rs2, false);
		AddVersionToOperand(op.rs3, false);
		AddVersionToOperand(op.rd, true);
		AddVersionToOperand(op.rd2, true);
	}
	
	ConstPropPass(blk);

	//SimplifyExpressionPass(blk);

	srt_waw(blk);


	int regnum = 0;
	#if 0
	//Register optimizer
	for (int opnum = 0; opnum < (int)blk->oplist.size() - 1; opnum++){
		shil_opcode& op = blk->oplist[opnum];

		UsedStaticRegister = false;

		//check if we can use caller free register instead of callee
		if (op.rs1.is_reg()) op.psp_rs1 = MapRegister(blk, op.rs1._reg, psp_a0);
		if (op.rs2.is_reg()) op.psp_rs2 = MapRegister(blk, op.rs2._reg, psp_a1);
		if (op.rs3.is_reg()) op.psp_rs3 = MapRegister(blk, op.rs3._reg, psp_a2);

		if (op.rd.is_reg())  op.psp_rd  = MapRegister(blk, op.rd._reg,  255);
		if (op.rd2.is_reg()) op.psp_rd2 = MapRegister(blk, op.rd2._reg, 255);

		//Functions specific optimization
		/* ..... TODO ...... */
		//if (op.rd._reg == next_op.rs1._reg) 
	}
	#endif
	#if 1

	//Special case

	//printf("BEFORE: %d\n", (int)blk->oplist.size());

	for (int opnum = 0; opnum < (int)blk->oplist.size() - 1; opnum++)
	{
		shil_opcode& op = blk->oplist[opnum];
		shil_opcode& next_op = blk->oplist[opnum + 1];

		//printf("%d :: %d -> %d\n", op.op , next_op.op, opnum);

		if (op.op == shop_shl && next_op.op == shop_shl) {
			if (op.rd._reg == next_op.rs1._reg){
				op.rs2._imm = op.rs2._imm + next_op.rs2._imm;
				op.rd._reg = next_op.rd._reg;
				blk->oplist.erase(blk->oplist.begin()+opnum+1);
				opnum--;
			}
			continue;
		}

		if (op.op == shop_shr && next_op.op == shop_shr) {
			if (op.rd._reg == next_op.rs1._reg){
				op.rs2._imm = op.rs2._imm + next_op.rs2._imm;
				op.rd._reg = next_op.rd._reg;
				blk->oplist.erase(blk->oplist.begin()+opnum+1); 
				opnum--;
			}
			continue;
		}

		if (op.op == shop_mov32  && next_op.op == shop_mov32 && op.rd._reg == next_op.rd._reg && !(op.rs1._reg == next_op.rs1._reg)){
			blk->oplist.erase(blk->oplist.begin()+opnum); 
			opnum--;
			continue;
		}

		if (op.op == shop_mov32 && op.rs1.is_imm() && next_op.rs2.is_imm() && next_op.op == shop_sub && op.rd._reg == next_op.rs1._reg){
			next_op.rs1 = shil_param(FMT_IMM, op.rs1._imm - next_op.rs2._imm);
			next_op.op = shop_mov32;
			blk->oplist.erase(blk->oplist.begin()+opnum); 
			opnum--;
			continue;
		}

		if (op.op == shop_mov32 && op.rs1.is_imm() && next_op.rs2.is_imm() && next_op.op == shop_shl && op.rd._reg == next_op.rs1._reg){
			next_op.rs1 = shil_param(FMT_IMM, op.rs1._imm << next_op.rs2._imm);
			next_op.op = shop_mov32;
			blk->oplist.erase(blk->oplist.begin()+opnum); 
			opnum--;
			continue;
		}

		if (op.op == shop_mov32  && next_op.op == shop_readm)
			if (op.rd._reg == next_op.rs1._reg && !op.rs1.is_imm() && op.rd._reg == next_op.rd._reg){
				next_op.rs1._reg = op.rs1._reg;
				blk->oplist.erase(blk->oplist.begin()+opnum); 
				opnum--;
				continue;
			}

		if (op.op == shop_mov32  && supportednoSave(next_op))
			if (op.rd._reg == next_op.rs1._reg && op.rd._reg == next_op.rd._reg && !op.rs1.is_imm()){
				next_op.rs1._reg = op.rs1._reg;
				blk->oplist.erase(blk->oplist.begin()+opnum); 
				opnum--;
				continue;
			}

		if (op.op == shop_mov32  && next_op.op == shop_writem)
			if (!next_op.rs3.is_imm() && next_op.rs3._reg == op.rd._reg && !op.rs1.is_imm()){
				next_op.rs3._reg = op.rs1._reg;
				blk->oplist.erase(blk->oplist.begin()+opnum); 
				opnum--;
				continue;
			}
	}

	//printf("After: %d\n", (int)blk->oplist.size());

	/*for (int opnum = 0; opnum < (int)blk->oplist.size() - 2; opnum++)
	{
		shil_opcode& op = blk->oplist[opnum];
		shil_opcode& next_op = blk->oplist[opnum + 1];

		if ((op.rd._reg == blk->oplist[opnum + 2].rs1._reg) && supportedOP(blk->oplist[opnum + 2])){
			
				if (   next_op.rs1._reg != op.rd._reg 
				    && !op.rs1.is_imm() && !next_op.rs1.is_imm() 
					&& supportednoSave(op) && supportednoSave(next_op)){
						printf("CHI BBOI %d\n", opnum);
	//If the current op isn't important for the next one, swap them and try to optmize it later
						shil_opcode tmp_op = blk->oplist[opnum];
						op = next_op;
						next_op = tmp_op;

						opnum+= 2;

					}
			}
	}*/

	//Experimental register optimizer (OLD)
	for (int opnum = 0; opnum < (int)blk->oplist.size() - 1; opnum++)
	{
		shil_opcode& op = blk->oplist[opnum];
		shil_opcode& next_op = blk->oplist[opnum + 1];

		const bool fpu_op = supportedFPU_OP(op);

		if(fpu_op && supportedFPU_OP(next_op)){
			if (op.rd._reg == next_op.rs1._reg) next_op.loadReg = false;
			if (op.rs2._reg == next_op.rs2._reg) { next_op.SkipLoadReg2 = true; }
			if (op.rd._reg == next_op.rs2._reg) { next_op.SkipLoadReg2 = true; op.SwapSaveReg = true; }
			if (op.rd._reg == next_op.rd._reg)  op.SaveReg = false;
			continue;
		}

		if (next_op.op == shop_mov32  && fpu_op){
			if (op.rd._reg == next_op.rs1._reg && !next_op.rs1.is_imm()){
				next_op.op = shop_mov32f;
				next_op.loadReg = false;
			}
			continue;
		}

		if (op.op == shop_mov32  && (next_op.op == shop_fmac || next_op.op == shop_fabs || next_op.op == shop_fneg || next_op.op == shop_fsqrt)){
			if (op.rd._reg == next_op.rs1._reg && !op.rs1.is_imm()){
					op.op = shop_mov32f;
					next_op.loadReg = false;
					if (op.rd._reg == next_op.rd._reg && next_op.op != shop_fmac) op.SaveReg = false;
			}
			continue;
		}

		if (fpu_op) { continue; }

		if (op.op == shop_mov32  && next_op.op == shop_mov32){

			if (op.rs1._reg == next_op.rs1._reg && op.loadReg == false){
				next_op.loadReg = false;
				continue;
			}

			if (blk->oplist[opnum + 2].op == shop_mov32){  
				if (op.rd._reg == blk->oplist[opnum + 2].rs1._reg && !blk->oplist[opnum + 2].rs1.is_imm() && !op.rs1.is_imm()){
					op.UseCustomReg = true;
					op.customReg = psp_a1;
					blk->oplist[opnum + 2].UseCustomReg = true;
					blk->oplist[opnum + 2].customReg	= psp_a1;
					blk->oplist[opnum + 2].loadReg 		= false;
					if (op.rd._reg == next_op.rs1._reg) {
						next_op.loadReg = false;
						next_op.UseCustomReg = true;
						next_op.customReg = psp_a1;
					}
					opnum+=2;
					continue;
				}
			}

			if (!op.UseCustomReg){
				op.UseCustomReg = true;
				op.customReg = psp_a1;
				next_op.UseCustomReg = true;
				next_op.customReg = psp_a2;
				opnum++;
			}

			continue; 
		}

		const bool nextop_SUP = supportednoSave(next_op);

		if (op.op == shop_mov32  && nextop_SUP){
						
			if (!next_op.rs2.is_imm() && next_op.rs2._reg == op.rd._reg && op.rs1.is_imm() && op.rs1._imm != 0){
				if (supportedSwapRs2(next_op)){
					op.SwapSaveReg = true;
					next_op.SkipLoadReg2 = true;
				}
				if (op.rd._reg == next_op.rd._reg) op.SaveReg = false;
			}

			if (op.rd._reg == next_op.rs1._reg && op.rs1._imm!=0){
				next_op.loadReg = false;
				if (op.rd._reg == next_op.rd._reg) op.SaveReg = false;
			}

			continue;
		}

		if (op.op == shop_add  && next_op.op == shop_writem){

			if (op.rd._reg == next_op.rs3._reg && next_op.rs3.is_reg()){
				op.UseCustomReg = true;
				op.customReg = 0x7;
				next_op.SkipLoadReg2 = true;
				continue;
			}
			
		}

	
		if (op.op == shop_mov32  && next_op.op == shop_readm){
			if (op.rd._reg == next_op.rs1._reg && op.rs1._imm != 0){
				next_op.loadReg = false;
				if (op.rd._reg == next_op.rd._reg) op.SaveReg = false;
			}
			
			if (next_op.rs3._reg == op.rd._reg){
				if(op.rs1.is_imm()){
					op.UseMemReg2 = true;
					next_op.SkipLoadReg2 = true;
					if (op.rd._reg == next_op.rd._reg) op.SaveReg = false;
				}
			}
			continue;
		}

		if (op.op == shop_mov32  && next_op.op == shop_writem){

			if (!next_op.rs3.is_imm() && next_op.rs3._reg == op.rd._reg){
				
				if(op.rs1.is_imm()){
					op.UseMemReg2 = true;
					next_op.SkipLoadReg2 = true;
					if (op.rd._reg == next_op.rd._reg) op.SaveReg = false;
					continue;
				}
			}

			if (next_op.rs2._reg == op.rd._reg && !op.rs1.is_imm() && next_op.flags != 8){
				op.UseCustomReg = true;
				next_op.UseCustomReg = true;
				op.customReg = psp_a1;
				continue;
			}

			if (op.rd._reg == next_op.rs1._reg && op.rs1._imm != 0){
				next_op.loadReg = false;
			}

			continue;
		}


		if ((op.op == shop_shr || op.op == shop_shl) && next_op.op == shop_writem) {
			if (op.rd._reg == next_op.rs2._reg){
				op.SwapSaveReg = true;
				next_op.UseCustomReg = true;
			}	
			continue;
		}


		if (op.op == shop_readm){
			
			if(next_op.op == shop_mov32 && op.rd._reg == next_op.rs1._reg){
				op.SwapReg = true;
				next_op.loadReg = false;
			}

			if (next_op.op == shop_writem && op.rd._reg == next_op.rs1._reg) {
				next_op.loadReg = false;
				op.SwapReg = true;
			}
			
			if (!next_op.rd2.is_imm() && op.rd._reg == next_op.rs2._reg && supportedSwapRs2(next_op)){
				op.SwapReg = true;
				op.SwapSaveReg = true;
				next_op.SkipLoadReg2 = true;
				if(op.rd._reg == next_op.rd._reg) op.SaveReg = false;
			}

			if (nextop_SUP && op.rd._reg == next_op.rs1._reg) {
				next_op.loadReg = false;
				op.SwapReg = true;
				if(op.rd._reg == next_op.rd._reg) op.SaveReg = false;
			}

			continue;
		}
		

		if (!next_op.rd2.is_imm() && op.rd._reg == next_op.rs2._reg && supportedSwapRs2(next_op) && supportedSwapRs2(op)){
			
			op.SwapSaveReg = true;
			next_op.SkipLoadReg2 = true;
			
			if(op.rd._reg == next_op.rd._reg)
				op.SaveReg = false;

			continue;
		}

		if (!supportedOP(op)) continue;

		if (next_op.op == shop_mov32){
			if (op.rd._reg == next_op.rs1._reg && !next_op.rs1.is_imm()){
				next_op.loadReg = false;
			}
			continue;
		}

		if (op.rd._reg == next_op.rs1._reg){
			next_op.loadReg = false;
			if (op.rd._reg == next_op.rd._reg && nextop_SUP) op.SaveReg = false;
		}

		if (op.rd._reg == next_op.rs2._reg && supportedSwapRs2(next_op)){
			next_op.SkipLoadReg2 = true;
			op.SwapSaveReg = true;
			if (op.rd._reg == next_op.rd._reg ) op.SaveReg = false;
		}
		
	}

	//PrintRegStatus();

	//if (regnum)	printf("Register skipped %d, BL Size: %d\n",regnum,blk->oplist.size());
	#endif
}

void FASTCALL do_sqw_mmu(u32 dst);
void FASTCALL do_sqw_nommu_full(u32 dst);
void UpdateFPSCR();
bool UpdateSR();
#include "dc/sh4/ccn.h"
#include "ngen.h"
#include "dc/sh4/sh4_registers.h"


#define SHIL_MODE 1
#include "shil_canonical.h"

//#define SHIL_MODE 2
//#include "shil_canonical.h"

#define SHIL_MODE 3
#include "shil_canonical.h"

