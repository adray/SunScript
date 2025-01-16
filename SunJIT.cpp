#include "SunJIT.h"
#include "SunScript.h"
#include <vector>
#include <string>
#include <stack>
#include <iostream>
#include <unordered_map>
#include <memory>
#include <assert.h>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <sstream>

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

using namespace SunScript;

//================

enum vm_register
{
    VM_REGISTER_EAX = 0x0,
    VM_REGISTER_ECX = 0x1,
    VM_REGISTER_EDX = 0x2,
    VM_REGISTER_EBX = 0x3,
    VM_REGISTER_ESP = 0x4,
    VM_REGISTER_EBP = 0x5,
    VM_REGISTER_ESI = 0x6,
    VM_REGISTER_EDI = 0x7,
    VM_REGISTER_R8 = 0x8,
    VM_REGISTER_R9 = 0x9,
    VM_REGISTER_R10 = 0xa,
    VM_REGISTER_R11 = 0xb,
    VM_REGISTER_R12 = 0xc,
    VM_REGISTER_R13 = 0xd,
    VM_REGISTER_R14 = 0xe,
    VM_REGISTER_R15 = 0xf,

    VM_REGISTER_MAX = 0x10
};

enum vm_sse_register
{
    VM_SSE_REGISTER_XMM0 = 0x0,
    VM_SSE_REGISTER_XMM1 = 0x1,
    VM_SSE_REGISTER_XMM2 = 0x2,
    VM_SSE_REGISTER_XMM3 = 0x3,
    VM_SSE_REGISTER_XMM4 = 0x4,
    VM_SSE_REGISTER_XMM5 = 0x5,
    VM_SSE_REGISTER_XMM6 = 0x6,
    VM_SSE_REGISTER_XMM7 = 0x7,
    VM_SSE_REGISTER_XMM8 = 0x8,
    VM_SSE_REGISTER_XMM9 = 0x9,
    VM_SSE_REGISTER_XMM10 = 0xa,
    VM_SSE_REGISTER_XMM11 = 0xb,
    VM_SSE_REGISTER_XMM12 = 0xc,
    VM_SSE_REGISTER_XMM13 = 0xd,
    VM_SSE_REGISTER_XMM14 = 0xe,
    VM_SSE_REGISTER_XMM15 = 0xf,

    VM_SSE_REGISTER_MAX = 0x10
};

#if WIN32
#define VM_ARG1 VM_REGISTER_ECX
#define VM_ARG2 VM_REGISTER_EDX
#define VM_ARG3 VM_REGISTER_R8
#define VM_ARG4 VM_REGISTER_R9
#define VM_ARG5 (-1)
#define VM_ARG6 (-1)
#define VM_SSE_ARG1 VM_SSE_REGISTER_XMM0
#define VM_SSE_ARG2 VM_SSE_REGISTER_XMM1
#define VM_SSE_ARG3 VM_SSE_REGISTER_XMM2
#define VM_SSE_ARG4 VM_SSE_REGISTER_XMM3
#define VM_SSE_ARG5 (-1)
#define VM_SSE_ARG6 (-1)
#define VM_SSE_ARG7 (-1)
#define VM_SSE_ARG8 (-1)
#define VM_MAX_ARGS 4
#define VM_MAX_SSE_ARGS 4
#else
#define VM_ARG1 VM_REGISTER_EDI
#define VM_ARG2 VM_REGISTER_ESI
#define VM_ARG3 VM_REGISTER_EDX
#define VM_ARG4 VM_REGISTER_ECX
#define VM_ARG5 VM_REGISTER_R8
#define VM_ARG6 VM_REGISTER_R9
#define VM_SSE_ARG1 VM_SSE_REGISTER_XMM0
#define VM_SSE_ARG2 VM_SSE_REGISTER_XMM1
#define VM_SSE_ARG3 VM_SSE_REGISTER_XMM2
#define VM_SSE_ARG4 VM_SSE_REGISTER_XMM3
#define VM_SSE_ARG5 VM_SSE_REGISTER_XMM4
#define VM_SSE_ARG6 VM_SSE_REGISTER_XMM5
#define VM_SSE_ARG7 VM_SSE_REGISTER_XMM6
#define VM_SSE_ARG8 VM_SSE_REGISTER_XMM7
#define VM_MAX_ARGS 6
#define VM_MAX_SSE_ARGS 8
#endif

enum vm_instruction_type
{
    VM_INSTRUCTION_NONE = 0x0,
    VM_INSTRUCTION_UNARY = 0x1,
    VM_INSTRUCTION_BINARY = 0x2
};

enum vm_instruction_code
{
    VM_INSTRUCTION_CODE_NONE = 0x0,
    VM_INSTRUCTION_CODE_DST_REGISTER = 0x1,
    VM_INSTRUCTION_CODE_DST_MEMORY = 0x2,
    VM_INSTRUCTION_CODE_IMMEDIATE = 0x4,
    VM_INSTRUCTION_CODE_SRC_REGISTER = 0x8,
    VM_INSTRUCTION_CODE_SRC_MEMORY = 0x10,
    VM_INSTRUCTION_CODE_OFFSET = 0x20
};

#define CODE_NONE (VM_INSTRUCTION_CODE_NONE)
#define CODE_UR (VM_INSTRUCTION_CODE_DST_REGISTER)
#define CODE_UM (VM_INSTRUCTION_CODE_DST_MEMORY)
#define CODE_UMO (VM_INSTRUCTION_CODE_DST_MEMORY | VM_INSTRUCTION_CODE_OFFSET)
#define CODE_UI (VM_INSTRUCTION_CODE_IMMEDIATE)
#define CODE_BRR (VM_INSTRUCTION_CODE_DST_REGISTER | VM_INSTRUCTION_CODE_SRC_REGISTER)
#define CODE_BRM (VM_INSTRUCTION_CODE_DST_REGISTER | VM_INSTRUCTION_CODE_SRC_MEMORY)
#define CODE_BMR (VM_INSTRUCTION_CODE_DST_MEMORY | VM_INSTRUCTION_CODE_SRC_REGISTER)
#define CODE_BMRO (VM_INSTRUCTION_CODE_DST_MEMORY | VM_INSTRUCTION_CODE_SRC_REGISTER | VM_INSTRUCTION_CODE_OFFSET)
#define CODE_BRMO (VM_INSTRUCTION_CODE_SRC_MEMORY | VM_INSTRUCTION_CODE_DST_REGISTER | VM_INSTRUCTION_CODE_OFFSET)
#define CODE_BRI (VM_INSTRUCTION_CODE_DST_REGISTER | VM_INSTRUCTION_CODE_IMMEDIATE)

enum vm_instruction_encoding
{
    VMI_ENC_I = 0,      // destination R, source immediate
    VMI_ENC_MR = 1,     // destination R/M, source R
    VMI_ENC_RM = 2,     // destination R, source R/M
    VMI_ENC_MI = 3,     // destination M, source immediate
    VMI_ENC_M = 4,      // destination and source R/M
    VMI_ENC_OI = 5,     // destination opcode rd (w), source immediate
    VMI_ENC_D = 6,      // jump instruction
    VMI_ENC_A = 7,      // destination R, source R/M
    VMI_ENC_C = 8       // destination R/M, source R
};

enum vm_instructions
{
    VMI_ADD64_SRC_REG_DST_REG,
    VMI_ADD64_SRC_IMM_DST_REG,
    VMI_ADD64_SRC_MEM_DST_REG,
    VMI_ADD64_SRC_REG_DST_MEM,

    VMI_SUB64_SRC_REG_DST_REG,
    VMI_SUB64_SRC_IMM_DST_REG,
    VMI_SUB64_SRC_MEM_DST_REG,
    VMI_SUB64_SRC_REG_DST_MEM,

    VMI_MOV64_SRC_REG_DST_REG,
    VMI_MOV64_SRC_REG_DST_MEM,
    VMI_MOV64_SRC_MEM_DST_REG,
    VMI_MOV64_SRC_IMM_DST_REG,

    VMI_MOV32_SRC_IMM_DST_REG,

    VMI_MUL64_SRC_REG_DST_REG,
    VMI_MUL64_SRC_MEM_DST_REG,

    VMI_INC64_DST_MEM,
    VMI_INC64_DST_REG,

    VMI_DEC64_DST_MEM,
    VMI_DEC64_DST_REG,

    VMI_NEAR_RETURN,
    VMI_FAR_RETURN,

    VMI_CMP64_SRC_REG_DST_REG,
    VMI_CMP64_SRC_REG_DST_MEM,
    VMI_CMP64_SRC_MEM_DST_REG,

    VMI_J8,
    VMI_JE8,
    VMI_JNE8,
    VMI_JL8,
    VMI_JG8,
    VMI_JLE8,
    VMI_JGE8,
    VMI_JA64,

    VMI_J32,
    VMI_JE32,
    VMI_JNE32,
    VMI_JL32,
    VMI_JG32,
    VMI_JLE32,
    VMI_JGE32,

    VMI_NEG64_DST_MEM,
    VMI_NEG64_DST_REG,
    VMI_IDIV_SRC_REG,
    VMI_IDIV_SRC_MEM,

    // End of instructions
    VMI_MAX_INSTRUCTIONS
};

enum vm_sse_instructions
{
    VMI_SSE_MOVSD_SRC_REG_DST_REG,
    VMI_SSE_MOVSD_SRC_REG_DST_MEM,
    VMI_SSE_MOVSD_SRC_MEM_DST_REG,

    VMI_SSE_ADDPD_SRC_REG_DST_REG,

    VMI_SSE_ADDSD_SRC_REG_DST_REG,
    VMI_SSE_ADDSD_SRC_MEM_DST_REG,

    VMI_SSE_SUBSD_SRC_REG_DST_REG,
    VMI_SSE_SUBSD_SRC_MEM_DST_REG,

    VMI_SSE_MULSD_SRC_REG_DST_REG,
    VMI_SSE_MULSD_SRC_MEM_DST_REG,

    VMI_SSE_DIVSD_SRC_REG_DST_REG,
    VMI_SSE_DIVSD_SRC_MEM_DST_REG,

    VMI_SSE_CVTSI2SD_SRC_REG_DST_REG,
    VMI_SSE_CVTSI2SD_SRC_MEM_DST_REG,

    VMI_SSE_UCOMISD_SRC_REG_DST_REG,
    VMI_SSE_UCOMISD_SRC_MEM_DST_REG,

    VMI_SSE_XORPD_SRC_REG_DST_REG,

    VMI_SSE_MOVSS_SRC_REG_DST_REG,
    VMI_SSE_MOVSS_SRC_REG_DST_MEM,
    VMI_SSE_MOVSS_SRC_MEM_DST_REG,

    VMI_SSE_ADDSS_SRC_REG_DST_REG,
    VMI_SSE_ADDSS_SRC_MEM_DST_REG,

    VMI_SSE_SUBSS_SRC_REG_DST_REG,
    VMI_SSE_SUBSS_SRC_MEM_DST_REG,

    VMI_SSE_MULSS_SRC_REG_DST_REG,
    VMI_SSE_MULSS_SRC_MEM_DST_REG,

    VMI_SSE_DIVSS_SRC_REG_DST_REG,
    VMI_SSE_DIVSS_SRC_MEM_DST_REG,

    VMI_SSE_CVTSI2SS_SRC_REG_DST_REG,
    VMI_SSE_CVTSI2SS_SRC_MEM_DST_REG,

    VMI_SSE_UCOMISS_SRC_REG_DST_REG,
    VMI_SSE_UCOMISS_SRC_MEM_DST_REG,

    VMI_SSE_XORPS_SRC_REG_DST_REG,

    // End of instructions
    VMI_SSE_MAX_INSTRUCTIONS
};

struct vm_instruction
{
    unsigned char rex;
    unsigned char ins;
    unsigned char subins;
    unsigned char type;
    unsigned char code;
    unsigned char enc;
};

struct vm_sse_instruction
{
    unsigned char rex;
    unsigned char ins1;
    unsigned char ins2;
    unsigned char ins3;
    unsigned char type;
    unsigned char code;
    unsigned char enc;
};

#define INS(rex, ins, subins, type, code, enc) \
    { (unsigned char)rex, (unsigned char)ins, (unsigned char)subins, (unsigned char)type, (unsigned char)code, (unsigned char)enc }

#define SSE_INS(rex, ins1, ins2, ins3, type, code, enc) \
    { (unsigned char)rex, (unsigned char)ins1, (unsigned char)ins2, (unsigned char)ins3, (unsigned char)type, (unsigned char)code, (unsigned char)enc }

#define VMI_UNUSED 0xFF

