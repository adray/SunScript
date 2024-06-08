#pragma once
#include <string>
#include <chrono>

namespace SunScript
{
    struct VirtualMachine;
    struct Program;

    struct Callstack
    {
        std::string functionName;
        int numArgs = 0;
        int debugLine = 0;
        int programCounter = 0;

        Callstack* next = nullptr;
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

    VirtualMachine* CreateVirtualMachine();

    void ShutdownVirtualMachine(VirtualMachine* vm);

    void SetHandler(VirtualMachine* vm, void handler(VirtualMachine* vm));

    void* GetUserData(VirtualMachine* vm);

    void SetUserData(VirtualMachine* vm, void* userData);

    int LoadScript(const std::string& filepath, unsigned char** program);

    int RunScript(VirtualMachine* vm, unsigned char* program);

    int RunScript(VirtualMachine* vm, unsigned char* program, unsigned char* debugData);

    int RunScript(VirtualMachine* vm, unsigned char* program, std::chrono::duration<int, std::nano> timeout);

    int RunScript(VirtualMachine* vm, unsigned char* program, unsigned char* debugData, std::chrono::duration<int, std::nano> timeout);

    int ResumeScript(VirtualMachine* vm, unsigned char* program);

    void PushReturnValue(VirtualMachine* vm, const std::string& value);
    
    void PushReturnValue(VirtualMachine* vm, int value);

    int GetCallName(VirtualMachine* vm, std::string* name);

    int GetParamInt(VirtualMachine* vm, int* param);

    int GetParamString(VirtualMachine* vm, std::string* param);

    Program* CreateProgram();

    void ResetProgram(Program* program);

    int GetProgram(Program* program, unsigned char** programData);

    int GetDebugData(Program* program, unsigned char** debug);

    void ReleaseProgram(Program* program);

    void Disassemble(std::stringstream& ss, unsigned char* programData, unsigned char* debugData);

    void EmitReturn(Program* program);

    void EmitBeginFunction(Program* program, const std::string& name, int numArgs);

    void EmitEndFunction(Program* program);

    void EmitLocal(Program* program, const std::string& name);
    
    void EmitSet(Program* program, const std::string& name, int value);

    void EmitSet(Program* program, const std::string& name, const std::string& value);
    
    void EmitPushLocal(Program* program, const std::string& localName);
    
    void EmitPush(Program* program, int value);

    void EmitPush(Program* program, const std::string& value);

    void EmitPop(Program* program, const std::string& localName);

    void EmitPop(Program* program);

    void EmitYield(Program* program, const std::string& name);

    void EmitCall(Program* program, const std::string& name);

    void EmitAdd(Program* program);

    void EmitSub(Program* program);

    void EmitDiv(Program* program);

    void EmitMul(Program* program);

    void EmitEquals(Program* program);
    
    void EmitNotEquals(Program* program);
    
    void EmitGreaterThan(Program* program);
    
    void EmitLessThan(Program* program);

    void EmitAnd(Program* program);

    void EmitOr(Program* program);

    void EmitIf(Program* program);

    void EmitElse(Program* program);

    void EmitElseIf(Program* program);

    void EmitEndIf(Program* program);
    
    void EmitLoop(Program* program);

    void EmitEndLoop(Program* program);

    void EmitFormat(Program* program);

    void EmitDone(Program* program);

    void EmitDebug(Program* program, int line);
}
