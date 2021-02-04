/*
	Some WIP optimisation stuff and maby helper functions for shil
*/

#include "types.h"
#include "shil.h"
#include "decoder.h"

//u32 RegisterWrite[sh4_reg_count];
//u32 RegisterRead[sh4_reg_count];

void RegReadInfo(shil_param p,size_t ord)
{
	/*if (p.is_reg())
	{
		for (int i=0;i<p.count();i++)
			RegisterRead[p._reg+i]=ord;
	}*/
}
void RegWriteInfo(shil_opcode* ops, shil_param p,size_t ord)
{
	/*if (p.is_reg())
	{
		for (int i=0;i<p.count();i++)
		{
			if (RegisterWrite[p._reg+i]>=RegisterRead[p._reg+i] && RegisterWrite[p._reg+i]!=0xFFFFFFFF)	//if last read was before last write, and there was a last write
			{
				printf("DEAD OPCODE %d %d!\n",RegisterWrite[p._reg+i],ord);
				ops[RegisterWrite[p._reg+i]].Flow=1;						//the last write was unused
			}
			RegisterWrite[p._reg+i]=ord;
		}
	}*/
}
u32 fallback_blocks;
u32 total_blocks;
u32 REMOVED_OPS;

void ReplaceByMov32(shil_opcode& op)
{
	verify(op.rd2.is_null());
	op.op = shop_mov32;
	op.rs2.type = FMT_NULL;
	op.rs3.type = FMT_NULL;
}

void ReplaceByMov32(shil_opcode& op, u32 v)
{
	verify(op.rd2.is_null());
	op.op = shop_mov32;
	op.rs1 = shil_param(FMT_IMM, v);
	op.rs2.type = FMT_NULL;
	op.rs3.type = FMT_NULL;
}


void IdentityMovePass(DecodedBlock* blk)
{
	// This pass creates holes in reg versions and should be run last
	// The versioning pass must be re-run if needed
	for (int opnum = 0; opnum < blk->oplist.size(); opnum++)
	{
		shil_opcode& op = blk->oplist[opnum];
		if (op.op == shop_mov32 && op.rs1.is_reg() && op.rd._reg == op.rs1._reg)
		{
			//printf("%08x DIDN %s\n", block->vaddr + op.guest_offs, op.dissasm().c_str());
			blk->oplist.erase(blk->oplist.begin() + opnum);
			opnum--;
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

void ConstPropPass(DecodedBlock* blk) 
{
	for (int opnum = 0; opnum < (int)blk->oplist.size(); opnum++)
	{
		shil_opcode& op = blk->oplist[opnum];

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

				// If we know the address to read and it's in the same memory page(s) as the block
				// and if those pages are read-only, then we can directly read the memory at compile time
				// and propagate the read value as a constant.
				if (op.rs1.is_imm() && op.op == shop_readm
						&& (op.rs1._imm >> 12) >= (blk->start >> 12)
						&& (op.rs1._imm >> 12) <= ((blk->start + blk->sh4_code_size - 1) >> 12)
						&& (op.flags & 0x7f) <= 4)
				{
					if (IsOnRam(op.rs1._imm))
					{
						u32 v;
						switch (op.flags & 0x7f)
						{
						case 1:
							v = (s32)(::s8)ReadMem8(op.rs1._imm);
							break;
						case 2:
							v = (s32)(::s16)ReadMem16(op.rs1._imm);
							break;
						case 4:
							v = ReadMem32(op.rs1._imm);
							break;
						default:
							//die("invalid size");
							v = 0;
							break;
						}
						ReplaceByMov32(op, v);
					}
				}
			}
		}
		/*else if (ExecuteConstOp(&op))
		{
		}*/
		//else
		 if (op.op == shop_and || op.op == shop_or || op.op == shop_xor || op.op == shop_add || op.op == shop_mul_s16 || op.op == shop_mul_u16
					|| op.op == shop_mul_i32 || op.op == shop_test || op.op == shop_seteq || op.op == shop_fseteq || op.op == shop_fadd || op.op == shop_fmul
					|| op.op == shop_mul_u64 || op.op == shop_mul_s64 )
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

//Simplistic Write after Write without read pass to remove (a few) dead opcodes
//Seems to be working
void AnalyseBlock(DecodedBlock* blk)
{
	//ConstPropPass(blk);
	SimplifyExpressionPass(blk);

	for (int opnum = 0; opnum < (int)blk->oplist.size() - 1; opnum++)
	{
		shil_opcode& op = blk->oplist[opnum];
		shil_opcode& next_op = blk->oplist[opnum + 1];
		if (op.op == next_op.op && (op.op == shop_shl || op.op == shop_shr || op.op == shop_sar) && next_op.rs1.is_r32i() && op.rd._reg == next_op.rs1._reg)
		{
			if (next_op.rs2._imm + op.rs2._imm <= 31)
			{
				next_op.rs2._imm += op.rs2._imm;
				ReplaceByMov32(op);
			}
		}
	}
	
	//IdentityMovePass(blk);
}

void FASTCALL do_sqw_mmu(u32 dst);
void FASTCALL do_sqw_nommu(u32 dst);
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