static constexpr vm_instruction gInstructions[VMI_MAX_INSTRUCTIONS] = {
    INS(0x48, 0x1, VMI_UNUSED, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_MR),     // VMI_ADD64_SRC_REG_DST_REG
    INS(0x48, 0x81, 0x0, VM_INSTRUCTION_BINARY, CODE_BRI, VMI_ENC_MI),    // VMI_ADD64_SRC_IMM_DST_REG
    INS(0x48, 0x3, VMI_UNUSED, VM_INSTRUCTION_BINARY, CODE_BRMO, VMI_ENC_RM),    // VMI_ADD64_SRC_MEM_DST_REG
    INS(0x48, 0x1, VMI_UNUSED, VM_INSTRUCTION_BINARY, CODE_BMRO, VMI_ENC_MR),    // VMI_ADD64_SRC_REG_DST_MEM

    INS(0x48, 0x29, VMI_UNUSED, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_MR),    // VMI_SUB64_SRC_REG_DST_REG
    INS(0x48, 0x81, 5, VM_INSTRUCTION_BINARY, CODE_BRI, VMI_ENC_MI),    // VMI_SUB64_SRC_IMM_DST_REG
    INS(0x48, 0x2B, VMI_UNUSED, VM_INSTRUCTION_BINARY, CODE_BRMO, VMI_ENC_RM),   // VMI_SUB64_SRC_MEM_DST_REG
    INS(0x48, 0x29, VMI_UNUSED, VM_INSTRUCTION_BINARY, CODE_BMRO, VMI_ENC_MR),   // VMI_SUB64_SRC_REG_DST_MEM

    INS(0x48, 0x89, VMI_UNUSED, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_MR),    // VMI_MOV64_SRC_REG_DST_REG
    INS(0x48, 0x89, VMI_UNUSED, VM_INSTRUCTION_BINARY, CODE_BMRO, VMI_ENC_MR),   // VMI_MOV64_SRC_REG_DST_MEM
    INS(0x48, 0x8B, VMI_UNUSED, VM_INSTRUCTION_BINARY, CODE_BRMO, VMI_ENC_RM),   // VMI_MOV64_SRC_MEM_DST_REG
    INS(0x48, 0xC7, 0x0, VM_INSTRUCTION_BINARY, CODE_BRI, VMI_ENC_MI),    // VMI_MOV64_SRC_IMM_DST_REG

    INS(0x0, 0xB8, VMI_UNUSED, VM_INSTRUCTION_BINARY, CODE_BRI, VMI_ENC_OI),    // VMI_MOV32_SRC_IMM_DST_REG

    INS(0x48, 0x0F, 0xAF, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_RM), // VMI_MUL64_SRC_REG_DST_REG
    INS(0x48, 0x0F, 0xAF, VM_INSTRUCTION_BINARY, CODE_BRMO, VMI_ENC_RM), // VMI_MUL64_SRC_MEM_DST_REG

    INS(0x48, 0xFF, 0x0, VM_INSTRUCTION_UNARY, CODE_UMO, VMI_ENC_M),    // VMI_INC64_DST_MEM
    INS(0x48, 0xFF, 0x0, VM_INSTRUCTION_UNARY, CODE_UR, VMI_ENC_M),    // VMI_INC64_DST_REG

    INS(0x48, 0xFF, 0x1, VM_INSTRUCTION_UNARY, CODE_UMO, VMI_ENC_M),    // VMI_DEC64_DST_MEM
    INS(0x48, 0xFF, 0x1, VM_INSTRUCTION_UNARY, CODE_UR, VMI_ENC_M),    // VMI_DEC64_DST_REG

    INS(0x0, 0xC3, VMI_UNUSED, VM_INSTRUCTION_NONE, CODE_NONE, 0x0),           // VMI_NEAR_RETURN
    INS(0x0, 0xCB, VMI_UNUSED, VM_INSTRUCTION_NONE, CODE_NONE, 0x0),           // VMI_FAR_RETURN

    INS(0x48, 0x3B, VMI_UNUSED, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_RM), // VMI_CMP64_SRC_REG_DST_REG
    INS(0x48, 0x39, VMI_UNUSED, VM_INSTRUCTION_BINARY, CODE_BMRO, VMI_ENC_MR), // VMI_CMP64_SRC_REG_DST_MEM
    INS(0x48, 0x3B, VMI_UNUSED, VM_INSTRUCTION_BINARY, CODE_BRMO, VMI_ENC_RM), // VMI_CMP64_SRC_MEM_DST_REG

    INS(0x0, 0xEB, VMI_UNUSED, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_J8
    INS(0x0, 0x74, VMI_UNUSED, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_JE8
    INS(0x0, 0x75, VMI_UNUSED, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_JNE8
    INS(0x0, 0x72, VMI_UNUSED, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_JL8,
    INS(0x0, 0x77, VMI_UNUSED, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_JG8,
    INS(0x0, 0x76, VMI_UNUSED, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_JLE8,
    INS(0x0, 0x73, VMI_UNUSED, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_JGE8,
    INS(0x0, 0xFF, 0x4, VM_INSTRUCTION_CODE_OFFSET, CODE_UR, VMI_ENC_M), // VMI_JA64

    INS(0x0, 0xE9, VMI_UNUSED, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_J32
    INS(0x0, 0x0F, 0x84, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_JE32
    INS(0x0, 0x0F, 0x85, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_JNE32
    INS(0x0, 0x0F, 0x82, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_JL32,
    INS(0x0, 0x0F, 0x87, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_JG32,
    INS(0x0, 0x0F, 0x86, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_JLE32,
    INS(0x0, 0x0F, 0x83, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_JGE32,

    INS(0x48, 0xF7, 0x3, VM_INSTRUCTION_UNARY, CODE_UMO, VMI_ENC_M),     // VMI_NEG64_DST_MEM
    INS(0x48, 0xF7, 0x3, VM_INSTRUCTION_UNARY, CODE_UR, VMI_ENC_M),     // VMI_NEG64_DST_REG

    INS(0x48, 0xF7, 0x7, VM_INSTRUCTION_UNARY, CODE_UR, VMI_ENC_M),     // VMI_IDIV_SRC_REG
    INS(0x48, 0xF7, 0x7, VM_INSTRUCTION_UNARY, CODE_UMO, VMI_ENC_M),     // VMI_IDIV_SRC_MEM
};

static constexpr vm_sse_instruction gInstructions_SSE[VMI_SSE_MAX_INSTRUCTIONS] = {
    SSE_INS(0x0, 0xF2, 0xF, 0x10, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_A), // VMI_SSE_MOVSD_SRC_REG_DST_REG
    SSE_INS(0x0, 0xF2, 0xF, 0x11, VM_INSTRUCTION_BINARY, CODE_BMRO, VMI_ENC_C), // VMI_SSE_MOVSD_SRC_REG_DST_MEM
    SSE_INS(0x0, 0xF2, 0xF, 0x10, VM_INSTRUCTION_BINARY, CODE_BRMO, VMI_ENC_A), // VMI_SSE_MOVSD_SRC_MEM_DST_REG

    SSE_INS(0x0, 0x66, 0xF, 0x58, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_A), // VMI_SSE_ADDPD_SRC_REG_DST_REG


    SSE_INS(0x0, 0xF2, 0xF, 0x58, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_A), // VMI_SSE_ADDSD_SRC_REG_DST_REG
    SSE_INS(0x0, 0xF2, 0xF, 0x58, VM_INSTRUCTION_BINARY, CODE_BMRO, VMI_ENC_A), // VMI_SSE_ADDSD_SRC_MEM_DST_REG

    SSE_INS(0x0, 0xF2, 0xF, 0x5C, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_A), // VMI_SSE_SUBSD_SRC_REG_DST_REG
    SSE_INS(0x0, 0xF2, 0xF, 0x5C, VM_INSTRUCTION_BINARY, CODE_BMRO, VMI_ENC_A), // VMI_SSE_SUBSD_SRC_MEM_DST_REG

    SSE_INS(0x0, 0xF2, 0xF, 0x59, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_A), // VMI_SSE_MULSD_SRC_REG_DST_REG
    SSE_INS(0x0, 0xF2, 0xF, 0x59, VM_INSTRUCTION_BINARY, CODE_BMRO, VMI_ENC_A), // VMI_SSE_MULSD_SRC_MEM_DST_REG

    SSE_INS(0x0, 0xF2, 0xF, 0x5E, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_A), // VMI_SSE_DIVSD_SRC_REG_DST_REG
    SSE_INS(0x0, 0xF2, 0xF, 0x5E, VM_INSTRUCTION_BINARY, CODE_BMRO, VMI_ENC_A), // VMI_SSE_DIVSD_SRC_MEM_DST_REG

    SSE_INS(0x48, 0xF2, 0xF, 0x2A, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_A), // VMI_SSE_CVTSI2SD_SRC_REG_DST_REG
    SSE_INS(0x48, 0xF2, 0xF, 0x2A, VM_INSTRUCTION_BINARY, CODE_BMRO, VMI_ENC_A), // VMI_SSE_CVTSI2SD_SRC_MEM_DST_REG

    SSE_INS(0x0, 0x66, 0xF, 0x2E, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_A),   // VMI_SSE_UCOMISD_SRC_REG_DST_REG,
    SSE_INS(0x0, 0x66, 0xF, 0x2E, VM_INSTRUCTION_BINARY, CODE_BMRO, VMI_ENC_A),    // VMI_SSE_UCOMISD_SRC_MEM_DST_REG,

    SSE_INS(0x0, 0x66, 0xF, 0x57, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_A), // VMI_SSE_XORPD_SRC_REG_DST_REG

    SSE_INS(0x0, 0xF3, 0xF, 0x10, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_A), // VMI_SSE_MOVSS_SRC_REG_DST_REG
    SSE_INS(0x0, 0xF3, 0xF, 0x11, VM_INSTRUCTION_BINARY, CODE_BMRO, VMI_ENC_C), // VMI_SSE_MOVSS_SRC_REG_DST_MEM
    SSE_INS(0x0, 0xF3, 0xF, 0x10, VM_INSTRUCTION_BINARY, CODE_BRMO, VMI_ENC_A), // VMI_SSE_MOVSS_SRC_MEM_DST_REG

    SSE_INS(0x0, 0xF3, 0xF, 0x58, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_A), // VMI_SSE_ADDSS_SRC_REG_DST_REG
    SSE_INS(0x0, 0xF3, 0xF, 0x58, VM_INSTRUCTION_BINARY, CODE_BRMO, VMI_ENC_A), // VMI_SSE_ADDSS_SRC_MEM_DST_REG

    SSE_INS(0x0, 0xF3, 0xF, 0x5C, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_A), // VMI_SSE_SUBSS_SRC_REG_DST_REG
    SSE_INS(0x0, 0xF3, 0xF, 0x5C, VM_INSTRUCTION_BINARY, CODE_BRMO, VMI_ENC_A), // VMI_SSE_SUBSS_SRC_MEM_DST_REG

    SSE_INS(0x0, 0xF3, 0xF, 0x59, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_A), // VMI_SSE_MULSS_SRC_REG_DST_REG
    SSE_INS(0x0, 0xF3, 0xF, 0x59, VM_INSTRUCTION_BINARY, CODE_BRMO, VMI_ENC_A), // VMI_SSE_MULSS_SRC_MEM_DST_REG

    SSE_INS(0x0, 0xF3, 0xF, 0x5E, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_A), // VMI_SSE_DIVSS_SRC_REG_DST_REG
    SSE_INS(0x0, 0xF3, 0xF, 0x5E, VM_INSTRUCTION_BINARY, CODE_BRMO, VMI_ENC_A), // VMI_SSE_DIVSS_SRC_MEM_DST_REG

    SSE_INS(0x0, 0xF3, 0xF, 0x2A, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_A), // VMI_SSE_CVTSI2SS_SRC_REG_DST_REG
    SSE_INS(0x0, 0xF3, 0xF, 0x2A, VM_INSTRUCTION_BINARY, CODE_BRMO, VMI_ENC_A), // VMI_SSE_CVTSI2SS_SRC_MEM_DST_REG

    SSE_INS(0x0, VMI_UNUSED, 0xF, 0x2E, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_A), // VMI_SSE_UCOMISS_SRC_REG_DST_REG
    SSE_INS(0x0, VMI_UNUSED, 0xF, 0x2E, VM_INSTRUCTION_BINARY, CODE_BRMO, VMI_ENC_A), // VMI_SSE_UCOMISS_SRC_MEM_DST_REG

    SSE_INS(0x0, VMI_UNUSED, 0xF, 0x57, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_A), // VMI_SSE_XORPS_SRC_REG_DST_REG

};

//=====================
// R/M 0-2
// Op/Reg 3-5
// Mod 6-8
//=====================

static void vm_emit(const vm_instruction& ins, unsigned char* program, int& count)
{
    assert(ins.code == CODE_NONE);
    if (ins.rex > 0) { program[count++] = ins.rex; }
    program[count++] = ins.ins;
}

static void vm_emit_ur(const vm_instruction& ins, unsigned char* program, int& count, char reg)
{
    assert(ins.code == CODE_UR);
    if (ins.rex > 0)
    {
        program[count++] = ins.rex | (reg >= VM_REGISTER_R8 ? 0x1 : 0x0);
    }
    if (ins.subins != VMI_UNUSED)
    {
        program[count++] = ins.ins;
        program[count++] = (ins.subins << 3) | ((reg % 8) & 0x7) | (0x3 << 6);
    }
    else
    {
        program[count++] = ins.ins | ((reg % 8) & 0x7);
    }
}

static void vm_emit_um(const vm_instruction& ins, unsigned char* program, int& count, char reg)
{
    assert(ins.code == CODE_UM);
    if (ins.rex > 0) { program[count++] = ins.rex; }
    program[count++] = ins.ins;

    if (reg == VM_REGISTER_ESP)
    {
        program[count++] = ((ins.subins & 0x7) << 3) | 0x4 | (0x0 << 6);
        program[count++] = 0x24; // SIB byte
    }
    else
    {
        program[count++] = ((ins.subins & 0x7) << 3) | (0x0 << 6) | (reg & 0x7);
    }
}

static void vm_emit_umo(const vm_instruction& ins, unsigned char* program, int& count, char reg, int offset)
{
    assert(ins.code == CODE_UMO);
    if (ins.rex > 0) { program[count++] = ins.rex; }
    program[count++] = ins.ins;

    if (reg == VM_REGISTER_ESP)
    {
        program[count++] = ((ins.subins & 0x7) << 3) | 0x4 | (0x2 << 6);
        program[count++] = 0x24; // SIB byte
        program[count++] = (unsigned char)(offset & 0xff);
        program[count++] = (unsigned char)((offset >> 8) & 0xff);
        program[count++] = (unsigned char)((offset >> 16) & 0xff);
        program[count++] = (unsigned char)((offset >> 24) & 0xff);
    }
    else
    {
        program[count++] = ((ins.subins & 0x7) << 3) | (0x2 << 6) | (reg & 0x7);
        program[count++] = (unsigned char)(offset & 0xff);
        program[count++] = (unsigned char)((offset >> 8) & 0xff);
        program[count++] = (unsigned char)((offset >> 16) & 0xff);
        program[count++] = (unsigned char)((offset >> 24) & 0xff);
    }
}

static void vm_emit_bri(const vm_instruction& ins, unsigned char* program, int& count, char reg, int imm)
{
    assert(ins.code == CODE_BRI);
    if (ins.rex > 0)
    {
        program[count++] = ins.rex | (reg >= VM_REGISTER_R8 ? 0x1 : 0x0);
        program[count++] = ins.ins;
        program[count++] = (ins.subins << 3) | (reg % 8) | (0x3 << 6);
    }
    else
    {
        program[count++] = ins.ins | reg;
    }

    program[count++] = (unsigned char)(imm & 0xff);
    program[count++] = (unsigned char)((imm >> 8) & 0xff);
    program[count++] = (unsigned char)((imm >> 16) & 0xff);
    program[count++] = (unsigned char)((imm >> 24) & 0xff);
}

static void vm_emit_brr(const vm_instruction& ins, unsigned char* program, int& count, char dst, char src)
{
    assert(ins.code == CODE_BRR);

    if (ins.enc == VMI_ENC_MR)
    {
        if (ins.rex > 0) { program[count++] = ins.rex | (dst >= VM_REGISTER_R8 ? 0x1 : 0x0) | (src >= VM_REGISTER_R8 ? 0x4 : 0x0); }
        program[count++] = ins.ins;
        if (ins.subins != VMI_UNUSED) { program[count++] = ins.subins; }

        program[count++] = 0xC0 | (((src % 8) & 0x7) << 0x3) | (((dst % 8) & 0x7) << 0);
    }
    else if (ins.enc == VMI_ENC_RM)
    {
        if (ins.rex > 0) { program[count++] = ins.rex | (dst >= VM_REGISTER_R8 ? 0x4 : 0x0) | (src >= VM_REGISTER_R8 ? 0x1 : 0x0); }
        program[count++] = ins.ins;
        if (ins.subins != VMI_UNUSED) { program[count++] = ins.subins; }

        program[count++] = 0xC0 | (((dst % 8) & 0x7) << 0x3) | (((src % 8) & 0x7) << 0);
    }
}

static void vm_emit_brm(const vm_instruction& ins, unsigned char* program, int& count, char dst, char src)
{
    assert(ins.code == CODE_BRM);
    if (ins.rex > 0) { program[count++] = ins.rex; }
    program[count++] = ins.ins;

    if (src == VM_REGISTER_ESP)
    {
        program[count++] = ((dst & 0x7) << 3) | 0x4 | (0x0 << 6);
        program[count++] = 0x24; // SIB byte
    }
    else
    {
        program[count++] = ((dst & 0x7) << 3) | (0x0 << 6) | (src & 0x7);
    }
}

static void vm_emit_bmr(const vm_instruction& ins, unsigned char* program, int& count, char dst, char src)
{
    assert(ins.code == CODE_BMR);
    if (ins.rex > 0) { program[count++] = ins.rex; }
    program[count++] = ins.ins;

    if (dst == VM_REGISTER_ESP)
    {
        program[count++] = ((src & 0x7) << 3) | 0x4 | (0x0 << 6);
        program[count++] = 0x24; // SIB byte
    }
    else
    {
        program[count++] = ((src & 0x7) << 3) | (0x0 << 6) | (dst & 0x7);
    }
}

static void vm_emit_brmo(const vm_instruction& ins, unsigned char* program, int& count, char dst, char src, int offset)
{
    assert(ins.code == CODE_BRMO);
    if (ins.rex > 0)
    {
        program[count++] = ins.rex | (dst >= VM_REGISTER_R8 ? 0x4 : 0x0) | (src >= VM_REGISTER_R8 ? 0x1 : 0x0);
    }
    program[count++] = ins.ins;
    if (ins.subins != VMI_UNUSED) { program[count++] = ins.subins; }

    if (src == VM_REGISTER_ESP)
    {
        program[count++] = (((dst % 8) & 0x7) << 3) | 0x4 | (0x2 << 6);
        program[count++] = 0x24; // SIB byte
        program[count++] = (unsigned char)(offset & 0xff);
        program[count++] = (unsigned char)((offset >> 8) & 0xff);
        program[count++] = (unsigned char)((offset >> 16) & 0xff);
        program[count++] = (unsigned char)((offset >> 24) & 0xff);
    }
    else
    {
        program[count++] = (((dst % 8) & 0x7) << 3) | (0x2 << 6) | ((src % 8) & 0x7);
        program[count++] = (unsigned char)(offset & 0xff);
        program[count++] = (unsigned char)((offset >> 8) & 0xff);
        program[count++] = (unsigned char)((offset >> 16) & 0xff);
        program[count++] = (unsigned char)((offset >> 24) & 0xff);
    }
}

static void vm_emit_bmro(const vm_instruction& ins, unsigned char* program, int& count, char dst, char src, int offset)
{
    assert(ins.code == CODE_BMRO);
    if (ins.rex > 0)
    {
        program[count++] = ins.rex | (dst >= VM_REGISTER_R8 ? 0x1 : 0x0) | (src >= VM_REGISTER_R8 ? 0x4 : 0x0);
    }
    program[count++] = ins.ins;

    if (dst == VM_REGISTER_ESP)
    {
        program[count++] = ((src & 0x7) << 3) | 0x4 | (0x2 << 6);
        program[count++] = 0x24; // SIB byte
        program[count++] = (unsigned char)(offset & 0xff);
        program[count++] = (unsigned char)((offset >> 8) & 0xff);
        program[count++] = (unsigned char)((offset >> 16) & 0xff);
        program[count++] = (unsigned char)((offset >> 24) & 0xff);
    }
    else
    {
        program[count++] = (((src % 8) & 0x7) << 3) | (0x2 << 6) | ((dst % 8) & 0x7);
        program[count++] = (unsigned char)(offset & 0xff);
        program[count++] = (unsigned char)((offset >> 8) & 0xff);
        program[count++] = (unsigned char)((offset >> 16) & 0xff);
        program[count++] = (unsigned char)((offset >> 24) & 0xff);
    }
}

static void vm_emit_ui(const vm_instruction& ins, unsigned char* program, int& count, char imm)
{
    assert(ins.code == CODE_UI);
    if (ins.rex > 0) { program[count++] = ins.rex; }
    program[count++] = ins.ins;
    program[count++] = imm;
}

static void vm_emit_ui(const vm_instruction& ins, unsigned char* program, int& count, int imm)
{
    assert(ins.code == CODE_UI);
    if (ins.rex > 0) { program[count++] = ins.rex; }
    program[count++] = ins.ins;
    if (ins.subins != VMI_UNUSED)
    {
        program[count++] = ins.subins;
    }
    program[count++] = imm & 0xFF;
    program[count++] = (imm >> 8) & 0xFF;
    program[count++] = (imm >> 16) & 0xFF;
    program[count++] = (imm >> 24) & 0xFF;
}

//================

static void vm_emit_sse_brr(const vm_sse_instruction& ins, unsigned char* program, int& count, int src, int dst)
{
    assert(ins.code == CODE_BRR);
    if (ins.ins1 != VMI_UNUSED) {
        program[count++] = ins.ins1;
    }
    unsigned char rex = ins.rex;
    if (dst >= VM_SSE_REGISTER_XMM8) { rex |= 0x1 | (1 << 6); }
    if (src >= VM_SSE_REGISTER_XMM8) { rex |= 0x4 | (1 << 6); }

    if (rex > 0) { program[count++] = rex;  }
    program[count++] = ins.ins2;
    program[count++] = ins.ins3;

    if (ins.enc == VMI_ENC_A)
    {
        program[count++] = (((dst % 8) & 0x7) << 3) | (0x3 << 6) | ((src % 8) & 0x7);
    }
    else if (ins.enc == VMI_ENC_C)
    {
        program[count++] = (((src % 8) & 0x7) << 3) | (0x3 << 6) | ((dst % 8) & 0x7);
    }
}

static void vm_emit_sse_brmo(const vm_sse_instruction& ins, unsigned char* program, int& count, int src, int dst, int src_offset)
{
    assert(ins.code == CODE_BRMO);
    assert(ins.enc == VMI_ENC_A);
    if (ins.ins1 != VMI_UNUSED) {
        program[count++] = ins.ins1;
    }
    unsigned char rex = ins.rex;
    if (src >= VM_SSE_REGISTER_XMM8) { rex |= 0x1 | (1 << 6); }
    if (dst >= VM_REGISTER_R8) { rex |= 0x4 | (1 << 6); }

    if (rex > 0) { program[count++] = rex;  }

    program[count++] = ins.ins2;
    program[count++] = ins.ins3;

    if (src == VM_REGISTER_ESP)
    {
        program[count++] = (((dst % 8) & 0x7) << 3) | 0x4 | (0x2 << 6);
        program[count++] = 0x24; // SIB byte
        program[count++] = (unsigned char)(src_offset & 0xff);
        program[count++] = (unsigned char)((src_offset >> 8) & 0xff);
        program[count++] = (unsigned char)((src_offset >> 16) & 0xff);
        program[count++] = (unsigned char)((src_offset >> 24) & 0xff); 
    }
    else
    {
        program[count++] = (((dst % 8) & 0x7) << 3) | (0x2 << 6) | ((src % 8) & 0x7);
        program[count++] = src_offset & 0xFF;
        program[count++] = (src_offset >> 8) & 0xFF;
        program[count++] = (src_offset >> 16) & 0xFF;
        program[count++] = (src_offset >> 24) & 0xFF;
    }
}

static void vm_emit_sse_bmro(const vm_sse_instruction& ins, unsigned char* program, int& count, int src, int dst, int dst_offset)
{
    assert(ins.code == CODE_BMRO);
    assert(ins.enc == VMI_ENC_A || ins.enc == VMI_ENC_C);
    if (ins.ins1 != VMI_UNUSED) {
        program[count++] = ins.ins1;
    }
    unsigned char rex = ins.rex;

    if (dst >= VM_SSE_REGISTER_XMM8) { rex |= 0x1 | (1 << 6); }
    if (src >= VM_REGISTER_R8) { rex |= 0x4 | (1 << 6); }

    if (rex > 0) { program[count++] = rex;  }

    program[count++] = ins.ins2;
    program[count++] = ins.ins3;

    if (dst == VM_REGISTER_ESP)
    {
        program[count++] = (((src % 8) & 0x7) << 3) | 0x4 | (0x2 << 6);
        program[count++] = 0x24; // SIB byte
        program[count++] = (unsigned char)(dst_offset & 0xff);
        program[count++] = (unsigned char)((dst_offset >> 8) & 0xff);
        program[count++] = (unsigned char)((dst_offset >> 16) & 0xff);
        program[count++] = (unsigned char)((dst_offset >> 24) & 0xff);
    }
    else
    {
        program[count++] = (((src % 8) & 0x7) << 3) | (0x2 << 6) | ((dst % 8) & 0x7);
        program[count++] = dst_offset & 0xFF;
        program[count++] = (dst_offset >> 8) & 0xFF;
        program[count++] = (dst_offset >> 16) & 0xFF;
        program[count++] = (dst_offset >> 24) & 0xFF;
    }
}

static void vm_emit_sse_brm(const vm_sse_instruction& ins, unsigned char* program, int& count, int dst, int disp32)
{
    assert(ins.code == CODE_BRMO);
    assert(ins.enc == VMI_ENC_A);
    if (ins.ins1 != VMI_UNUSED) {
        program[count++] = ins.ins1;
    }
    unsigned char rex = ins.rex;

    if (dst >= VM_SSE_REGISTER_XMM8) { rex |= 0x4 | (1 << 6); }

    if (rex > 0) { program[count++] = rex;  }

    program[count++] = ins.ins2;
    program[count++] = ins.ins3;

    /* Relative RIP addressing (Displacement from end of instruction) */

    program[count++] = (((dst % 8) & 0x7) << 3) | 0x5 | (0x0 << 6);
    program[count++] = (unsigned char)(disp32 & 0xff);
    program[count++] = (unsigned char)((disp32 >> 8) & 0xff);
    program[count++] = (unsigned char)((disp32 >> 16) & 0xff);
    program[count++] = (unsigned char)((disp32 >> 24) & 0xff);
}

//================
#define VM_ALIGN_16(x) ((x + 0xf) & ~(0xf))

static void vm_reserve(unsigned char* program, int& count)
{
    program[count++] = 0x90; // nop
}

static void vm_reserve2(unsigned char* program, int& count)
{
    program[count++] = 0x90; // nop
    program[count++] = 0x90; // nop
}

static void vm_reserve3(unsigned char* program, int& count)
{
    program[count++] = 0x90; // nop
    program[count++] = 0x90; // nop
    program[count++] = 0x90; // nop
}

static void vm_cdq(unsigned char* program, int& count)
{
    program[count++] = 0x99;
}

static void vm_return(unsigned char* program, int& count)
{
    //vm_emit(table.ret, program, count);

    vm_emit(gInstructions[VMI_NEAR_RETURN], program, count);
}

static void vm_push_32(unsigned char* program, int& count, const int imm)
{
    program[count++] = 0x68;
    program[count++] = (unsigned char)(imm & 0xFF);
    program[count++] = (unsigned char)((imm >> 8) & 0xFF);
    program[count++] = (unsigned char)((imm >> 16) & 0xFF);
    program[count++] = (unsigned char)((imm >> 24) & 0xFF);
}

static void vm_push_reg(unsigned char* program, int& count, char reg)
{
    if (reg >= VM_REGISTER_R8)
    {
        program[count++] = 0x48 | 0x1;
    }

    program[count++] = 0x50 | ((reg % 8) & 0x7);
}

static void vm_pop_reg(unsigned char* program, int& count, char reg)
{
    if (reg >= VM_REGISTER_R8)
    {
        program[count++] = 0x48 | 0x1;
    }

    program[count++] = 0x58 | ((reg % 8) & 0x7);
}

static void vm_mov_imm_to_reg(unsigned char* program, int& count, char dst, int imm)
{
    vm_emit_bri(gInstructions[VMI_MOV32_SRC_IMM_DST_REG], program, count, dst, imm);
}

static void vm_mov_imm_to_reg_x64(unsigned char* program, int& count, char dst, long long imm)
{
    //vm_emit_bri(gInstructions[VMI_MOV64_SRC_IMM_DST_REG], program, count, dst, imm);

    program[count++] = 0x48 | (dst >= VM_REGISTER_R8 ? 0x1 : 0x0);
    program[count++] = 0xB8 | (dst % 8);
    program[count++] = (unsigned char)(imm & 0xff);
    program[count++] = (unsigned char)((imm >> 8) & 0xff);
    program[count++] = (unsigned char)((imm >> 16) & 0xff);
    program[count++] = (unsigned char)((imm >> 24) & 0xff);
    program[count++] = (unsigned char)((imm >> 32) & 0xff);
    program[count++] = (unsigned char)((imm >> 40) & 0xff);
    program[count++] = (unsigned char)((imm >> 48) & 0xff);
    program[count++] = (unsigned char)((imm >> 56) & 0xff);
}

inline static void vm_mov_reg_to_memory_x64(unsigned char* program, int& count, char dst, int dst_offset, char src)
{
    vm_emit_bmro(gInstructions[VMI_MOV64_SRC_REG_DST_MEM], program, count, dst, src, dst_offset);
}

inline static void vm_mov_memory_to_reg_x64(unsigned char* program, int& count, char dst, char src, int src_offset)
{
    vm_emit_brmo(gInstructions[VMI_MOV64_SRC_MEM_DST_REG], program, count, dst, src, src_offset);
}

inline static void vm_mov_reg_to_reg_x64(unsigned char* program, int& count, char dst, char src)
{
    vm_emit_brr(gInstructions[VMI_MOV64_SRC_REG_DST_REG], program, count, dst, src);
}

inline static void vm_add_reg_to_reg_x64(unsigned char* program, int& count, char dst, char src)
{
    vm_emit_brr(gInstructions[VMI_ADD64_SRC_REG_DST_REG], program, count, dst, src);
}

inline static void vm_add_memory_to_reg_x64(unsigned char* program, int& count, char dst, char src, int src_offset)
{
    vm_emit_brmo(gInstructions[VMI_ADD64_SRC_MEM_DST_REG], program, count, dst, src, src_offset);
}

inline static void vm_add_reg_to_memory_x64(unsigned char* program, int& count, char dst, char src, int dst_offset)
{
    vm_emit_bmro(gInstructions[VMI_ADD64_SRC_REG_DST_MEM], program, count, dst, src, dst_offset);
}

inline static void vm_add_imm_to_reg_x64(unsigned char* program, int& count, char dst, int imm)
{
    vm_emit_bri(gInstructions[VMI_ADD64_SRC_IMM_DST_REG], program, count, dst, imm);
}

inline static void vm_sub_reg_to_memory_x64(unsigned char* program, int& count, char dst, char src, int dst_offset)
{
    vm_emit_bmro(gInstructions[VMI_SUB64_SRC_REG_DST_MEM], program, count, dst, src, dst_offset);
}

inline static void vm_sub_reg_to_reg_x64(unsigned char* program, int& count, char dst, char src)
{
    vm_emit_brr(gInstructions[VMI_SUB64_SRC_REG_DST_REG], program, count, dst, src);
}

inline static void vm_sub_memory_to_reg_x64(unsigned char* program, int& count, char dst, char src, int src_offset)
{
    vm_emit_brmo(gInstructions[VMI_SUB64_SRC_MEM_DST_REG], program, count, dst, src, src_offset);
}

inline static void vm_sub_imm_to_reg_x64(unsigned char* program, int& count, char reg, int imm)
{
    vm_emit_bri(gInstructions[VMI_SUB64_SRC_IMM_DST_REG], program, count, reg, imm);
}

inline static void vm_mul_reg_to_reg_x64(unsigned char* program, int& count, char dst, char src)
{
    vm_emit_brr(gInstructions[VMI_MUL64_SRC_REG_DST_REG], program, count, dst, src);
}

inline static void vm_mul_memory_to_reg_x64(unsigned char* program, int& count, char dst, char src, int src_offset)
{
    vm_emit_brmo(gInstructions[VMI_MUL64_SRC_MEM_DST_REG], program, count, dst, src, src_offset);
}

inline static void vm_div_reg_x64(unsigned char* program, int& count, char reg)
{
    vm_emit_ur(gInstructions[VMI_IDIV_SRC_REG], program, count, reg);
}

inline static void vm_div_memory_x64(unsigned char* program, int& count, char src, int src_offset)
{
    vm_emit_umo(gInstructions[VMI_IDIV_SRC_MEM], program, count, src, src_offset);
}

inline static void vm_cmp_reg_to_reg_x64(unsigned char* program, int& count, char dst, char src)
{
    vm_emit_brr(gInstructions[VMI_CMP64_SRC_REG_DST_REG], program, count, dst, src);
}

inline static void vm_cmp_reg_to_memory_x64(unsigned char* program, int& count, char dst, int dst_offset, char src)
{
    vm_emit_bmro(gInstructions[VMI_CMP64_SRC_REG_DST_MEM], program, count, dst, src, dst_offset);
}

inline static void vm_cmp_memory_to_reg_x64(unsigned char* program, int& count, char dst, char src, int src_offset)
{
    vm_emit_brmo(gInstructions[VMI_CMP64_SRC_MEM_DST_REG], program, count, dst, src, src_offset);
}

inline static void vm_jump_unconditional_8(unsigned char* program, int& count, char imm)
{
    vm_emit_ui(gInstructions[VMI_J8], program, count, imm);
}

inline static void vm_jump_equals_8(unsigned char* program, int& count, char imm)
{
    vm_emit_ui(gInstructions[VMI_JE8], program, count, imm);
}

inline static void vm_jump_not_equals_8(unsigned char* program, int& count, char imm)
{
    vm_emit_ui(gInstructions[VMI_JNE8], program, count, imm);
}

inline static void vm_jump_less_8(unsigned char* program, int& count, char imm)
{
    vm_emit_ui(gInstructions[VMI_JL8], program, count, imm);
}

inline static void vm_jump_less_equal_8(unsigned char* program, int& count, char imm)
{
    vm_emit_ui(gInstructions[VMI_JLE8], program, count, imm);
}

inline static void vm_jump_greater_8(unsigned char* program, int& count, char imm)
{
    vm_emit_ui(gInstructions[VMI_JG8], program, count, imm);
}

inline static void vm_jump_greater_equal_8(unsigned char* program, int& count, char imm)
{
    vm_emit_ui(gInstructions[VMI_JGE8], program, count, imm);
}

inline static void vm_jump_unconditional(unsigned char* program, int& count, int imm)
{
    vm_emit_ui(gInstructions[VMI_J32], program, count, imm);
}

inline static void vm_jump_equals(unsigned char* program, int& count, int imm)
{
    vm_emit_ui(gInstructions[VMI_JE32], program, count, imm);
}

inline static void vm_jump_not_equals(unsigned char* program, int& count, int imm)
{
    vm_emit_ui(gInstructions[VMI_JNE32], program, count, imm);
}

inline static void vm_jump_less(unsigned char* program, int& count, int imm)
{
    vm_emit_ui(gInstructions[VMI_JL32], program, count, imm);
}

inline static void vm_jump_less_equal(unsigned char* program, int& count, int imm)
{
    vm_emit_ui(gInstructions[VMI_JLE32], program, count, imm);
}

inline static void vm_jump_greater(unsigned char* program, int& count, int imm)
{
    vm_emit_ui(gInstructions[VMI_JG32], program, count, imm);
}

inline static void vm_jump_greater_equal(unsigned char* program, int& count, int imm)
{
    vm_emit_ui(gInstructions[VMI_JGE32], program, count, imm);
}

inline static void vm_jump_absolute(unsigned char* program, int& count, char reg)
{
    vm_emit_ur(gInstructions[VMI_JA64], program, count, reg);
}

static void vm_call(unsigned char* program, int& count, int offset)
{
    offset -= 5;

    program[count++] = 0xE8;
    program[count++] = (unsigned char)(offset & 0xff);
    program[count++] = (unsigned char)((offset >> 8) & 0xff);
    program[count++] = (unsigned char)((offset >> 16) & 0xff);
    program[count++] = (unsigned char)((offset >> 24) & 0xff);
}

inline static void vm_call_absolute(unsigned char* program, int& count, int reg)
{
    if (reg >= VM_REGISTER_R8)
    {
        program[count++] = 0x1 | (0x1 << 6);
    }

    program[count++] = 0xFF;
    program[count++] = (0x2 << 3) | (reg % 8) | (0x3 << 6);
}

inline static void vm_inc_reg_x64(unsigned char* program, int& count, int reg)
{
    vm_emit_ur(gInstructions[VMI_INC64_DST_REG], program, count, reg);
}

inline static void vm_inc_memory_x64(unsigned char* program, int& count, int reg, int offset)
{
    vm_emit_umo(gInstructions[VMI_INC64_DST_MEM], program, count, reg, offset);
}

inline static void vm_dec_reg_x64(unsigned char* program, int& count, int reg)
{
    vm_emit_ur(gInstructions[VMI_DEC64_DST_REG], program, count, reg);
}

inline static void vm_dec_memory_x64(unsigned char* program, int& count, int reg, int offset)
{
    vm_emit_umo(gInstructions[VMI_DEC64_DST_MEM], program, count, reg, offset);
}

inline static void vm_neg_memory_x64(unsigned char* program, int& count, int reg, int offset)
{
    vm_emit_umo(gInstructions[VMI_NEG64_DST_MEM], program, count, reg, offset);
}

inline static void vm_neg_reg_x64(unsigned char* program, int& count, int reg)
{
    vm_emit_ur(gInstructions[VMI_NEG64_DST_REG], program, count, reg);
}

inline static void vm_movsd_reg_to_reg_x64(unsigned char* program, int& count, int dst, int src)
{
#ifdef USE_SUN_FLOAT
    vm_emit_sse_brr(gInstructions_SSE[VMI_SSE_MOVSS_SRC_REG_DST_REG], program, count, src, dst);
#else
    vm_emit_sse_brr(gInstructions_SSE[VMI_SSE_MOVSD_SRC_REG_DST_REG], program, count, src, dst);
#endif
}

inline static void vm_movsd_reg_to_memory_x64(unsigned char* program, int& count, int dst, int src, int dst_offset)
{
#ifdef USE_SUN_FLOAT
    vm_emit_sse_bmro(gInstructions_SSE[VMI_SSE_MOVSS_SRC_REG_DST_MEM], program, count, dst, src, dst_offset);
#else
    vm_emit_sse_bmro(gInstructions_SSE[VMI_SSE_MOVSD_SRC_REG_DST_MEM], program, count, dst, src, dst_offset);
#endif
}

inline static void vm_movsd_memory_to_reg_x64(unsigned char* program, int& count, int dst, int src, int src_offset)
{
#ifdef USE_SUN_FLOAT
    vm_emit_sse_brmo(gInstructions_SSE[VMI_SSE_MOVSS_SRC_MEM_DST_REG], program, count, src, dst, src_offset);
#else
    vm_emit_sse_brmo(gInstructions_SSE[VMI_SSE_MOVSD_SRC_MEM_DST_REG], program, count, src, dst, src_offset);
#endif
}

inline static void vm_movsd_memory_to_reg_x64(unsigned char* program, int& count, int dst, int addr)
{
#ifdef USE_SUN_FLOAT
    vm_emit_sse_brm(gInstructions_SSE[VMI_SSE_MOVSS_SRC_MEM_DST_REG], program, count, dst, addr);
#else
    vm_emit_sse_brm(gInstructions_SSE[VMI_SSE_MOVSD_SRC_MEM_DST_REG], program, count, dst, addr);
#endif
}

inline static void vm_addsd_reg_to_reg_x64(unsigned char* program, int& count, int dst, int src)
{
#ifdef USE_SUN_FLOAT
    vm_emit_sse_brr(gInstructions_SSE[VMI_SSE_ADDSS_SRC_REG_DST_REG], program, count, src, dst);
#else
    vm_emit_sse_brr(gInstructions_SSE[VMI_SSE_ADDSD_SRC_REG_DST_REG], program, count, src, dst);
#endif
}

inline static void vm_addsd_memory_to_reg_x64(unsigned char* program, int& count, int dst, int src, int src_offset)
{
#ifdef USE_SUN_FLOAT
    vm_emit_sse_brmo(gInstructions_SSE[VMI_SSE_ADDSS_SRC_MEM_DST_REG], program, count, src, dst, src_offset);
#else
    vm_emit_sse_brmo(gInstructions_SSE[VMI_SSE_ADDSD_SRC_MEM_DST_REG], program, count, src, dst, src_offset);
#endif
}

inline static void vm_subsd_reg_to_reg_x64(unsigned char* program, int& count, int dst, int src)
{
#ifdef USE_SUN_FLOAT
    vm_emit_sse_brr(gInstructions_SSE[VMI_SSE_SUBSS_SRC_REG_DST_REG], program, count, src, dst);
#else
    vm_emit_sse_brr(gInstructions_SSE[VMI_SSE_SUBSD_SRC_REG_DST_REG], program, count, src, dst);
#endif
}

inline static void vm_subsd_memory_to_reg_x64(unsigned char* program, int& count, int dst, int src, int src_offset)
{
#ifdef USE_SUN_FLOAT
    vm_emit_sse_brmo(gInstructions_SSE[VMI_SSE_SUBSS_SRC_MEM_DST_REG], program, count, src, dst, src_offset);
#else
    vm_emit_sse_brmo(gInstructions_SSE[VMI_SSE_SUBSD_SRC_MEM_DST_REG], program, count, src, dst, src_offset);
#endif
}

inline static void vm_mulsd_reg_to_reg_x64(unsigned char* program, int& count, int dst, int src)
{
#ifdef USE_SUN_FLOAT
    vm_emit_sse_brr(gInstructions_SSE[VMI_SSE_MULSS_SRC_REG_DST_REG], program, count, src, dst);
#else
    vm_emit_sse_brr(gInstructions_SSE[VMI_SSE_MULSD_SRC_REG_DST_REG], program, count, src, dst);
#endif
}

inline static void vm_mulsd_memory_to_reg_x64(unsigned char* program, int& count, int dst, int src, int dst_offset)
{
#ifdef USE_SUN_FLOAT
    vm_emit_sse_brmo(gInstructions_SSE[VMI_SSE_MULSS_SRC_MEM_DST_REG], program, count, src, dst, dst_offset);
#else
    vm_emit_sse_brmo(gInstructions_SSE[VMI_SSE_MULSD_SRC_MEM_DST_REG], program, count, src, dst, dst_offset);
#endif
}

inline static void vm_divsd_reg_to_reg_x64(unsigned char* program, int& count, int dst, int src)
{
#ifdef USE_SUN_FLOAT
    vm_emit_sse_brr(gInstructions_SSE[VMI_SSE_DIVSS_SRC_REG_DST_REG], program, count, src, dst);
#else
    vm_emit_sse_brr(gInstructions_SSE[VMI_SSE_DIVSD_SRC_REG_DST_REG], program, count, src, dst);
#endif
}

inline static void vm_divsd_memory_to_reg_x64(unsigned char* program, int& count, int dst, int src, int dst_offset)
{
#ifdef USE_SUN_FLOAT
    vm_emit_sse_brmo(gInstructions_SSE[VMI_SSE_DIVSS_SRC_MEM_DST_REG], program, count, src, dst, dst_offset);
#else
    vm_emit_sse_brmo(gInstructions_SSE[VMI_SSE_DIVSD_SRC_MEM_DST_REG], program, count, src, dst, dst_offset);
#endif
}

inline static void vm_cvtitod_memory_to_reg_x64(unsigned char* program, int& count, int dst, int src, int src_offset)
{
#ifdef USE_SUN_FLOAT
    vm_emit_sse_brmo(gInstructions_SSE[VMI_SSE_CVTSI2SS_SRC_MEM_DST_REG], program, count, src, dst, src_offset);
#else
    vm_emit_sse_brmo(gInstructions_SSE[VMI_SSE_CVTSI2SD_SRC_MEM_DST_REG], program, count, src, dst, src_offset);
#endif
}

inline static void vm_cvtitod_reg_to_reg_x64(unsigned char* program, int& count, int dst, int src)
{
#ifdef USE_SUN_FLOAT
    vm_emit_sse_brr(gInstructions_SSE[VMI_SSE_CVTSI2SS_SRC_REG_DST_REG], program, count, src, dst);
#else
    vm_emit_sse_brr(gInstructions_SSE[VMI_SSE_CVTSI2SD_SRC_REG_DST_REG], program, count, src, dst);
#endif
}

inline static void vm_ucmpd_reg_to_reg_x64(unsigned char* program, int& count, int dst, int src)
{
#ifdef USE_SUN_FLOAT
    vm_emit_sse_brr(gInstructions_SSE[VMI_SSE_UCOMISS_SRC_REG_DST_REG], program, count, src, dst);
#else
    vm_emit_sse_brr(gInstructions_SSE[VMI_SSE_UCOMISD_SRC_REG_DST_REG], program, count, src, dst);
#endif
}

inline static void vm_ucmpd_memory_to_reg_x64(unsigned char* program, int& count, int dst, int src, int src_offset)
{
#ifdef USE_SUN_FLOAT
    vm_emit_sse_brmo(gInstructions_SSE[VMI_SSE_UCOMISS_SRC_MEM_DST_REG], program, count, src, dst, src_offset);
#else
    vm_emit_sse_brmo(gInstructions_SSE[VMI_SSE_UCOMISD_SRC_MEM_DST_REG], program, count, src, dst, src_offset);
#endif
}

inline static void vm_xorpd_reg_to_reg_x64(unsigned char* program, int& count, int dst, int src)
{
#ifdef USE_SUN_FLOAT
    vm_emit_sse_brr(gInstructions_SSE[VMI_SSE_XORPS_SRC_REG_DST_REG], program, count, src, dst);
#else
    vm_emit_sse_brr(gInstructions_SSE[VMI_SSE_XORPD_SRC_REG_DST_REG], program, count, src, dst);
#endif
}

//===========================
// Forward decl
//===========================
static int vm_jit_read_int(unsigned char* program, unsigned int* pc);
static std::string vm_jit_read_string(unsigned char* program, unsigned int* pc);
static real vm_jit_read_real(unsigned char* program, unsigned int* pc);

//==================================
// Basic block descriptor
//==================================

struct BasicBlockDescriptor
{
    unsigned int _begin;
    unsigned int _end;
    unsigned int _jumpPos;
    char _jumpType;
    short _jump;
};

//==================================
// Basic block edge
//==================================
class BasicBlock;

struct BasicBlockEdge
{
    BasicBlock* _from;
    BasicBlock* _true;
    BasicBlock* _false;
    double _trueWeight;
    double _falseWeight;
};

//==================================
// Basic block
//==================================

class BasicBlock
{
public:
    BasicBlock(BasicBlockDescriptor desc);

    inline void SetNext(BasicBlock* blk) { _next = blk; }
    
    inline void SetPrev(BasicBlock* blk) { _prev = blk; }

    void CreateEdge();

    inline BasicBlock* Next() const { return _next; }
    
    inline BasicBlock* Prev() const { return _prev; }

    inline unsigned int EndPos() const { return _end; }
    
    inline unsigned int BeginPos() const { return _begin; }

    inline const std::vector<BasicBlockEdge>& Edges() const { return _edges; }

    inline short Jump() const { return _jump; }

    inline char JumpType() const { return _jumpType; }

    inline unsigned int JumpPos() const { return _jumpPos; }

private:
    BasicBlock* _next;
    BasicBlock* _prev;
    unsigned int _begin;
    unsigned int _end;
    unsigned int _jumpPos;
    char _jumpType;
    short _jump;
    std::vector<BasicBlockEdge> _edges; // the jumps between the blocks
};

BasicBlock::BasicBlock(BasicBlockDescriptor desc)
    :
    _next(nullptr),
    _prev(nullptr),
    _begin(desc._begin),
    _end(desc._end),
    _jumpPos(desc._jumpPos),
    _jumpType(desc._jumpType),
    _jump(desc._jump)
{
}

void BasicBlock::CreateEdge()
{
    if (_jump != 0)
    {
        BasicBlock* blk = nullptr;
        unsigned int target = _jumpPos + _jump;
        if (_jump >= 0)
        {
            // Forward search
            blk = _next;
            while (blk && target != blk->_begin)
            {
                blk = blk->Next();
            }

            if (blk)
            {
                BasicBlockEdge& edge = _edges.emplace_back();
                edge._true = blk;
                edge._false = _next;
                edge._from = this;
                edge._falseWeight = 0.5;
                edge._trueWeight = 0.5;

                blk->_edges.push_back(edge);
            }
        }
        else
        {
            // Backward search
            blk = _prev;
            while (blk && target != blk->_begin)
            {
                blk = blk->Prev();
            }

            if (blk)
            {
                BasicBlockEdge& edge = _edges.emplace_back(BasicBlockEdge());
                edge._true = blk;
                edge._false = _next;
                edge._from = this;
                edge._falseWeight = 0.5;
                edge._trueWeight = 0.5;

                blk->_edges.push_back(edge);
            }
        }
    }
    else if (_next)
    {
        BasicBlockEdge& edge = _edges.emplace_back(BasicBlockEdge());
        edge._true = _next;
        edge._from = this;
        edge._trueWeight = 1.0;
        edge._false = nullptr;
        edge._falseWeight = 0.0;

        _next->_edges.push_back(edge);
    }
}

//==================================
// Bitfield
//==================================

class JIT_BitField
{
public:
    JIT_BitField();
    void Init(const int size);
    void SetBit(const int pos, const bool value);
    bool GetBit(const int pos);
    ~JIT_BitField();

private:
    int* _data;
    int _size;
};

JIT_BitField::JIT_BitField()
    :
    _data(nullptr),
    _size(0)
{
}

JIT_BitField::~JIT_BitField()
{
    delete[] _data;
    _data = nullptr;
}

void JIT_BitField::Init(const int size)
{
    _size = size;
    const int count = _size / 8 + (_size % 8 > 0 ? 1 : 0);
    _data = new int[count];
    std::memset(_data, 0, count);
}

void JIT_BitField::SetBit(const int pos, const bool value)
{
    const int p = pos / 8;
    assert(pos >= 0 && pos < _size);
    if (value)
    {
        _data[p] |= 1 << (pos % 8);
    }
    else
    {
        _data[p] &= ~_data[p] & (1 << (pos % 8));
    }
}

bool JIT_BitField::GetBit(const int pos)
{
    const int p = pos / 8;
    const int mask = 1 << (pos % 8);
    return (_data[p] & (mask)) == mask;
}

//==================================
// Flow graph
//==================================

/*static void ConsumeInstruction(unsigned char* ins, unsigned int& pc)
{
    char type;

    switch (ins[pc])
    {
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
        pc++;
        break;
    case OP_PUSH:
        pc++;
        type = ins[pc];
        if (type == TY_INT)
        {
            pc += 4;
        }
        else if (type == TY_STRING)
        {
            pc += static_cast<unsigned int>(strlen((char*)&ins[pc])) + 1;
        }
        break;
    case OP_CALL:
        pc += 9; // ins (byte) + numArgs (int) + id (int)
        //pc += static_cast<unsigned int>(strlen((char*)&ins[pc])) + 1;
        break;
    case OP_CMP:
        pc++;
        break;
    case OP_JUMP:
        pc += 4;
        break;
    case OP_DECREMENT:
    case OP_INCREMENT:
        pc++;
        break;
    case OP_LOCAL:
        pc++;
        pc += static_cast<unsigned int>(strlen((char*)&ins[pc])) + 1;
        break;
    case OP_POP:
        pc++;
        pc += static_cast<unsigned int>(strlen((char*)&ins[pc])) + 1;
        break;
    case OP_POP_DISCARD:
        pc++;
        break;
    case OP_PUSH_LOCAL:
        pc++;
        pc += static_cast<unsigned int>(strlen((char*)&ins[pc])) + 1;
        break;
    case OP_RETURN:
        pc++;
        break;
    case OP_UNARY_MINUS:
        pc++;
        break;
    case OP_YIELD:
        pc += 2; // ins (byte) + numArgs (int)
        pc += static_cast<unsigned int>(strlen((char*)&ins[pc])) + 1;
        break;
    case OP_DONE:
        pc++;
        break;
    }
}

class JIT_FlowGraph
{
public:
    JIT_FlowGraph();
    void Init(unsigned char* ins, unsigned int pc, unsigned int size);
    inline BasicBlock* Head() { return _head; }

private:
    //void ConsumeInstruction(unsigned char* ins, unsigned int& pc);
    void ScanJumps(unsigned char* ins, unsigned int pc, unsigned int size);
    void ProcessBlocks();
    
    BasicBlock* _head;
    BasicBlock* _tail;
    JIT_BitField _jumpMask;
};

JIT_FlowGraph::JIT_FlowGraph()
    :
    _head(nullptr),
    _tail(nullptr)
{
}

void JIT_FlowGraph::ScanJumps(unsigned char* ins, unsigned int pc, unsigned int size)
{
    short pos;
    char type;
    bool done = false;
    const int offset = pc;
    const unsigned int end = pc + size;
    while (!done && pc < end)
    {
        switch (ins[pc])
        {
        case OP_JUMP:
            pc++;
            type = ins[pc++];
            pos = short(ins[pc]) | (short(ins[pc + 1]) << 8);
            pc += 2;
            _jumpMask.SetBit(pc + pos - offset, true);
            break;
        case OP_DONE:
            done = true;
            break;
        default:
            ConsumeInstruction(ins, pc);
            break;
        }
    }
}

void JIT_FlowGraph::Init(unsigned char* ins, unsigned int pc, unsigned int size)
{
    _jumpMask.Init(size);
    ScanJumps(ins, pc, size);

    BasicBlockDescriptor desc {};
    desc._begin = pc;

    const int offset = pc;
    const unsigned int end = pc + size;
    bool done = false;
    while (!done)
    {
        bool createBlock = false;
        
        switch (ins[pc])
        {
        case OP_JUMP:
            pc++;
            desc._jumpType = ins[pc++];
            desc._jump = short(ins[pc]) | (short(ins[pc + 1]) << 8);
            pc += 2;
            desc._jumpPos = pc;
            desc._end = pc;
            createBlock = true;
            break;
        case OP_DONE:
            pc++;
            desc._end = pc;
            createBlock = true;
            done = true;
            break;
        case OP_RETURN:
            pc++;
            desc._end = pc;
            createBlock = true;
            break;
        default:
            ConsumeInstruction(ins, pc);
            break;
        }

        if (_jumpMask.GetBit(pc - offset))
        {
            desc._end = pc;
            createBlock = true;
        }

        if (pc == end)
        {
            desc._end = pc;
            createBlock = true;
            done = true;
        }

        if (createBlock)
        {
            BasicBlock* blk = new BasicBlock(desc);
            if (_head == nullptr)
            {
                _head = blk;
                _tail = blk;
            }
            else if (_tail)
            {
                blk->SetPrev(_tail);
                _tail->SetNext(blk);
                _tail = blk;
            }

            std::memset(&desc, 0, sizeof(desc));
            desc._begin = pc;
        }
    }

    ProcessBlocks();
}

void JIT_FlowGraph::ProcessBlocks()
{
    BasicBlock* node = _head;
    while (node)
    {
        node->CreateEdge();
        node = node->Next();
    }
}*/

//==================================

constexpr unsigned int ST_REG = 0;
constexpr unsigned int ST_STACK = 1;

struct JIT_Allocation
{
    unsigned int type;
    unsigned int reg;
    unsigned int pos;
    bool isSSE;
};

struct JIT_LiveValue
{
    unsigned int ref;
    JIT_Allocation al;
};

class JIT_Analyzer
{
public:
    void Load(unsigned char* ir, unsigned int count);
    JIT_Allocation GetAllocation(int index);
    /*bool IsRegisterUsed(int reg);*/
    int StackSize();
    void GetLiveValues(int index, std::vector<JIT_LiveValue>& live);
    void Dump();
    ~JIT_Analyzer();

private:

    struct Phi
    {
        bool init;
        int left;
        int right;
        int ref;
    };

    struct Node
    {
        int ref;
        int start;
        int end;
        Node* prev;
        Node* next;
    };

    class Allocation
    {
    public:
        Allocation();
        void Initialize(int reg, bool enabled, bool sse);
        void Initialize(int pos, bool sse);
        inline void SetEnabled(bool enabled) { _enabled = enabled; }
        void InsertBefore(Node* before, int ref, int start, int end);
        void InsertAfter(Node* after, int ref, int start, int end);
        void Insert(int ref, int start, int end);

        inline bool IsSSE() const { return _sse; }
        inline bool Enabled() const { return _enabled; }
        inline Node* Head() const { return _head; }
        inline Node* Tail() const { return _tail; }
        inline int Register() const { return _reg; }
        inline int Type() const { return _type; }
        inline int Pos() const { return _pos; }

    private:
        int _type;
        int _reg;
        int _pos;
        bool _sse;
        bool _enabled;
        Node* _head;
        Node* _tail;
    };

    void InitializeAllocations();
    void AllocateRegister(int ref, int start, int end, bool sse);

    std::vector<int> liveness;
    std::vector<bool> sse;
    std::vector<Allocation> allocations;
    std::vector<JIT_Allocation> registers;
};

JIT_Analyzer::Allocation::Allocation()
    :
    _reg(0),
    _pos(0),
    _type(0),
    _enabled(true),
    _sse(false),
    _head(nullptr),
    _tail(nullptr)
{
}

JIT_Analyzer::~JIT_Analyzer()
{
    for (size_t i = 0; i < allocations.size(); i++)
    {
        Node* node = allocations[i].Head();
        while (node)
        {
            Node* next = node->next;
            delete node;
            node = next;
        }
    }
}

int JIT_Analyzer::StackSize()
{
    return int(allocations.size()) - VM_REGISTER_MAX;
}

/*bool JIT_Analyzer::IsRegisterUsed(int reg)
{
    if (reg >= 0 && reg < VM_REGISTER_MAX)
    {
        return allocations[reg].Head() != nullptr;
    }

    return false;
}*/

JIT_Allocation JIT_Analyzer::GetAllocation(int index)
{
    if (index >= 0 && index < registers.size())
    {
        return registers[index];
    }

    return JIT_Allocation();
}

void JIT_Analyzer::GetLiveValues(int index, std::vector<JIT_LiveValue>& live)
{
    for (size_t i = 0; i < allocations.size(); i++)
    {
        const auto& al = allocations[i];
        JIT_Analyzer::Node* node = al.Head();
        while (node)
        {
            if (node->start > index)
            {
                break;
            }

            if (node->start <= index && node->end >= index)
            {
                JIT_LiveValue& value = live.emplace_back();
                value.al.pos = al.Pos();
                value.al.type = al.Type();
                value.al.reg = al.Register();
                value.al.isSSE = al.IsSSE();
                value.ref = node->ref;
                break;
            }

            node = node->next;
        }
    }
}

void JIT_Analyzer::Allocation::Initialize(int reg, bool enabled, bool sse)
{
    _type = ST_REG;
    _reg = reg;
    _enabled = enabled;
    _sse = sse;
}

void JIT_Analyzer::Allocation::Initialize(int pos, bool sse)
{
    _type = ST_STACK;
    _reg = VM_REGISTER_ESP;
    _pos = pos;
    _sse = sse;
    _enabled = true;
}

void JIT_Analyzer::Allocation::Insert(int ref, int start, int end)
{
    assert(_head == nullptr);

    Node* node = new Node();
    node->end = end;
    node->next = nullptr;
    node->prev = nullptr;
    node->ref = ref;
    node->start = start;

    _head = node;
    _tail = node;
}

void JIT_Analyzer::Allocation::InsertBefore(Node* before, int ref, int start, int end)
{
    Node* node = new Node();
    node->end = end;
    node->next = before;
    node->prev = before->prev;
    node->ref = ref;
    node->start = start;

    if (before->prev)
    {
        before->prev->next = node;
    }
    before->prev = node;

    if (before == _head)
    {
        _head = node;
    }
}

void JIT_Analyzer::Allocation::InsertAfter(Node* after, int ref, int start, int end)
{
    Node* node = new Node();
    node->end = end;
    node->next = after->next;
    node->prev = after;
    node->ref = ref;
    node->start = start;

    if (after->next)
    {
        after->next->prev = node;
    }
    after->next = node;

    if (after == _tail)
    {
        _tail = node;
    }
}

void JIT_Analyzer::InitializeAllocations()
{
    allocations.resize(int(VM_REGISTER_MAX) + int(VM_SSE_REGISTER_MAX));
    for (size_t i = 0; i < VM_REGISTER_MAX; i++)
    {
        auto& a = allocations[i];
        a.Initialize(int(i), true, false);
    }
    
    // Reserved registers

    allocations[VM_REGISTER_ESP].SetEnabled(false);
    allocations[VM_REGISTER_EBP].SetEnabled(false);
    allocations[VM_REGISTER_EAX].SetEnabled(false);

    allocations[VM_REGISTER_R10].SetEnabled(false);
    allocations[VM_REGISTER_R11].SetEnabled(false);

    allocations[VM_ARG1].SetEnabled(false);
    allocations[VM_ARG2].SetEnabled(false);
    allocations[VM_ARG3].SetEnabled(false);
    allocations[VM_ARG4].SetEnabled(false);

#if VM_MAX_ARGS == 6
    allocations[VM_ARG5].SetEnabled(false);
    allocations[VM_ARG6].SetEnabled(false);
#endif

    for (size_t i = 0; i < VM_SSE_REGISTER_MAX; i++)
    {
        auto& a = allocations[i + int(VM_REGISTER_MAX)];
        a.Initialize(int(i), true, true);
    }

    // Reserved registers

    allocations[VM_SSE_ARG1 + int(VM_REGISTER_MAX)].SetEnabled(false);
    allocations[VM_SSE_ARG2 + int(VM_REGISTER_MAX)].SetEnabled(false);
    allocations[VM_SSE_ARG3 + int(VM_REGISTER_MAX)].SetEnabled(false);
    allocations[VM_SSE_ARG4 + int(VM_REGISTER_MAX)].SetEnabled(false);
    allocations[VM_SSE_ARG5 + int(VM_REGISTER_MAX)].SetEnabled(false);
    allocations[VM_SSE_ARG6 + int(VM_REGISTER_MAX)].SetEnabled(false);

#if VM_MAX_SSE_ARGS == 8
    allocations[VM_SSE_ARG7 + int(VM_REGISTER_MAX)].SetEnabled(false);
    allocations[VM_SSE_ARG8 + int(VM_REGISTER_MAX)].SetEnabled(false);
#endif
}

void JIT_Analyzer::AllocateRegister(int ref, int start, int end, bool sse)
{
    bool ok = false;

    size_t i = sse ? VM_REGISTER_MAX : 0;   // for SSE skip the general registers
    for (; i < allocations.size(); i++)
    {
        auto& allocation = allocations[i];
        if (!allocation.Enabled()) { continue; }
        if (allocation.IsSSE() != sse) { continue; }

        if (allocation.Head() == nullptr)
        {
            // Insert as first node
            allocation.Insert(ref, start, end);
            ok = true;
            break;
        }

        // Check if we can use this register/memory
        // If we don't overlap with anything existing we can use it

        Node* node = allocation.Head();
        while (node)
        {
            if ((node->start >= start && node->start <= end) ||
                (node->end >= start && node->end <= end) ||
                (start >= node->start && start <= node->end) ||
                (end >= node->start && end <= node->end))
            {
                node = nullptr;
                break;
            }

            if (node->next)
            {
                if (node->next->start > end)
                {
                    allocation.InsertAfter(node, ref, start, end);
                    ok = true;
                    node = nullptr;
                }
                else
                {
                    node = node->next;
                    continue;
                }
            }
            else
            {
                allocation.InsertAfter(node, ref, start, end);
                ok = true;
                node = nullptr;
            }
        }

        if (node)
        {
            // Insert before the node
            allocation.InsertBefore(node, ref, start, end);
            ok = true;
        }

        if (ok)
        {
            break;
        }
    }

    // Allocate a new place on the stack instead
    if (!ok)
    {
        const int size = StackSize() * 8 + 32;
        JIT_Analyzer::Allocation& allocation = allocations.emplace_back();
        allocation.Initialize(size, sse);
        allocation.Insert(ref, start, end);
    }
}

void JIT_Analyzer::Load(unsigned char* ir, unsigned int count)
{
    std::vector<Phi> phis;
    unsigned int pc = 0;
    int ref = 0;

    const int constantSize = vm_jit_read_int(ir, &pc);
    pc += constantSize;

    while (pc < count)
    {
        int p1 = -1;
        int p2 = -1;
        bool isSSE = false;

        const int op = ir[pc++];
        switch (op)
        {
        case IR_ADD_INT:
        case IR_SUB_INT:
        case IR_MUL_INT:
        case IR_DIV_INT:
        case IR_CMP_INT:
        case IR_CMP_STRING:
            p1 = vm_jit_read_int(ir, &pc);
            p2 = vm_jit_read_int(ir, &pc);
            break;
        case IR_CONV_INT_TO_REAL:
            p1 = vm_jit_read_int(ir, &pc);
            isSSE = true;
            break;
        case IR_ADD_REAL:
        case IR_SUB_REAL:
        case IR_MUL_REAL:
        case IR_DIV_REAL:
        case IR_CMP_REAL:
        case IR_CMP_TABLE:
            p1 = vm_jit_read_int(ir, &pc);
            p2 = vm_jit_read_int(ir, &pc);
            isSSE = true;
            break;
        case IR_LOAD_INT:
        case IR_LOAD_STRING:
            vm_jit_read_int(ir, &pc);
            break;
        case IR_LOAD_REAL:
            vm_jit_read_int(ir, &pc);
            isSSE = true;
            break;
        case IR_APP_INT_STRING:
        case IR_APP_STRING_STRING:
        case IR_APP_STRING_INT:
        case IR_APP_STRING_REAL:
        case IR_APP_REAL_STRING:
            p1 = vm_jit_read_int(ir, &pc);
            p2 = vm_jit_read_int(ir, &pc);
            break;
        case IR_CALL:
        case IR_YIELD:
            vm_jit_read_int(ir, &pc);
            {
                const int numArgs = ir[pc++];
            }
            break;
        case IR_INT_ARG:
        case IR_STRING_ARG:
        case IR_REAL_ARG:
        case IR_TABLE_ARG:
        {
            const int arg = vm_jit_read_int(ir, &pc);
            liveness[arg] = std::max(liveness[arg], ref - arg);
        }
            break;
        case IR_DECREMENT_INT:
        case IR_INCREMENT_INT:
            p1 = vm_jit_read_int(ir, &pc);
            break;
        case IR_GUARD:
            pc++;
            break;
        case IR_LOOPEXIT:
            pc += 3;
            break;
        case IR_LOOPBACK:
            pc += 3;
            {
                // Extend the lifetime to end of the loop.
                for (auto& phi : phis)
                {
                    const int maxRef = std::max(phi.left, phi.right);
                    if (!phi.init && ref > maxRef)
                    {
                        liveness[maxRef] = ref - maxRef;
                        liveness[phi.ref] = ref - phi.ref;
                        phi.init = true;
                    }
                }
            }
            break;
        case IR_UNARY_MINUS_INT:
            p1 = vm_jit_read_int(ir, &pc);
            break;
        case IR_UNARY_MINUS_REAL:
            p1 = vm_jit_read_int(ir, &pc);
            isSSE = true;
            break;
        case IR_LOOPSTART:
            break;
        case IR_PHI:
            p1 = vm_jit_read_int(ir, &pc);
            p2 = vm_jit_read_int(ir, &pc);
            {
                Phi& phi = phis.emplace_back();
                phi.left = p1;
                phi.right = p2;
                phi.ref = ref;
                phi.init = false;
            }
            break;
        case IR_SNAP:
            pc++; // number
            {
                const int numSlots = ir[pc++];
                for (int i = 0; i < numSlots; i++)
                {
                    const int slot = ir[pc++];
                    liveness[slot] = std::max(liveness[slot], ref - slot);
                }
            }
            break;
        case IR_NOP:
            break;
        case IR_BOX:
            p1 = vm_jit_read_int(ir, &pc);
            pc++; // type
            break;
        case IR_UNBOX:
            p1 = vm_jit_read_int(ir, &pc);
            if (ir[pc++] == TY_REAL) // type
            {
                isSSE = true;
            }
            break;
        case IR_LOAD_STRING_LOCAL:
        case IR_LOAD_INT_LOCAL:
        case IR_LOAD_TABLE_LOCAL:
            pc++;
            break;
        case IR_LOAD_REAL_LOCAL:
            pc++;
            isSSE = true;
            break;
        case IR_TABLE_NEW:
            break;
        case IR_TABLE_HGET:
        case IR_TABLE_AGET:
        case IR_TABLE_ASET:
        case IR_TABLE_HSET:
        case IR_TABLE_AREF:
        case IR_TABLE_HREF:
            p1 = vm_jit_read_int(ir, &pc);
            p2 = vm_jit_read_int(ir, &pc);
            break;
        default:
            abort();
            break;
        }

        if (p1 > -1 && p1 < ref) { liveness[p1] = std::max(liveness[p1], ref - p1); }
        if (p2 > -1 && p2 < ref) { liveness[p2] = std::max(liveness[p2], ref - p2); }

        liveness.push_back(0);
        sse.push_back(isSSE);
        ref++;
    }

    // Determine which registers to use.

    InitializeAllocations();
    for (int i = 0; i < int(liveness.size()); i++)
    {
        if (liveness[i] > 0)
        {
            AllocateRegister(i, i, liveness[i] + i, sse[i]);
        }
    }

    // Write them back to a list the same size as the number of instructions
    
    registers.resize(liveness.size());
    for (size_t j = 0; j < allocations.size(); j++)
    {
        auto& al = allocations[j];

        Node* node = al.Head();
        while (node)
        {
            auto& item = registers[node->ref];
            item.reg = al.Register();
            item.type = al.Type();
            item.pos = al.Pos();
            item.isSSE = al.IsSSE();
            node = node->next;
        }
    }
}

void JIT_Analyzer::Dump()
{
    const std::string line = "================";
    std::cout << line << std::endl;
    for (size_t i = 0; i < liveness.size(); i++)
    {
        std::cout << i << " " << liveness[i] << " ";
        
        if (registers[i].isSSE)
        {
            switch (registers[i].reg)
            {
            case VM_SSE_REGISTER_XMM0:
                std::cout << "XMM0";
                break;
            case VM_SSE_REGISTER_XMM1:
                std::cout << "XMM1";
                break;
            case VM_SSE_REGISTER_XMM2:
                std::cout << "XMM2";
                break;
            case VM_SSE_REGISTER_XMM3:
                std::cout << "XMM3";
                break;
            case VM_SSE_REGISTER_XMM4:
                std::cout << "XMM4";
                break;
            case VM_SSE_REGISTER_XMM5:
                std::cout << "XMM5";
                break;
            case VM_SSE_REGISTER_XMM6:
                std::cout << "XMM6";
                break;
            case VM_SSE_REGISTER_XMM7:
                std::cout << "XMM7";
                break;
            case VM_SSE_REGISTER_XMM8:
                std::cout << "XMM8";
                break;
            case VM_SSE_REGISTER_XMM9:
                std::cout << "XMM9";
                break;
            case VM_SSE_REGISTER_XMM10:
                std::cout << "XMM10";
                break;
            case VM_SSE_REGISTER_XMM11:
                std::cout << "XMM11";
                break;
            case VM_SSE_REGISTER_XMM12:
                std::cout << "XMM12";
                break;
            case VM_SSE_REGISTER_XMM13:
                std::cout << "XMM13";
                break;
            case VM_SSE_REGISTER_XMM14:
                std::cout << "XMM14";
                break;
            case VM_SSE_REGISTER_XMM15:
                std::cout << "XMM15";
                break;
            }
        }
        else
        {
            switch (registers[i].reg)
            {
            case VM_REGISTER_EAX:
                std::cout << "EAX";
                break;
            case VM_REGISTER_EBX:
                std::cout << "EBX";
                break;
            case VM_REGISTER_ECX:
                std::cout << "ECX";
                break;
            case VM_REGISTER_EDX:
                std::cout << "EDX";
                break;
            case VM_REGISTER_EDI:
                std::cout << "EDI";
                break;
            case VM_REGISTER_EBP:
                std::cout << "EBP";
                break;
            case VM_REGISTER_ESI:
                std::cout << "ESI";
                break;
            case VM_REGISTER_ESP:
                std::cout << "ESP";
                break;
            case VM_REGISTER_R8:
                std::cout << "R8";
                break;
            case VM_REGISTER_R9:
                std::cout << "R9";
                break;
            case VM_REGISTER_R10:
                std::cout << "R10";
                break;
            case VM_REGISTER_R11:
                std::cout << "R11";
                break;
            case VM_REGISTER_R12:
                std::cout << "R12";
                break;
            case VM_REGISTER_R13:
                std::cout << "R13";
                break;
            case VM_REGISTER_R14:
                std::cout << "R14";
                break;
            case VM_REGISTER_R15:
                std::cout << "R15";
                break;
            }
        }
        std::cout << std::endl;
    }
    std::cout << line << std::endl;
}

//===========================================

constexpr int PATCH_INITIALIZED = 0;
constexpr int PATCH_APPLIED = 1;

struct JIT_Patch
{
    int _state;
    int _reg;
    int _offset;
    void* _data;
    int _size;
};

struct JIT_ExitJump
{
    int _state;
    int _pos;
    int _type;
    int _size;
    int _offset;
    int _exitRef;
};

struct JIT_BackwardJump
{
    int _state;
    int _pos;
    int _target;
    int _type;
};

struct JIT_Guard
{
    int _state;
    int _offset;
    int _pos;
    int _type;
    int _size;
    int _snap;
    int _ref;
};

struct JIT_Phi
{
    int _state;
    int _pos;   // # of instruction
    int _left;
    int _right;
};

struct JIT_Snapshot
{
    int _ref;
    std::vector<int> entries;
};

struct JIT_Trace
{
    void* _jit_data;
    int _size;
    int _jumpPos;
    unsigned char* _record;
    void* _constantPage;
    int _constantSize;

    int _id;
    MemoryManager _mm;

    std::vector<JIT_Guard> _forwardJumps;
    std::vector<JIT_BackwardJump> _backwardJumps;
    std::vector<JIT_ExitJump> _exitJumps;
    std::vector<JIT_Phi> _phis;
    std::vector<JIT_Snapshot> _snaps;

    uint64_t _startTime;     // compilation start time
    uint64_t _endTime;       // compilation end time
    uint64_t _runCount;      // number of times the trace has been invoked
};

class JIT_Coroutine
{
public:
    void* _vm_stub;
    void* _vm_yielded;
    void* _vm_suspend;
    void* _yield_resume;
    void* _vm_resume;
    long long _stackPtr;
    long long _stackSize;
};

class JIT_Manager
{
public:
    MemoryManager _mm;
    JIT_Coroutine _co;
};

class Jitter
{
public:
    unsigned char* program;
    unsigned char* jit;
    int count;
    unsigned int* pc;
    JIT_Trace* _trace;
    JIT_Manager* _manager;
    JIT_Analyzer analyzer;
    int size;
    int refIndex;
    int argsProcessed;
    int snapshot;
    int snapRef;
    bool running;
    bool error;

    Jitter() :
        program(nullptr),
        jit(nullptr),
        count(0),
        pc(nullptr),
        running(true),
        error(false),
        _trace(nullptr),
        argsProcessed(0),
        size(0),
        refIndex(0),
        _manager(nullptr),
        snapshot(0),
        snapRef(0)
    {
    }

    void SetError()
    {
        error = true;
        running = false;
    }
};

struct JIT_SnapSlot
{
    int64_t ref;
    int64_t data;
};

struct JIT_Snap
{
    int64_t size;
    int64_t ref;
    JIT_SnapSlot slots[1];
};

//===================================
#ifdef WIN32
void* vm_allocate(int size)
{
    return VirtualAlloc(0, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void vm_initialize(void* data, int size)
{
    unsigned long oldProtect;
    if (VirtualProtect(data, size, PAGE_EXECUTE, &oldProtect))
    {
        FlushInstructionCache(0, data, size);
    }
}

void vm_readonly(void* data, int size)
{
    unsigned long oldProtect;
    VirtualProtect(data, size, PAGE_READONLY, &oldProtect);
}

//void vm_execute(void* data)
//{
//    int(*fn)(void) = (int(*)(void))((unsigned char*)data);
//    int result = fn();
//
//}

void vm_free(void* data, int size)
{
    VirtualFree(data, size, MEM_RELEASE | MEM_DECOMMIT);
}

void vm_begin_patch(void* data, int size)
{
    unsigned long oldProtect;
    VirtualProtect(data, size, PAGE_READWRITE, &oldProtect);
}

void vm_commit_patch(void* data, int size)
{
    unsigned long oldProtect;
    if (VirtualProtect(data, size, PAGE_EXECUTE, &oldProtect))
    {
        FlushInstructionCache(0, data, size);
    }
}
#else
#include <sys/mman.h>
void* vm_allocate(int size)
{
    void* data = mmap(nullptr, size, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (data == MAP_FAILED)
    {
        std::cout << errno << std::endl;
        abort();
    }
    return data;
}

void vm_initialize(void* data, int size)
{
    mprotect(data, size, PROT_EXEC | PROT_READ);
    //cacheflush(0, size, ICACHE);
}

void vm_readonly(void* data, int size)
{
    mprotect(data, size, PROT_READ);
    //cacheflush(0, size, ICACHE);
}

void vm_free(void* data, int size)
{
    munmap(data, size);
}

void vm_begin_patch(void* data, int size)
{
}

void vm_commit_patch(void* data, int size)
{

}
#endif

extern "C"
{
    static int vm_pop_int_stub(VirtualMachine* vm)
    {
        int value;
        GetParamInt(vm, &value);
        return value;
    }

    static void vm_push_int_stub(VirtualMachine* vm, int value)
    {
        PushParamInt(vm, value);
    }

    static void vm_push_string_stub(VirtualMachine* vm, char* value)
    {
        PushParamString(vm, value);
    }

    static void vm_push_real_stub(VirtualMachine* vm, real value)
    {
        PushParamReal(vm, value);
    }

    static void* vm_call_stub(VirtualMachine* vm, MemoryManager* mm, char* name, const int numArgs)
    {
        InvokeHandler(vm, name, numArgs);

        void* val = nullptr;
        GetParam(vm, &val);
        return val;
    }

    static int vm_restore_snapshot(VirtualMachine* vm, int64_t* data, const int size, const int snap)
    {
        JIT_Snap* s = reinterpret_cast<JIT_Snap*>(data);

        MemoryManager* mm = GetMemoryManager(vm);

        Snapshot sn(int(s->size), mm);
        for (int64_t i = 0; i < s->size; i++)
        {
            sn.Add(int(s->slots[i].ref), s->slots[i].data);
        }

        RestoreSnapshot(vm, sn, snap, int(s->ref));

        return size;
    }

    static void* vm_box_int(MemoryManager* mm, int value)
    {
        int* integer = reinterpret_cast<int*>(mm->New(sizeof(int), TY_INT));
        *integer = value;
        return integer;
    }

    static void* vm_box_func(MemoryManager* mm, int value)
    {
        int* integer = reinterpret_cast<int*>(mm->New(sizeof(int), TY_FUNC));
        *integer = value;
        return integer;
    }

    static void* vm_box_real(MemoryManager* mm, real value)
    {
        real* realVal = reinterpret_cast<real*>(mm->New(sizeof(real), TY_REAL));
        *realVal = value;
        return realVal;
    }

    static int vm_check_type(MemoryManager* mm, void* obj, int type)
    {
        // TODO: the memory manager we a getting here is different
        // than the one used to allocate thus it fails
        return MemoryManager::GetTypeUnsafe(obj) == type ? VM_OK : VM_ERROR;
    }

    static char* vm_duplicate_string(MemoryManager* mm, char* str)
    {
        const size_t len = std::strlen(str) + 1;
        char* dup = reinterpret_cast<char*>(mm->New(len, TY_STRING));
        std::memcpy(dup, str, len);
        return dup;
    }

    static char* vm_append_string_int(MemoryManager* mm, char* left, int right)
    {
        std::stringstream ss;
        ss << left << right;
        std::string str = ss.str();
        char* data = (char*)mm->New(str.length() + 1, TY_STRING);
        std::memcpy(data, str.c_str(), str.length());
        data[str.length()] = '\0';
        return data;
    }

    static char* vm_append_string_real(MemoryManager* mm, char* left, real right)
    {
        std::stringstream ss;
        ss << left << right;
        std::string str = ss.str();
        char* data = (char*)mm->New(str.length() + 1, TY_STRING);
        std::memcpy(data, str.c_str(), str.length());
        data[str.length()] = '\0';
        return data;
    }

    static char* vm_append_string_string(MemoryManager* mm, char* left, char* right)
    {
        std::stringstream ss;
        ss << left << right;
        std::string s = ss.str();
        char* data = (char*)mm->New(s.length() + 1, TY_STRING);
        std::memcpy(data, s.c_str(), s.length());
        data[s.length()] = '\0';
        return data;
    }

    static char* vm_append_int_string(MemoryManager* mm, int left, char* right)
    {
        std::stringstream ss;
        ss << left << right;
        std::string s = ss.str();
        char* data = (char*)mm->New(s.length() + 1, TY_STRING);
        std::memcpy(data, s.c_str(), s.length());
        data[s.length()] = '\0';
        return data;
    }

    static char* vm_append_real_string(MemoryManager* mm, real left, char* right)
    {
        std::stringstream ss;
        ss << left << right;
        std::string s = ss.str();
        char* data = (char*)mm->New(s.length() + 1, TY_STRING);
        std::memcpy(data, s.c_str(), s.length());
        data[s.length()] = '\0';
        return data;
    }

    static void* vm_table_new(MemoryManager* mm)
    {
        return CreateTable(mm);
    }

    static void* vm_table_aget(void* table, int index)
    {
        return GetTableArray(table, index);
    }

    static void* vm_table_hget(void* table, char* key)
    {
        return GetTableHash(table, key);
    }

    static void vm_table_aset(void* table, int key, void* value)
    {
        SetTableArray(table, key, value);
    }
    
    static void vm_table_hset(void* table, char* key, void* value)
    {
        SetTableHash(table, key, value);
    }
}

// =================================

static void vm_jit_call_internal_x64(Jitter* jitter, void* address)
{
    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, (long long)address);
    vm_call_absolute(jitter->jit, jitter->count, VM_REGISTER_EAX);
}

static std::string vm_jit_read_string(unsigned char* program, unsigned int* pc)
{
    std::string str;
    int index = 0;
    char ch = (char)program[*pc];
    (*pc)++;
    while (ch != 0)
    {
        str = str.append(1, ch);
        ch = (char)program[*pc];
        (*pc)++;
    }
    return str;
}

static int vm_jit_read_int(unsigned char* program, unsigned int* pc)
{
    int a = program[(*pc)];
    int b = program[(*pc) + 1];
    int c = program[(*pc) + 2];
    int d = program[(*pc) + 3];

    *pc += 4;
    return a | (b << 8) | (c << 16) | (d << 24);
}

static real vm_jit_read_real(unsigned char* program, unsigned int* pc)
{
    real* r = reinterpret_cast<real*>(&program[*pc]);
    *pc += SUN_REAL_SIZE;
    return *r;
}

static int vm_jit_decode_dst_sse(const JIT_Allocation& al)
{
    int dst = 0;
    switch (al.type)
    {
        case ST_REG:
            dst = al.reg;
            break;
        case ST_STACK:
            dst = VM_SSE_REGISTER_XMM0;
            break;
    }

    return dst;
}

static int vm_jit_decode_dst(const JIT_Allocation& al)
{
    int dst = 0;
    switch (al.type)
    {
    case ST_REG:
        dst = al.reg;
        break;
    case ST_STACK:
        dst = VM_REGISTER_EAX;
        break;
    }

    return dst;
}

static void vm_jit_mov_sse(Jitter* jitter, const JIT_Allocation& al, int dst)
{
    switch (al.type)
    {
        case ST_REG:
            if (al.reg != dst)
            {
                vm_movsd_reg_to_reg_x64(jitter->jit, jitter->count, dst, al.reg);
            }
            break;
        case ST_STACK:
            vm_movsd_memory_to_reg_x64(jitter->jit, jitter->count, dst, al.reg, al.pos);
            break;
    }
}

static void vm_jit_mov(Jitter* jitter, const JIT_Allocation& al, int dst)
{
    switch (al.type)
    {
    case ST_REG:
        if (al.reg != dst)
        {
            vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, dst, al.reg);
        }
        break;
    case ST_STACK:
        vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, dst, al.reg, al.pos);
        break;
    }
}

static void vm_jit_jump(Jitter* jitter, char type, int& count, int imm)
{
    switch (type)
    {
    case JUMP_NE:
        vm_jump_not_equals(jitter->jit, count, imm);
        break;
    case JUMP_E:
        vm_jump_equals(jitter->jit, count, imm);
        break;
    case JUMP_L:
        vm_jump_less(jitter->jit, count, imm);
        break;
    case JUMP_G:
        vm_jump_greater(jitter->jit, count, imm);
        break;
    case JUMP_LE:
        vm_jump_less_equal(jitter->jit, count, imm);
        break;
    case JUMP_GE:
        vm_jump_greater_equal(jitter->jit, count, imm);
        break;
    case JUMP:
        vm_jump_unconditional(jitter->jit, count, imm);
        break;
    }
}

static void vm_jit_patch_jump(Jitter* jitter, JIT_Guard& jump)
{
    jump._state = PATCH_APPLIED;

    const int rel = jitter->count - (jump._offset + jump._size);

    vm_jit_jump(jitter, jump._type, jump._offset, rel);
}

static void vm_jit_cmp_string(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);

    vm_jit_mov(jitter, a1, VM_ARG1);
    vm_jit_mov(jitter, a2, VM_ARG2);

    vm_jit_call_internal_x64(jitter, (void*)strcmp);

    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_R10, 0);
    vm_cmp_reg_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_R10, VM_REGISTER_EAX);
}

static void vm_jit_cmp_int(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);

    const int dst = vm_jit_decode_dst(a2);
    vm_jit_mov(jitter, a2, dst);

    switch (a1.type)
    {
        case ST_REG:
            vm_cmp_reg_to_reg_x64(jitter->jit, jitter->count, a1.reg, dst);
            break;
        case ST_STACK:
            vm_cmp_reg_to_memory_x64(jitter->jit, jitter->count, a1.reg, a1.pos, dst);
            break;
    }
}

static void vm_jit_cmp_real(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);

    const int dst = vm_jit_decode_dst_sse(a1);
    vm_jit_mov_sse(jitter, a1, dst);

    switch (a2.type)
    {
        case ST_REG:
            vm_ucmpd_reg_to_reg_x64(jitter->jit, jitter->count, dst, a2.reg);
            break;
        case ST_STACK:
            vm_ucmpd_memory_to_reg_x64(jitter->jit, jitter->count, dst, a2.pos, a2.reg);
            break;
    }
}

static void vm_jit_cmp_table(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);

    vm_cmp_reg_to_reg_x64(jitter->jit, jitter->count, a1.reg, a2.reg);
}

static void vm_jit_exitloop(Jitter* jitter)
{
    const int type = jitter->program[*jitter->pc];
    (*jitter->pc)++;
    const int offset =
        short(jitter->program[*jitter->pc]) |
        short(jitter->program[*jitter->pc + 1]) << 8;
    (*jitter->pc) += 2;

    // Forward jump (requires patching)

    JIT_ExitJump jump;
    jump._offset = jitter->count;
    jump._type = type;
    jump._state = PATCH_INITIALIZED;
    jump._pos = *jitter->pc;
    jump._exitRef = jitter->refIndex + offset;

    // Just emit equals it will be overwritten by the patch process
    vm_jump_equals(jitter->jit, jitter->count, 0);

    jump._size = jitter->count - jump._offset;

    jitter->_trace->_exitJumps.push_back(jump);
}

static void vm_jit_startloop(Jitter* jitter)
{
    // Record backward jump point

    JIT_BackwardJump bwj;
    bwj._pos = jitter->count;
    bwj._state = PATCH_INITIALIZED;
    bwj._type = 0;
    bwj._target = jitter->refIndex;
    jitter->_trace->_backwardJumps.push_back(bwj);
}

static void vm_jit_guard(Jitter* jitter)
{
    const int type = jitter->program[*jitter->pc];
    (*jitter->pc)++;

    // Forward jump (requires patching)

    JIT_Guard jump;
    jump._offset = jitter->count;
    jump._type = type;
    jump._state = PATCH_INITIALIZED;
    jump._pos = *jitter->pc;
    jump._ref = jitter->refIndex;
    jump._snap = jitter->snapshot;

    // Just emit equals it will be overwritten by the patch process
    vm_jump_equals(jitter->jit, jitter->count, 0);

    jump._size = jitter->count - jump._offset;

    jitter->_trace->_forwardJumps.push_back(jump);
}

static void vm_jit_loopback(Jitter* jitter)
{
    const int type = jitter->program[*jitter->pc];
    (*jitter->pc)++;
    short offset = jitter->program[*jitter->pc];
    (*jitter->pc)++;
    offset |= (jitter->program[*jitter->pc] << 8);
    (*jitter->pc)++;

    assert (offset <= 0);
    
    // Apply Phis
    for (size_t i = 0; i < jitter->_trace->_phis.size(); i++)
    {
        auto& phi = jitter->_trace->_phis[i];
        const int maxRef = std::max(phi._left, phi._right);
        // Apply the PHI node if we have passed both the nodes used in the PHI instruction.
        if (phi._state == PATCH_INITIALIZED && jitter->refIndex > maxRef)
        {
            phi._state = PATCH_APPLIED;

            JIT_Allocation a1 = jitter->analyzer.GetAllocation(phi._right);
            JIT_Allocation a2 = jitter->analyzer.GetAllocation(phi._pos);

            const int dst = vm_jit_decode_dst(a2);
            vm_jit_mov(jitter, a1, dst);

            if (a2.type == ST_STACK)
            {
                vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, a2.reg, a2.pos, dst);
            }
        }
    }

    // Backward jump (no patching)
    for (size_t i = 0; i < jitter->_trace->_backwardJumps.size(); i++)
    {
        auto& jump = jitter->_trace->_backwardJumps[i];
        if (offset + jitter->refIndex == jump._target)
        {
            const int imm = jump._pos - (jitter->count + 5 /*Length of jump instruction*/);
            vm_jit_jump(jitter, jump._type, jitter->count, imm);
            break;
        }
    }

    // Patch loop exits
    for (size_t i = 0; i < jitter->_trace->_exitJumps.size(); i++)
    {
        auto& jump = jitter->_trace->_exitJumps[i];
        if (jump._state == PATCH_INITIALIZED &&
            jump._exitRef == jitter->refIndex)
        {
            jump._state = PATCH_APPLIED;
            
            const int imm = jitter->count - (jump._offset + jump._size);
            vm_jit_jump(jitter, jump._type, jump._offset, imm);
        }
    }
}

static void vm_jit_append_string_int(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);
    const JIT_Allocation a3 = jitter->analyzer.GetAllocation(jitter->refIndex);

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)&jitter->_trace->_mm);

    vm_jit_mov(jitter, a1, VM_ARG2);
    vm_jit_mov(jitter, a2, VM_ARG3);

    vm_jit_call_internal_x64(jitter, (void*)vm_append_string_int);
    
    if (a3.reg != VM_REGISTER_EAX)
    {
        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, a3.reg, VM_REGISTER_EAX);
    }
}

static void vm_jit_append_int_string(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);
    const JIT_Allocation a3 = jitter->analyzer.GetAllocation(jitter->refIndex);

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)&jitter->_trace->_mm);

    vm_jit_mov(jitter, a1, VM_ARG2);
    vm_jit_mov(jitter, a2, VM_ARG3);

    vm_jit_call_internal_x64(jitter, (void*)vm_append_int_string);

    if (a3.reg != VM_REGISTER_EAX)
    {
        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, a3.reg, VM_REGISTER_EAX);
    }
}

