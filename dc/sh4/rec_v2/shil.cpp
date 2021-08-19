/*
	Some WIP optimisation stuff and maby helper functions for shil
*/

#include "types.h"
#include "shil.h"
#include "decoder.h"
#include <map>
#include <set>

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
	op.op = shop_mov32;
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

	//printf("val: %d\n", v);
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
				if (mark){
					blk->oplist[c].flags2=0x1337;
				}
			}
			else{
				break;
			}
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

bool istype_float(shil_opcode* op)
{
	return (op->rs2.is_r32f() || op->rd.is_r32f());
}

void sq_pref(DecodedBlock* blk)
{
	for (int i=0;i<blk->oplist.size();i++)
	{
		blk->oplist[i].flags2=0;
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

void ConstantMovePass(DecodedBlock* blk)
{
	for (int opnum = 0; opnum < (int)blk->oplist.size() - 1; opnum++)
	{
		shil_opcode&      op = blk->oplist[opnum + 0];
		shil_opcode& next_op = blk->oplist[opnum + 1];

		if (op.op == shop_mov32 && op.rs1.is_imm() && op.rd._reg == next_op.rs1._reg && next_op.rs2.is_imm()){
			switch (next_op.op)
			{
				case shop_add:
					ReplaceByMov32(next_op, op.rs1._imm + next_op.rs2._imm);
					op.op = shop_nop;
				break;

				case shop_sub:
					ReplaceByMov32(next_op, op.rs1._imm - next_op.rs2._imm);
					op.op = shop_nop;
				break;

				case shop_and:
					ReplaceByMov32(next_op, op.rs1._imm & next_op.rs2._imm);
					op.op = shop_nop;
				break;

				case shop_or:
					ReplaceByMov32(next_op, op.rs1._imm | next_op.rs2._imm);
					op.op = shop_nop;
				break;

				case shop_xor:
					ReplaceByMov32(next_op, op.rs1._imm ^ next_op.rs2._imm);
					op.op = shop_nop;
				break;
				
				default:
				//printf("OP: %d\n", (int)next_op.op);
				break;
				
			}	
		}
	}
}



bool reg_optimizzation = false;
bool _SRA = true;

bool DefinesHigherVersion(const shil_param& param, RegValue reg_ver)
{
	return param.is_reg()
			&& reg_ver.get_reg() >= param._reg
			&& reg_ver.get_reg() < (Sh4RegType)(param._reg + param.count())
			&& param.version[reg_ver.get_reg() - param._reg] > reg_ver.get_version();
}

bool UsesRegValue(const shil_param& param, RegValue reg_ver)
{
	return param.is_reg()
			&& reg_ver.get_reg() >= param._reg
			&& reg_ver.get_reg() < (Sh4RegType)(param._reg + param.count())
			&& param.version[reg_ver.get_reg() - param._reg] == reg_ver.get_version();
}

void ReplaceByAlias(shil_param& param, const RegValue& from, const RegValue& to)
{
	if (param.is_r32() && param._reg == from.get_reg())
	{
		verify(param.version[0] == from.get_version());
		param._reg = to.get_reg();
		param.version[0] = to.get_version();
		//printf("DeadRegisterPass replacing %s.%d by %s.%d\n", name_reg(from.get_reg()).c_str(), from.get_version(),
		//		name_reg(to.get_reg()).c_str(), to.get_version());
	}
}

std::set<RegValue> writeback_values;

void DeadCodeRemovalPass(DecodedBlock* block)
{
	u32 last_versions[sh4_reg_count];
	std::set<RegValue> uses;

	memset(last_versions, -1, sizeof(last_versions));
	for (int opnum = block->oplist.size() - 1; opnum >= 0; opnum--)
	{
		shil_opcode& op = block->oplist[opnum];
		bool dead_code = false;

		if (op.op == shop_ifb)
		{
			// if mmu enabled, mem accesses can throw an exception
			// so last_versions must be reset so the regs are correctly saved beforehand
			memset(last_versions, -1, sizeof(last_versions));
			continue;
		}
		if (op.op == shop_pref)
		{
			if (op.rs1.is_imm() && (op.rs1._imm & 0xFC000000) != 0xE0000000)
				dead_code = true;
		}
		if (op.op == shop_sync_sr)
		{
			last_versions[reg_sr_T] = -1;
			last_versions[reg_sr_status] = -1;
			last_versions[reg_old_sr_status] = -1;
			for (int i = reg_r0; i <= reg_r7; i++)
				last_versions[i] = -1;
			for (int i = reg_r0_Bank; i <= reg_r7_Bank; i++)
				last_versions[i] = -1;
			continue;
		}
		if (op.op == shop_sync_fpscr)
		{
			last_versions[reg_fpscr] = -1;
			last_versions[reg_old_fpscr] = -1;
			for (int i = reg_fr_0; i <= reg_xf_15; i++)
				last_versions[i] = -1;
			continue;
		}

		if (op.rd.is_reg())
		{
			bool unused_rd = true;
			for (u32 i = 0; i < op.rd.count(); i++)
			{
				if (last_versions[op.rd._reg + i] == (u32)-1)
				{
					last_versions[op.rd._reg + i] = op.rd.version[i];
					unused_rd = false;
					writeback_values.insert(RegValue(op.rd, i));
				}
				else
				{
					verify(op.rd.version[i] < last_versions[op.rd._reg + i]);
					if (uses.find(RegValue(op.rd, i)) != uses.end())
					{
						unused_rd = false;
					}
				}
			}
			dead_code = dead_code || unused_rd;
		}
		if (op.rd2.is_reg())
		{
			bool unused_rd = true;
			for (u32 i = 0; i < op.rd2.count(); i++)
			{
				if (last_versions[op.rd2._reg + i] == (u32)-1)
				{
					last_versions[op.rd2._reg + i] = op.rd2.version[i];
					unused_rd = false;
					writeback_values.insert(RegValue(op.rd2, i));
				}
				else
				{
					verify(op.rd2.version[i] < last_versions[op.rd2._reg + i]);
					if (uses.find(RegValue(op.rd2, i)) != uses.end())
					{
						unused_rd = false;
					}
				}
			}
			dead_code = dead_code && unused_rd;
		}
		if (dead_code && op.op != shop_readm)	// memory read on registers can have side effects
		{
			//printf("%08x DEAD %s\n", block->vaddr + op.guest_offs, op.dissasm().c_str());
			block->oplist.erase(block->oplist.begin() + opnum);
		}
		else
		{
			if (op.rs1.is_reg())
			{
				for (u32 i = 0; i < op.rs1.count(); i++)
					uses.insert(RegValue(op.rs1, i));
			}
			if (op.rs2.is_reg())
			{
				for (u32 i = 0; i < op.rs2.count(); i++)
					uses.insert(RegValue(op.rs2, i));
			}
			if (op.rs3.is_reg())
			{
				for (u32 i = 0; i < op.rs3.count(); i++)
					uses.insert(RegValue(op.rs3, i));
			}
		}
	}
}

void DeadRegisterPass(DecodedBlock* block)
{
	std::map<RegValue, RegValue> aliases;		// (dest reg, version) -> (source reg, version)

	// Find aliases
	for (shil_opcode& op : block->oplist)
	{
		// ignore moves from/to int regs to/from fpu regs
		if (op.op == shop_mov32 && op.rs1.is_reg() && op.rd.is_r32i() == op.rs1.is_r32i())
		{
			RegValue dest_reg(op.rd);
			RegValue src_reg(op.rs1);
			auto it = aliases.find(src_reg);
			if (it != aliases.end())
				// use the final value if the src is itself aliased
				aliases[dest_reg] = it->second;
			else
				aliases[dest_reg] = src_reg;
		}
	}

	// Attempt to eliminate them
	for (auto& alias : aliases)
	{
		if (writeback_values.count(alias.first) > 0)
			continue;

		// Do a first pass to check that we can replace the value
		size_t defnum = -1;
		size_t usenum = -1;
		size_t aliasdef = -1;
		for (size_t opnum = 0; opnum < block->oplist.size(); opnum++)
		{
			shil_opcode* op = &block->oplist[opnum];
			// find def
			if (op->rd.is_r32() && RegValue(op->rd) == alias.first)
				defnum = opnum;
			else if (op->rd2.is_r32() && RegValue(op->rd2) == alias.first)
				defnum = opnum;

			// find alias redef
			if (DefinesHigherVersion(op->rd, alias.second) && aliasdef == (size_t)-1)
				aliasdef = opnum;
			else if (DefinesHigherVersion(op->rd2, alias.second) && aliasdef == (size_t)-1)
				aliasdef = opnum;

			// find last use
			if (UsesRegValue(op->rs1, alias.first))
			{
				if (op->rs1.count() == 1)
					usenum = opnum;
				else
				{
					usenum = 0xFFFF;	// Can't alias values used by vectors cuz they need adjacent regs
					aliasdef = 0;
					break;
				}
			}
			else if (UsesRegValue(op->rs2, alias.first))
			{
				if (op->rs2.count() == 1)
					usenum = opnum;
				else
				{
					usenum = 0xFFFF;
					aliasdef = 0;
					break;
				}
			}
			else if (UsesRegValue(op->rs3, alias.first))
			{
				if (op->rs3.count() == 1)
					usenum = opnum;
				else
				{
					usenum = 0xFFFF;
					aliasdef = 0;
					break;
				}
			}
		}
		verify(defnum != (size_t)-1);
		// If the alias is redefined before any use we can't use it
		if (aliasdef != (size_t)-1 && usenum != (size_t)-1 && aliasdef < usenum)
			continue;

		for (size_t opnum = defnum + 1; opnum <= usenum && usenum != (size_t)-1; opnum++)
		{
			shil_opcode* op = &block->oplist[opnum];
			ReplaceByAlias(op->rs1, alias.first, alias.second);
			ReplaceByAlias(op->rs2, alias.first, alias.second);
			ReplaceByAlias(op->rs3, alias.first, alias.second);
		}
		//printf("%08x DREG %s\n", block->vaddr + block->oplist[defnum].guest_offs, block->oplist[defnum].dissasm().c_str());
		block->oplist.erase(block->oplist.begin() + defnum);
	}
}

void SimplifyExpressionPass(DecodedBlock* block)
{
	for (size_t opnum = 0; opnum < block->oplist.size(); opnum++)
	{
		shil_opcode& op = block->oplist[opnum];
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
				// a * 0 == 0
				/* TODO 64-bit result
				else if (op.op == shop_mul_u64 || op.op == shop_mul_s64)
				{
					printf("%08x ZERO %s\n", block->vaddr + op.guest_offs, op.dissasm().c_str());
					ReplaceByMov32(op, 0);
				}
				*/
				// a + 0 == a
				// a - 0 == a
				// a | 0 == a
				// a ^ 0 == a
				// a >> 0 == a
				// a << 0 == a
				// Not true for FPU ops because of Inf and NaN
				else if (op.op == shop_shl || op.op == shop_shr || op.op == shop_sar || op.op == shop_shad || op.op == shop_shld)
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

int _opnum = 0;

bool ExecuteConstOp(DecodedBlock* block, shil_opcode* op);

void ConstPropPass(DecodedBlock* block)
	{
		for (_opnum = 0; _opnum < (int)block->oplist.size(); _opnum++)
		{
			shil_opcode& op = block->oplist[_opnum];

			if (op.op == shop_nop) continue; 

			// TODO do shop_sub and others
			/*if (op.op == shop_fadd )
				ConstPropOperand(op.rs1);
			if (op.op == shop_fadd )
				ConstPropOperand(op.rs2);
			if (op.op == shop_fadd )
				ConstPropOperand(op.rs3);*/

			if (op.op == shop_ifb)
			{
				constprop_values.clear();
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

					if(op.rs1.is_imm() && op.op == shop_readm 
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
										ReplaceByMov32(op, v);
										break;
									default:
										die("invalid size");
										v = 0;
										break;
									}

									constprop_values[RegValue(op.rd)] = v;
								}	
							}	
					}
			}
			/*else if (op.op == shop_sync_sr)
			{
				for (auto it = constprop_values.begin(); it != constprop_values.end(); )
				{
					Sh4RegType reg = it->first.get_reg();
					if (reg == reg_sr_status || reg == reg_old_sr_status || (reg >= reg_r0 && reg <= reg_r7)
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
			else if (ExecuteConstOp(block, &op))
			{
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
			}*/
		}
	}

#define GetImm12(str) ((str>>0) & 0xfff)
#define GetSImm12(str) (((short)((GetImm12(str))<<4))>>4)

void dec_updateBlockCycles(DecodedBlock *block, u16 op)
{
	if (op < 0xF000)
		block->cycles++;
}

bool skipSingleBranchTarget(DecodedBlock *block, u32& addr, bool updateCycles)
{
	if (addr == 0xFFFFFFFF)
		return false;
	bool success = false;
	const u32 start_page = block->start >> 12;
	const u32 end_page = (block->start + (block->opcodes - 1) * 2) >> 12;
	while (true)
	{
		if ((addr >> 12) < start_page || ((addr + 2) >> 12) > end_page)
			break;

		u32 op = IReadMem16(addr);
		// Axxx: bra <bdisp12>
		if ((op & 0xF000) != 0xA000)
			break;

		u16 delayOp = IReadMem16(addr + 2);
		if (delayOp != 0x0000 && delayOp != 0x0009)	// nop
			break;

		int disp = GetSImm12(op) * 2 + 4;
		if (disp == 0)
			// infiniloop
			break;
		addr += disp;
		if (updateCycles)
		{
			dec_updateBlockCycles(block, op);
			dec_updateBlockCycles(block, delayOp);
		}
		success = true;
	}
	return success;
}

void SingleBranchTargetPass(DecodedBlock *block)
{
	if (block->read_only)
	{
		bool updateCycles = !skipSingleBranchTarget(block, block->BranchBlock, true);
		skipSingleBranchTarget(block, block->NextBlock, updateCycles);
	}
}

//"links" consts to each other
void constlink(DecodedBlock* blk)
{
	Sh4RegType def=NoReg;
	s32 val=0;

	for (size_t i=0;i<blk->oplist.size();i++)
	{
		shil_opcode& op= blk->oplist[i];

		if (op.op!=shop_mov32)
			def=NoReg;
		else
		{

			if (def!=NoReg && op.rs1.is_imm() && op.rs1._imm==val)
			{
				op.rs1=shil_param(def);
			}
			else if (def==NoReg && op.rs1.is_imm() && op.rs1._imm==0)
			{
				def=op.rd._reg;
				val=op.rs1._imm;
			}
		}

		if (op.op == shop_readm || op.op == shop_writem)
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

				if(op.rs1.is_imm() && op.op == shop_readm 
					&& (op.rs1._imm >> 12) >= (blk->start >> 12)
					&& (op.rs1._imm >> 12) <= ((blk->start + blk->sh4_code_size - 1) >> 12)
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
							}	
						}	
				}
			}
	}
}


void AnalyseBlock(DecodedBlock* blk)
{
	memset(reg_versions, 0, sizeof(reg_versions));

	for (shil_opcode& op : blk->oplist)
	{ 
		AddVersionToOperand(op.rs1, false);
		AddVersionToOperand(op.rs2, false);
		AddVersionToOperand(op.rs3, false);
		AddVersionToOperand(op.rd, true);
		AddVersionToOperand(op.rd2, true);
	}

	DeadCodeRemovalPass(blk);

	sq_pref(blk);

	//ConstPropPass(blk);
	ConstantMovePass(blk);

	constlink(blk);

	SimplifyExpressionPass(blk);

	DeadRegisterPass(blk);

	srt_waw(blk);

	//SingleBranchTargetPass(blk);
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

#define SHIL_MODE 3
#include "shil_canonical.h"

#define SHIL_MODE 4 
#include "shil_canonical.h"
