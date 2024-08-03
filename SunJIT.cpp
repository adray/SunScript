#include "SunJIT.h"
#include "SunScript.h"
#include <Windows.h>
#include <vector>
#include <string>
#include <stack>
#include <iostream>
#include <unordered_map>
#include <memory>
#include <assert.h>
#include <chrono>
#include <algorithm>

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
    VM_REGISTER_R15 = 0xe,
    VM_REGISTER_R16 = 0xf,

    VM_REGISTER_MAX = 0x10
};

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

static vm_instruction gInstructions[VMI_MAX_INSTRUCTIONS] = {
    INS(0x48, 0x1, 0, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_MR),     // VMI_ADD64_SRC_REG_DST_REG
    INS(0x48, 0x81, 0, VM_INSTRUCTION_BINARY, CODE_BRI, VMI_ENC_MI),    // VMI_ADD64_SRC_IMM_DST_REG
    INS(0x48, 0x3, 0, VM_INSTRUCTION_BINARY, CODE_BRMO, VMI_ENC_RM),    // VMI_ADD64_SRC_MEM_DST_REG
    INS(0x48, 0x1, 0, VM_INSTRUCTION_BINARY, CODE_BMRO, VMI_ENC_MR),    // VMI_ADD64_SRC_REG_DST_MEM

    INS(0x48, 0x29, 0, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_MR),    // VMI_SUB64_SRC_REG_DST_REG
    INS(0x48, 0x81, 5, VM_INSTRUCTION_BINARY, CODE_BRI, VMI_ENC_MI),    // VMI_SUB64_SRC_IMM_DST_REG
    INS(0x48, 0x2B, 0, VM_INSTRUCTION_BINARY, CODE_BRMO, VMI_ENC_RM),   // VMI_SUB64_SRC_MEM_DST_REG
    INS(0x48, 0x29, 0, VM_INSTRUCTION_BINARY, CODE_BMRO, VMI_ENC_MR),   // VMI_SUB64_SRC_REG_DST_MEM

    INS(0x48, 0x89, 0, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_MR),    // VMI_MOV64_SRC_REG_DST_REG
    INS(0x48, 0x89, 0, VM_INSTRUCTION_BINARY, CODE_BMRO, VMI_ENC_MR),   // VMI_MOV64_SRC_REG_DST_MEM
    INS(0x48, 0x8B, 0, VM_INSTRUCTION_BINARY, CODE_BRMO, VMI_ENC_RM),   // VMI_MOV64_SRC_MEM_DST_REG
    INS(0x48, 0xC7, 0, VM_INSTRUCTION_BINARY, CODE_BRI, VMI_ENC_MI),    // VMI_MOV64_SRC_IMM_DST_REG

    INS(0x0, 0xB8, 0, VM_INSTRUCTION_BINARY, CODE_BRI, VMI_ENC_OI),    // VMI_MOV32_SRC_IMM_DST_REG

    INS(0x48, 0x0F, 0xAF, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_RM), // VMI_MUL64_SRC_REG_DST_REG
    INS(0x48, 0x0F, 0xAF, VM_INSTRUCTION_BINARY, CODE_BRMO, VMI_ENC_RM), // VMI_MUL64_SRC_MEM_DST_REG

    INS(0x48, 0xFF, 0x0, VM_INSTRUCTION_UNARY, CODE_UMO, VMI_ENC_M),    // VMI_INC64_DST_MEM
    INS(0x48, 0xFF, 0x0, VM_INSTRUCTION_UNARY, CODE_UR, VMI_ENC_M),    // VMI_INC64_DST_REG

    INS(0x48, 0xFF, 0x1, VM_INSTRUCTION_UNARY, CODE_UMO, VMI_ENC_M),    // VMI_DEC64_DST_MEM
    INS(0x48, 0xFF, 0x1, VM_INSTRUCTION_UNARY, CODE_UR, VMI_ENC_M),    // VMI_DEC64_DST_REG

    INS(0x0, 0xC3, 0x0, VM_INSTRUCTION_NONE, CODE_NONE, 0x0),           // VMI_NEAR_RETURN
    INS(0x0, 0xCB, 0x0, VM_INSTRUCTION_NONE, CODE_NONE, 0x0),           // VMI_FAR_RETURN

    INS(0x48, 0x3B, 0x0, VM_INSTRUCTION_BINARY, CODE_BRR, VMI_ENC_RM), // VMI_CMP64_SRC_REG_DST_REG
    INS(0x48, 0x39, 0x0, VM_INSTRUCTION_BINARY, CODE_BMR, VMI_ENC_MR), // VMI_CMP64_SRC_REG_DST_MEM
    INS(0x48, 0x3B, 0x0, VM_INSTRUCTION_BINARY, CODE_BRM, VMI_ENC_RM), // VMI_CMP64_SRC_MEM_DST_REG

    INS(0x0, 0xEB, 0x0, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_J8
    INS(0x0, 0x74, 0x0, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_JE8
    INS(0x0, 0x75, 0x0, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_JNE8
    INS(0x0, 0x72, 0x0, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_JL8,
    INS(0x0, 0x77, 0x0, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_JG8,
    INS(0x0, 0x76, 0x0, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_JLE8,
    INS(0x0, 0x73, 0x0, VM_INSTRUCTION_CODE_OFFSET, CODE_UI, VMI_ENC_D), // VMI_JGE8,
    INS(0x0, 0xFF, 0x4, VM_INSTRUCTION_CODE_OFFSET, CODE_UR, VMI_ENC_M), // VMI_JA64

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
    if (ins.subins > 0)
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
        if (ins.subins > 0) { program[count++] = ins.subins; }

        program[count++] = 0xC0 | (((src % 8) & 0x7) << 0x3) | (((dst % 8) & 0x7) << 0);
    }
    else if (ins.enc == VMI_ENC_RM)
    {
        if (ins.rex > 0) { program[count++] = ins.rex | (dst >= VM_REGISTER_R8 ? 0x4 : 0x0) | (src >= VM_REGISTER_R8 ? 0x1 : 0x0); }
        program[count++] = ins.ins;
        if (ins.subins > 0) { program[count++] = ins.subins; }

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
    if (ins.subins > 0) { program[count++] = ins.subins; }

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
    program[count++] = 0x50 | (reg & 0x7);
}

static void vm_pop_reg(unsigned char* program, int& count, char reg)
{
    program[count++] = 0x58 | (reg & 0x7);
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

inline static void vm_jump_unconditional(unsigned char* program, int& count, char imm)
{
    vm_emit_ui(gInstructions[VMI_J8], program, count, imm);
}

inline static void vm_jump_equals(unsigned char* program, int& count, char imm)
{
    vm_emit_ui(gInstructions[VMI_JE8], program, count, imm);
}

inline static void vm_jump_not_equals(unsigned char* program, int& count, char imm)
{
    vm_emit_ui(gInstructions[VMI_JNE8], program, count, imm);
}

inline static void vm_jump_less(unsigned char* program, int& count, char imm)
{
    vm_emit_ui(gInstructions[VMI_JL8], program, count, imm);
}

inline static void vm_jump_less_equal(unsigned char* program, int& count, char imm)
{
    vm_emit_ui(gInstructions[VMI_JLE8], program, count, imm);
}

inline static void vm_jump_greater(unsigned char* program, int& count, char imm)
{
    vm_emit_ui(gInstructions[VMI_JG8], program, count, imm);
}

inline static void vm_jump_greater_equal(unsigned char* program, int& count, char imm)
{
    vm_emit_ui(gInstructions[VMI_JGE8], program, count, imm);
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
            pc += unsigned int(strlen((char*)&ins[pc])) + 1;
        }
        break;
    case OP_CALL:
        pc += 5; // ins (byte) + numArgs (int)
        pc += unsigned int(strlen((char*)&ins[pc])) + 1;
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
        pc += unsigned int(strlen((char*)&ins[pc])) + 1;
        break;
    case OP_POP:
        pc++;
        pc += unsigned int(strlen((char*)&ins[pc])) + 1;
        break;
    case OP_POP_DISCARD:
        pc++;
        break;
    case OP_PUSH_LOCAL:
        pc++;
        pc += unsigned int(strlen((char*)&ins[pc])) + 1;
        break;
    case OP_RETURN:
        pc++;
        break;
    case OP_UNARY_MINUS:
        pc++;
        break;
    case OP_YIELD:
        pc += 2; // ins (byte) + numArgs (int)
        pc += unsigned int(strlen((char*)&ins[pc])) + 1;
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

/*void JIT_FlowGraph::ConsumeInstruction(unsigned char* ins, unsigned int& pc)
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
            pc += unsigned int(strlen((char*)&ins[pc])) + 1;
        }
        break;
    case OP_CALL:
        pc += 5; // ins (byte) + numArgs (int)
        pc += unsigned int(strlen((char*)&ins[pc])) + 1;
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
        pc += unsigned int(strlen((char*)&ins[pc])) + 1;
        break;
    case OP_POP:
        pc++;
        pc += unsigned int(strlen((char*)&ins[pc])) + 1;
        break;
    case OP_POP_DISCARD:
        pc++;
        break;
    case OP_PUSH_LOCAL:
        pc++;
        pc += unsigned int(strlen((char*)&ins[pc])) + 1;
        break;
    case OP_RETURN:
        pc++;
        break;
    case OP_UNARY_MINUS:
        pc++;
        break;
    case OP_YIELD:
        pc += 2; // ins (byte) + numArgs (int)
        pc += unsigned int(strlen((char*)&ins[pc])) + 1;
        break;
    case OP_DONE:
        pc++;
        break;
    }
}*/

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

class RegisterAllocator
{
public:
    RegisterAllocator()
    {
        std::memset(registers, 0, sizeof(registers));

        // Reserved registers
        registers[VM_REGISTER_ESP] = true;
        registers[VM_REGISTER_EBP] = true;
        registers[VM_REGISTER_EDI] = true;
        registers[VM_REGISTER_ESI] = true;
    }

    int Allocate(int reg)
    {
        if (!registers[reg])
        {
            registers[reg] = true;
            //std::cout << "Alloc " << reg << std::endl;
            return reg;
        }

        return -1;
    }

    int Allocate()
    {
        for (int i = 0; i < VM_REGISTER_MAX; i++)
        {
            if (!registers[i])
            {
                registers[i] = true;
                //std::cout << "Alloc " << i << std::endl;
                return i;
            }
        }

        return -1;
    }

    bool IsUsed(int reg)
    {
        return registers[reg];
    }

    void Free(int reg)
    {
        registers[reg] = false;
        //std::cout << "Free " << reg << std::endl;
    }
private:
    bool registers[VM_REGISTER_MAX];
};

struct Local
{
    int type;   // The type TY_INT etc
    int pos;    // The offset on the stack 
};

unsigned int ST_REG = 0;
unsigned int ST_STACK = 1;
unsigned int SF_PUSH = 0x1;

struct StackItem
{
    int store;      // Where the stack item resides : ST_REG/ST_STACK
    int pos;        // The offset on the stack 
    int reg;        // The register it is stored in
    int type;       // The type TY_INT etc
    int flags;
};

class VirtualStack
{
public:
    VirtualStack() : pos(32) { }

    int Local()
    {
        pos += 8;   // 8 bytes
        return pos;
    }

    void Push_Local(int pos, int type)
    {
        auto& item = stack.emplace_back();
        item.store = ST_STACK;
        item.pos = pos;
        item.type = type;
        item.flags = 0;
        item.reg = VM_REGISTER_ESP;
    }

    //int Push_Stack(int type)
    //{
    //    pos -= 8;   // 8 bytes

    //    auto& item = stack.emplace_back();
    //    item.store = ST_STACK;
    //    item.pos = pos;
    //    item.type = type;
    //    item.flags = SF_PUSH;
    //    item.reg = VM_REGISTER_EBP;

    //    return item.pos;
    //}

    void Push_Register(int reg, int type)
    {
        auto& item = stack.emplace_back();
        item.store = ST_REG;
        item.reg = reg;
        item.type = type;
        item.flags = 0;
        item.pos = 0;
    }

    void Mov(int dst, int src)
    {
        for (int i = 0; i < stack.size(); i++)
        {
            if (stack[i].reg == src)
            {
                stack[i].reg = dst;
                break;
            }
        }
    }

    void Peek(int depth, StackItem* item)
    {
        const size_t pos = Size() - 1 - depth;
        if (pos >= 0)
        {
            *item = stack[pos];
        }
    }

    StackItem Pop()
    {
        StackItem item = *stack.rbegin();
        stack.erase(stack.begin() + stack.size() - 1);
        if ((item.flags & SF_PUSH) == SF_PUSH)
        {
            pos -= 8;   // 8 bytes
        }

        return item;
    }

    inline size_t Size() const { return stack.size(); }

private:
    std::vector<StackItem> stack;
    int pos;
};

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

struct JIT_Method;

struct JIT_SunStub
{
    int _numArgs;
    JIT_Patch _patch;
    JIT_Method* _stubMethod;
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

struct JIT_Method
{
    void* _jit_data;
    int _size;
    int _jumpPos;

    std::string _cacheKey;
    std::string _signature;
    std::string _name;

    std::vector<JIT_SunStub> _stubs;
    std::vector<JIT_Jump> _forwardJumps;
    std::vector<JIT_BackwardJump> _backwardJumps;

    uint64_t _startTime;     // compilation start time
    uint64_t _endTime;       // compilation end time
    uint64_t _runCount;      // number of times the method has been invoked
};

class JIT_Cache
{
public:
    int CacheJIT(const std::string& key, JIT_Method* jit)
    {
        const auto& it = _cache.find(key);
        if (it != _cache.end())
        {
            return VM_ERROR;
        }

        _cache.insert(std::pair<std::string, JIT_Method*>(key, jit));

        return VM_OK;
    }

    JIT_Method* SearchJITCache(const std::string& key)
    {
        const auto& it = _cache.find(key);
        if (it != _cache.end())
        {

            return it->second;
        }

        return nullptr;
    }

    void GetData(std::vector<JIT_Method*>& methods)
    {
        for (auto& item : _cache)
        {
            methods.push_back(item.second);
        }
    }

private:
    std::unordered_map<std::string, JIT_Method*> _cache;
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
    JIT_Cache _cache;
    JIT_Coroutine _co;
};

class Jitter
{
public:
    unsigned char* program;
    unsigned char* jit;
    int count;
    unsigned int* pc;
    RegisterAllocator allocator;
    VirtualStack stack;
    std::unordered_map<std::string, Local> locals;
    JIT_Method* _method;
    JIT_Manager* _manager;
    JIT_FlowGraph fg;
    FunctionInfo* info;
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
        _method(nullptr),
        argsProcessed(0),
        _manager(nullptr)
    {
    }

    void SetError()
    {
        error = true;
        running = false;
    }
};

//===================================

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

    static void vm_call_stub(VirtualMachine* vm, char* name, const int numArgs)
    {
        InvokeHandler(vm, name, numArgs);
    }

    static void* vm_box_int(JIT_Manager* man, int value)
    {
        int* mem = (int*)man->_mm.New(sizeof(int64_t), TY_INT);
        *mem = value;
        return mem;
    }

    static char* vm_append_string_int(JIT_Manager* man, char* left, int right)
    {
        std::stringstream ss;
        ss << left << right;
        std::string str = ss.str();
        char* data = (char*)man->_mm.New(str.length() + 1, TY_STRING);
        std::memcpy(data, str.c_str(), str.length());
        data[str.length()] = '\0';
        return data;
    }

    static char* vm_append_string_string(JIT_Manager* man, char* left, char* right)
    {
        std::stringstream ss;
        ss << left << right;
        std::string s = ss.str();
        char* data = (char*)man->_mm.New(s.length() + 1, TY_STRING);
        std::memcpy(data, s.c_str(), s.length());
        data[s.length()] = '\0';
        return data;
    }

    static char* vm_append_int_string(JIT_Manager* man, int left, char* right)
    {
        std::stringstream ss;
        ss << left << right;
        std::string s = ss.str();
        char* data = (char*)man->_mm.New(s.length() + 1, TY_STRING);
        std::memcpy(data, s.c_str(), s.length());
        data[s.length()] = '\0';
        return data;
    }

    static void vm_call_sun_stub(JIT_Manager* mm, VirtualMachine* vm, JIT_Method* caller, JIT_Method* stub)
    {
        /*
        * The stub is a method specifically generated for the caller method.
        * It is stored in the cache appended to the caller name.
        * 
        * The stub method itself has a stub which points to NULL.
        * This should be patched.
        * 
        * The method which owns the stub method has at least one stub to patch.
        */

        bool cache = false;
        void* data = mm->_cache.SearchJITCache(stub->_cacheKey);
        if (!data)
        {
            FunctionInfo* info;
            if (VM_OK == FindFunction(vm, stub->_name, &info))
            {
                data = JIT_Compile(mm, vm, GetLoadedProgram(vm), info, stub->_signature);
                cache = true;
            }

            if (!data)
            {
                return;
            }
        }

        JIT_Method* method = reinterpret_cast<JIT_Method*>(data);

        // We need to replace the caller stub call address with a JIT compiled version.

        for (auto& s : stub->_stubs)
        {
            if (s._patch._state == PATCH_INITIALIZED)
            {
                vm_begin_patch(s._patch._data, s._patch._size);
                vm_mov_imm_to_reg_x64((unsigned char*)s._patch._data, s._patch._offset, s._patch._reg, (long long)method->_jit_data);
                vm_commit_patch(s._patch._data, s._patch._size);

                s._patch._state = PATCH_APPLIED;
            }
        }

        for (auto& s : caller->_stubs)
        {
            auto& patch = s._patch;
            if (patch._state == PATCH_INITIALIZED && s._stubMethod == stub)
            {
                vm_begin_patch(patch._data, patch._size);
                vm_mov_imm_to_reg_x64((unsigned char*)patch._data, patch._offset, patch._reg, (long long)method->_jit_data);
                vm_commit_patch(patch._data, patch._size);

                patch._state = PATCH_APPLIED;
            }
        }

        if (cache)
        {
            mm->_cache.CacheJIT(stub->_cacheKey, method);
        }
    }
}

// =================================

static void vm_jit_call_internal_x64(Jitter* jitter, int numParams, void* address, int retType)
{
    jitter->allocator.Allocate(VM_REGISTER_ECX);
    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_ECX, (long long)jitter->_manager);

    int registers[4] = {
        VM_REGISTER_ECX,
        VM_REGISTER_EDX,
        VM_REGISTER_R8,
        VM_REGISTER_R9
    };
    for (int i = 1; i <= numParams; i++)
    {
        const StackItem item = jitter->stack.Pop();
        if (item.store == ST_REG)
        {
            vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, registers[i], item.reg);
            jitter->allocator.Allocate(registers[i]);
        }
        else if (item.store == ST_STACK)
        {
            vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, registers[i], item.reg, item.pos);
            jitter->allocator.Allocate(registers[i]);
        }
    }

    const int call = jitter->allocator.Allocate();
    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, call, (long long)address);
    vm_call_absolute(jitter->jit, jitter->count, call);
    jitter->allocator.Free(call);

    if (retType != TY_VOID)
    {
        jitter->allocator.Allocate(VM_REGISTER_EAX);
        jitter->stack.Push_Register(VM_REGISTER_EAX, retType);
    }

    for (int i = 1; i <= numParams; i++)
    {
        jitter->allocator.Free(registers[i]);
    }

    jitter->allocator.Free(VM_REGISTER_ECX);
}

static void vm_jit_spill_register(Jitter* jitter, int reg)
{
    if (jitter->allocator.IsUsed(reg))
    {
        const int spill = jitter->allocator.Allocate();
        vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, spill, reg);

        jitter->stack.Mov(spill, reg);
        
        // NOTE: we don't free this register in case we need to spill more
        // and we don't want to spill them into registers we just freed.
    }
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

static void vm_jit_local(Jitter* jitter)
{
    const std::string name = vm_jit_read_string(jitter->program, jitter->pc);

    const int pos = jitter->stack.Local();
    //vm_sub_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_ESP, 8); // grow stack

    Local local = { .type = TY_VOID, .pos = pos };

    jitter->locals.insert(std::pair<std::string, Local>(name, local));
}

static void vm_jit_pop(Jitter* jitter)
{
    const std::string name = vm_jit_read_string(jitter->program, jitter->pc);

    StackItem it = jitter->stack.Pop();
    Local& local = jitter->locals[name];
    if (it.store == ST_STACK)
    {
        if (it.pos != local.pos)
        {
            const int reg = jitter->allocator.Allocate();
            vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg, it.reg, it.pos);
            vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, VM_REGISTER_ESP, local.pos, reg);
            jitter->allocator.Free(reg);

            local.type = it.type;
        }
    }
    else if (it.store == ST_REG)
    {
        vm_mov_reg_to_memory_x64(jitter->jit, jitter->count, VM_REGISTER_ESP, local.pos, it.reg);
        jitter->allocator.Free(it.reg);

        local.type = it.type;
    }
}

static void vm_jit_push_local(Jitter* jitter)
{
    const std::string name = vm_jit_read_string(jitter->program, jitter->pc);

    const auto& it = jitter->locals.find(name);
    if (it != jitter->locals.end())
    {
        jitter->stack.Push_Local(it->second.pos, it->second.type);
    }
    else
    {
        jitter->SetError();
    }
}

static void vm_jit_push(Jitter* jitter)
{
    unsigned char type = jitter->program[*(jitter->pc)];
    (*(jitter->pc))++;
    switch (type)
    {
    case TY_INT:
        {
            const int value = vm_jit_read_int(jitter->program, jitter->pc);
            const int reg = jitter->allocator.Allocate();
            if (reg != -1)
            {
                vm_mov_imm_to_reg(jitter->jit, jitter->count, reg, value);
                jitter->stack.Push_Register(reg, TY_INT);
            }
            else
            {
                // No registers free...
                jitter->SetError();
            }
        }
        break;
    case TY_STRING:
        {
            const char* str = (char*)&jitter->program[*jitter->pc];
            (*jitter->pc) += unsigned int(strlen(str)) + 1;
            
            const int reg = jitter->allocator.Allocate();
            if (reg != -1)
            {
                vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, reg, (long long)str);
                jitter->stack.Push_Register(reg, TY_STRING);
            }
            else
            {
                // No registers free...
                jitter->SetError();
            }
        }
        break;
    default:
        // error
        jitter->SetError();
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

static void vm_jit_patch_next_jump(Jitter* jitter)
{
    bool cont = true;
    
    while (jitter->_method->_jumpPos < jitter->_method->_forwardJumps.size() && cont)
    {
        cont = false;

        auto& jump = jitter->_method->_forwardJumps[jitter->_method->_jumpPos];
        if (*jitter->pc == jump._pos &&
            jump._state == PATCH_INITIALIZED)
        {
            cont = true;
            jitter->_method->_jumpPos++;
            jump._state = PATCH_APPLIED;

            const int rel = jitter->count - (jump._offset + jump._size);

            vm_jit_jump(jitter, jump._type, jump._offset, rel);
        }
    }
}

static void vm_jit_cmp(Jitter* jitter)
{
    if (jitter->stack.Size() < 2)
    {
        jitter->SetError();
        return;
    }

    StackItem i1;
    StackItem i2;
    jitter->stack.Peek(0, &i1);
    jitter->stack.Peek(1, &i2);

    jitter->stack.Pop();
    jitter->stack.Pop();

    if (i1.store == ST_REG && i2.store == ST_REG)
    {
        vm_cmp_reg_to_reg_x64(jitter->jit, jitter->count, i1.reg, i2.reg);

        jitter->allocator.Free(i1.reg);
        jitter->allocator.Free(i2.reg);
    }
    else if (i1.store == ST_REG && i2.store == ST_STACK)
    {
        const int reg = jitter->allocator.Allocate();
        vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg, i2.reg, i2.pos);
        vm_cmp_reg_to_reg_x64(jitter->jit, jitter->count, i1.reg, reg);
        jitter->allocator.Free(reg);
        jitter->allocator.Free(i1.reg);
    }
    else if (i1.store == ST_STACK && i2.store == ST_REG)
    {
        const int reg = jitter->allocator.Allocate();
        vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg, i1.reg, i1.pos);
        vm_cmp_reg_to_reg_x64(jitter->jit, jitter->count, reg, i2.reg);
        jitter->allocator.Free(reg);
        jitter->allocator.Free(i2.reg);
    }
    else if(i1.store == ST_STACK && i2.store == ST_STACK)
    {
        const int reg1 = jitter->allocator.Allocate();
        const int reg2 = jitter->allocator.Allocate();
        vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg1, i1.reg, i1.pos);
        vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg2, i2.reg, i2.pos);
        vm_cmp_reg_to_reg_x64(jitter->jit, jitter->count, reg1, reg2);
        jitter->allocator.Free(reg1);
        jitter->allocator.Free(reg2);
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

        const auto& pos = std::lower_bound(jitter->_method->_forwardJumps.begin(), jitter->_method->_forwardJumps.end(), jump, [](const JIT_Jump& j1, const JIT_Jump& j2) {
            return j1._pos < j2._pos;
            });
        jitter->_method->_forwardJumps.insert(pos, jump);
    }
    else
    {
        // Backward jump (no patching)
        for (size_t i = 0; i < jitter->_method->_backwardJumps.size(); i++)
        {
            auto& jump = jitter->_method->_backwardJumps[i];
            if (offset + *jitter->pc == jump._target)
            {
                const int imm = jump._pos - (jitter->count + 2 /*Length of jump instruction*/);
                vm_jit_jump(jitter, jump._type, jitter->count, imm);
                break;
            }
        }
    }
}