static void vm_jit_append_string_string(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);
    const JIT_Allocation a3 = jitter->analyzer.GetAllocation(jitter->refIndex);

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)&jitter->_trace->_mm);
    
    vm_jit_mov(jitter, a1, VM_ARG2);
    vm_jit_mov(jitter, a2, VM_ARG3);

    vm_jit_call_internal_x64(jitter, (void*)vm_append_string_string);

    if (a3.reg != VM_REGISTER_EAX)
    {
        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, a3.reg, VM_REGISTER_EAX);
    }
}

static void vm_jit_append_string_real(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);
    const JIT_Allocation a3 = jitter->analyzer.GetAllocation(jitter->refIndex);

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)&jitter->_trace->_mm);

    vm_jit_mov(jitter, a1, VM_ARG2);
    vm_jit_mov_sse(jitter, a2, VM_SSE_ARG3);

    vm_jit_call_internal_x64(jitter, (void*)vm_append_string_real);

    if (a3.reg != VM_REGISTER_EAX)
    {
        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, a3.reg, VM_REGISTER_EAX);
    }
}

static void vm_jit_append_real_string(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);
    const JIT_Allocation a3 = jitter->analyzer.GetAllocation(jitter->refIndex);

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)&jitter->_trace->_mm);

    vm_jit_mov_sse(jitter, a1, VM_SSE_ARG2);
    vm_jit_mov(jitter, a2, VM_ARG3);

    vm_jit_call_internal_x64(jitter, (void*)vm_append_real_string);

    if (a3.reg != VM_REGISTER_EAX)
    {
        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, a3.reg, VM_REGISTER_EAX);
    }
}

