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

#if WIN32
#define VM_ARG1 VM_REGISTER_ECX
#define VM_ARG2 VM_REGISTER_EDX
#define VM_ARG3 VM_REGISTER_R8
#define VM_ARG4 VM_REGISTER_R9
#else
#define VM_ARG1 VM_REGISTER_EDI
#define VM_ARG2 VM_REGISTER_ESI
#define VM_ARG3 VM_REGISTER_EDX
#define VM_ARG4 VM_REGISTER_ECX
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
    VMI_ENC_D = 6       // jump instruction
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

    // End of instructions
    VMI_MAX_INSTRUCTIONS
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

#define INS(rex, ins, subins, type, code, enc) \
    { (unsigned char)rex, (unsigned char)ins, (unsigned char)subins, (unsigned char)type, (unsigned char)code, (unsigned char)enc }

#define VMI_UNUSED 0xFF

static vm_instruction gInstructions[VMI_MAX_INSTRUCTIONS] = {
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
    INS(0x48, 0x39, VMI_UNUSED, VM_INSTRUCTION_BINARY, CODE_BMR, VMI_ENC_MR), // VMI_CMP64_SRC_REG_DST_MEM
    INS(0x48, 0x3B, VMI_UNUSED, VM_INSTRUCTION_BINARY, CODE_BRM, VMI_ENC_RM), // VMI_CMP64_SRC_MEM_DST_REG

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
};

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

        if (reg == VM_REGISTER_ESP)
        {
            program[count++] = (ins.subins << 3) | ((reg % 8) & 0x7) | (0x3 << 6);
        }
        else
        {
            program[count++] = (ins.subins << 3) | (reg % 8) | (0x3 << 6);
        }
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
#define VM_ALIGN_16(x) ((x + 0xf) & ~(0xf))


//static void vm_cpuid(const vm_instruction_table& table, unsigned char* program, int& count)
//{
//    vm_emit(table.cpuid, program, count);
//}

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

inline static void vm_cmp_reg_to_reg_x64(unsigned char* program, int& count, char dst, char src)
{
    vm_emit_brr(gInstructions[VMI_CMP64_SRC_REG_DST_REG], program, count, dst, src);
}

inline static void vm_cmp_reg_to_memory_x64(unsigned char* program, int& count, char dst, char src)
{
    vm_emit_bmr(gInstructions[VMI_CMP64_SRC_REG_DST_MEM], program, count, dst, src);
}

inline static void vm_cmp_memory_to_reg_x64(unsigned char* program, int& count, char dst, char src)
{
    vm_emit_brm(gInstructions[VMI_CMP64_SRC_MEM_DST_REG], program, count, dst, src);
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

//===========================
// Forward decl
//===========================
static int vm_jit_read_int(unsigned char* program, unsigned int* pc);
static std::string vm_jit_read_string(unsigned char* program, unsigned int* pc);


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

static void ConsumeInstruction(unsigned char* ins, unsigned int& pc)
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
}

//==================================

constexpr unsigned int ST_REG = 0;
constexpr unsigned int ST_STACK = 1;

struct JIT_Allocation
{
    unsigned int type;
    unsigned int reg;
    unsigned int pos;
};

class JIT_Analyzer
{
public:
    void Load(unsigned char* ir, unsigned int count);
    JIT_Allocation GetAllocation(int index);
    bool IsRegisterUsed(int reg);
    int StackSize();
    ~JIT_Analyzer();

private:

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
        void Initialize(int reg, bool enabled);
        void Initialize(int pos);
        inline void SetEnabled(bool enabled) { _enabled = enabled; }
        void InsertBefore(Node* before, int ref, int start, int end);
        void InsertAfter(Node* after, int ref, int start, int end);
        void Insert(int ref, int start, int end);

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
        bool _enabled;
        Node* _head;
        Node* _tail;
    };

    void InitializeAllocations();
    void AllocateRegister(int ref, int start, int end);

    std::vector<int> liveness;
    std::vector<Allocation> allocations;
    std::vector<JIT_Allocation> registers;
};

JIT_Analyzer::Allocation::Allocation()
    :
    _reg(0),
    _pos(0),
    _type(0),
    _enabled(true),
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

bool JIT_Analyzer::IsRegisterUsed(int reg)
{
    if (reg >= 0 && reg < registers.size())
    {
        return allocations[reg].Head() != nullptr;
    }

    return false;
}

JIT_Allocation JIT_Analyzer::GetAllocation(int index)
{
    if (index >= 0 && index < registers.size())
    {
        return registers[index];
    }

    return JIT_Allocation();
}

void JIT_Analyzer::Allocation::Initialize(int reg, bool enabled)
{
    _type = ST_REG;
    _reg = reg;
    _enabled = enabled;
}

void JIT_Analyzer::Allocation::Initialize(int pos)
{
    _type = ST_STACK;
    _reg = VM_REGISTER_ESP;
    _pos = pos;
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
    allocations.resize(VM_REGISTER_MAX);
    for (size_t i = 0; i < allocations.size(); i++)
    {
        auto& a = allocations[i];
        a.Initialize(int(i), true);
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
}

void JIT_Analyzer::AllocateRegister(int ref, int start, int end)
{
    bool ok = false;

    for (size_t i = 0; i < allocations.size(); i++)
    {
        auto& allocation = allocations[i];
        if (!allocation.Enabled()) { continue; }

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
        JIT_Analyzer::Allocation& allocation = allocations.emplace_back();
        allocation.Initialize(32);
        allocation.Insert(ref, start, end);
    }
}

void JIT_Analyzer::Load(unsigned char* ir, unsigned int count)
{
    unsigned int pc = 0;
    int ref = 0;
    while (pc < count)
    {
        int p1 = -1;
        int p2 = -1;

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
        case IR_LOAD_INT:
            vm_jit_read_int(ir, &pc);
            break;
        case IR_LOAD_STRING:
            vm_jit_read_string(ir, &pc);
            break;
        case IR_APP_INT_STRING:
        case IR_APP_STRING_STRING:
        case IR_APP_STRING_INT:
            p1 = vm_jit_read_int(ir, &pc);
            p2 = vm_jit_read_int(ir, &pc);
            break;
        case IR_CALL:
        case IR_YIELD:
            vm_jit_read_int(ir, &pc);
            {
                const int numArgs = ir[pc++];
                for (int i = 0; i < numArgs; i++)
                {
                    pc++; // skip type
                    const int arg = vm_jit_read_int(ir, &pc);
                    liveness[arg] = std::max(liveness[arg], ref - arg);
                }
            }
            break;
        case IR_DECREMENT_INT:
        case IR_INCREMENT_INT:
            p1 = vm_jit_read_int(ir, &pc);
            break;
        case IR_GUARD:
        case IR_LOOPEXIT:
            pc++;
            break;
        case IR_LOOPBACK:
            pc += 3;
            break;
        case IR_UNARY_MINUS_INT:
            p1 = vm_jit_read_int(ir, &pc);
            break;
        case IR_LOOPSTART:
            break;
        }

        if (p1 > -1) { liveness[p1] = std::max(liveness[p1], ref - p1); }
        if (p2 > -1) { liveness[p2] = std::max(liveness[p2], ref - p2); }

        liveness.push_back(0);
        ref++;
    }

    // Determine which registers to use.

    InitializeAllocations();
    for (int i = 0; i < int(liveness.size()); i++)
    {
        if (liveness[i] > 0)
        {
            AllocateRegister(i, i, liveness[i] + i);
        }
    }

    // Write them back to an list the same size as the number of instructions
    
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
            node = node->next;
        }
    }
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
};

