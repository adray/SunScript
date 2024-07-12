#pragma once
#include <string>
#include <chrono>

namespace SunScript
{
    constexpr unsigned char OP_PUSH = 0x0;
    constexpr unsigned char OP_POP = 0x1;
    constexpr unsigned char OP_CALL = 0x2;
    constexpr unsigned char OP_YIELD = 0x3;
    constexpr unsigned char OP_LOCAL = 0x4;
    constexpr unsigned char OP_SET = 0x5;

    constexpr unsigned char OP_DONE = 0x8;
    constexpr unsigned char OP_PUSH_LOCAL = 0x9;

    constexpr unsigned char OP_UNARY_MINUS = 0xd;
    constexpr unsigned char OP_INCREMENT = 0xe;
    constexpr unsigned char OP_DECREMENT = 0xf;
    constexpr unsigned char OP_ADD = 0x10;
    constexpr unsigned char OP_SUB = 0x1a;
    constexpr unsigned char OP_MUL = 0x1b;
    constexpr unsigned char OP_DIV = 0x1c;

    constexpr unsigned char OP_FORMAT = 0x22;
    constexpr unsigned char OP_JUMP = 0x23;
    constexpr unsigned char OP_CMP = 0x24;

    constexpr unsigned char OP_RETURN = 0x25;
    constexpr unsigned char OP_POP_DISCARD = 0x26;

    constexpr unsigned char TY_VOID = 0x0;
    constexpr unsigned char TY_INT = 0x1;
    constexpr unsigned char TY_STRING = 0x2;

    constexpr unsigned char JUMP = 0x0;
    constexpr unsigned char JUMP_E = 0x1;
    constexpr unsigned char JUMP_NE = 0x2;
    constexpr unsigned char JUMP_GE = 0x3;
    constexpr unsigned char JUMP_LE = 0x4;
    constexpr unsigned char JUMP_L = 0x5;
    constexpr unsigned char JUMP_G = 0x6;

    struct VirtualMachine;
    struct Program;
    struct ProgramBlock;

    struct Label
    {
        int pos;
        std::vector<int> jumps;
    };

    struct Callstack
    {
        std::string functionName;
        int numArgs = 0;
        int debugLine = 0;
        int programCounter = 0;

        Callstack* next = nullptr;
    };

    struct FunctionInfo
    {
        unsigned int pc;
        unsigned int size;
        std::string name;
        std::vector<std::string> parameters;
        std::vector<std::string> locals;
        std::vector<int> labels;
    };

    struct Jit
    {
        void* (*jit_initialize) (void);
        void* (*jit_compile) (void* instance, VirtualMachine* vm, unsigned char* program, 
            const FunctionInfo& info, const std::string& signature);
        int (*jit_execute) (void* data);
        void* (*jit_search_cache) (void* instance, const std::string& key);
        int (*jit_cache) (void* instance, const std::string& key, void* data);
        std::string(*jit_stats) (void* data);
        void (*jit_shutdown) (void* instance);
    };

    constexpr int VM_OK = 0;
    constexpr int VM_ERROR = 1;
    constexpr int VM_YIELDED = 2;
    constexpr int VM_PAUSED = 3;
    constexpr int VM_TIMEOUT = 4;

    constexpr int ERR_NONE = 0;
    constexpr int ERR_INTERNAL = 1;

    Callstack* GetCallStack(VirtualMachine* vm);
    
    void DestroyCallstack(Callstack* callstack);

    /*
    * Creates an instance of a Virtual Machine to
    * execute scripts.
    */
    VirtualMachine* CreateVirtualMachine();

    /*
    * Shuts down an instance of a Virtual Machine.
    */
    void ShutdownVirtualMachine(VirtualMachine* vm);

    /*
    * Sets a handler function which will handle functions
    * defined by the host program.
    * 
    * The handler should return VM_OK in the case the method was handled correctly.
    * Otherwise it should return VM_ERROR.
    * 
    * The handler can use the following functions to retrieve state information for the function call:
    * - GetCallNumArgs
    * - GetCallName
    * - GetParamInt
    * - GetParamString
    */
    void SetHandler(VirtualMachine* vm, int handler(VirtualMachine* vm));