static void vm_jit_add(Jitter* jitter)
{
    if (jitter->stack.Size() >= 2)
    {
        StackItem i1;
        StackItem i2;

        jitter->stack.Peek(0, &i1);
        jitter->stack.Peek(1, &i2);

        if (i1.type == TY_INT && i2.type == TY_INT)
        {
            jitter->stack.Pop();
            jitter->stack.Pop();

            if (i1.store == ST_REG && i2.store == ST_REG)
            {
                vm_add_reg_to_reg_x64(jitter->jit, jitter->count, i1.reg, i2.reg);
                jitter->allocator.Free(i2.reg);
                jitter->stack.Push_Register(i1.reg, TY_INT);
            }
            else if (i1.store == ST_REG && i2.store == ST_STACK)
            {
                vm_add_memory_to_reg_x64(jitter->jit, jitter->count, i1.reg, i2.reg, i2.pos);
                jitter->stack.Push_Register(i1.reg, TY_INT);
            }
            else if (i2.store == ST_REG && i1.store == ST_STACK)
            {
                vm_add_memory_to_reg_x64(jitter->jit, jitter->count, i2.reg, i1.reg, i1.pos);
                jitter->stack.Push_Register(i2.reg, TY_INT);
            }
            else if (i1.store == ST_STACK && i2.store == ST_STACK)
            {
                const int reg1 = jitter->allocator.Allocate();

                vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg1, i1.reg, i1.pos);
                vm_add_memory_to_reg_x64(jitter->jit, jitter->count, reg1, i2.reg, i2.pos);

                jitter->stack.Push_Register(reg1, TY_INT);
            }
        }
        else if (i1.type == TY_STRING && i2.type == TY_INT)
        {
            vm_jit_call_internal_x64(jitter, 2, vm_append_string_int, TY_STRING);
        }
        else if (i1.type == TY_INT && i2.type == TY_STRING)
        {
            vm_jit_call_internal_x64(jitter, 2, vm_append_int_string, TY_STRING);
        }
        else if (i1.type == TY_STRING && i2.type == TY_STRING)
        {
            vm_jit_call_internal_x64(jitter, 2, vm_append_string_string, TY_STRING);
        }
        else
        {
            // Unsupported
            jitter->SetError();
        }
    }
    else
    {
        // Error
        jitter->SetError();
    }
}