struct JIT_BackwardJump
{
    int _state;
    int _pos;
    int _target;
    int _type;
};

struct JIT_Jump
{
    int _state;
    int _offset;
    int _pos;
    int _type;
    int _size;
};

struct JIT_Trace
{
    void* _jit_data;
    int _size;
    int _jumpPos;

    int _id;
    MemoryManager _mm;

    std::vector<JIT_Jump> _forwardJumps;
    std::vector<JIT_BackwardJump> _backwardJumps;
    std::vector<JIT_ExitJump> _exitJumps;

    uint64_t _startTime;     // compilation start time
    uint64_t _endTime;       // compilation end time
    uint64_t _runCount;      // number of times the method has been invoked
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
    //RegisterAllocator allocator;
    //VirtualStack stack;
    //std::vector<Local> locals;
    //JIT_FlowGraph fg;
    JIT_Trace* _trace;
    JIT_Manager* _manager;
    JIT_Analyzer analyzer;
    FunctionInfo* info;
    int size;
    int refIndex;
    int argsProcessed;
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
        info(nullptr)
    {
    }

    void SetError()
    {
        error = true;
        running = false;
    }
};

//===================================
#ifdef WIN32
void* vm_allocate(int size)
{
    return VirtualAlloc(0, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void vm_initialize(void* data, unsigned char* jit, int size)
{
    memcpy(data, jit, size);
    unsigned long oldProtect;
    if (VirtualProtect(data, size, PAGE_EXECUTE, &oldProtect))
    {
        FlushInstructionCache(0, data, size);
    }
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

void vm_initialize(void* data, unsigned char* jit, int size)
{
    std::memcpy(data, jit, size);
    mprotect(data, size, PROT_EXEC | PROT_READ);
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

    static void* vm_call_stub(VirtualMachine* vm, char* name, const int numArgs)
    {
        InvokeHandler(vm, name, numArgs);

        int retInt;
        std::string val;
        if (GetParamInt(vm, &retInt) == VM_OK)
        {
            return (void*)(long long)retInt;
        }
        else if (GetParamString(vm, &val) == VM_OK)
        {
            // TODO: this should use the memory manager
            char* copy = new char[val.size() + 1];
            std::memcpy(copy, val.c_str(), val.size());
            copy[val.size()] = 0;
            return copy;
        }

        return 0;
    }

    //static void* vm_box_int(JIT_Manager* man, int value)
    //{
    //    int* mem = (int*)man->_mm.New(sizeof(int64_t), TY_INT);
    //    *mem = value;
    //    return mem;
    //}

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
}

// =================================

static void vm_jit_call_internal_x64(Jitter* jitter, void* address)
{
    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, (long long)address);
    vm_call_absolute(jitter->jit, jitter->count, VM_REGISTER_EAX);
}

//static void vm_jit_call_internal_x64(Jitter* jitter, int numParams, void* address, int retType)
//{
//    //jitter->allocator.Allocate(VM_ARG1);
//    //vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)jitter->_manager);
//
//    //int registers[4] = {
//    //    VM_ARG1,
//    //    VM_ARG2,
//    //    VM_ARG3,
//    //    VM_ARG4
//    //};
//    //for (int i = 1; i <= numParams; i++)
//    //{
//    //    const StackItem item = jitter->stack.Pop();
//    //    if (item.store == ST_REG)
//    //    {
//    //        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, registers[i], item.reg);
//    //        jitter->allocator.Allocate(registers[i]);
//    //    }
//    //    else if (item.store == ST_STACK)
//    //    {
//    //        vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, registers[i], item.reg, item.pos);
//    //        jitter->allocator.Allocate(registers[i]);
//    //    }
//    //}
//
//    const int call = jitter->allocator.Allocate();
//    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, call, (long long)address);
//    vm_call_absolute(jitter->jit, jitter->count, call);
//    jitter->allocator.Free(call);
//
//    if (retType != TY_VOID)
//    {
//        jitter->allocator.Allocate(VM_REGISTER_EAX);
//        jitter->stack.Push_Register(VM_REGISTER_EAX, retType);
//    }
//
//    for (int i = 1; i <= numParams; i++)
//    {
//        jitter->allocator.Free(registers[i]);
//    }
//
//    //jitter->allocator.Free(VM_REGISTER_ECX);
//}

//static void vm_jit_spill_register(Jitter* jitter, int reg)
//{
//    if (jitter->allocator.IsUsed(reg))
//    {
//        const int spill = jitter->allocator.Allocate();
//        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, spill, reg);
//
//        jitter->stack.Mov(spill, reg);
//        
//        // NOTE: we don't free this register in case we need to spill more
//        // and we don't want to spill them into registers we just freed.
//    }
//}

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

//static void vm_jit_pop(Jitter* jitter)
//{
//    const int id = jitter->program[*jitter->pc];
//    (*jitter->pc)++;
//
//    StackItem it = jitter->stack.Pop();
//    Local& local = jitter->locals[id];
//    if (it.store == ST_STACK)
//    {
//        if (it.pos != local.pos)
//        {
//            const int reg = jitter->allocator.Allocate();
//            vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg, it.reg, it.pos);
//            vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, VM_REGISTER_ESP, local.pos, reg);
//            jitter->allocator.Free(reg);
//
//            local.type = it.type;
//        }
//    }
//    else if (it.store == ST_REG)
//    {
//        vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, VM_REGISTER_ESP, local.pos, it.reg);
//        jitter->allocator.Free(it.reg);
//
//        local.type = it.type;
//    }
//    else
//    {
//        jitter->SetError();
//    }
//}

//static void vm_jit_push_local(Jitter* jitter)
//{
//    const int id = jitter->program[*jitter->pc];
//    (*jitter->pc)++;
//
//    if (jitter->locals.size() > id)
//    {
//        auto& local = jitter->locals[id];
//        jitter->stack.Push_Local(local.pos, local.type);
//    }
//    else
//    {
//        jitter->SetError();
//    }
//}

//static void vm_jit_push(Jitter* jitter)
//{
//    unsigned char type = jitter->program[*(jitter->pc)];
//    (*(jitter->pc))++;
//    switch (type)
//    {
//    case TY_INT:
//        {
//            const int value = vm_jit_read_int(jitter->program, jitter->pc);
//            const int reg = jitter->allocator.Allocate();
//            if (reg != -1)
//            {
//                vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, reg, value);
//                jitter->stack.Push_Register(reg, TY_INT);
//            }
//            else
//            {
//                // No registers free...
//                jitter->SetError();
//            }
//        }
//        break;
//    case TY_STRING:
//        {
//            const char* str = (char*)&jitter->program[*jitter->pc];
//            (*jitter->pc) += static_cast<unsigned int>(strlen(str)) + 1;
//            
//            const int reg = jitter->allocator.Allocate();
//            if (reg != -1)
//            {
//                vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, reg, (long long)str);
//                jitter->stack.Push_Register(reg, TY_STRING);
//            }
//            else
//            {
//                // No registers free...
//                jitter->SetError();
//            }
//        }
//        break;
//    default:
//        // error
//        jitter->SetError();
//        break;
//    }
//}

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

static void vm_jit_patch_jump(Jitter* jitter, JIT_Jump& jump)
{
    jump._state = PATCH_APPLIED;

    const int rel = jitter->count - (jump._offset + jump._size);

    vm_jit_jump(jitter, jump._type, jump._offset, rel);
}

static void vm_jit_patch_next_jump(Jitter* jitter)
{
    bool cont = true;
    
    while (jitter->_trace->_jumpPos < jitter->_trace->_forwardJumps.size() && cont)
    {
        cont = false;

        auto& jump = jitter->_trace->_forwardJumps[jitter->_trace->_jumpPos];
        if (*jitter->pc == jump._pos &&
            jump._state == PATCH_INITIALIZED)
        {
            cont = true;
            jitter->_trace->_jumpPos++;
            jump._state = PATCH_APPLIED;

            const int rel = jitter->count - (jump._offset + jump._size);

            vm_jit_jump(jitter, jump._type, jump._offset, rel);
        }
    }
}

static void vm_jit_cmp_string(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);

    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, a1.reg);
    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG2, a2.reg);

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

    vm_cmp_reg_to_reg_x64(jitter->jit, jitter->count, a1.reg, a2.reg);
}

