#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

#include "Common/CPUDetect.h"
#include "Common/JitRegister.h"
#include "Common/x64ABI.h"
#include "VideoCommon/VertexLoaderX64.h"

using namespace Gen;

static const X64Reg src_reg = ABI_PARAM1;
static const X64Reg dst_reg = ABI_PARAM2;
static const X64Reg scratch1 = RAX;
static const X64Reg scratch2 = ABI_PARAM3;
static const X64Reg scratch3 = ABI_PARAM4;
static const X64Reg count_reg = R10;
static const X64Reg skipped_reg = R11;

VertexLoaderX64::VertexLoaderX64(const TVtxDesc& vtx_desc, const VAT& vtx_att): VertexLoaderBase(vtx_desc, vtx_att)
{
	if (!IsInitialized())
		return;

	AllocCodeSpace(4096);
	ClearCodeSpace();
	GenerateVertexLoader();
	WriteProtect();

	std::string name;
	AppendToString(&name);
	JitRegister::Register(region, (u32)(GetCodePtr() - region), name.c_str());
}

OpArg VertexLoaderX64::GetVertexAddr(int array, u64 attribute)
{
	OpArg data = MDisp(src_reg, m_src_ofs);
	if (attribute & MASK_INDEXED)
	{
		if (attribute == INDEX8)
		{
			MOVZX(64, 8, scratch1, data);
			m_src_ofs += 1;
		}
		else
		{
			MOV(16, R(scratch1), data);
			m_src_ofs += 2;
			BSWAP(16, scratch1);
			MOVZX(64, 16, scratch1, R(scratch1));
		}
		if (array == ARRAY_POSITION)
		{
			CMP(attribute == INDEX8 ? 8 : 16, R(scratch1), Imm8(-1));
			m_skip_vertex = J_CC(CC_E, true);
		}
		// TODO: Move cached_arraybases into CPState and use MDisp() relative to a constant register loaded with &g_main_cp_state.
		IMUL(32, scratch1, M(&g_main_cp_state.array_strides[array]));
		MOV(64, R(scratch2), M(&cached_arraybases[array]));
		return MRegSum(scratch1, scratch2);
	}
	else
	{
		return data;
	}
}