static void vm_jit_add_real(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);
    const JIT_Allocation a3 = jitter->analyzer.GetAllocation(jitter->refIndex);

    const int dst = vm_jit_decode_dst_sse(a3);
    vm_jit_mov_sse(jitter, a1, dst);

    switch (a2.type)
    {
        case ST_REG:
            vm_addsd_reg_to_reg_x64(jitter->jit, jitter->count, dst, a2.reg);
            break;
        case ST_STACK:
            vm_addsd_memory_to_reg_x64(jitter->jit, jitter->count, dst, a2.reg, a2.pos);
            break;
    }

    if (a3.type == ST_STACK)
    {
        vm_movsd_reg_to_memory_x64(jitter->jit, jitter->count, a3.reg, a3.pos, dst);
    }
}

static void vm_jit_sub_real(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);
    const JIT_Allocation a3 = jitter->analyzer.GetAllocation(jitter->refIndex);

    const int dst = vm_jit_decode_dst_sse(a3);
    vm_jit_mov_sse(jitter, a2, dst);

    switch (a2.type)
    {
        case ST_REG:
            vm_subsd_reg_to_reg_x64(jitter->jit, jitter->count, a3.reg, a1.reg);
            break;
        case ST_STACK:
            vm_subsd_memory_to_reg_x64(jitter->jit, jitter->count, a3.reg, a1.reg, a1.pos);
            break;
    }

    if (a3.type == ST_STACK)
    {
        vm_movsd_reg_to_memory_x64(jitter->jit, jitter->count, a3.reg, a3.pos, dst);
    }
}