//static void vm_jit_cmp(Jitter* jitter)
//{
//    if (jitter->stack.Size() < 2)
//    {
//        jitter->SetError();
//        return;
//    }
//
//    StackItem i1;
//    StackItem i2;
//    jitter->stack.Peek(0, &i1);
//    jitter->stack.Peek(1, &i2);
//
//    jitter->stack.Pop();
//    jitter->stack.Pop();
//
//    if (i1.store == ST_REG && i2.store == ST_REG)
//    {
//        vm_cmp_reg_to_reg_x64(jitter->jit, jitter->count, i1.reg, i2.reg);
//
//        jitter->allocator.Free(i1.reg);
//        jitter->allocator.Free(i2.reg);
//    }
//    else if (i1.store == ST_REG && i2.store == ST_STACK)
//    {
//        const int reg = jitter->allocator.Allocate();
//        vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg, i2.reg, i2.pos);
//        vm_cmp_reg_to_reg_x64(jitter->jit, jitter->count, i1.reg, reg);
//        jitter->allocator.Free(reg);
//        jitter->allocator.Free(i1.reg);
//    }
//    else if (i1.store == ST_STACK && i2.store == ST_REG)
//    {
//        const int reg = jitter->allocator.Allocate();
//        vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg, i1.reg, i1.pos);
//        vm_cmp_reg_to_reg_x64(jitter->jit, jitter->count, reg, i2.reg);
//        jitter->allocator.Free(reg);
//        jitter->allocator.Free(i2.reg);
//    }
//    else if(i1.store == ST_STACK && i2.store == ST_STACK)
//    {
//        const int reg1 = jitter->allocator.Allocate();
//        const int reg2 = jitter->allocator.Allocate();
//        vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg1, i1.reg, i1.pos);
//        vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg2, i2.reg, i2.pos);
//        vm_cmp_reg_to_reg_x64(jitter->jit, jitter->count, reg1, reg2);
//        jitter->allocator.Free(reg1);
//        jitter->allocator.Free(reg2);
//    }
//}

static void vm_jit_exitloop(Jitter* jitter)
{
    const int type = jitter->program[*jitter->pc];
    (*jitter->pc)++;

    // Forward jump (requires patching)

    JIT_ExitJump jump;
    jump._offset = jitter->count;
    jump._type = type;
    jump._state = PATCH_INITIALIZED;
    jump._pos = *jitter->pc;

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

    JIT_Jump jump;
    jump._offset = jitter->count;
    jump._type = type;
    jump._state = PATCH_INITIALIZED;
    jump._pos = *jitter->pc;

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
        if (jump._state == PATCH_INITIALIZED)
        {
            jump._state = PATCH_APPLIED;
            
            const int imm = jitter->count - (jump._offset + jump._size);
            vm_jit_jump(jitter, jump._type, jump._offset, imm);
        }
    }
}