static void vm_jit_sub(Jitter* jitter)
{
    if (jitter->stack.Size() >= 2)
    {
        const StackItem i1 = jitter->stack.Pop();
        const StackItem i2 = jitter->stack.Pop();

        if (i1.type == TY_INT && i2.type == TY_INT)
        {
            if (i1.store == ST_REG && i2.store == ST_REG)
            {
                vm_sub_reg_to_reg_x64(jitter->jit, jitter->count, i1.reg, i2.reg);
                jitter->allocator.Free(i2.reg);
                jitter->stack.Push_Register(i1.reg, TY_INT);
            }
            else if (i1.store == ST_REG && i2.store == ST_STACK)
            {
                vm_sub_memory_to_reg_x64(jitter->jit, jitter->count, i1.reg, i2.reg, i2.pos);
                jitter->stack.Push_Register(i1.reg, TY_INT);
            }
            else if (i2.store == ST_REG && i1.store == ST_STACK)
            {
                const int reg = jitter->allocator.Allocate();
                vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg, i1.reg, i1.pos);
                vm_sub_reg_to_reg_x64(jitter->jit, jitter->count, reg, i2.reg);
                jitter->stack.Push_Register(reg, TY_INT);
                jitter->allocator.Free(i2.reg);
            }
            else if (i1.store == ST_STACK && i2.store == ST_STACK)
            {
                const int reg1 = jitter->allocator.Allocate();

                vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg1, i1.reg, i1.pos);
                vm_sub_memory_to_reg_x64(jitter->jit, jitter->count, reg1, i2.reg, i2.pos);

                jitter->stack.Push_Register(reg1, TY_INT);
            }
        }
        else
        {
            // Unsupported
            jitter->SetError();
        }
    }
    else
    {
        // Error
        jitter->SetError();
    }
}