int VertexLoaderX64::ReadVertex(OpArg data, u64 attribute, int format, int count_in, int count_out, bool dequantize, u8 scaling_exponent, AttributeFormat* native_format)
{
	static const __m128i shuffle_lut[5][3] = {
		{_mm_set_epi32(0xFFFFFFFFL, 0xFFFFFFFFL, 0xFFFFFFFFL, 0xFFFFFF00L),  // 1x u8
		 _mm_set_epi32(0xFFFFFFFFL, 0xFFFFFFFFL, 0xFFFFFF01L, 0xFFFFFF00L),  // 2x u8
		 _mm_set_epi32(0xFFFFFFFFL, 0xFFFFFF02L, 0xFFFFFF01L, 0xFFFFFF00L)}, // 3x u8
		{_mm_set_epi32(0xFFFFFFFFL, 0xFFFFFFFFL, 0xFFFFFFFFL, 0x00FFFFFFL),  // 1x s8
		 _mm_set_epi32(0xFFFFFFFFL, 0xFFFFFFFFL, 0x01FFFFFFL, 0x00FFFFFFL),  // 2x s8
		 _mm_set_epi32(0xFFFFFFFFL, 0x02FFFFFFL, 0x01FFFFFFL, 0x00FFFFFFL)}, // 3x s8
		{_mm_set_epi32(0xFFFFFFFFL, 0xFFFFFFFFL, 0xFFFFFFFFL, 0xFFFF0001L),  // 1x u16
		 _mm_set_epi32(0xFFFFFFFFL, 0xFFFFFFFFL, 0xFFFF0203L, 0xFFFF0001L),  // 2x u16
		 _mm_set_epi32(0xFFFFFFFFL, 0xFFFF0405L, 0xFFFF0203L, 0xFFFF0001L)}, // 3x u16
		{_mm_set_epi32(0xFFFFFFFFL, 0xFFFFFFFFL, 0xFFFFFFFFL, 0x0001FFFFL),  // 1x s16
		 _mm_set_epi32(0xFFFFFFFFL, 0xFFFFFFFFL, 0x0203FFFFL, 0x0001FFFFL),  // 2x s16
		 _mm_set_epi32(0xFFFFFFFFL, 0x0405FFFFL, 0x0203FFFFL, 0x0001FFFFL)}, // 3x s16
		{_mm_set_epi32(0xFFFFFFFFL, 0xFFFFFFFFL, 0xFFFFFFFFL, 0x00010203L),  // 1x float
		 _mm_set_epi32(0xFFFFFFFFL, 0xFFFFFFFFL, 0x04050607L, 0x00010203L),  // 2x float
		 _mm_set_epi32(0xFFFFFFFFL, 0x08090A0BL, 0x04050607L, 0x00010203L)}, // 3x float
	};
	static const __m128 scale_factors[32] = {
		_mm_set_ps1(1./(1u<< 0)), _mm_set_ps1(1./(1u<< 1)), _mm_set_ps1(1./(1u<< 2)), _mm_set_ps1(1./(1u<< 3)),
		_mm_set_ps1(1./(1u<< 4)), _mm_set_ps1(1./(1u<< 5)), _mm_set_ps1(1./(1u<< 6)), _mm_set_ps1(1./(1u<< 7)),
		_mm_set_ps1(1./(1u<< 8)), _mm_set_ps1(1./(1u<< 9)), _mm_set_ps1(1./(1u<<10)), _mm_set_ps1(1./(1u<<11)),
		_mm_set_ps1(1./(1u<<12)), _mm_set_ps1(1./(1u<<13)), _mm_set_ps1(1./(1u<<14)), _mm_set_ps1(1./(1u<<15)),
		_mm_set_ps1(1./(1u<<16)), _mm_set_ps1(1./(1u<<17)), _mm_set_ps1(1./(1u<<18)), _mm_set_ps1(1./(1u<<19)),
		_mm_set_ps1(1./(1u<<20)), _mm_set_ps1(1./(1u<<21)), _mm_set_ps1(1./(1u<<22)), _mm_set_ps1(1./(1u<<23)),
		_mm_set_ps1(1./(1u<<24)), _mm_set_ps1(1./(1u<<25)), _mm_set_ps1(1./(1u<<26)), _mm_set_ps1(1./(1u<<27)),
		_mm_set_ps1(1./(1u<<28)), _mm_set_ps1(1./(1u<<29)), _mm_set_ps1(1./(1u<<30)), _mm_set_ps1(1./(1u<<31)),
	};

	X64Reg coords = XMM0;

	int elem_size = 1 << (format / 2);
	int load_bytes  = elem_size * count_in;
	if (load_bytes >= 8)
		MOVDQU(coords, data);
	else if (load_bytes >= 4)
		MOVQ_xmm(coords, data);
	else
		MOVD_xmm(coords, data);

	PSHUFB(coords, M(&shuffle_lut[format][count_in - 1]));

	if (format != FORMAT_FLOAT)
	{
		// Sign extend
		if (format == FORMAT_BYTE)
			PSRAD(coords, 24);
		if (format == FORMAT_SHORT)
			PSRAD(coords, 16);

		CVTDQ2PS(coords, R(coords));

		if (dequantize && scaling_exponent)
			MULPS(coords, M(&scale_factors[scaling_exponent]));
	}

	OpArg dest = MDisp(dst_reg, m_dst_ofs);
	switch (count_out)
	{
		case 1: MOVSS(dest, coords); break;
		case 2: MOVLPS(dest, coords); break;
		case 3: MOVUPS(dest, coords); break;
	}

	native_format->components = count_out;
	native_format->enable = true;
	native_format->offset = m_dst_ofs;
	native_format->type = VAR_FLOAT;
	native_format->integer = false;
	m_dst_ofs += sizeof(float) * count_out;

	if (attribute == DIRECT)
		m_src_ofs += load_bytes;

	return load_bytes;
}