static void vm_jit_jump(Jitter* jitter)
{
    const int type = jitter->program[*jitter->pc];
    (*jitter->pc)++;
    short offset = jitter->program[*jitter->pc];
    (*jitter->pc)++;
    offset |= (jitter->program[*jitter->pc] << 8);
    (*jitter->pc)++;

    if (offset > 0)
    {
        // Forward jump (requires patching)

        JIT_Jump jump;
        jump._offset = jitter->count;
        jump._type = type;
        jump._state = PATCH_INITIALIZED;
        jump._pos = *jitter->pc + offset;

        // Just emit equals it will be overwritten by the patch process
        vm_jump_equals(jitter->jit, jitter->count, 0);

        jump._size = jitter->count - jump._offset;

        const auto& pos = std::lower_bound(jitter->_trace->_forwardJumps.begin(), jitter->_trace->_forwardJumps.end(), jump, [](const JIT_Jump& j1, const JIT_Jump& j2) {
            return j1._pos < j2._pos;
            });
        jitter->_trace->_forwardJumps.insert(pos, jump);
    }
    else
    {
        // Backward jump (no patching)
        for (size_t i = 0; i < jitter->_trace->_backwardJumps.size(); i++)
        {
            auto& jump = jitter->_trace->_backwardJumps[i];
            if (offset + *jitter->pc == jump._target)
            {
                const int imm = jump._pos - (jitter->count + 2 /*Length of jump instruction*/);
                vm_jit_jump(jitter, jump._type, jitter->count, imm);
                break;
            }
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
    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG2, a1.reg);
    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG3, a2.reg);

    vm_jit_call_internal_x64(jitter, (void*)vm_append_string_int);
    
    if (a3.reg != VM_REGISTER_EAX)
    {
        vm_mov_reg_to_reg_x64(jitter->program, jitter->count, a3.reg, VM_REGISTER_EAX);
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
    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG2, a1.reg);
    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG3, a2.reg);

    vm_jit_call_internal_x64(jitter, (void*)vm_append_int_string);

    if (a3.reg != VM_REGISTER_EAX)
    {
        vm_mov_reg_to_reg_x64(jitter->program, jitter->count, a3.reg, VM_REGISTER_EAX);
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
    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG2, a1.reg);
    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_ARG3, a2.reg);

    vm_jit_call_internal_x64(jitter, (void*)vm_append_string_string);

    if (a3.reg != VM_REGISTER_EAX)
    {
        vm_mov_reg_to_reg_x64(jitter->program, jitter->count, a3.reg, VM_REGISTER_EAX);
    }
}

static void vm_jit_add_int(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);
    const JIT_Allocation a3 = jitter->analyzer.GetAllocation(jitter->refIndex);

    int dst = 0;
    switch (a3.type)
    {
    case ST_REG:
        dst = a3.reg;
        break;
    case ST_STACK:
        dst = VM_REGISTER_EAX;
        break;
    }

    switch (a1.type)
    {
    case ST_REG:
        if (a1.reg != dst)
        {
            vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, dst, a1.reg);
        }
        break;
    case ST_STACK:
        vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, dst, a1.reg, a1.pos);
        break;
    }

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

    //if (a1.reg != a3.reg)
    //{
    //    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, a3.reg, a1.reg);
    //}
    //vm_add_reg_to_reg_x64(jitter->jit, jitter->count, a3.reg, a2.reg);
}

//static void vm_jit_add(Jitter* jitter)
//{
//    if (jitter->stack.Size() >= 2)
//    {
//        StackItem i1;
//        StackItem i2;
//
//        jitter->stack.Peek(0, &i1);
//        jitter->stack.Peek(1, &i2);
//
//        if (i1.type == TY_INT && i2.type == TY_INT)
//        {
//            jitter->stack.Pop();
//            jitter->stack.Pop();
//
//            if (i1.store == ST_REG && i2.store == ST_REG)
//            {
//                vm_add_reg_to_reg_x64(jitter->jit, jitter->count, i1.reg, i2.reg);
//                jitter->allocator.Free(i2.reg);
//                jitter->stack.Push_Register(i1.reg, TY_INT);
//            }
//            else if (i1.store == ST_REG && i2.store == ST_STACK)
//            {
//                vm_add_memory_to_reg_x64(jitter->jit, jitter->count, i1.reg, i2.reg, i2.pos);
//                jitter->stack.Push_Register(i1.reg, TY_INT);
//            }
//            else if (i2.store == ST_REG && i1.store == ST_STACK)
//            {
//                vm_add_memory_to_reg_x64(jitter->jit, jitter->count, i2.reg, i1.reg, i1.pos);
//                jitter->stack.Push_Register(i2.reg, TY_INT);
//            }
//            else if (i1.store == ST_STACK && i2.store == ST_STACK)
//            {
//                const int reg1 = jitter->allocator.Allocate();
//
//                vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg1, i1.reg, i1.pos);
//                vm_add_memory_to_reg_x64(jitter->jit, jitter->count, reg1, i2.reg, i2.pos);
//
//                jitter->stack.Push_Register(reg1, TY_INT);
//            }
//        }
//        else if (i1.type == TY_STRING && i2.type == TY_INT)
//        {
//            vm_jit_call_internal_x64(jitter, 2, reinterpret_cast<void*>(vm_append_string_int), TY_STRING);
//        }
//        else if (i1.type == TY_INT && i2.type == TY_STRING)
//        {
//            vm_jit_call_internal_x64(jitter, 2, reinterpret_cast<void*>(vm_append_int_string), TY_STRING);
//        }
//        else if (i1.type == TY_STRING && i2.type == TY_STRING)
//        {
//            vm_jit_call_internal_x64(jitter, 2, reinterpret_cast<void*>(vm_append_string_string), TY_STRING);
//        }
//        else
//        {
//            // Unsupported
//            jitter->SetError();
//        }
//    }
//    else
//    {
//        // Error
//        jitter->SetError();
//    }
//}

static void vm_jit_sub_int(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);
    const JIT_Allocation a3 = jitter->analyzer.GetAllocation(jitter->refIndex);
    
    if (a2.reg != a3.reg)
    {
        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, a3.reg, a2.reg);
    }
    vm_sub_reg_to_reg_x64(jitter->jit, jitter->count, a3.reg, a1.reg);
}

//static void vm_jit_sub(Jitter* jitter)
//{
//    if (jitter->stack.Size() >= 2)
//    {
//        const StackItem i1 = jitter->stack.Pop();
//        const StackItem i2 = jitter->stack.Pop();
//
//        if (i1.type == TY_INT && i2.type == TY_INT)
//        {
//            if (i1.store == ST_REG && i2.store == ST_REG)
//            {
//                vm_sub_reg_to_reg_x64(jitter->jit, jitter->count, i2.reg, i1.reg);
//                jitter->allocator.Free(i2.reg);
//                jitter->stack.Push_Register(i1.reg, TY_INT);
//            }
//            else if (i1.store == ST_REG && i2.store == ST_STACK)
//            {
//                const int reg = jitter->allocator.Allocate();
//                vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg, i2.reg, i2.pos);
//                vm_sub_reg_to_reg_x64(jitter->jit, jitter->count, reg, i1.reg);
//                jitter->stack.Push_Register(reg, TY_INT);
//                jitter->allocator.Free(i1.reg);
//            }
//            else if (i2.store == ST_REG && i1.store == ST_STACK)
//            {
//                vm_sub_memory_to_reg_x64(jitter->jit, jitter->count, i2.reg, i1.reg, i1.pos);
//                jitter->stack.Push_Register(i2.reg, TY_INT);
//            }
//            else if (i1.store == ST_STACK && i2.store == ST_STACK)
//            {
//                const int reg1 = jitter->allocator.Allocate();
//
//                vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg1, i2.reg, i2.pos);
//                vm_sub_memory_to_reg_x64(jitter->jit, jitter->count, reg1, i1.reg, i1.pos);
//
//                jitter->stack.Push_Register(reg1, TY_INT);
//            }
//        }
//        else
//        {
//            // Unsupported
//            jitter->SetError();
//        }
//    }
//    else
//    {
//        // Error
//        jitter->SetError();
//    }
//}