static void vm_jit_mul_real(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);
    const JIT_Allocation a3 = jitter->analyzer.GetAllocation(jitter->refIndex);

    const int dst = vm_jit_decode_dst_sse(a3);
    vm_jit_mov_sse(jitter, a1, dst);

    switch (a2.type)
    {
        case ST_REG:
            vm_mulsd_reg_to_reg_x64(jitter->jit, jitter->count, dst, a2.reg);
            break;
        case ST_STACK:
            vm_mulsd_memory_to_reg_x64(jitter->jit, jitter->count, dst, a2.reg, a2.pos);
            break;
    }

    if (a3.type == ST_STACK)
    {
        vm_movsd_reg_to_memory_x64(jitter->jit, jitter->count, a3.reg, a3.pos, dst);
    }
}

static void vm_jit_div_real(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);
    const JIT_Allocation a3 = jitter->analyzer.GetAllocation(jitter->refIndex);

    const int dst = vm_jit_decode_dst_sse(a3);
    vm_jit_mov_sse(jitter, a2, dst);

    switch (a2.type)
    {
        case ST_REG:
            vm_divsd_reg_to_reg_x64(jitter->jit, jitter->count, a3.reg, a1.reg);
            break;
        case ST_STACK:
            vm_divsd_memory_to_reg_x64(jitter->jit, jitter->count, a3.reg, a1.reg, a1.pos);
            break;
    }

    if (a3.type == ST_STACK)
    {
        vm_movsd_reg_to_memory_x64(jitter->jit, jitter->count, a3.reg, a3.pos, dst);
    }
}