void VertexLoaderX64::ReadColor(OpArg data, u64 attribute, int format)
{
	int load_bytes = 0;
	switch (format)
	{
		case FORMAT_24B_888:
		case FORMAT_32B_888x:
		case FORMAT_32B_8888:
			MOV(32, R(scratch1), data);
			if (format != FORMAT_32B_8888)
				OR(32, R(scratch1), Imm32(0xFF000000));
			MOV(32, MDisp(dst_reg, m_dst_ofs), R(scratch1));
			load_bytes = 3 + (format != FORMAT_24B_888);
			break;

		case FORMAT_16B_565:
			//                   RRRRRGGG GGGBBBBB
			// AAAAAAAA BBBBBBBB GGGGGGGG RRRRRRRR
			LoadAndSwap(16, scratch1, data);
			if (cpu_info.bBMI1 && cpu_info.bBMI2)
			{
				MOV(32, R(scratch2), Imm32(0x07C3F7C0));
				PDEP(32, scratch3, scratch1, R(scratch2));

				MOV(32, R(scratch2), Imm32(0xF8FCF800));
				PDEP(32, scratch1, scratch1, R(scratch2));
				ANDN(32, scratch2, scratch2, R(scratch3));

				OR(32, R(scratch1), R(scratch2));
			}
			else
			{
				MOV(32, R(scratch3), R(scratch1));
				SHL(32, R(scratch1), Imm8(16));
				AND(32, R(scratch1), Imm32(0xF8000000));

				MOV(32, R(scratch2), R(scratch3));
				SHL(32, R(scratch2), Imm8(13));
				AND(32, R(scratch2), Imm32(0x00FC0000));
				OR(32, R(scratch1), R(scratch2));

				SHL(32, R(scratch3), Imm8(11));
				AND(32, R(scratch3), Imm32(0x0000F800));
				OR(32, R(scratch1), R(scratch3));

				MOV(32, R(scratch2), R(scratch1));
				SHR(32, R(scratch1), Imm8(5));
				AND(32, R(scratch1), Imm32(0x07000700));
				OR(32, R(scratch1), R(scratch2));

				SHR(32, R(scratch2), Imm8(6));
				AND(32, R(scratch2), Imm32(0x00030000));
				OR(32, R(scratch1), R(scratch2));
			}

			OR(32, R(scratch1), Imm32(0x000000FF));
			SwapAndStore(32, MDisp(dst_reg, m_dst_ofs), scratch1);
			load_bytes = 2;
			break;

		case FORMAT_16B_4444:
			//                   RRRRGGGG BBBBAAAA
			// AAAAAAAA BBBBBBBB GGGGGGGG RRRRRRRR
			LoadAndSwap(16, scratch1, data);
			if (cpu_info.bBMI2)
			{
				MOV(32, R(scratch3), Imm32(0x0F0F0F0F));
				PDEP(32, scratch2, scratch1, R(scratch3));
				MOV(32, R(scratch3), Imm32(0xF0F0F0F0));
				PDEP(32, scratch1, scratch1, R(scratch3));
			}
			else
			{
				MOV(32, R(scratch3), R(scratch1));
				SHL(32, R(scratch1), Imm8(12));
				AND(32, R(scratch1), Imm32(0x0F000000));
				MOV(32, R(scratch2), R(scratch1));

				MOV(32, R(scratch1), R(scratch3));
				SHL(32, R(scratch1), Imm8(8));
				AND(32, R(scratch1), Imm32(0x000F0000));
				OR(32, R(scratch2), R(scratch1));

				MOV(32, R(scratch1), R(scratch3));
				SHL(32, R(scratch1), Imm8(4));
				AND(32, R(scratch1), Imm32(0x00000F00));
				OR(32, R(scratch2), R(scratch1));

				AND(32, R(scratch3), Imm8(0x0F));
				OR(32, R(scratch2), R(scratch3));

				MOV(32, R(scratch1), R(scratch2));
				SHL(32, R(scratch1), Imm8(4));
			}
			OR(32, R(scratch1), R(scratch2));
			SwapAndStore(32, MDisp(dst_reg, m_dst_ofs), scratch1);
			load_bytes = 2;
			break;

		case FORMAT_24B_6666:
			//          RRRRRRGG GGGGBBBB BBAAAAAA
			// AAAAAAAA BBBBBBBB GGGGGGGG RRRRRRRR
			data.offset -= 1;
			LoadAndSwap(32, scratch1, data);
			if (cpu_info.bBMI2)
			{
				MOV(32, R(scratch2), Imm32(0xFCFCFCFC));
				PDEP(32, scratch1, scratch1, R(scratch2));
				MOV(32, R(scratch2), R(scratch1));
			}
			else
			{
				MOV(32, R(scratch3), R(scratch1));
				SHL(32, R(scratch1), Imm8(8));
				AND(32, R(scratch1), Imm32(0xFC000000));
				MOV(32, R(scratch2), R(scratch1));

				MOV(32, R(scratch1), R(scratch3));
				SHL(32, R(scratch1), Imm8(6));
				AND(32, R(scratch1), Imm32(0x00FC0000));
				OR(32, R(scratch2), R(scratch1));

				MOV(32, R(scratch1), R(scratch3));
				SHL(32, R(scratch1), Imm8(4));
				AND(32, R(scratch1), Imm32(0x0000FC00));
				OR(32, R(scratch2), R(scratch1));

				SHL(32, R(scratch3), Imm8(2));
				AND(32, R(scratch3), Imm32(0x000000FC));
				OR(32, R(scratch2), R(scratch3));

				MOV(32, R(scratch1), R(scratch2));
			}

			SHR(32, R(scratch1), Imm8(6));
			AND(32, R(scratch1), Imm32(0x03030303));
			OR(32, R(scratch1), R(scratch2));

			SwapAndStore(32, MDisp(dst_reg, m_dst_ofs), scratch1);
			load_bytes = 3;
			break;
	}
	if (attribute == DIRECT)
		m_src_ofs += load_bytes;
}