static void vm_jit_mul_int(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);
    const JIT_Allocation a3 = jitter->analyzer.GetAllocation(jitter->refIndex);

    if (a1.reg != a3.reg)
    {
        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, a3.reg, a1.reg);
    }
    vm_mul_reg_to_reg_x64(jitter->jit, jitter->count, a3.reg, a2.reg);
}

//static void vm_jit_mul(Jitter* jitter)
//{
//    if (jitter->stack.Size() >= 2)
//    {
//        const StackItem i1 = jitter->stack.Pop();
//        const StackItem i2 = jitter->stack.Pop();
//
//        if (i1.type == TY_INT && i2.type == TY_INT)
//        {
//            if (i1.store == ST_REG && i2.store == ST_REG)
//            {
//                vm_mul_reg_to_reg_x64(jitter->jit, jitter->count, i1.reg, i2.reg);
//                jitter->allocator.Free(i2.reg);
//                jitter->stack.Push_Register(i1.reg, TY_INT);
//            }
//            else if (i1.store == ST_REG && i2.store == ST_STACK)
//            {
//                vm_mul_memory_to_reg_x64(jitter->jit, jitter->count, i1.reg, i2.reg, i2.pos);
//                jitter->stack.Push_Register(i1.reg, TY_INT);
//            }
//            else if (i2.store == ST_REG && i1.store == ST_STACK)
//            {
//                vm_mul_memory_to_reg_x64(jitter->jit, jitter->count, i2.reg, i1.reg, i1.pos);
//                jitter->stack.Push_Register(i2.reg, TY_INT);
//            }
//            else if (i1.store == ST_STACK && i2.store == ST_STACK)
//            {
//                const int reg1 = jitter->allocator.Allocate();
//                vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg1, i1.reg, i1.pos);
//                vm_mul_memory_to_reg_x64(jitter->jit, jitter->count, reg1, i2.reg, i2.pos);
//
//                jitter->stack.Push_Register(reg1, TY_INT);
//            }
//        }
//        else
//        {
//            // Unsupported
//            jitter->SetError();
//        }
//    }
//    else
//    {
//        // Error
//        jitter->SetError();
//    }
//}

static void vm_jit_div_int(Jitter* jitter)
{
    const int ref1 = vm_jit_read_int(jitter->program, jitter->pc);
    const int ref2 = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref1);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(ref2);
    const JIT_Allocation a3 = jitter->analyzer.GetAllocation(jitter->refIndex);

    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, a2.reg);
    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EDX, 0);
    vm_div_reg_x64(jitter->jit, jitter->count, a1.reg);

    vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, a3.reg, VM_REGISTER_EAX);
}

//static void vm_jit_div(Jitter* jitter)
//{
//    if (jitter->stack.Size() >= 2)
//    {
//        StackItem i1;
//        StackItem i2;
//
//        jitter->stack.Peek(0, &i1);
//        jitter->stack.Peek(1, &i2);
//
//        if (i1.type == TY_INT && i2.type == TY_INT)
//        {
//            if (i1.store == ST_REG && i2.store == ST_REG)
//            {
//                vm_jit_spill_register(jitter, VM_REGISTER_EAX);
//                vm_jit_spill_register(jitter, VM_REGISTER_EDX);
//
//                i1 = jitter->stack.Pop();
//                i2 = jitter->stack.Pop();
//
//                vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, i2.reg);
//                vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EDX, 0);
//                vm_div_reg_x64(jitter->jit, jitter->count, i1.reg);
//
//                jitter->allocator.Free(VM_REGISTER_EDX);
//                jitter->allocator.Free(VM_REGISTER_EAX);
//
//                jitter->allocator.Free(i2.reg);
//                jitter->allocator.Free(i1.reg);
//                jitter->stack.Push_Register(VM_REGISTER_EAX, TY_INT);
//            }
//            else if (i1.store == ST_STACK && i2.store == ST_REG)
//            {
//
//            }
//        }
//        else
//        {
//            // Unsupported
//            jitter->SetError();
//        }
//    }
//    else
//    {
//        // Error
//        jitter->SetError();
//    }
//}

static void vm_jit_dec_int(Jitter* jitter)
{
    const int ref = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(jitter->refIndex);

    vm_dec_reg_x64(jitter->jit, jitter->count, a1.reg);
    if (a1.reg != a2.reg)
    {
        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, a2.reg, a1.reg);
    }
}

//static void vm_jit_dec(Jitter* jitter)
//{
//    if (jitter->stack.Size() >= 1)
//    {
//        const StackItem item = jitter->stack.Pop();
//        if (item.type == TY_INT)
//        {
//            if (item.store == ST_REG)
//            {
//                vm_dec_reg_x64(jitter->jit, jitter->count, item.reg);
//                jitter->stack.Push_Register(item.reg, item.type);
//            }
//            else if (item.store == ST_STACK)
//            {
//                vm_dec_memory_x64(jitter->jit, jitter->count, item.reg, item.pos);
//                jitter->stack.Push_Local(item.pos, item.type);
//            }
//            else
//            {
//                // Unsupported
//                jitter->SetError();
//            }
//        }
//        else
//        {
//            // Unsupported
//            jitter->SetError();
//        }
//    }
//    else
//    {
//        // Error
//        jitter->SetError();
//    }
//}

static void vm_jit_inc_int(Jitter* jitter)
{
    const int ref = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(jitter->refIndex);

    vm_inc_reg_x64(jitter->jit, jitter->count, a1.reg);
    if (a1.reg != a2.reg)
    {
        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, a2.reg, a1.reg);
    }
}