static void vm_jit_mul(Jitter* jitter)
{
    if (jitter->stack.Size() >= 2)
    {
        const StackItem i1 = jitter->stack.Pop();
        const StackItem i2 = jitter->stack.Pop();

        if (i1.type == TY_INT && i2.type == TY_INT)
        {
            if (i1.store == ST_REG && i2.store == ST_REG)
            {
                vm_mul_reg_to_reg_x64(jitter->jit, jitter->count, i1.reg, i2.reg);
                jitter->allocator.Free(i2.reg);
                jitter->stack.Push_Register(i1.reg, TY_INT);
            }
            else if (i1.store == ST_REG && i2.store == ST_STACK)
            {
                vm_mul_memory_to_reg_x64(jitter->jit, jitter->count, i1.reg, i2.reg, i2.pos);
                jitter->stack.Push_Register(i1.reg, TY_INT);
            }
            else if (i2.store == ST_REG && i1.store == ST_STACK)
            {
                vm_mul_memory_to_reg_x64(jitter->jit, jitter->count, i2.reg, i1.reg, i1.pos);
                jitter->stack.Push_Register(i2.reg, TY_INT);
            }
            else if (i1.store == ST_STACK && i2.store == ST_STACK)
            {
                const int reg1 = jitter->allocator.Allocate();
                vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg1, i1.reg, i1.pos);
                vm_mul_memory_to_reg_x64(jitter->jit, jitter->count, reg1, i2.reg, i2.pos);

                jitter->stack.Push_Register(reg1, TY_INT);
            }
        }
        else
        {
            // Unsupported
            jitter->SetError();
        }
    }
    else
    {
        // Error
        jitter->SetError();
    }
}