static void vm_jit_add_int(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);
    const JIT_Allocation a3 = jitter->analyzer.GetAllocation(jitter->refIndex);

    const int dst = vm_jit_decode_dst(a3);
    vm_jit_mov(jitter, a1, dst);
    
    switch (a2.type)
    {
    case ST_REG:
        vm_add_reg_to_reg_x64(jitter->jit, jitter->count, dst, a2.reg);
        break;
    case ST_STACK:
        vm_add_memory_to_reg_x64(jitter->jit, jitter->count, dst, a2.reg, a2.pos);
        break;
    }

    if (a3.type == ST_STACK)
    {
        vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, a3.reg, a3.pos, dst);
    }
}

static void vm_jit_sub_int(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);
    const JIT_Allocation a3 = jitter->analyzer.GetAllocation(jitter->refIndex);
    
    const int dst = vm_jit_decode_dst(a3);
    vm_jit_mov(jitter, a2, dst);

    switch (a1.type)
    {
        case ST_REG:
            vm_sub_reg_to_reg_x64(jitter->jit, jitter->count, a3.reg, a1.reg);
            break;
        case ST_STACK:
            vm_sub_memory_to_reg_x64(jitter->jit, jitter->count, a3.reg, a1.reg, a1.pos);
            break;
    }

    if (a3.type == ST_STACK)
    {
        vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, a3.reg, a3.pos, dst);
    }
}

static void vm_jit_mul_int(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);
    const JIT_Allocation a3 = jitter->analyzer.GetAllocation(jitter->refIndex);

    const int dst = vm_jit_decode_dst(a3);
    vm_jit_mov(jitter, a1, dst);

    switch (a2.type)
    {
        case ST_REG:
            vm_mul_reg_to_reg_x64(jitter->jit, jitter->count, dst, a2.reg);
            break;
        case ST_STACK:
            vm_mul_memory_to_reg_x64(jitter->jit, jitter->count, dst, a2.reg, a2.pos);
            break;
    }

    if (a3.type == ST_STACK)
    {
        vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, a3.reg, a3.pos, dst);
    }
}

static void vm_jit_div_int(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);
    const JIT_Allocation a3 = jitter->analyzer.GetAllocation(jitter->refIndex);

    vm_jit_mov(jitter, a2, VM_REGISTER_EAX);
    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EDX, 0);
    switch (a1.type)
    {
        case ST_REG:
            vm_div_reg_x64(jitter->jit, jitter->count, a1.reg);
            break;
        case ST_STACK:
            vm_div_memory_x64(jitter->jit, jitter->count, a1.reg, a1.pos);
            break;
    }

    switch (a3.type)
    {
        case ST_REG:
            vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, a3.reg, VM_REGISTER_EAX);
            break;
        case ST_STACK:
            vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, a3.reg, a3.pos, VM_REGISTER_EAX);
            break;
    }
}

static void vm_jit_dec_int(Jitter* jitter)
{
    const int ref = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(jitter->refIndex);

    const int dst = vm_jit_decode_dst(a2);
    vm_jit_mov(jitter, a1, dst);
    
    vm_dec_reg_x64(jitter->jit, jitter->count, dst);
 
    if (a2.type == ST_STACK)
    {
        vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, a2.reg, a2.pos, dst);
    }
}

static void vm_jit_inc_int(Jitter* jitter)
{
    const int ref = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(jitter->refIndex);

    const int dst = vm_jit_decode_dst(a2);
    vm_jit_mov(jitter, a1, dst);
 
    vm_inc_reg_x64(jitter->jit, jitter->count, dst);

    if (a2.type == ST_STACK)
    {
        vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, a2.reg, a2.pos, dst);
    }
}

static void vm_jit_neg_int(Jitter* jitter)
{
    const int ref = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(jitter->refIndex);

    const int dst = vm_jit_decode_dst(a2);
    vm_jit_mov(jitter, a1, dst);

    vm_neg_reg_x64(jitter->jit, jitter->count, dst);

    if (a2.type == ST_STACK)
    {
        vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, a2.reg, a2.pos, dst);
    }
}

static void vm_jit_neg_real(Jitter* jitter)
{
    const int ref = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(jitter->refIndex);

    const int dst = vm_jit_decode_dst(a2);

    vm_xorpd_reg_to_reg_x64(jitter->jit, jitter->count, dst, dst);
    vm_subsd_reg_to_reg_x64(jitter->jit, jitter->count, dst, a1.reg);

    if (a2.type == ST_STACK)
    {
        vm_movsd_reg_to_memory_x64(jitter->jit, jitter->count, a2.reg, a2.pos, dst);
    }
}

static void vm_jit_load_real(Jitter* jitter)
{
    const int offset = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a = jitter->analyzer.GetAllocation(jitter->refIndex);
    unsigned char* src = jitter->jit + jitter->count;
    src += 8; // size of instruction
    if (a.reg >= VM_SSE_REGISTER_XMM8)
    {
        /* IF we have the REX byte */
        src++;
    }

    std::ptrdiff_t mem = (reinterpret_cast<unsigned char*>(jitter->_trace->_constantPage) + offset) - src;
    assert(int(mem) < INT_MAX && int(mem) > INT_MIN);
    vm_movsd_memory_to_reg_x64(jitter->jit, jitter->count, a.reg, int(mem));
}

static void vm_jit_load_string(Jitter* jitter)
{
    const int offset = vm_jit_read_int(jitter->program, jitter->pc);
    unsigned char* data = reinterpret_cast<unsigned char*>(jitter->_trace->_constantPage) + offset;

    const JIT_Allocation a = jitter->analyzer.GetAllocation(jitter->refIndex);

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, a.reg, (long long)data);
}

static void vm_jit_load_int(Jitter* jitter)
{
    const int offset = vm_jit_read_int(jitter->program, jitter->pc);
    const int value = *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(jitter->_trace->_constantPage) + offset);

    const JIT_Allocation al = jitter->analyzer.GetAllocation(jitter->refIndex);

    switch (al.type)
    {
    case ST_REG:
        vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, al.reg, value);
        break;
    case ST_STACK:
        vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, value);
        vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, al.reg, al.pos, VM_REGISTER_EAX);
        break;
    }
}

static void vm_jit_call_push_stub(Jitter* jitter, VirtualMachine* vm, unsigned char* jit, int& count)
{
    const int type = jitter->program[*jitter->pc];
    (*jitter->pc)++;
    const int ref = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation al = jitter->analyzer.GetAllocation(ref);

    switch (al.type)
    {
    case ST_REG:
        if (al.isSSE)
        {
            vm_movsd_reg_to_reg_x64(jit, count, VM_SSE_ARG2, al.reg);
        }
        else
        {
            vm_mov_reg_to_reg_x64(jit, count, VM_ARG2, al.reg);
        }
        break;
    case ST_STACK:
        if (al.isSSE)
        {
            vm_movsd_memory_to_reg_x64(jit, count, VM_SSE_ARG2, al.reg);
        }
        else
        {
            vm_mov_memory_to_reg_x64(jit, count, VM_ARG2, al.reg, al.pos);
        }
        break;
    }

    switch (type)
    {
    case IR_INT_ARG:
        vm_mov_imm_to_reg_x64(jit, count, VM_ARG4, (long long)vm_push_int_stub);
        break;
    case IR_STRING_ARG:
        vm_mov_imm_to_reg_x64(jit, count, VM_ARG4, (long long)vm_push_string_stub);
        break;
    case IR_REAL_ARG:
        vm_mov_imm_to_reg_x64(jit, count, VM_ARG4, (long long)vm_push_real_stub);
        break;
    }

    // Store the VM pointer in ARG1.
    // We do this each time in case the register is cleared.
    vm_mov_imm_to_reg_x64(jit, count, VM_ARG1, (long long)vm);

    // Call the function.
    vm_call_absolute(jit, count, VM_ARG4);
}

static void vm_jit_call_x64(VirtualMachine* vm, Jitter* jitter, int numParams, const char* name)
{
    // Calls are the in the form:
    // 
    // R13 - CALLEE ADDR
    // ARG1 - VM ADDR
    // ARG2 - PARAMETER

    for (int i = 0; i < numParams; i++)
    {
        vm_jit_call_push_stub(jitter, vm, jitter->jit, jitter->count);
    }

    // Store the VM pointer in ARG1.
    // We do this each time in case the register is cleared.
    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)vm);
    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG2, (long long)&jitter->_manager->_mm);
    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG3, (long long)name);
    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG4, numParams);
    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, (long long)vm_call_stub);
    vm_call_absolute(jitter->jit, jitter->count, VM_REGISTER_EAX);
}

static void vm_jit_call(VirtualMachine* vm, Jitter* jitter)
{
    const int id = vm_jit_read_int(jitter->program, jitter->pc);

    const int numParams = jitter->program[*jitter->pc];
    (*jitter->pc)++;

    const char* name = FindFunctionName(vm, id);
    assert(name);

    vm_jit_call_x64(vm, jitter, numParams, name);

    // Handle return value; we need to check the
    // return value is the type we are expecting to get back.
    // Otherwise abort the trace.

    JIT_Allocation al = jitter->analyzer.GetAllocation(jitter->refIndex);
    switch (al.type)
    {
    case ST_STACK:
        vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, al.reg, al.pos, VM_REGISTER_EAX);
        break;
    case ST_REG:
        if (al.reg != VM_REGISTER_EAX)
        {
            vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, al.reg, VM_REGISTER_EAX);
        }
        break;
    }

    jitter->refIndex += numParams;
}

static void vm_jit_yield(VirtualMachine* vm, Jitter* jitter)
{
    const int id = vm_jit_read_int(jitter->program, jitter->pc);

    const int numParams = jitter->program[*jitter->pc];
    (*jitter->pc)++;
    
    const char* name = FindFunctionName(vm, id);
    assert(name);

    // First we do call_x64
    vm_jit_call_x64(vm, jitter, numParams, name);

    // Then we make a call to 'vm_yield'.
    // Which will record the stack pointer and the instruction pointer.
    // Then we will copy the stack from the initial entry point in the Sunscript to the heap.
    // Then we will set the vm state to paused and move the stack pointer upwards and jump to code which invoked sunscript.
    // Volatile register will be pushed to the stack before where the instruction pointer ends (hopefully this will preserve the registers)
    vm_jit_call_internal_x64(jitter, jitter->_manager->_co._vm_suspend);

    // Upon resuming we need to copy the stack back from the heap and the jump back to the resumption point.

    jitter->refIndex += numParams;
}

static void vm_jit_box(VirtualMachine* vm, Jitter* jitter)
{
    const int id = vm_jit_read_int(jitter->program, jitter->pc);

    JIT_Allocation a1 = jitter->analyzer.GetAllocation(id); // src
    JIT_Allocation a2 = jitter->analyzer.GetAllocation(jitter->refIndex); // dst

    const int type = jitter->program[*jitter->pc];
    (*jitter->pc)++;

    const int dst = vm_jit_decode_dst(a2);
    if (!a1.isSSE)
    {
        vm_jit_mov(jitter, a1, dst);
    }

    // Box

    switch (type)
    {
    case TY_INT:
        vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)&jitter->_manager->_mm);
        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG2, dst);
        vm_jit_call_internal_x64(jitter, (void*)vm_box_int);
        break;
    case TY_FUNC:
        vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)&jitter->_manager->_mm);
        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG2, dst);
        vm_jit_call_internal_x64(jitter, (void*)vm_box_func);
        break;
    case TY_REAL:
        vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)&jitter->_manager->_mm);
        if (a1.type == ST_REG)
        {
            vm_movsd_reg_to_reg_x64(jitter->jit, jitter->count, VM_SSE_ARG2, a1.reg);
        }
        else
        {
            vm_movsd_memory_to_reg_x64(jitter->jit, jitter->count, VM_SSE_ARG2, a1.reg, a1.pos);
        }
        vm_jit_call_internal_x64(jitter, (void*)vm_box_real);
        break;
    case TY_STRING:

        // Duplicate the string, since if the string is a constant it doesn't exist in the memory manager.
        // Therefore, when it is unboxed it fails because we unable ascertain the type.

        vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)&jitter->_manager->_mm);
        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG2, dst);
        vm_jit_call_internal_x64(jitter, (void*)vm_duplicate_string);
        break;
    }

    switch (type)
    {
    case TY_REAL:
    case TY_INT:
    case TY_FUNC:
    case TY_STRING:
        if (a2.type == ST_REG)
        {
            vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, a2.reg, VM_REGISTER_EAX);
        }
        else
        {
            vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, a2.reg, a2.pos, VM_REGISTER_EAX);
        }
        break;
    }
}