//static void vm_jit_inc(Jitter* jitter)
//{
//    if (jitter->stack.Size() >= 1)
//    {
//        const StackItem item = jitter->stack.Pop();
//        if (item.type == TY_INT)
//        {
//            if (item.store == ST_REG)
//            {
//                vm_inc_reg_x64(jitter->jit, jitter->count, item.reg);
//                jitter->stack.Push_Register(item.reg, item.type);
//            }
//            else if (item.store == ST_STACK)
//            {
//                vm_inc_memory_x64(jitter->jit, jitter->count, item.reg, item.pos);
//                jitter->stack.Push_Local(item.pos, item.type);
//            }
//            else
//            {
//                // Unsupported
//                jitter->SetError();
//            }
//        }
//        else
//        {
//            // Unsupported
//            jitter->SetError();
//        }
//    }
//    else
//    {
//        // Error
//        jitter->SetError();
//    }
//}

static void vm_jit_neg_int(Jitter* jitter)
{
    const int ref = vm_jit_read_int(jitter->program, jitter->pc);
    const JIT_Allocation a1 = jitter->analyzer.GetAllocation(ref);
    const JIT_Allocation a2 = jitter->analyzer.GetAllocation(jitter->refIndex);

    vm_neg_reg_x64(jitter->jit, jitter->count, a1.reg);
    if (a1.reg != a2.reg)
    {
        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, a2.reg, a1.reg);
    }
}

//static void vm_jit_neg(Jitter* jitter)
//{
//    if (jitter->stack.Size() >= 1)
//    {
//        const StackItem item = jitter->stack.Pop();
//        if (item.type == TY_INT)
//        {
//            if (item.store == ST_REG)
//            {
//                vm_neg_reg_x64(jitter->jit, jitter->count, item.reg);
//                jitter->stack.Push_Register(item.reg, item.type);
//            }
//            else if (item.store == ST_STACK)
//            {
//                const int reg = jitter->allocator.Allocate();
//                vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg, item.reg, item.pos);
//
//                vm_neg_reg_x64(jitter->jit, jitter->count, reg);
//                jitter->stack.Push_Register(reg, item.type);
//            }
//        }
//        else
//        {
//            // Error
//            jitter->SetError();
//        }
//    }
//}

static void vm_jit_load_string(Jitter* jitter)
{
    const char* str = (char*)&jitter->program[*jitter->pc];
    (*jitter->pc) += static_cast<unsigned int>(strlen(str)) + 1;
    const JIT_Allocation a = jitter->analyzer.GetAllocation(jitter->refIndex);

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, a.reg, (long long)str);
}

static void vm_jit_load_int(Jitter* jitter)
{
    const int value = vm_jit_read_int(jitter->program, jitter->pc);
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
        vm_mov_reg_to_reg_x64(jit, count, VM_ARG2, al.reg);
        break;
    case ST_STACK:
        vm_mov_memory_to_reg_x64(jit, count, VM_ARG2, al.reg, al.pos);
        break;
    }

    if (type == TY_INT)
    {
        vm_mov_imm_to_reg_x64(jit, count, VM_ARG4, (long long)vm_push_int_stub);
    }
    else if (type == TY_STRING)
    {
        vm_mov_imm_to_reg_x64(jit, count, VM_ARG4, (long long)vm_push_string_stub);
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

    if (numParams >= 1)
    {
       vm_jit_call_push_stub(jitter, vm, jitter->jit, jitter->count);
    }
    if (numParams >= 2)
    {
       vm_jit_call_push_stub(jitter, vm, jitter->jit, jitter->count);
    }

    // Store the VM pointer in ARG1.
    // We do this each time in case the register is cleared.
    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG1, (long long)vm);

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG2, (long long)name);
    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG3, numParams);
    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_ARG4, (long long)vm_call_stub);
    vm_call_absolute(jitter->jit, jitter->count, VM_ARG4);

    // TODO: handle return value; we simply need to check the
    // return value is the type we are expecting to get back.
    // Otherwise abort the trace.

    const JIT_Allocation dst = jitter->analyzer.GetAllocation(jitter->refIndex);
    if (dst.reg != VM_REGISTER_EAX)
    {
        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, dst.reg, VM_REGISTER_EAX);
    }
}

static void vm_jit_call(VirtualMachine* vm, Jitter* jitter)
{
    const int id = vm_jit_read_int(jitter->program, jitter->pc);

    const int numParams = jitter->program[*jitter->pc];
    (*jitter->pc)++;
    
    const char* name = FindFunctionName(vm, id);
    assert(name);

    vm_jit_call_x64(vm, jitter, numParams, name);
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
}

inline static void vm_jit_epilog(Jitter* jitter, const int stacksize)
{
    vm_add_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_ESP, stacksize);

    if (jitter->analyzer.IsRegisterUsed(VM_REGISTER_EBX)) {
        vm_pop_reg(jitter->jit, jitter->count, VM_REGISTER_EBX);
    }
    if (jitter->analyzer.IsRegisterUsed(VM_REGISTER_ESI)) {
        vm_pop_reg(jitter->jit, jitter->count, VM_REGISTER_ESI);
    }
    if (jitter->analyzer.IsRegisterUsed(VM_REGISTER_EDI)) {
        vm_pop_reg(jitter->jit, jitter->count, VM_REGISTER_EDI);
    }

    if (jitter->analyzer.IsRegisterUsed(VM_REGISTER_R15)) {
        vm_pop_reg(jitter->jit, jitter->count, VM_REGISTER_R15);
    }

    if (jitter->analyzer.IsRegisterUsed(VM_REGISTER_R14)) {
        vm_pop_reg(jitter->jit, jitter->count, VM_REGISTER_R14);
    }

    if (jitter->analyzer.IsRegisterUsed(VM_REGISTER_R13)) {
        vm_pop_reg(jitter->jit, jitter->count, VM_REGISTER_R13);
    }

    if (jitter->analyzer.IsRegisterUsed(VM_REGISTER_R12)) {
        vm_pop_reg(jitter->jit, jitter->count, VM_REGISTER_R12);
    }
}

//static void vm_jit_box(Jitter* jitter)
//{
//    StackItem item;
//    jitter->stack.Peek(0, &item);
//
//    if (item.store == ST_REG)
//    {
//        switch (item.type)
//        {
//        case TY_INT:
//            vm_jit_call_internal_x64(jitter, 1, (void*)&vm_box_int, TY_OBJECT);
//            break;
//        }
//    }
//    else if (item.store == ST_STACK)
//    {
//        switch (item.type)
//        {
//        case TY_INT:
//            vm_jit_spill_register(jitter, VM_REGISTER_EDX);
//            vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EDX, item.reg, item.pos);
//            jitter->stack.Push_Register(VM_REGISTER_EDX, TY_OBJECT);
//
//            vm_jit_call_internal_x64(jitter, 1, (void*)&vm_box_int, TY_OBJECT);
//            break;
//        }
//    }
//}