void VertexLoaderX64::GenerateVertexLoader()
{
	// Backup count since we're going to count it down.
	PUSH(32, R(ABI_PARAM3));

	// We use ABI_PARAM3 for scratch2.
	MOV(32, R(count_reg), R(ABI_PARAM3));

	if (m_VtxDesc.Position & MASK_INDEXED)
		XOR(32, R(skipped_reg), R(skipped_reg));

	// TODO: load constants into registers outside the main loop

	const u8* loop_start = GetCodePtr();

	if (m_VtxDesc.PosMatIdx)
	{
		MOVZX(32, 8, scratch1, MDisp(src_reg, m_src_ofs));
		AND(32, R(scratch1), Imm8(0x3F));
		MOV(32, MDisp(dst_reg, m_dst_ofs), R(scratch1));
		m_native_components |= VB_HAS_POSMTXIDX;
		m_native_vtx_decl.posmtx.components = 4;
		m_native_vtx_decl.posmtx.enable = true;
		m_native_vtx_decl.posmtx.offset = m_dst_ofs;
		m_native_vtx_decl.posmtx.type = VAR_UNSIGNED_BYTE;
		m_native_vtx_decl.posmtx.integer = true;
		m_src_ofs += sizeof(u8);
		m_dst_ofs += sizeof(u32);
	}

	u32 texmatidx_ofs[8];
	const u64 tm[8] = {
		m_VtxDesc.Tex0MatIdx, m_VtxDesc.Tex1MatIdx, m_VtxDesc.Tex2MatIdx, m_VtxDesc.Tex3MatIdx,
		m_VtxDesc.Tex4MatIdx, m_VtxDesc.Tex5MatIdx, m_VtxDesc.Tex6MatIdx, m_VtxDesc.Tex7MatIdx,
	};
	for (int i = 0; i < 8; i++)
	{
		if (tm[i])
			texmatidx_ofs[i] = m_src_ofs++;
	}

	OpArg data = GetVertexAddr(ARRAY_POSITION, m_VtxDesc.Position);
	ReadVertex(data, m_VtxDesc.Position, m_VtxAttr.PosFormat, m_VtxAttr.PosElements + 2, 3,
	           m_VtxAttr.ByteDequant, m_VtxAttr.PosFrac, &m_native_vtx_decl.position);

	if (m_VtxDesc.Normal)
	{
		static const u8 map[8] = {7, 6, 15, 14};
		u8 scaling_exponent = map[m_VtxAttr.NormalFormat];

		for (int i = 0; i < (m_VtxAttr.NormalElements ? 3 : 1); i++)
		{
			if (!i || m_VtxAttr.NormalIndex3)
			{
				data = GetVertexAddr(ARRAY_NORMAL, m_VtxDesc.Normal);
				int elem_size = 1 << (m_VtxAttr.NormalFormat / 2);
				data.offset += i * elem_size * 3;
			}
			data.offset += ReadVertex(data, m_VtxDesc.Normal, m_VtxAttr.NormalFormat, 3, 3,
			                          true, scaling_exponent, &m_native_vtx_decl.normals[i]);
		}

		m_native_components |= VB_HAS_NRM0;
		if (m_VtxAttr.NormalElements)
			m_native_components |= VB_HAS_NRM1 | VB_HAS_NRM2;
	}

	const u64 col[2] = {m_VtxDesc.Color0, m_VtxDesc.Color1};
	for (int i = 0; i < 2; i++)
	{
		if (col[i])
		{
			data = GetVertexAddr(ARRAY_COLOR + i, col[i]);
			ReadColor(data, col[i], m_VtxAttr.color[i].Comp);
			m_native_components |= VB_HAS_COL0 << i;
			m_native_vtx_decl.colors[i].components = 4;
			m_native_vtx_decl.colors[i].enable = true;
			m_native_vtx_decl.colors[i].offset = m_dst_ofs;
			m_native_vtx_decl.colors[i].type = VAR_UNSIGNED_BYTE;
			m_native_vtx_decl.colors[i].integer = false;
			m_dst_ofs += 4;
		}
	}

	const u64 tc[8] = {
		m_VtxDesc.Tex0Coord, m_VtxDesc.Tex1Coord, m_VtxDesc.Tex2Coord, m_VtxDesc.Tex3Coord,
		m_VtxDesc.Tex4Coord, m_VtxDesc.Tex5Coord, m_VtxDesc.Tex6Coord, m_VtxDesc.Tex7Coord,
	};
	for (int i = 0; i < 8; i++)
	{
		int elements = m_VtxAttr.texCoord[i].Elements + 1;
		if (tc[i])
		{
			data = GetVertexAddr(ARRAY_TEXCOORD0 + i, tc[i]);
			u8 scaling_exponent = m_VtxAttr.texCoord[i].Frac;
			ReadVertex(data, tc[i], m_VtxAttr.texCoord[i].Format, elements, tm[i] ? 2 : elements,
			           m_VtxAttr.ByteDequant, scaling_exponent, &m_native_vtx_decl.texcoords[i]);
			m_native_components |= VB_HAS_UV0 << i;
		}
		if (tm[i])
		{
			m_native_components |= VB_HAS_TEXMTXIDX0 << i;
			m_native_vtx_decl.texcoords[i].components = 3;
			m_native_vtx_decl.texcoords[i].enable = true;
			m_native_vtx_decl.texcoords[i].type = VAR_FLOAT;
			m_native_vtx_decl.texcoords[i].integer = false;
			MOVZX(64, 8, scratch1, MDisp(src_reg, texmatidx_ofs[i]));
			if (tc[i])
			{
				CVTSI2SS(XMM0, R(scratch1));
				MOVSS(MDisp(dst_reg, m_dst_ofs), XMM0);
				m_dst_ofs += sizeof(float);
			}
			else
			{
				m_native_vtx_decl.texcoords[i].offset = m_dst_ofs;
				PXOR(XMM0, R(XMM0));
				CVTSI2SS(XMM0, R(scratch1));
				SHUFPS(XMM0, R(XMM0), 0x45);
				MOVUPS(MDisp(dst_reg, m_dst_ofs), XMM0);
				m_dst_ofs += sizeof(float) * 3;
			}
		}
	}

	// Prepare for the next vertex.
	ADD(64, R(dst_reg), Imm32(m_dst_ofs));
	const u8* cont = GetCodePtr();
	ADD(64, R(src_reg), Imm32(m_src_ofs));

	SUB(32, R(count_reg), Imm8(1));
	J_CC(CC_NZ, loop_start);

	// Get the original count.
	POP(32, R(ABI_RETURN));

	if (m_VtxDesc.Position & MASK_INDEXED)
	{
		SUB(32, R(ABI_RETURN), R(skipped_reg));
		RET();

		SetJumpTarget(m_skip_vertex);
		ADD(32, R(skipped_reg), Imm8(1));
		JMP(cont);
	}
	else
	{
		RET();
	}

	m_VertexSize = m_src_ofs;
	m_native_vtx_decl.stride = m_dst_ofs;
}

bool VertexLoaderX64::IsInitialized()
{
	// Uses PSHUFB.
	return cpu_info.bSSSE3;
}

int VertexLoaderX64::RunVertices(int primitive, int count, DataReader src, DataReader dst)
{
	m_numLoadedVertices += count;
	return ((int (*)(u8* src, u8* dst, int count))region)(src.GetPointer(), dst.GetPointer(), count);
}