static void vm_jit_unbox(VirtualMachine* vm, Jitter* jitter)
{
    const int id = vm_jit_read_int(jitter->program, jitter->pc);

    JIT_Allocation al = jitter->analyzer.GetAllocation(id); // src
    JIT_Allocation al2 = jitter->analyzer.GetAllocation(jitter->refIndex); // dst

    const int type = jitter->program[*jitter->pc];
    (*jitter->pc)++;

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)&jitter->_manager->_mm);
    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG2, VM_REGISTER_EAX);
    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG3, type);
    vm_jit_call_internal_x64(jitter, (void*)vm_check_type);

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, VM_ERROR);  // use Arg1 to hold this
    vm_cmp_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, VM_REGISTER_EAX);

    JIT_Guard& guard = jitter->_trace->_forwardJumps.emplace_back();
    guard._type = JUMP_E;
    guard._offset = jitter->count;
    guard._state = PATCH_INITIALIZED;
    guard._pos = *jitter->pc;
    guard._snap = jitter->snapshot;
    guard._ref = jitter->refIndex;

    vm_jump_equals(jitter->jit, jitter->count, 0); // patch

    guard._size = jitter->count - guard._offset;

    // Unbox

    int dst;
    switch (type)
    {
    case TY_INT:
    case TY_FUNC:
        dst = vm_jit_decode_dst(al2);
        vm_jit_mov(jitter, al, VM_REGISTER_EAX);
        vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, dst, VM_REGISTER_EAX, 0); // de-reference

        switch (al2.type)
        {
        case ST_STACK:
            vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, al2.reg, al2.pos, dst);
            break;
        case ST_REG:
            //vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, al.reg, VM_REGISTER_EAX);
            break;
        }

        break;
    case TY_REAL:
        dst = vm_jit_decode_dst_sse(al2);
        vm_jit_mov(jitter, al, VM_REGISTER_EAX);
        vm_movsd_memory_to_reg_x64(jitter->jit, jitter->count, dst, VM_REGISTER_EAX, 0); // de-reference

        switch (al2.type)
        {
        case ST_STACK:
            vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, al2.reg, al2.pos, dst);
            break;
        case ST_REG:
            //vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, al.reg, VM_REGISTER_EAX);
            break;
        }
        break;
    default:
        dst = vm_jit_decode_dst(al2);
        vm_jit_mov(jitter, al, dst);
        break;
    }
}

inline static void vm_jit_epilog(Jitter* jitter, const int stacksize)
{
    vm_add_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_ESP, stacksize);
}

static void vm_jit_conv_int_to_real(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);

    JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1); // int
    JIT_Allocation a2 = jitter->analyzer.GetAllocation(jitter->refIndex); // real

    const int dst = vm_jit_decode_dst_sse(a2);

    switch (a1.type)
    {
        case ST_REG:
            vm_cvtitod_reg_to_reg_x64(jitter->jit, jitter->count, dst, a1.reg);
            break;
        case ST_STACK:
            vm_cvtitod_memory_to_reg_x64(jitter->jit, jitter->count, dst, a1.reg, a1.pos);
            break;
    }

    if (a2.type == ST_STACK)
    {
        vm_jit_mov_sse(jitter, a2, dst);
    }
}

static void vm_jit_phi(VirtualMachine* vm, Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);

    JIT_Phi& phi = jitter->_trace->_phis.emplace_back();
    phi._state = PATCH_INITIALIZED;
    phi._pos = jitter->refIndex;
    phi._left = ref1;
    phi._right = ref2;

    JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    JIT_Allocation a2 = jitter->analyzer.GetAllocation(jitter->refIndex);

    const int dst = vm_jit_decode_dst(a2);
    vm_jit_mov(jitter, a1, dst);

    if (a2.type == ST_STACK)
    {
        vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, a2.reg, a2.pos, dst);
    }
}

static void vm_jit_snap(VirtualMachine* vm, Jitter* jitter)
{
    jitter->snapshot = jitter->program[*jitter->pc];
    jitter->snapRef = jitter->refIndex;
    (*jitter->pc)++;

    auto& snap = jitter->_trace->_snaps.emplace_back();
    snap._ref = jitter->refIndex;

    const int numSlots = jitter->program[*jitter->pc];
    (*jitter->pc)++;

    for (int i = 0; i < numSlots; i++)
    {
        snap.entries.push_back(jitter->program[*jitter->pc]);

        (*jitter->pc)++;
    }
}

static int vm_jit_store(Jitter* jitter, JIT_LiveValue& value)
{
    if (value.al.isSSE)
    {
        const int dst = vm_jit_decode_dst_sse(value.al);
        vm_jit_mov_sse(jitter, value.al, dst);
        vm_sub_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_ESP, 8 /*64 bit*/);
        vm_movsd_reg_to_memory_x64(jitter->jit, jitter->count, VM_REGISTER_ESP, dst, 0);
    }
    else
    {
        const int dst = vm_jit_decode_dst(value.al);
        vm_jit_mov(jitter, value.al, dst);
        vm_push_reg(jitter->jit, jitter->count, dst);
    }

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, value.ref);
    vm_push_reg(jitter->jit, jitter->count, VM_REGISTER_EAX);

    return 8 * 2; // 8 bytes for each register * number pushed
}

static void vm_jit_store_snapshot(VirtualMachine* vm, Jitter* jitter, const int ref, const int snap)
{
    /* Store snapshot
       Get variables and push the registers/stack items onto the stack.
       The live values could be temporaries.
    */

    const auto& snapshot = jitter->_trace->_snaps[snap];

    std::vector<JIT_LiveValue> live;
    jitter->analyzer.GetLiveValues(snapshot._ref, live);

    assert(snapshot.entries.size() <= live.size());

    int snapshotsize = 0;

    for (size_t i = 0; i < live.size(); i++)
    {
        snapshotsize += vm_jit_store(jitter, live[i]);
    }

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, ref);
    vm_push_reg(jitter->jit, jitter->count, VM_REGISTER_EAX);
    snapshotsize += 8;

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, live.size());
    vm_push_reg(jitter->jit, jitter->count, VM_REGISTER_EAX);
    snapshotsize += 8;

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG3, snapshotsize);
    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG4, snap);

    // We will do a jump after this which will use the argument(s)
}

static void vm_jit_exit_trace(VirtualMachine* vm, Jitter* jitter, const int stacksize)
{
    // Standard exit

    vm_jit_store_snapshot(vm, jitter, jitter->snapRef, jitter->snapshot);
    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)vm);
    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG2, VM_REGISTER_ESP);

    // TODO: For the ABI we really should move these into the space before the register homes.
    // Until then we duplicate the register homes down here.
    // 
    // Pushing them to the stack and then calling a method may break things
    // as they may get overwritten as they are the 'register homes'
    const int register_homes = 8 * 4; // 4 register homes 8 bytes each
    vm_sub_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_ESP, register_homes);

    // Generate a call to vm_restore_snapshot
    // ARG1 = vm
    // ARG2 = ESP
    // ARG3 = size
    // ARG4 = snap no

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, (long long)vm_restore_snapshot);
    vm_call_absolute(jitter->jit, jitter->count, VM_REGISTER_EAX);

    // The return value is the size
    vm_add_reg_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_ESP, VM_REGISTER_EAX);
    vm_add_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_ESP, register_homes);

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, VM_OK);
    vm_jit_epilog(jitter, stacksize);
    vm_return(jitter->jit, jitter->count);

    // Patch guard failures
    if (jitter->_trace->_forwardJumps.size() > 0)
    {
        std::vector<JIT_Guard> jumps;
        jumps.reserve(jitter->_trace->_forwardJumps.size() - 1);

        for (size_t i = 0; i < jitter->_trace->_forwardJumps.size(); i++)
        {
            auto& guard = jitter->_trace->_forwardJumps[i];

            vm_jit_patch_jump(jitter, guard);

            vm_jit_store_snapshot(vm, jitter, guard._ref, guard._snap);

            // The last guard handler can fall through to the epilog
            if (i < jitter->_trace->_forwardJumps.size() - 1)
            {
                auto& jump = jumps.emplace_back();
                jump._offset = jitter->count;
                jump._type = JUMP;
                jump._state = PATCH_INITIALIZED;
                jump._pos = *jitter->pc;
                jump._snap = 0;
                jump._ref = 0;

                vm_jit_jump(jitter, jump._type, jitter->count, 0); // to be patched

                jump._size = jitter->count - jump._offset;
            }
        }

        for (auto& jump : jumps)
        {
            vm_jit_patch_jump(jitter, jump);
        }

        // We need to store the VM_REGISTER_ESP before adjusting for the register homes
        vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)vm);
        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG2, VM_REGISTER_ESP);
        
        // TODO: For the ABI we really should move these into the space before the register homes.
        // Until then we duplicate the register homes down here.
        // 
        // Pushing them to the stack and then calling a method may break things
        // as they may get overwritten as they are the 'register homes'
        const int register_homes = 8 * 4; // 4 register homes 8 bytes each
        vm_sub_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_ESP, register_homes);

        // Generate a call to vm_restore_snapshot
        // ARG1 = vm
        // ARG2 = ESP
        // ARG3 = size
        // ARG4 = snap no

        vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, (long long)vm_restore_snapshot);
        vm_call_absolute(jitter->jit, jitter->count, VM_REGISTER_EAX);

        // The return value is the size
        vm_add_reg_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_ESP, VM_REGISTER_EAX);
        vm_add_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_ESP, register_homes);

        vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, VM_ERROR);
        vm_jit_epilog(jitter, stacksize);
        vm_return(jitter->jit, jitter->count);
    }
}

static void vm_jit_load_string_local(VirtualMachine* vm, Jitter* jitter)
{
    const int id = jitter->program[*jitter->pc];
    (*jitter->pc)++;

    JIT_Allocation allocation = jitter->analyzer.GetAllocation(jitter->refIndex);

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)&jitter->_trace->_record);
    vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, VM_ARG1, 0);

    const int dst = vm_jit_decode_dst(allocation);
    vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, dst, VM_ARG1, id * 16 + 8);
}

static void vm_jit_load_int_local(VirtualMachine* vm, Jitter* jitter)
{
    const int id = jitter->program[*jitter->pc];
    (*jitter->pc)++;

    JIT_Allocation allocation = jitter->analyzer.GetAllocation(jitter->refIndex);

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)&jitter->_trace->_record);
    vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, VM_ARG1, 0);

    const int dst = vm_jit_decode_dst(allocation);
    vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, dst, VM_ARG1, id * 16 + 8);
}

static void vm_jit_load_real_local(VirtualMachine* vm, Jitter* jitter)
{
    const int id = jitter->program[*jitter->pc];
    (*jitter->pc)++;

    JIT_Allocation allocation = jitter->analyzer.GetAllocation(jitter->refIndex);

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)&jitter->_trace->_record);
    vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, VM_ARG1, 0);

    const int dst = vm_jit_decode_dst_sse(allocation);
    vm_movsd_memory_to_reg_x64(jitter->jit, jitter->count, dst, VM_ARG1, id * 16 + 8);
}

static void vm_jit_load_table_local(VirtualMachine* vm, Jitter* jitter)
{
    const int id = jitter->program[*jitter->pc];
    (*jitter->pc)++;

    JIT_Allocation allocation = jitter->analyzer.GetAllocation(jitter->refIndex);

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)&jitter->_trace->_record);
    vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, VM_ARG1, 0);

    const int dst = vm_jit_decode_dst(allocation);
    vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, dst, VM_ARG1, id * 16 + 8);
}

static void vm_jit_table_new(VirtualMachine* vm, Jitter* jitter)
{
    JIT_Allocation allocation = jitter->analyzer.GetAllocation(jitter->refIndex);

    // Emit a call to the table allocation function

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)&jitter->_trace->_mm);
    vm_jit_call_internal_x64(jitter, (void*)vm_table_new);

    switch (allocation.type)
    {
    case ST_REG:
        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, allocation.reg, VM_REGISTER_EAX);
        break;
    case ST_STACK:
        vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, allocation.reg, allocation.pos, VM_REGISTER_EAX);
        break;
    }
}

static void vm_jit_table_hget(VirtualMachine* vm, Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);

    JIT_Allocation allocation = jitter->analyzer.GetAllocation(jitter->refIndex);
    JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);
 
    // Emit a call to the table hashmap get function

    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, a2.reg);
    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG2, a1.reg);
    vm_jit_call_internal_x64(jitter, (void*)vm_table_hget);

    switch (allocation.type)
    {
    case ST_REG:
        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, allocation.reg, VM_REGISTER_EAX);
        break;
    case ST_STACK:
        vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, allocation.reg, allocation.pos, VM_REGISTER_EAX);
        break;
    }
}

static void vm_jit_table_aget(VirtualMachine* vm, Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);

    JIT_Allocation allocation = jitter->analyzer.GetAllocation(jitter->refIndex);
    JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);

    // Emit a call to the table array get function

    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, a2.reg);
    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG2, a1.reg);
    vm_jit_call_internal_x64(jitter, (void*)vm_table_aget);

    switch (allocation.type)
    {
    case ST_REG:
        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, allocation.reg, VM_REGISTER_EAX);
        break;
    case ST_STACK:
        vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, allocation.reg, allocation.pos, VM_REGISTER_EAX);
        break;
    }
}

static void vm_jit_table_hset(VirtualMachine* vm, Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);

    JIT_Allocation allocation = jitter->analyzer.GetAllocation(jitter->refIndex);
    JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);

    // Emit a call to the table hashmap set function

    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG3, a2.reg);
    vm_jit_call_internal_x64(jitter, (void*)vm_table_hset);
}

static void vm_jit_table_aset(VirtualMachine* vm, Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);

    JIT_Allocation allocation = jitter->analyzer.GetAllocation(jitter->refIndex);
    JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);

    // Emit a call to the table array set function

    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG3, a2.reg);
    vm_jit_call_internal_x64(jitter, (void*)vm_table_aset);
}

static void vm_jit_table_href(VirtualMachine* vm, Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);

    JIT_Allocation allocation = jitter->analyzer.GetAllocation(jitter->refIndex);
    JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);

    // Emit a call to the table hashmap set function

    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, a2.reg);
    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG2, a1.reg);
}

static void vm_jit_table_aref(VirtualMachine* vm, Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);

    JIT_Allocation allocation = jitter->analyzer.GetAllocation(jitter->refIndex);
    JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);

    // Emit a call to the table array set function

    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, a2.reg);
    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG2, a1.reg);
}

static void vm_jit_generate_trace(VirtualMachine* vm, Jitter* jitter)
{
    // Store non-volatile registers

    const int stackNeeded = (32 /* 4 register homes for callees */ + 8 * jitter->analyzer.StackSize()/*space for locals*/);
    const int totalSize = VM_ALIGN_16(stackNeeded + 8);
    const int stacksize = totalSize - 8;

    assert((stacksize + 8/*pc push by call*/) % 16 == 0); // Check the stack is 16-byte aligned

    vm_sub_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_ESP, stacksize); // grow stack

    while (jitter->running && *jitter->pc < size_t(jitter->size))
    {
        //std::cout << "INS " << *jitter->pc << " " << int(jitter->program[*jitter->pc]) << std::endl;
        const int ins = jitter->program[(*(jitter->pc))++];

        switch (ins)
        {
        case IR_LOAD_INT:
            vm_jit_load_int(jitter);
            break;
        case IR_LOAD_STRING:
            vm_jit_load_string(jitter);
            break;
        case IR_LOAD_REAL:
            vm_jit_load_real(jitter);
            break;
        case IR_ADD_INT:
            vm_jit_add_int(jitter);
            break;
        case IR_ADD_REAL:
            vm_jit_add_real(jitter);
            break;
        case IR_APP_STRING_INT:
            vm_jit_append_string_int(jitter);
            break;
        case IR_APP_STRING_STRING:
            vm_jit_append_string_string(jitter);
            break;
        case IR_APP_INT_STRING:
            vm_jit_append_int_string(jitter);
            break;
        case IR_APP_REAL_STRING:
            vm_jit_append_real_string(jitter);
            break;
        case IR_APP_STRING_REAL:
            vm_jit_append_string_real(jitter);
            break;
        case IR_CALL:
            vm_jit_call(vm, jitter);
            break;
        case IR_CMP_INT:
            vm_jit_cmp_int(jitter);
            break;
        case IR_CMP_STRING:
            vm_jit_cmp_string(jitter);
            break;
        case IR_CMP_REAL:
            vm_jit_cmp_real(jitter);
            break;
        case IR_CMP_TABLE:
            vm_jit_cmp_table(jitter);
            break;
        case IR_CONV_INT_TO_REAL:
            vm_jit_conv_int_to_real(jitter);
            break;
        case IR_DECREMENT_INT:
            vm_jit_dec_int(jitter);
            break;
        case IR_DIV_INT:
            vm_jit_div_int(jitter);
            break;
        case IR_DIV_REAL:
            vm_jit_div_real(jitter);
            break;
        case IR_GUARD:
            vm_jit_guard(jitter);
            break;
        case IR_INCREMENT_INT:
            vm_jit_inc_int(jitter);
            break;
        case IR_LOOPBACK:
            vm_jit_loopback(jitter);
            break;
        case IR_LOOPSTART:
            vm_jit_startloop(jitter);
            break;
        case IR_LOOPEXIT:
            vm_jit_exitloop(jitter);
            break;
        case IR_MUL_INT:
            vm_jit_mul_int(jitter);
            break;
        case IR_MUL_REAL:
            vm_jit_mul_real(jitter);
            break;
        case IR_SUB_INT:
            vm_jit_sub_int(jitter);
            break;
        case IR_SUB_REAL:
            vm_jit_sub_real(jitter);
            break;
        case IR_UNARY_MINUS_INT:
            vm_jit_neg_int(jitter);
            break;
        case IR_UNARY_MINUS_REAL:
            vm_jit_neg_real(jitter);
            break;
        case IR_YIELD:
            vm_jit_yield(vm, jitter);
            break;
        case IR_PHI:
            vm_jit_phi(vm, jitter);
            break;
        case IR_SNAP:
            vm_jit_snap(vm, jitter);
            break;
        case IR_UNBOX:
            vm_jit_unbox(vm, jitter);
            break;
        case IR_BOX:
            vm_jit_box(vm, jitter);
            break;
        case IR_NOP:
            // Nothing
            break;
        case IR_LOAD_INT_LOCAL:
            vm_jit_load_int_local(vm, jitter);
            break;
        case IR_LOAD_STRING_LOCAL:
            vm_jit_load_string_local(vm, jitter);
            break;
        case IR_LOAD_REAL_LOCAL:
            vm_jit_load_real_local(vm, jitter);
            break;
        case IR_LOAD_TABLE_LOCAL:
            vm_jit_load_table_local(vm, jitter);
            break;
        case IR_TABLE_NEW:
            vm_jit_table_new(vm, jitter);
            break;
        case IR_TABLE_AGET:
            vm_jit_table_aget(vm, jitter);
            break;
        case IR_TABLE_HGET:
            vm_jit_table_hget(vm, jitter);
            break;
        case IR_TABLE_ASET:
            vm_jit_table_aset(vm, jitter);
            break;
        case IR_TABLE_HSET:
            vm_jit_table_hset(vm, jitter);
            break;
        case IR_TABLE_AREF:
            vm_jit_table_aref(vm, jitter);
            break;
        case IR_TABLE_HREF:
            vm_jit_table_href(vm, jitter);
            break;
        default:
            abort();
        }

        jitter->refIndex++;
    }

    vm_jit_exit_trace(vm, jitter, stacksize);
}

static void vm_jit_push_registers(unsigned char* jit, int& count)
{
    vm_push_reg(jit, count, VM_REGISTER_R12);
    vm_push_reg(jit, count, VM_REGISTER_R13);
    vm_push_reg(jit, count, VM_REGISTER_R14);
    vm_push_reg(jit, count, VM_REGISTER_R15);
    vm_push_reg(jit, count, VM_REGISTER_EDI);
    vm_push_reg(jit, count, VM_REGISTER_ESI);
    vm_push_reg(jit, count, VM_REGISTER_EBX);
}

static void vm_jit_pop_registers(unsigned char* jit, int& count)
{
    vm_pop_reg(jit, count, VM_REGISTER_EBX);
    vm_pop_reg(jit, count, VM_REGISTER_ESI);
    vm_pop_reg(jit, count, VM_REGISTER_EDI);
    vm_pop_reg(jit, count, VM_REGISTER_R15);
    vm_pop_reg(jit, count, VM_REGISTER_R14);
    vm_pop_reg(jit, count, VM_REGISTER_R13);
    vm_pop_reg(jit, count, VM_REGISTER_R12);
}