static void vm_jit_div(Jitter* jitter)
{
    if (jitter->stack.Size() >= 2)
    {
        StackItem i1;
        StackItem i2;

        jitter->stack.Peek(0, &i1);
        jitter->stack.Peek(1, &i2);

        if (i1.type == TY_INT && i2.type == TY_INT)
        {
            if (i1.store == ST_REG && i2.store == ST_REG)
            {
                vm_jit_spill_register(jitter, VM_REGISTER_EAX);
                vm_jit_spill_register(jitter, VM_REGISTER_EDX);

                i1 = jitter->stack.Pop();
                i2 = jitter->stack.Pop();

                vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, i1.reg);
                vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EDX, 0);
                vm_div_reg_x64(jitter->jit, jitter->count, i2.reg);

                jitter->allocator.Free(VM_REGISTER_EDX);
                jitter->allocator.Free(VM_REGISTER_EAX);

                jitter->allocator.Free(i2.reg);
                jitter->allocator.Free(i1.reg);
                jitter->stack.Push_Register(VM_REGISTER_EAX, TY_INT);
            }
            else if (i1.store == ST_STACK && i2.store == ST_REG)
            {

            }
        }
        else
        {
            // Unsupported
            jitter->SetError();
        }
    }
    else
    {
        // Error
        jitter->SetError();
    }
}

static void vm_jit_dec(Jitter* jitter)
{
    if (jitter->stack.Size() >= 1)
    {
        const StackItem item = jitter->stack.Pop();
        if (item.type == TY_INT)
        {
            if (item.store == ST_REG)
            {
                vm_dec_reg_x64(jitter->jit, jitter->count, item.reg);
                jitter->stack.Push_Register(item.reg, item.type);
            }
            else if (item.store == ST_STACK)
            {
                vm_dec_memory_x64(jitter->jit, jitter->count, item.reg, item.pos);
                jitter->stack.Push_Local(item.pos, item.type);
            }
            else
            {
                // Unsupported
                jitter->SetError();
            }
        }
        else
        {
            // Unsupported
            jitter->SetError();
        }
    }
    else
    {
        // Error
        jitter->SetError();
    }
}

static void vm_jit_inc(Jitter* jitter)
{
    if (jitter->stack.Size() >= 1)
    {
        const StackItem item = jitter->stack.Pop();
        if (item.type == TY_INT)
        {
            if (item.store == ST_REG)
            {
                vm_inc_reg_x64(jitter->jit, jitter->count, item.reg);
                jitter->stack.Push_Register(item.reg, item.type);
            }
            else if (item.store == ST_STACK)
            {
                vm_inc_memory_x64(jitter->jit, jitter->count, item.reg, item.pos);
                jitter->stack.Push_Local(item.pos, item.type);
            }
            else
            {
                // Unsupported
                jitter->SetError();
            }
        }
        else
        {
            // Unsupported
            jitter->SetError();
        }
    }
    else
    {
        // Error
        jitter->SetError();
    }
}

static void vm_jit_neg(Jitter* jitter)
{
    if (jitter->stack.Size() >= 1)
    {
        const StackItem item = jitter->stack.Pop();
        if (item.type == TY_INT)
        {
            if (item.store == ST_REG)
            {
                vm_neg_reg_x64(jitter->jit, jitter->count, item.reg);
                jitter->stack.Push_Register(item.reg, item.type);
            }
            else if (item.store == ST_STACK)
            {
                const int reg = jitter->allocator.Allocate();
                vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, reg, item.reg, item.pos);

                vm_neg_reg_x64(jitter->jit, jitter->count, reg);
                jitter->stack.Push_Register(reg, item.type);
            }
        }
        else
        {
            // Error
            jitter->SetError();
        }
    }
}

static void* vm_jit_sun_stub(VirtualMachine* vm, Jitter* jitter, JIT_Method* method, int numParams, char* name, const std::string& signature)
{
    auto& stub = method->_stubs.emplace_back();

    stub._numArgs = numParams;
    stub._patch._state = PATCH_INITIALIZED;
    stub._stubMethod = nullptr;

    unsigned char jit[512];
    int count = 0;

    vm_push_reg(jit, count, VM_REGISTER_EDI);
    vm_push_reg(jit, count, VM_REGISTER_ESP);
    vm_push_reg(jit, count, VM_REGISTER_EBX);
    vm_sub_imm_to_reg_x64(jit, count, VM_REGISTER_ESP, 32); // grow stack enough for callees

    // 1) Push the parameter registers onto the stack

    if (numParams >= 1)
    {
        vm_mov_reg_to_memory_x64(jit, count, VM_REGISTER_EDI, 16, VM_REGISTER_ECX);
    }
    if (numParams >= 2)
    {
        vm_mov_reg_to_memory_x64(jit, count, VM_REGISTER_EDI, 24, VM_REGISTER_EDX);
    }

    // 2) Call the 'vm_call_sun_stub' to Trigger JIT compilation.

    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_ECX, (long long)jitter->_manager);
    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EDX, (long long)vm);
    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_R8, (long long)jitter->_method);
    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_R9, (long long)method);
    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EBX, (long long)vm_call_sun_stub);
    vm_call_absolute(jit, count, VM_REGISTER_EBX);

    // 3) Restore the parameter registers
    
    if (numParams >= 1)
    {
        vm_mov_memory_to_reg_x64(jit, count, VM_REGISTER_ECX, VM_REGISTER_EDI, 16);
    }

    // 4) Output a call to NULL. When JIT compilation occurs this will be patched to
    //    point to the newly compiled method. The registers which were passed to this
    //    will be forwarded to the new method.

    stub._patch._offset = count;

    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EBX, (long long)0);
    vm_call_absolute(jit, count, VM_REGISTER_EBX);

    vm_add_imm_to_reg_x64(jit, count, VM_REGISTER_ESP, 32); // free stack

    // Restore volatile registers
    vm_pop_reg(jit, count, VM_REGISTER_EBX);
    vm_pop_reg(jit, count, VM_REGISTER_ESP);
    vm_pop_reg(jit, count, VM_REGISTER_EDI);
    vm_return(jit, count);

    void* data = vm_allocate(count);
    vm_initialize(data, jit, count);
    method->_size = count;
    stub._patch._data = data;
    stub._patch._size = count;
    stub._patch._reg = VM_REGISTER_EBX;
    return data;
}

static void vm_jit_call_push_stub(VirtualStack& stack, VirtualMachine* vm, unsigned char* jit, int& count, int param)
{
    StackItem item;
    stack.Peek(param, &item);

    if (item.type == TY_INT)
    {
        vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EBX, (long long)vm_push_int_stub);
    }
    else if (item.type == TY_STRING)
    {
        vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EBX, (long long)vm_push_string_stub);
    }

    // Store the VM pointer in ECX.
    // We do this each time in case the register is cleared.
    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_ECX, (long long)vm);

    // Call the function.
    vm_call_absolute(jit, count, VM_REGISTER_EBX);
}