    /*
    * This function sets the handlers for JIT compilation. 
    */
    void SetJIT(VirtualMachine* vm, Jit* jit);

    /*
    * Retrieve data collected by the JIT compilier.
    */
    std::string JITStats(VirtualMachine* vm);

    /*
    * Gets user data associated with the Virtual Machine.
    */
    void* GetUserData(VirtualMachine* vm);

    /*
    * Sets the user data associated with the Virtual Machine.
    */
    void SetUserData(VirtualMachine* vm, void* userData);

    /*
    * Loads a script for the given filepath.
    * The program data should be freed by the caller.
    */
    int LoadScript(const std::string& filepath, unsigned char** program);

    int RunScript(VirtualMachine* vm, unsigned char* program);

    int RunScript(VirtualMachine* vm, unsigned char* program, unsigned char* debugData);

    int RunScript(VirtualMachine* vm, unsigned char* program, std::chrono::duration<int, std::nano> timeout);

    int RunScript(VirtualMachine* vm, unsigned char* program, unsigned char* debugData, std::chrono::duration<int, std::nano> timeout);

    int ResumeScript(VirtualMachine* vm, unsigned char* program);

    void PushReturnValue(VirtualMachine* vm, const std::string& value);
    
    void PushReturnValue(VirtualMachine* vm, int value);

    int GetCallNumArgs(VirtualMachine* vm, int* numArgs);

    int GetCallName(VirtualMachine* vm, std::string* name);

    int GetParamInt(VirtualMachine* vm, int* param);

    int GetParamString(VirtualMachine* vm, std::string* param);

    int PushParamString(VirtualMachine* vm, const std::string& param);

    int PushParamInt(VirtualMachine* vm, int param);

    void InvokeHandler(VirtualMachine* vm, const std::string& callName, int numParams);

    int FindFunction(VirtualMachine* vm, const std::string& callName, FunctionInfo& info);

    unsigned char* GetLoadedProgram(VirtualMachine* vm);

    Program* CreateProgram();

    ProgramBlock* CreateProgramBlock(bool topLevel, const std::string& name, int numArgs);

    void ReleaseProgramBlock(ProgramBlock* block);
    
    void ResetProgram(Program* program);

    int GetProgram(Program* program, unsigned char** programData);

    int GetDebugData(Program* program, unsigned char** debug);

    void ReleaseProgram(Program* program);

    void Disassemble(std::stringstream& ss, unsigned char* programData, unsigned char* debugData);

    void EmitProgramBlock(Program* program, ProgramBlock* block);

    void EmitReturn(ProgramBlock* program);

    void EmitParameter(ProgramBlock* program, const std::string& name);

    void EmitLocal(ProgramBlock* program, const std::string& name);
    
    void EmitSet(ProgramBlock* program, const std::string& name, int value);

    void EmitSet(ProgramBlock* program, const std::string& name, const std::string& value);
    
    void EmitPushLocal(ProgramBlock* program, const std::string& localName);
    
    void EmitPush(ProgramBlock* program, int value);

    void EmitPush(ProgramBlock* program, const std::string& value);

    void EmitPop(ProgramBlock* program, const std::string& localName);

    void EmitPop(ProgramBlock* program);

    void EmitYield(ProgramBlock* program, const std::string& name, unsigned char numArgs);

    void EmitCall(ProgramBlock* program, const std::string& name, unsigned char numArgs);

    void EmitAdd(ProgramBlock* program);

    void EmitSub(ProgramBlock* program);

    void EmitDiv(ProgramBlock* program);

    void EmitMul(ProgramBlock* program);

    void EmitFormat(ProgramBlock* program);

    void EmitUnaryMinus(ProgramBlock* program);

    void EmitIncrement(ProgramBlock* program);

    void EmitDecrement(ProgramBlock* program);

    void MarkLabel(ProgramBlock* program, Label* label);

    void EmitMarkedLabel(ProgramBlock* program, Label* label);

    void EmitLabel(ProgramBlock* program, Label* label);

    void EmitCompare(ProgramBlock* program);

    void EmitJump(ProgramBlock* program, char type, Label* label);

    void EmitDone(ProgramBlock* program);

    void EmitDebug(ProgramBlock* program, int line);
}