//static void vm_jit_return(Jitter* jitter, const int stacksize)
//{
//    // Handle return value
//    if (jitter->stack.Size() > 0)
//    {
//        vm_jit_box(jitter);
//
//        StackItem item = jitter->stack.Pop();
//
//        if (item.store == ST_REG)
//        {
//            if (item.reg != VM_REGISTER_EAX)
//            {
//                vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, item.reg);
//            }
//        }
//        else if (item.store == ST_STACK)
//        {
//            vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, VM_REGISTER_ESP, item.pos);
//        }
//    }
//
//    vm_jit_epilog(jitter, stacksize);
//    vm_return(jitter->jit, jitter->count);
//}

//static void vm_jit_pop_discard(Jitter* jitter)
//{
//    if (jitter->stack.Size() > 0)
//    {
//        jitter->stack.Pop();
//    }
//}

static void vm_jit_generate_trace(VirtualMachine* vm, Jitter* jitter)
{
    // Store non-volatile registers
    if (jitter->analyzer.IsRegisterUsed(VM_REGISTER_R12)) {
        vm_push_reg(jitter->jit, jitter->count, VM_REGISTER_R12);
    }
    if (jitter->analyzer.IsRegisterUsed(VM_REGISTER_R13)) {
        vm_push_reg(jitter->jit, jitter->count, VM_REGISTER_R13);
    }
    if (jitter->analyzer.IsRegisterUsed(VM_REGISTER_R14)) {
        vm_push_reg(jitter->jit, jitter->count, VM_REGISTER_R14);
    }
    if (jitter->analyzer.IsRegisterUsed(VM_REGISTER_R15)) {
        vm_push_reg(jitter->jit, jitter->count, VM_REGISTER_R15);
    }
    if (jitter->analyzer.IsRegisterUsed(VM_REGISTER_EDI)) {
        vm_push_reg(jitter->jit, jitter->count, VM_REGISTER_EDI);
    }
    if (jitter->analyzer.IsRegisterUsed(VM_REGISTER_ESI)) {
        vm_push_reg(jitter->jit, jitter->count, VM_REGISTER_ESI);
    }
    if (jitter->analyzer.IsRegisterUsed(VM_REGISTER_EBX)) {
        vm_push_reg(jitter->jit, jitter->count, VM_REGISTER_EBX);
    }

    const int stacksize = VM_ALIGN_16(32 /* 4 register homes for callees */ + 8 * jitter->analyzer.StackSize()/*space for locals*/ );

    vm_sub_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_ESP, stacksize); // grow stack

    // TODO: store local variables
    //for (int i = 0; i < int(jitter->locals.size()); i++)
    //{
    //    jitter->locals[i].pos = jitter->stack.Local();
    //}

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
        case IR_ADD_INT:
            vm_jit_add_int(jitter);
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
        case IR_CALL:
            vm_jit_call(vm, jitter);
            break;
        case IR_CMP_INT:
            vm_jit_cmp_int(jitter);
            break;
        case IR_CMP_STRING:
            vm_jit_cmp_string(jitter);
            break;
        case IR_DECREMENT_INT:
            vm_jit_dec_int(jitter);
            break;
        case IR_DIV_INT:
            vm_jit_div_int(jitter);
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
        case IR_SUB_INT:
            vm_jit_sub_int(jitter);
            break;
        case IR_UNARY_MINUS_INT:
            vm_jit_neg_int(jitter);
            break;
        case IR_YIELD:
            vm_jit_yield(vm, jitter);
            break;
        default:
            abort();
        }

        jitter->refIndex++;
    }

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, VM_OK);
    vm_jit_epilog(jitter, stacksize);
    vm_return(jitter->jit, jitter->count);

    // Patch guard failures
    if (jitter->_trace->_forwardJumps.size() > 0)
    {
        for (auto& jump : jitter->_trace->_forwardJumps)
        {
            vm_jit_patch_jump(jitter, jump);
        }

        vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, VM_ERROR);
        vm_jit_epilog(jitter, stacksize);
        vm_return(jitter->jit, jitter->count);
    }
}

static void vm_jit_suspend(JIT_Manager* manager)
{
    unsigned char jit[1024];
    int count = 0;
    
    // Store volatile registers

    vm_push_reg(jit, count, VM_REGISTER_EDI);
    vm_push_reg(jit, count, VM_REGISTER_ESI);
    vm_push_reg(jit, count, VM_REGISTER_EBX);


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
    vm_pop_reg(jit, count, VM_REGISTER_EBX);
    vm_pop_reg(jit, count, VM_REGISTER_ESI);
    vm_pop_reg(jit, count, VM_REGISTER_EDI);
    vm_return(jit, count);

    // Finalize

    manager->_co._vm_suspend = vm_allocate(count);
    vm_initialize(manager->_co._vm_suspend, jit, count);

    manager->_co._vm_resume = (unsigned char*)manager->_co._vm_suspend + resume;
}

static void vm_jit_yielded(JIT_Manager* manager)
{
    unsigned char jit[1024];
    int count = 0;

    // Store volatile registers

    vm_push_reg(jit, count, VM_REGISTER_EDI);
    vm_push_reg(jit, count, VM_REGISTER_ESI);
    vm_push_reg(jit, count, VM_REGISTER_EBX);

    const int stacksize = VM_ALIGN_16(32 /* 4 register homes for callees */);

    vm_sub_imm_to_reg_x64(jit, count, VM_REGISTER_ESP, stacksize); // grow stack

    // Record resume point

    const int resumePosition = count;

    // Return value - VM_YIELDED

    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EAX, VM_YIELDED);

    // Epilogue

    vm_add_imm_to_reg_x64(jit, count, VM_REGISTER_ESP, stacksize);
    vm_pop_reg(jit, count, VM_REGISTER_EBX);
    vm_pop_reg(jit, count, VM_REGISTER_ESI);
    vm_pop_reg(jit, count, VM_REGISTER_EDI);
    vm_return(jit, count);

    // Finalize

    manager->_co._vm_yielded = vm_allocate(count);
    vm_initialize(manager->_co._vm_yielded, jit, count);

    manager->_co._yield_resume = (unsigned char*)manager->_co._vm_yielded + resumePosition;
}