static void* vm_jit_cpp_interop(VirtualMachine* vm, Jitter* jitter, int numParams, char* name)
{
    unsigned char jit[512];
    int count = 0;

    vm_push_reg(jit, count, VM_REGISTER_EDI);
    vm_push_reg(jit, count, VM_REGISTER_ESP);
    vm_push_reg(jit, count, VM_REGISTER_EBX);
    vm_sub_imm_to_reg_x64(jit, count, VM_REGISTER_ESP, 32); // grow stack enough for callees

    // Calls are the in the form:
    // 
    // EBX - CALLEE ADDR
    // ECX - VM ADDR
    // EDX - PARAMETER

    // Store the first two parameters in: EDX and R12
    vm_mov_reg_to_reg_x64(jit, count, VM_REGISTER_R12, VM_REGISTER_EDX);
    vm_mov_reg_to_reg_x64(jit, count, VM_REGISTER_EDX, VM_REGISTER_ECX);

    if (numParams >= 1)
    {
        // The first parameter is already within EDX

        vm_jit_call_push_stub(jitter->stack, vm, jit, count, 0);
    }
    if (numParams >= 2)
    {
        // The second parameter in within R12

        vm_mov_reg_to_reg_x64(jit, count, VM_REGISTER_EDX, VM_REGISTER_R12);
        vm_jit_call_push_stub(jitter->stack, vm, jit, count, 1);
    }

    // Store the VM pointer in ECX.
    // We do this each time in case the register is cleared.
    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_ECX, (long long)vm);

    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EDX, (long long)name);
    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_R8, numParams);
    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EBX, (long long)vm_call_stub);
    vm_call_absolute(jit, count, VM_REGISTER_EBX);

    vm_add_imm_to_reg_x64(jit, count, VM_REGISTER_ESP, 32); // free stack

    // Restore volatile registers
    vm_pop_reg(jit, count, VM_REGISTER_EBX);
    vm_pop_reg(jit, count, VM_REGISTER_ESP);
    vm_pop_reg(jit, count, VM_REGISTER_EDI);
    vm_return(jit, count);

    void* jit_compiled = vm_allocate(count);
    vm_initialize(jit_compiled, jit, count);
    return jit_compiled;
}

static std::string vm_generate_function_signature(Jitter* jitter, int numParams)
{
    std::stringstream signature;
    for (int i = 0; i < numParams; i++)
    {
        StackItem item;
        jitter->stack.Peek(i, &item);
        switch (item.type)
        {
        case TY_INT:
            signature << "I";
            break;
        case TY_STRING:
            signature << "S";
            break;
        }
    }

    return signature.str();
}

static void vm_jit_call_x64(VirtualMachine* vm, Jitter* jitter, int numParams, char* name)
{
    // Look up if the function is in the cache. Call that directly.
    // Otherwise we need to compile the function:

    std::string signature = vm_generate_function_signature(jitter, numParams);
    std::stringstream key;
    key << name << "_" << signature;

    bool shouldPatch = false;
    JIT_Method* method = reinterpret_cast<JIT_Method*>(jitter->_manager->_cache.SearchJITCache(key.str()));
    char returnType = TY_VOID;

    if (!method)
    {
        method = new JIT_Method();
        method->_endTime = 0;
        method->_name = name;
        method->_runCount = 0;
        method->_signature = signature;
        method->_startTime = 0;
        method->_size = 0;
        method->_cacheKey = key.str();
        method->_jumpPos = 0;

        FunctionInfo* info;
        if (FindFunction(vm, name, &info) == VM_OK)
        {
            // Generate a unique stub method for this method.
            method->_jit_data = vm_jit_sun_stub(vm, jitter, method, numParams, name, signature);
            jitter->_manager->_cache.CacheJIT(jitter->_method->_cacheKey + "_" + key.str(), method);
            shouldPatch = true;

            // TODO: handle this properly
            returnType = TY_INT;
        }
        else
        {
            method->_jit_data = vm_jit_cpp_interop(vm, jitter, numParams, name);
            jitter->_manager->_cache.CacheJIT(key.str(), method);
        }
    }

    // ======================================
    // Now we generate a call to the compiled method
    //
    // x64 calling convention
    // Pass in the initial parameters by registers:
    // (leftmost) RCX, RDX, R8, R9, then passed on the stack
    //

    VirtualStack stack;

    int registers[] = {
        VM_REGISTER_ECX, VM_REGISTER_EDX, VM_REGISTER_R8, VM_REGISTER_R9
    };
    for (int i = 0; i < numParams; i++)
    {
        StackItem item = jitter->stack.Pop();
        if (item.store == ST_REG)
        {
            if (item.reg != registers[i])
            {
                vm_jit_spill_register(jitter, registers[i]);

                vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, registers[i], item.reg);

                jitter->allocator.Free(item.reg);
                jitter->allocator.Allocate(registers[i]);
            }

            stack.Push_Register(registers[i], item.type);
        }
        else if (item.store == ST_STACK)
        {
            vm_jit_spill_register(jitter, registers[i]);
            vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, registers[i], item.reg, item.pos);

            jitter->allocator.Allocate(registers[i]);
            stack.Push_Register(registers[i], item.type);
        }
    }

    const int reg = jitter->allocator.Allocate();

    if (shouldPatch)
    {
        JIT_SunStub& stub = jitter->_method->_stubs.emplace_back();
        stub._patch._reg = reg;
        stub._patch._offset = jitter->count;
        stub._patch._state = PATCH_INITIALIZED;
        stub._stubMethod = method;
    }

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, reg, (long long)method->_jit_data);
    vm_call_absolute(jitter->jit, jitter->count, reg);
    jitter->allocator.Free(reg);

    for (int i = 0; i < numParams; i++)
    {
        StackItem item = stack.Pop();
        jitter->allocator.Free(item.reg);
    }

    // ======================
    // Handle return value
    // ======================

    if (returnType != TY_VOID)
    {
        // This register is probably nuked anyway?
        vm_jit_spill_register(jitter, VM_REGISTER_EAX);

        // Push return value
        jitter->allocator.Allocate(VM_REGISTER_EAX);
        jitter->stack.Push_Register(VM_REGISTER_EAX, returnType);
    }
}

static void vm_jit_call(VirtualMachine* vm, Jitter* jitter)
{
    const int numParams = jitter->program[*jitter->pc];
    (*jitter->pc)++;

    char* name = (char*) & jitter->program[*jitter->pc];
    (*jitter->pc) += unsigned int(strlen(name)) + 1;

    vm_jit_call_x64(vm, jitter, numParams, name);
}

static void vm_jit_yield(VirtualMachine* vm, Jitter* jitter)
{
    const int numParams = jitter->program[*jitter->pc];
    (*jitter->pc)++;
    char* name = (char*)&jitter->program[*jitter->pc];
    (*jitter->pc) += unsigned int(strlen(name)) + 1;

    // First we do call_x64
    vm_jit_call_x64(vm, jitter, numParams, name);

    // Then we make a call to 'vm_yield'.
    // Which will record the stack pointer and the instruction pointer.
    // Then we will copy the stack from the initial entry point in the Sunscript to the heap.
    // Then we will set the vm state to paused and move the stack pointer upwards and jump to code which invoked sunscript.
    // Volatile register will be pushed to the stack before where the instruction pointer ends (hopefully this will preserve the registers)
    vm_jit_call_internal_x64(jitter, 0, jitter->_manager->_co._vm_suspend, TY_VOID);

    // Upon resuming we need to copy the stack back from the heap and the jump back to the resumption point.
}

inline static void vm_jit_epilog(Jitter* jitter, const int stacksize)
{
    vm_add_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_ESP, stacksize);
    vm_pop_reg(jitter->jit, jitter->count, VM_REGISTER_EBX);
    vm_pop_reg(jitter->jit, jitter->count, VM_REGISTER_EDX);
    vm_pop_reg(jitter->jit, jitter->count, VM_REGISTER_EDI);
}

