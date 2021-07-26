#pragma once
#include "dc/sh4/sh4_if.h"
#include "../../mem/sh4_mem.h"

struct shil_opcode;
typedef void shil_chfp(shil_opcode* op);
extern shil_chfp* shil_chf[];

enum shil_param_type
{
	//2 bits
	FMT_NULL,
	FMT_IMM,
	FMT_I32,
	FMT_F32,
	FMT_F64,
	
	FMT_V2,
	FMT_V3,
	FMT_V4,
	FMT_V8,
	FMT_V16,

	FMT_REG_BASE=FMT_I32,
	FMT_VECTOR_BASE=FMT_V2,

	FMT_MASK=0xFFFF,
};

/*
	formats : 16u 16s 32u 32s, 32f, 64f
	param types: r32, r64
*/


#define SHIL_MODE 0
#include "shil_canonical.h"

//this should be really removed ...
u32* GetRegPtr(u32 reg);

struct shil_param
{
	shil_param()
	{
		type=FMT_NULL;
		_imm=0xFFFFFFFF;
	}
	shil_param(u32 type,u32 imm)
	{
		this->type=type;
		if (type >= FMT_REG_BASE)
			new (this) shil_param((Sh4RegType)imm);
		_imm=imm;
	}

	shil_param(Sh4RegType reg)
	{
		type=FMT_NULL;
		if (reg>=reg_fr_0 && reg<=reg_xf_15)
		{
			type=FMT_F32;
			_imm=reg;
		}
		else if (reg>=regv_dr_0 && reg<=regv_dr_14)
		{
			type=FMT_F64;
			_imm=(reg-regv_dr_0)*2+reg_fr_0;
		}
		else if (reg>=regv_xd_0 && reg<=regv_xd_14)
		{
			type=FMT_F64;
			_imm=(reg-regv_xd_0)*2+reg_xf_0;
		}
		else if (reg>=regv_fv_0 && reg<=regv_fv_12)
		{
			type=FMT_V4;
			_imm=(reg-regv_fv_0)*4+reg_fr_0;
		}
		else if (reg==regv_xmtrx)
		{
			type=FMT_V16;
			_imm=reg_xf_0;
		}else if (reg==regv_fmtrx)
		{
			type=FMT_V16;
			_imm=reg_fr_0;
		}
		else
		{
			type=FMT_I32;
			_reg=reg;
		}
		memset(version, 0, sizeof(version));
	}
	union
	{
		u32 _imm;
		Sh4RegType _reg;
	};
	u32 type;
	u16 version[16];

	bool is_null() const { return type==FMT_NULL; }
	bool is_imm() const { return type==FMT_IMM; }
	bool is_reg() const { return type>=FMT_REG_BASE; }

	bool is_r32i() const { return type==FMT_I32; }
	bool is_r32f() const { return type==FMT_F32; }
	u32 is_r32fv()  const { return type>=FMT_VECTOR_BASE?count():0; }
	bool is_r64f() const { return type==FMT_F64; }

	bool is_r32() const { return is_r32i() || is_r32f(); }
	bool is_r64() const { return is_r64f(); }	//just here for symmetry ...

	bool is_imm_s8() const { return is_imm() && is_s8(_imm); }
	bool is_imm_u8() const { return is_imm() && is_u8(_imm); }
	bool is_imm_s16() const { return is_imm() && is_s16(_imm); }
	bool is_imm_u16() const { return is_imm() && is_u16(_imm); }

	u32* reg_ptr()  { verify(is_reg()); return GetRegPtr(_reg); }
	u32  reg_ofs()  { verify(is_reg()); return (u8*)GetRegPtr(_reg) - (u8*)GetRegPtr(reg_xf_0); }

	bool is_vector() { return type>=FMT_VECTOR_BASE; }

	u32 count() const { return  type==FMT_F64?2:type==FMT_V2?2:
								type==FMT_V3?3:type==FMT_V4?4:type==FMT_V8?8:
								type==FMT_V16?16:1; }	//count of hardware regs

	/*	
		Imms:
		is_imm
		
		regs:	
		integer regs			: is_r32i,is_r32,count=1
		fpu regs, single view	: is_r32f,is_r32,count=1
		fpu regs, double view	: is_r64f,count=2
		fpu regs, quad view		: is_vector,is_r32fv=4, count=4
		fpu regs, matrix view	: is_vector,is_r32fv=16, count=16
	*/
};

struct shil_opcode
{
	shilop op;
	u32 Flow;
	u32 flags;
	u32 flags2;

	bool loadReg = true;
	bool SwapReg = false;
	bool SaveReg = true;

	bool UseCustomReg = false;
	u8 customReg = 0;

	bool SwapWFloatR = false;

	bool SwapSaveReg = false;
	bool SkipLoadReg2 = false;

	bool UseMemReg2 = false;

	bool UseStaticReg = false;

	shil_param rd,rd2;
	shil_param rs1,rs2,rs3;
};

class RegValue : public std::pair<Sh4RegType, u32>
	{
	public:
		RegValue(const shil_param& param, int index = 0)
			: std::pair<Sh4RegType, u32>((Sh4RegType)(param._reg + index), param.version[index])
		{
		}
		RegValue(Sh4RegType reg, u32 version)
			: std::pair<Sh4RegType, u32>(reg, version) { }
		RegValue() : std::pair<Sh4RegType, u32>() { }

		Sh4RegType get_reg() const { return first; }
		u32 get_version() const { return second; }
	};