static void vm_jit_suspend(JIT_Manager* manager)
{
    unsigned char jit[1024];
    int count = 0;
    
    // Store volatile registers

    vm_jit_push_registers(jit, count);

    const int stacksize = VM_ALIGN_16(32 /* 4 register homes for callees */);

    vm_sub_imm_to_reg_x64(jit, count, VM_REGISTER_ESP, stacksize); // grow stack

    // TODO: dynamically reserve
    void* mem = manager->_mm.New(2048, TY_OBJECT);

    // Calculate stack size
    // StackPtr - ESP
    vm_mov_imm_to_reg_x64(jit, count, VM_ARG3, (long long)&manager->_co._stackPtr);
    vm_mov_memory_to_reg_x64(jit, count, VM_ARG3, VM_ARG3, 0);
    vm_sub_reg_to_reg_x64(jit, count, VM_ARG3, VM_REGISTER_ESP);

    // Store stack size - for resumption
    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EAX, (long long)&manager->_co._stackSize);
    vm_mov_reg_to_memory_x64(jit, count, VM_REGISTER_EAX, 0, VM_ARG3);

    // Generate a call to memcpy
    // Dst (ARG1 = mem)
    // Src (ARG2 = ESP)
    // Size (ARG3 = size)

    vm_mov_imm_to_reg_x64(jit, count, VM_ARG1, (long long)mem);
    vm_mov_reg_to_reg_x64(jit, count, VM_ARG2, VM_REGISTER_ESP);
    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EAX, (long long)&std::memcpy);
    vm_call_absolute(jit, count, VM_REGISTER_EAX);

    // Now we go back to the past
    // 1) Reset the ESP register

    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EAX, (long long)&manager->_co._stackPtr);
    vm_mov_memory_to_reg_x64(jit, count, VM_REGISTER_ESP, VM_REGISTER_EAX, 0);
    
    // 2) Jump to the return point

    void* stub = (unsigned char*) manager->_co._yield_resume;
    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EAX, (long long)stub);
    vm_jump_absolute(jit, count, VM_REGISTER_EAX);

    // Record the resumption point.

    const int resume = count;

    // Update the ESP to point to the bottom

    vm_mov_imm_to_reg_x64(jit, count, VM_ARG3, (long long)&manager->_co._stackSize);
    vm_sub_memory_to_reg_x64(jit, count, VM_REGISTER_ESP, VM_ARG3, 0);
    vm_sub_imm_to_reg_x64(jit, count, VM_REGISTER_ESP, -8);     // make an adjustment (for some reason?)

    // Now we restore the stack
    // Dst (ARG1 = VM_REGISTER_ESP)
    // Src (ARG2 = mem)
    // Size (ARG3 = size)

    vm_mov_memory_to_reg_x64(jit, count, VM_ARG3, VM_ARG3, 0);
    vm_mov_imm_to_reg_x64(jit, count, VM_ARG2, (long long)mem);
    vm_mov_reg_to_reg_x64(jit, count, VM_ARG1, VM_REGISTER_ESP);
    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EAX, (long long)&std::memcpy);
    vm_call_absolute(jit, count, VM_REGISTER_EAX);

    // Epilogue

    vm_add_imm_to_reg_x64(jit, count, VM_REGISTER_ESP, stacksize);
    vm_jit_pop_registers(jit, count);
    vm_return(jit, count);

    // Finalize

    manager->_co._vm_suspend = vm_allocate(count);
    std::memcpy(manager->_co._vm_suspend, jit, count);
    vm_initialize(manager->_co._vm_suspend, count);

    manager->_co._vm_resume = (unsigned char*)manager->_co._vm_suspend + resume;
}

static void vm_jit_yielded(JIT_Manager* manager)
{
    unsigned char jit[1024];
    int count = 0;

    // Store volatile registers

    vm_jit_push_registers(jit, count);

    const int stacksize = VM_ALIGN_16(32 /* 4 register homes for callees */);

    vm_sub_imm_to_reg_x64(jit, count, VM_REGISTER_ESP, stacksize); // grow stack

    // Record resume point

    const int resumePosition = count;

    // Return value - VM_YIELDED

    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EAX, VM_YIELDED);

    // Epilogue

    vm_add_imm_to_reg_x64(jit, count, VM_REGISTER_ESP, stacksize);
    vm_jit_pop_registers(jit, count);
    vm_return(jit, count);

    // Finalize

    manager->_co._vm_yielded = vm_allocate(count);
    std::memcpy(manager->_co._vm_yielded, jit, count);
    vm_initialize(manager->_co._vm_yielded, count);

    manager->_co._yield_resume = (unsigned char*)manager->_co._vm_yielded + resumePosition;
}

static void vm_jit_entry_stub(JIT_Manager* manager)
{
    unsigned char jit[1024];
    int count = 0;

    // Store volatile registers

    vm_jit_push_registers(jit, count);

    const int stacksize = VM_ALIGN_16(32 /* 4 register homes for callees */);

    vm_sub_imm_to_reg_x64(jit, count, VM_REGISTER_ESP, stacksize); // grow stack

    // Record the stack pointer

    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EDX, (long long)&manager->_co._stackPtr);
    vm_mov_reg_to_memory_x64(jit, count, VM_REGISTER_EDX, 0, VM_REGISTER_ESP);

    // Call the start method
    // This will place a return value into VM_REGISTER_EAX.
    // We just need to propagate this.
    
    vm_call_absolute(jit, count, VM_ARG1);

    // Epilogue

    vm_add_imm_to_reg_x64(jit, count, VM_REGISTER_ESP, stacksize);
    vm_jit_pop_registers(jit, count);
    vm_return(jit, count);

    // Finalize

    manager->_co._vm_stub = vm_allocate(count);
    std::memcpy(manager->_co._vm_stub, jit, count);
    vm_initialize(manager->_co._vm_stub, count);
}

//========================

#ifdef WIN32
#include <intrin.h>
int SunScript::JIT_Capabilities(char vendor[13])
{
    int regs[4]; /* EAX EBX ECX EDX */
    __cpuid(regs, 0x0);

    std::memcpy(&vendor[0], &regs[1], sizeof(int));
    std::memcpy(&vendor[4], &regs[3], sizeof(int));
    std::memcpy(&vendor[8], &regs[2], sizeof(int));

    vendor[12] = '\0';

    __cpuid(regs, 0x1);

    int flags = 0x0;
    if (regs[2] & 1) { flags |= SUN_CAPS_SSE3; }
    if ((regs[2] >> 19) & 1) { flags |= SUN_CAPS_SSE4_1; }
    if ((regs[2] >> 20) & 1) { flags |= SUN_CAPS_SSE4_2; }

    return flags;
}
#else
#include <cpuid.h>
int SunScript::JIT_Capabilities(char vendor[13])
{
    unsigned int regs[4]; /* EAX EBX ECX EDX */
    __get_cpuid(0x0, &regs[0], &regs[1], &regs[2], &regs[3]);

    std::memcpy(&vendor[0], &regs[1], sizeof(unsigned int));
    std::memcpy(&vendor[4], &regs[3], sizeof(unsigned int));
    std::memcpy(&vendor[8], &regs[2], sizeof(unsigned int));

    vendor[12] = '\0';

    __get_cpuid(0x1, &regs[0], &regs[1], &regs[2], &regs[3]);

    int flags = 0x0;
    if (regs[2] & 1) { flags |= SUN_CAPS_SSE3; }
    if ((regs[2] >> 19) & 1) { flags |= SUN_CAPS_SSE4_1; }
    if ((regs[2] >> 20) & 1) { flags |= SUN_CAPS_SSE4_2; }

    return flags;
}
#endif

void SunScript::JIT_Setup(Jit* jit)
{
    jit->jit_initialize = SunScript::JIT_Initialize;
    jit->jit_compile_trace = SunScript::JIT_CompileTrace;
    jit->jit_execute = SunScript::JIT_ExecuteTrace;
    jit->jit_resume = SunScript::JIT_Resume;
    jit->jit_shutdown = SunScript::JIT_Shutdown;
}

void* SunScript::JIT_Initialize()
{
    JIT_Manager* manager = new JIT_Manager();
    vm_jit_entry_stub(manager); // must generate stub before suspend
    vm_jit_yielded(manager);    // must generate yielded before suspend
    vm_jit_suspend(manager);
    return manager;
}

int SunScript::JIT_ExecuteTrace(void* instance, void* data, unsigned char* record)
{
    JIT_Manager* mm = reinterpret_cast<JIT_Manager*>(instance);
    JIT_Trace* trace = reinterpret_cast<JIT_Trace*>(data);
    trace->_mm.Reset();
    trace->_record = record;
    trace->_runCount++;

    int(*fn)(void*) = (int(*)(void*))((unsigned char*)mm->_co._vm_stub);
    return fn(trace->_jit_data);
}

int SunScript::JIT_Resume(void* instance)
{
    JIT_Manager* mm = reinterpret_cast<JIT_Manager*>(instance);
    //mm->_mm.Dump();

    int(*fn)(void*) = (int(*)(void*))((unsigned char*)mm->_co._vm_stub);
    return fn(mm->_co._vm_resume);
}

void SunScript::JIT_Free(void* data)
{
    JIT_Trace* trace = reinterpret_cast<JIT_Trace*>(data);

    vm_free(trace->_jit_data, trace->_size);
}

/*
std::string SunScript::JIT_Stats(void* data)
{
    JIT_Manager* man = reinterpret_cast<JIT_Manager*>(data);

    std::vector<JIT_Method*> methods;
    man->_cache.GetData(methods);

    std::stringstream ss;
    ss << "================" << std::endl;
    ss << "JIT Stats" << std::endl;
    ss << "================" << std::endl;
    ss << "Number cached methods: " << methods.size() << std::endl;
    ss << "================" << std::endl;

    for (auto& item : methods)
    {
        ss << JIT_Method_Stats(item) << std::endl;
        ss << "================" << std::endl;
    }

    return ss.str();
}*/

void SunScript::JIT_Shutdown(void* instance)
{
    JIT_Manager* mm = reinterpret_cast<JIT_Manager*>(instance);
    delete mm;
}

//===========================
// JIT tracing
//===========================

void SunScript::JIT_DumpTrace(unsigned char* trace, unsigned int size)
{
    unsigned int pc = 0;
    int ref = 0;
    int op1 = 0;
    int op2 = 0;
    short offset = 0;
    
    const int constantSize = vm_jit_read_int(trace, &pc);
    pc += constantSize;
    
    while (pc < size)
    {
        std::cout << ref;

        const int ir = trace[pc++];
        switch (ir)
        {
        case IR_ADD_INT:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_ADD_INT " << op1 << " " << op2 << std::endl;
            break;
        case IR_ADD_REAL:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_ADD_REAL " << op1 << " " << op2 << std::endl;
            break;
        case IR_APP_INT_STRING:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_APP_INT_STRING " << op1 << " " << op2 << std::endl;
            break;
        case IR_APP_STRING_INT:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_APP_STRING_INT " << op1 << " " << op2 << std::endl;
            break;
        case IR_APP_STRING_STRING:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_APP_STRING_STRING " << op1 << " " << op2 << std::endl;
            break;
        case IR_APP_STRING_REAL:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_APP_STRING_REAL " << op1 << " " << op2 << std::endl;
            break;
        case IR_APP_REAL_STRING:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_APP_REAL_STRING " << op1 << " " << op2 << std::endl;
            break;
        case IR_CALL:
        case IR_YIELD:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = trace[pc++]; // num args
            std::cout << " IR_CALL " << op1 << " " << op2 << std::endl;
            break;
        case IR_STRING_ARG:
            std::cout << " IR_STRING_ARG " << vm_jit_read_int(trace, &pc) << std::endl;
            break;
        case IR_REAL_ARG:
            std::cout << " IR_REAL_ARG " << vm_jit_read_int(trace, &pc) << std::endl;
            break;
        case IR_INT_ARG:
            std::cout << " IR_INT_ARG " << vm_jit_read_int(trace, &pc) << std::endl;
            break;
        case IR_TABLE_ARG:
            std::cout << " IR_TABLE_ARG " << vm_jit_read_int(trace, &pc) << std::endl;
            break;
        case IR_CMP_INT:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_CMP_INT " << op1 << " " << op2 << std::endl;
            break;
        case IR_CMP_REAL:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_CMP_REAL " << op1 << " " << op2 << std::endl;
            break;
        case IR_CMP_STRING:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_CMP_STRING " << op1 << " " << op2 << std::endl;
            break;
        case IR_CMP_TABLE:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_CMP_TABLE " << op1 << " " << op2 << std::endl;
            break;
        case IR_CONV_INT_TO_REAL:
            std::cout << " IR_CONV_INT_TO_REAL " << vm_jit_read_int(trace, &pc) << std::endl;
            break;
        case IR_DECREMENT_INT:
            std::cout << " IR_DECREMENT_INT " << vm_jit_read_int(trace, &pc) << std::endl;
            break;
        case IR_DIV_INT:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_DIV_INT " << op1 << " " << op2 << std::endl;
            break;
        case IR_DIV_REAL:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_DIV_REAL " << op1 << " " << op2 << std::endl;
            break;
        case IR_GUARD:
            std::cout << " IR_GUARD " << int(trace[pc++]) << std::endl;
            break;
        case IR_INCREMENT_INT:
            std::cout << " IR_INCREMENT_INT " << vm_jit_read_int(trace, &pc) << std::endl;
            break;
        case IR_LOAD_INT:
            op1 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_LOAD_INT " << op1 << " (" << *reinterpret_cast<int*>(&trace[op1 + 4]) << ")" << std::endl;
            break;
        case IR_LOAD_REAL:
            op1 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_LOAD_REAL " << op1 << std::endl;
            break;
        case IR_LOAD_STRING:
            op1 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_LOAD_STRING " << op1 << std::endl;
            break;
        case IR_LOOPBACK:
            op1 = int(trace[pc++]);
            offset = short(trace[pc++]);
            offset |= (short(trace[pc++]) << 8);
            std::cout << " IR_LOOPBACK " << op1 << " " << offset << std::endl;
            break;
        case IR_LOOPSTART:
            std::cout << " IR_LOOPSTART" << std::endl;
            break;
        case IR_MUL_INT:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_MUL_INT " << op1 << " " << op2 << std::endl;
            break;
        case IR_MUL_REAL:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_MUL_REAL " << op1 << " " << op2 << std::endl;
            break;
        case IR_NOP:
            std::cout << " IR_NOP" << std::endl;
            break;
        case IR_SUB_INT:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_SUB_INT " << op1 << " " << op2 << std::endl;
            break;
        case IR_SUB_REAL:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_SUB_REAL " << op1 << " " << op2 << std::endl;
            break;
        case IR_UNARY_MINUS_INT:
            std::cout << " IR_UNARY_MINUS_INT " << vm_jit_read_int(trace, &pc) << std::endl;
            break;
        case IR_UNARY_MINUS_REAL:
            std::cout << " IR_UNARY_MINUS_REAL " << vm_jit_read_int(trace, &pc) << std::endl;
            break;
        case IR_LOOPEXIT:
            op1 = int(trace[pc++]);
            offset = short(trace[pc++]);
            offset |= (short(trace[pc++]) << 8);
            std::cout << " IR_LOOPEXIT " << op1 << " " << offset << std::endl;
            break;
        case IR_PHI:
            std::cout << " IR_PHI " << vm_jit_read_int(trace, &pc) << " " << vm_jit_read_int(trace, &pc) << std::endl;
            break;
        case IR_SNAP:
            op1 = trace[pc++];
            op2 = trace[pc++];
            std::cout << " IR_SNAP #" << op1 << " [";
            {
                for (int i = 0; i < op2; i++)
                {
                    std::cout << " " << int(trace[pc++]);
                }
            }
            std::cout << " ]" << std::endl;
            break;
        case IR_BOX:
            op2 = vm_jit_read_int(trace, &pc);
            op1 = trace[pc++];
            std::cout << " IR_BOX " << op1 << " " << op2 << std::endl;
            break;
        case IR_UNBOX:
            op2 = vm_jit_read_int(trace, &pc);
            op1 = trace[pc++];
            std::cout << " IR_UNBOX " << op1 << " " << op2 << std::endl;
            break;
        case IR_LOAD_INT_LOCAL:
            op1 = trace[pc++];
            std::cout << " IR_LOAD_INT_LOCAL " << op1 << std::endl;
            break;
        case IR_LOAD_REAL_LOCAL:
            op1 = trace[pc++];
            std::cout << " IR_LOAD_REAL_LOCAL " << op1 << std::endl;
            break;
        case IR_LOAD_STRING_LOCAL:
            op1 = trace[pc++];
            std::cout << " IR_LOAD_STRING_LOCAL " << op1 << std::endl;
            break;
        case IR_LOAD_TABLE_LOCAL:
            op1 = trace[pc++];
            std::cout << " IR_LOAD_TABLE_LOCAL " << op1 << std::endl;
            break;
        case IR_TABLE_NEW:
            std::cout << " IR_NEW_TABLE" << std::endl;
            break;
        case IR_TABLE_HGET:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_TABLE_HGET " << op1 << " " << op2 << std::endl;
            break;
        case IR_TABLE_AGET:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_TABLE_AGET " << op1 << " " << op2 << std::endl;
            break;
        case IR_TABLE_HSET:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_TABLE_HSET " << op1 << " " << op2 << std::endl;
            break;
        case IR_TABLE_ASET:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_TABLE_ASET " << op1 << " " << op2 << std::endl;
            break;
        case IR_TABLE_HREF:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_TABLE_HREF " << op1 << " " << op2 << std::endl;
            break;
        case IR_TABLE_AREF:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_TABLE_AREF " << op1 << " " << op2 << std::endl;
            break;
        default:
            std::cout << " UNKNOWN" << std::endl;
        }

        ref++;
    }
}

void SunScript::JIT_DisassembleTrace(void* data)
{
    assert(data);

    JIT_Trace* trace = reinterpret_cast<JIT_Trace*>(data);
    unsigned char* jit = reinterpret_cast<unsigned char*>(trace->_jit_data);

    std::cout << std::hex;

    for (int i = 0; i < trace->_size; i++)
    {
        if (i > 0 && i % 20 == 0)
        {
            std::cout << std::endl;
        }

        std::cout << int(jit[i]) << " ";
    }
    std::cout << std::dec << std::endl;
}

void* SunScript::JIT_CompileTrace(void* instance, VirtualMachine* vm, unsigned char* trace, int size, int traceId)
{
    std::unique_ptr<Jitter> jitter = std::make_unique<Jitter>();
    jitter->_trace = new JIT_Trace();
    jitter->_trace->_id = traceId;
    jitter->_trace->_runCount = 0;
    jitter->_trace->_jumpPos = 0;

    std::chrono::steady_clock clock;

    //==============================
    // Run the JIT compilation
    //==============================
    jitter->_trace->_startTime = clock.now().time_since_epoch().count();

    const int jitDataSize = 1024 * 5;
    unsigned int pc = 0;

    jitter->program = trace;
    jitter->pc = &pc;
    jitter->size = size;
    jitter->_trace->_jit_data = vm_allocate(jitDataSize);
    jitter->jit = reinterpret_cast<unsigned char*>(jitter->_trace->_jit_data);
    jitter->_manager = reinterpret_cast<JIT_Manager*>(instance);

    // Create readonly constant data pages.
    const int constantSize = vm_jit_read_int(trace, &pc);
    jitter->_trace->_constantPage = vm_allocate(constantSize);
    std::memcpy(jitter->_trace->_constantPage, &trace[pc], constantSize);
    vm_readonly(jitter->_trace->_constantPage, constantSize);
    pc += constantSize;

    jitter->analyzer.Load(trace, size);
    //jitter->analyzer.Dump();

    vm_jit_generate_trace(vm, jitter.get());
    vm_initialize(jitter->_trace->_jit_data, jitter->count);

    /*for (auto& stub : jitter->_method->_stubs)
    {
        stub._patch._data = jitter->_method->_jit_data;
        stub._patch._size = jitter->count;
    }*/

    jitter->_trace->_size = jitter->count;

    //===============================
    jitter->_trace->_endTime = clock.now().time_since_epoch().count();

    //std::cout << "CompileTrace " << (jitter->_trace->_endTime - jitter->_trace->_startTime) << std::endl;

    return jitter->_trace;
}

//===========================