static void vm_jit_box(Jitter* jitter)
{
    StackItem item;
    jitter->stack.Peek(0, &item);

    if (item.store == ST_REG)
    {
        switch (item.type)
        {
        case TY_INT:
            vm_jit_call_internal_x64(jitter, 1, (void*)&vm_box_int, TY_OBJECT);
            break;
        }
    }
    else if (item.store == ST_STACK)
    {
        switch (item.type)
        {
        case TY_INT:
            vm_jit_spill_register(jitter, VM_REGISTER_EDX);
            vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EDX, item.reg, item.pos);
            jitter->stack.Push_Register(VM_REGISTER_EDX, TY_OBJECT);

            vm_jit_call_internal_x64(jitter, 1, (void*)&vm_box_int, TY_OBJECT);
            break;
        }
    }
}

static void vm_jit_return(Jitter* jitter, const int stacksize)
{
    // Handle return value
    if (jitter->stack.Size() > 0)
    {
        vm_jit_box(jitter);

        StackItem item = jitter->stack.Pop();

        if (item.store == ST_REG)
        {
            if (item.reg != VM_REGISTER_EAX)
            {
                vm_mov_reg_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, item.reg);
            }
        }
        else if (item.store == ST_STACK)
        {
            vm_mov_memory_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EAX, VM_REGISTER_ESP, item.pos);
        }
    }

    vm_jit_epilog(jitter, stacksize);
    vm_return(jitter->jit, jitter->count);
}

static void vm_jit_pop_discard(Jitter* jitter)
{
    if (jitter->stack.Size() > 0)
    {
        StackItem i = jitter->stack.Pop();
        if (i.store == ST_REG)
        {
            jitter->allocator.Free(i.reg);
        }
    }
}

static void vm_jit_generate_block(VirtualMachine* vm, Jitter* jitter, BasicBlock* block, const int stacksize)
{
    for (auto& edge : block->Edges())
    {
        if (edge._true == block && edge._from->Jump() < 0)
        {
            // Backward jump
            auto& jump = jitter->_method->_backwardJumps.emplace_back();
            jump._type = edge._from->JumpType();
            jump._state = PATCH_INITIALIZED;
            jump._pos = jitter->count;
            jump._target = edge._true->BeginPos();
        }
    }
    
    vm_jit_patch_next_jump(jitter);

    while (jitter->running && *jitter->pc < block->EndPos())
    {
        //std::cout << "INS " << *jitter->pc << std::endl;
        const int ins = jitter->program[(*(jitter->pc))++];

        switch (ins)
        {
        case OP_LOCAL:
            vm_jit_local(jitter);
            break;
        case OP_POP:
            vm_jit_pop(jitter);
            break;
        case OP_JUMP:
            vm_jit_jump(jitter);
            break;
        case OP_CMP:
            vm_jit_cmp(jitter);
            break;
        case OP_CALL:
            vm_jit_call(vm, jitter);
            break;
        case OP_YIELD:
            vm_jit_yield(vm, jitter);
            break;
        case OP_PUSH:
            vm_jit_push(jitter);
            break;
        case OP_PUSH_LOCAL:
            vm_jit_push_local(jitter);
            break;
        case OP_UNARY_MINUS:
            vm_jit_neg(jitter);
            break;
        case OP_ADD:
            vm_jit_add(jitter);
            break;
        case OP_SUB:
            vm_jit_sub(jitter);
            break;
        case OP_MUL:
            vm_jit_mul(jitter);
            break;
        case OP_DIV:
            vm_jit_div(jitter);
            break;
        case OP_INCREMENT:
            vm_jit_inc(jitter);
            break;
        case OP_DECREMENT:
            vm_jit_dec(jitter);
            break;
        case OP_DONE:
            vm_jit_epilog(jitter, stacksize);
            vm_return(jitter->jit, jitter->count);
            jitter->running = false;
            break;
        case OP_RETURN:
            vm_jit_return(jitter, stacksize);
            break;
        case OP_POP_DISCARD:
            vm_jit_pop_discard(jitter);
            break;
        default:
            abort();
        }
    }
}

static void vm_jit_generate(VirtualMachine* vm, Jitter* jitter)
{
    // Store volatile registers

    vm_push_reg(jitter->jit, jitter->count, VM_REGISTER_EDI);
    vm_push_reg(jitter->jit, jitter->count, VM_REGISTER_EDX);
    vm_push_reg(jitter->jit, jitter->count, VM_REGISTER_EBX);

    // We need to allocate the stack to fit the local variables
    // before the space for the 'register homes' are allocated.
    // 
    // Parameters passed into the function
    // should be stored on the caller's 'register homes' not
    // on our stack frame.
    //

    const auto& signature = jitter->_method->_signature;

    // Push the parameters onto the VirtualStack
    const int registers[] = {
        VM_REGISTER_ECX,
        VM_REGISTER_EDX,
        VM_REGISTER_R8,
        VM_REGISTER_R9
    };
    for (int i = 0; i < signature.size(); i++)
    {
        switch (signature[i])
        {
        case 'I':
            jitter->stack.Push_Register(registers[i], TY_INT);
            break;
        case 'S':
            jitter->stack.Push_Register(registers[i], TY_STRING);
            break;
        }
    }

    const int stacksize = VM_ALIGN_16(int(jitter->info->locals.size()) * 8 /* Locals */ + 32 /* 4 register homes for callees */);

    vm_sub_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_ESP, stacksize); // grow stack

    // Inject logic to capture metric data

    vm_mov_imm_to_reg_x64(jitter->jit, jitter->count, VM_REGISTER_EDX, (long long)&jitter->_method->_runCount);
    vm_inc_memory_x64(jitter->jit, jitter->count, VM_REGISTER_EDX, 0);

    BasicBlock* blk = jitter->fg.Head();
    while (jitter->running && blk)
    {
        vm_jit_generate_block(vm, jitter, blk, stacksize);
        blk = blk->Next();
    }
}

void vm_jit_suspend(JIT_Manager* manager)
{
    unsigned char jit[1024];
    int count = 0;
    
    // Store volatile registers

    vm_push_reg(jit, count, VM_REGISTER_EDI);
    vm_push_reg(jit, count, VM_REGISTER_EDX);
    vm_push_reg(jit, count, VM_REGISTER_EBX);


    const int stacksize = VM_ALIGN_16(32 /* 4 register homes for callees */);

    vm_sub_imm_to_reg_x64(jit, count, VM_REGISTER_ESP, stacksize); // grow stack

    // TODO: dynamically reserve
    void* mem = manager->_mm.New(2048, TY_OBJECT);

    // Calculate stack size
    // StackPtr - ESP
    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_R8, (long long)&manager->_co._stackPtr);
    vm_mov_memory_to_reg_x64(jit, count, VM_REGISTER_R8, VM_REGISTER_R8, 0);
    vm_sub_reg_to_reg_x64(jit, count, VM_REGISTER_R8, VM_REGISTER_ESP);

    // Store stack size - for resumption
    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EAX, (long long)&manager->_co._stackSize);
    vm_mov_reg_to_memory_x64(jit, count, VM_REGISTER_EAX, 0, VM_REGISTER_R8);

    // Generate a call to memcpy
    // Dst (ECX = mem)
    // Src (EDX = ESP)
    // Size (R8 = size)

    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_ECX, (long long)mem);
    vm_mov_reg_to_reg_x64(jit, count, VM_REGISTER_EDX, VM_REGISTER_ESP);
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

    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_R8, (long long)&manager->_co._stackSize);
    vm_sub_memory_to_reg_x64(jit, count, VM_REGISTER_ESP, VM_REGISTER_R8, 0);
    vm_sub_imm_to_reg_x64(jit, count, VM_REGISTER_ESP, -8);     // make an adjustment (for some reason?)

    // Now we restore the stack
    // Dst (ECX = VM_REGISTER_ESP)
    // Src (EDX = mem)
    // Size (R8 = size)

    vm_mov_memory_to_reg_x64(jit, count, VM_REGISTER_R8, VM_REGISTER_R8, 0);
    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EDX, (long long)mem);
    vm_mov_reg_to_reg_x64(jit, count, VM_REGISTER_ECX, VM_REGISTER_ESP);
    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EAX, (long long)&std::memcpy);
    vm_call_absolute(jit, count, VM_REGISTER_EAX);

    // Epilogue

    vm_add_imm_to_reg_x64(jit, count, VM_REGISTER_ESP, stacksize);
    vm_pop_reg(jit, count, VM_REGISTER_EBX);
    vm_pop_reg(jit, count, VM_REGISTER_EDX);
    vm_pop_reg(jit, count, VM_REGISTER_EDI);
    vm_return(jit, count);

    // Finalize

    manager->_co._vm_suspend = vm_allocate(count);
    vm_initialize(manager->_co._vm_suspend, jit, count);

    manager->_co._vm_resume = (unsigned char*)manager->_co._vm_suspend + resume;
}