static void vm_jit_entry_stub(JIT_Manager* manager)
{
    unsigned char jit[1024];
    int count = 0;

    // Store volatile registers

    vm_push_reg(jit, count, VM_REGISTER_EDI);
    vm_push_reg(jit, count, VM_REGISTER_ESI);
    vm_push_reg(jit, count, VM_REGISTER_EBX);

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
    vm_pop_reg(jit, count, VM_REGISTER_EBX);
    vm_pop_reg(jit, count, VM_REGISTER_ESI);
    vm_pop_reg(jit, count, VM_REGISTER_EDI);
    vm_return(jit, count);

    // Finalize

    manager->_co._vm_stub = vm_allocate(count);
    vm_initialize(manager->_co._vm_stub, jit, count);
}

//========================

void SunScript::JIT_Setup(Jit* jit)
{
    jit->jit_initialize = SunScript::JIT_Initialize;
    //jit->jit_compile = SunScript::JIT_Compile;
    jit->jit_compile_trace = SunScript::JIT_CompileTrace;
    jit->jit_execute = SunScript::JIT_ExecuteTrace;
    jit->jit_resume = SunScript::JIT_Resume;
    //jit->jit_search_cache = SunScript::JIT_SearchCache;
    //jit->jit_cache = SunScript::JIT_CacheData;
    //jit->jit_stats = SunScript::JIT_Stats;
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

int SunScript::JIT_ExecuteTrace(void* instance, void* data)
{
    JIT_Manager* mm = reinterpret_cast<JIT_Manager*>(instance);
    JIT_Trace* trace = reinterpret_cast<JIT_Trace*>(data);
    trace->_mm.Reset();

    //vm_execute(method->_jit_data);
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

/*static std::string JIT_Method_Stats(JIT_Method* method)
{
    int numPatchesApplied = 0;
    for (auto& stub : method->_stubs)
    {
        if (stub._patch._state == PATCH_APPLIED)
        {
            numPatchesApplied++;
        }
    }

    std::stringstream ss;
    ss << "Name: " << method->_id << std::endl;
    ss << "Signature: (" << method->_signature << ")" << std::endl;
    ss << "Cache Key: " << method->_cacheKey << std::endl;
    ss << "Patches Applied: " << numPatchesApplied << "/" << method->_stubs.size() << std::endl;
    ss << "Compile Time: " << (method->_endTime - method->_startTime) << "ns" << std::endl;
    ss << "Runs: " << method->_runCount;

    return ss.str();
}

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
        case IR_CALL:
        case IR_YIELD:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = trace[pc++]; 
            std::cout << " IR_CALL " << op1 << " " << op2;
            {
                for (int i = 0; i < op2; i++)
                {
                    pc++; // TYPE
                    std::cout << " " << vm_jit_read_int(trace, &pc);
                }
            }
            std::cout << std::endl;
            break;
        case IR_CMP_INT:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_CMP_INT " << op1 << " " << op2 << std::endl;
            break;
        case IR_CMP_STRING:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_CMP_STRING " << op1 << " " << op2 << std::endl;
            break;
        case IR_DECREMENT_INT:
            std::cout << " IR_DECREMENT_INT " << vm_jit_read_int(trace, &pc) << std::endl;
            break;
        case IR_DIV_INT:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_DIV_INT " << op1 << " " << op2 << std::endl;
            break;
        case IR_GUARD:
            std::cout << " IR_GUARD " << int(trace[pc++]) << std::endl;
            break;
        case IR_INCREMENT_INT:
            std::cout << " IR_INCREMENT_INT " << vm_jit_read_int(trace, &pc) << std::endl;
            break;
        case IR_LOAD_INT:
            op1 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_LOAD_INT " << op1 << std::endl;
            break;
        case IR_LOAD_STRING:
            std::cout << " IR_LOAD_STRING " << vm_jit_read_string(trace, &pc) << std::endl;
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
        case IR_SUB_INT:
            op1 = vm_jit_read_int(trace, &pc);
            op2 = vm_jit_read_int(trace, &pc);
            std::cout << " IR_SUB_INT " << op1 << " " << op2 << std::endl;
            break;
        case IR_UNARY_MINUS_INT:
            std::cout << " IR_UNARY_MINUS_INT " << vm_jit_read_int(trace, &pc) << std::endl;
            break;
        case IR_LOOPEXIT:
            std::cout << " IR_LOOPEXIT " << int(trace[pc++]) << std::endl;
            break;
        default:
            std::cout << " UNKOWN" << std::endl;
        }

        ref++;
    }
}

void* SunScript::JIT_CompileTrace(void* instance, VirtualMachine* vm, unsigned char* trace, int size)
{
    unsigned char* jit = new unsigned char [1024 * 3];
    unsigned int pc = 0;

    // =================================
    //for (int i = 0; i < size; i++)
    //{
    //std::cout << int(trace[i]) << " ";
    //}
    //std::cout << std::endl;
    //===============================
    
    //JIT_DumpTrace(trace, size);

    std::unique_ptr<Jitter> jitter = std::make_unique<Jitter>();
    jitter->program = trace;
    jitter->pc = &pc;
    jitter->size = size;
    jitter->jit = jit;
    jitter->info = nullptr; //info;
    //jitter->locals.resize(128/*info->locals.size() + info->parameters.size()*/);
    jitter->_manager = reinterpret_cast<JIT_Manager*>(instance);
    //jitter->fg.Init(jitter->program, pc, size);

    jitter->_trace = new JIT_Trace();
    jitter->_trace->_id = 0; //info->name; 
    jitter->_trace->_runCount = 0;
    jitter->_trace->_jumpPos = 0;

    std::chrono::steady_clock clock;

    //==============================
    // Run the JIT compilation
    //==============================
    jitter->_trace->_startTime = clock.now().time_since_epoch().count();

    jitter->analyzer.Load(trace, size);

    vm_jit_generate_trace(vm, jitter.get());

    jitter->_trace->_jit_data = vm_allocate(jitter->count);
    vm_initialize(jitter->_trace->_jit_data, jitter->jit, jitter->count);

    /*for (auto& stub : jitter->_method->_stubs)
    {
        stub._patch._data = jitter->_method->_jit_data;
        stub._patch._size = jitter->count;
    }*/

    jitter->_trace->_size = jitter->count;

    //===============================
    jitter->_trace->_endTime = clock.now().time_since_epoch().count();
    delete[] jit;

    return jitter->_trace;
}

//===========================