void vm_jit_yielded(JIT_Manager* manager)
{
    unsigned char jit[1024];
    int count = 0;

    // Store volatile registers

    vm_push_reg(jit, count, VM_REGISTER_EDI);
    vm_push_reg(jit, count, VM_REGISTER_EDX);
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
    vm_pop_reg(jit, count, VM_REGISTER_EDX);
    vm_pop_reg(jit, count, VM_REGISTER_EDI);
    vm_return(jit, count);

    // Finalize

    manager->_co._vm_yielded = vm_allocate(count);
    vm_initialize(manager->_co._vm_yielded, jit, count);

    manager->_co._yield_resume = (unsigned char*)manager->_co._vm_yielded + resumePosition;
}

void vm_jit_entry_stub(JIT_Manager* manager)
{
    unsigned char jit[1024];
    int count = 0;

    // Store volatile registers

    vm_push_reg(jit, count, VM_REGISTER_EDI);
    vm_push_reg(jit, count, VM_REGISTER_EDX);
    vm_push_reg(jit, count, VM_REGISTER_EBX);

    const int stacksize = VM_ALIGN_16(32 /* 4 register homes for callees */);

    vm_sub_imm_to_reg_x64(jit, count, VM_REGISTER_ESP, stacksize); // grow stack

    // Record the stack pointer

    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EDX, (long long)&manager->_co._stackPtr);
    vm_mov_reg_to_memory_x64(jit, count, VM_REGISTER_EDX, 0, VM_REGISTER_ESP);

    // Call the start method
    
    vm_call_absolute(jit, count, VM_REGISTER_ECX);

    // Return value - VM_OK

    vm_mov_imm_to_reg_x64(jit, count, VM_REGISTER_EAX, VM_OK);

    // Epilogue

    vm_add_imm_to_reg_x64(jit, count, VM_REGISTER_ESP, stacksize);
    vm_pop_reg(jit, count, VM_REGISTER_EBX);
    vm_pop_reg(jit, count, VM_REGISTER_EDX);
    vm_pop_reg(jit, count, VM_REGISTER_EDI);
    vm_return(jit, count);

    // Finalize

    manager->_co._vm_stub = vm_allocate(count);
    vm_initialize(manager->_co._vm_stub, jit, count);
}

static bool CanJITFunction(VirtualMachine* vm, unsigned int& pc, unsigned char* program)
{
    pc += 5;
    char* str = reinterpret_cast<char*>(&program[pc]);
    pc += unsigned int(strlen(str)) + 1;

    FunctionInfo* func;
    const int status = FindFunction(vm, str, &func);

    if (status == VM_OK)
    {
        return func->counter >= 1;
    }
    return false;
}

static bool CanJIT(VirtualMachine* vm, unsigned char* program, FunctionInfo* info)
{
    bool ok = info->counter > 0;
    
    if (ok)
    {
        unsigned int pc = info->pc;
        const unsigned int end = info->pc + info->size;
        for (unsigned int i = pc; i < end; i++)
        {
            switch (program[pc])
            {
            case OP_CALL:
                if (!CanJITFunction(vm, pc, program))
                {
                    return false;
                }
                break;
            default:
                ConsumeInstruction(program, pc);
                break;
            }
        }
    }

    return ok;
}

//========================

void SunScript::JIT_Setup(Jit* jit)
{
    jit->jit_initialize = SunScript::JIT_Initialize;
    jit->jit_compile = SunScript::JIT_Compile;
    jit->jit_execute = SunScript::JIT_Execute;
    jit->jit_resume = SunScript::JIT_Resume;
    jit->jit_search_cache = SunScript::JIT_SearchCache;
    jit->jit_cache = SunScript::JIT_CacheData;
    jit->jit_stats = SunScript::JIT_Stats;
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

void* SunScript::JIT_Compile(void* instance, VirtualMachine* vm, unsigned char* program, FunctionInfo* info, const std::string& signature)
{
    // Return nullptr is there is no type data
    // e.g. deoptimize
    if (!CanJIT(vm, program, info))
    {
        return nullptr;
    }

    unsigned char jit[1024];
    unsigned int pc = info->pc;

    std::unique_ptr<Jitter> jitter = std::make_unique<Jitter>();
    jitter->program = program;
    jitter->pc = &pc;
    jitter->jit = jit;
    jitter->info = info;
    jitter->_manager = reinterpret_cast<JIT_Manager*>(instance);
    jitter->fg.Init(program, pc, info->size);

    jitter->_method = new JIT_Method();
    jitter->_method->_name = info->name;
    jitter->_method->_signature = signature;
    jitter->_method->_runCount = 0;
    jitter->_method->_cacheKey = info->name + "_" + signature;
    jitter->_method->_jumpPos = 0;

    std::chrono::steady_clock clock;

    //==============================
    // Run the JIT compilation
    //==============================
    jitter->_method->_startTime = clock.now().time_since_epoch().count();

    vm_jit_generate(vm, jitter.get());
    jitter->_method->_jit_data = vm_allocate(jitter->count);
    vm_initialize(jitter->_method->_jit_data, jitter->jit, jitter->count);

    for (auto& stub : jitter->_method->_stubs)
    {
        stub._patch._data = jitter->_method->_jit_data;
        stub._patch._size = jitter->count;
    }
    
    jitter->_method->_size = jitter->count;

    //===============================
    jitter->_method->_endTime = clock.now().time_since_epoch().count();

    return jitter->_method;
}

int SunScript::JIT_Execute(void* instance, void* data)
{
    JIT_Manager* mm = reinterpret_cast<JIT_Manager*>(instance);
    JIT_Method* method = reinterpret_cast<JIT_Method*>(data);

    //vm_execute(method->_jit_data);
    int(*fn)(void*) = (int(*)(void*))((unsigned char*)mm->_co._vm_stub);
    return fn(method->_jit_data);
}

int SunScript::JIT_Resume(void* instance)
{
    JIT_Manager* mm = reinterpret_cast<JIT_Manager*>(instance);
    //mm->_mm.Dump();

    int(*fn)(void*) = (int(*)(void*))((unsigned char*)mm->_co._vm_stub);
    return fn(mm->_co._vm_resume);
}

void* SunScript::JIT_SearchCache(void* instance, const std::string& key)
{
    JIT_Manager* mm = reinterpret_cast<JIT_Manager*>(instance);
    return mm->_cache.SearchJITCache(key);
}

int SunScript::JIT_CacheData(void* instance, const std::string& key, void* data)
{
    JIT_Manager* mm = reinterpret_cast<JIT_Manager*>(instance);
    return mm->_cache.CacheJIT(key, reinterpret_cast<JIT_Method*>(data));
}

void SunScript::JIT_Free(void* data)
{
    JIT_Method* method = reinterpret_cast<JIT_Method*>(data);

    vm_free(method->_jit_data, method->_size);
}

static std::string JIT_Method_Stats(JIT_Method* method)
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
    ss << "Name: " << method->_name << std::endl;
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
}

void SunScript::JIT_Shutdown(void* instance)
{
    JIT_Manager* mm = reinterpret_cast<JIT_Manager*>(instance);
    delete mm;
}

//===========================
