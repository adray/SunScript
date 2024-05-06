#pragma once
#include <string>
#include <chrono>

namespace SunScript
{
    struct VirtualMachine;
    struct Program;

    constexpr int VM_OK = 0;
    constexpr int VM_ERROR = 1;
    constexpr int VM_YIELDED = 2;
    constexpr int VM_PAUSED = 3;
    constexpr int VM_TIMEOUT = 4;

    constexpr int ERR_NONE = 0;
    constexpr int ERR_INTERNAL = 1;

    VirtualMachine* CreateVirtualMachine();

    void ShutdownVirtualMachine(VirtualMachine* vm);

    void SetHandler(VirtualMachine* vm, void handler(VirtualMachine* vm));

    void* GetUserData(VirtualMachine* vm);

    void SetUserData(VirtualMachine* vm, void* userData);

    int LoadScript(const std::string& filepath, unsigned char** program);

    int RunScript(VirtualMachine* vm, unsigned char* program);

    int RunScript(VirtualMachine* vm, unsigned char* program, std::chrono::duration<int, std::nano> timeout);

    int ResumeScript(VirtualMachine* vm, unsigned char* program);

    void PushReturnValue(VirtualMachine* vm, const std::string& value);
    
    void PushReturnValue(VirtualMachine* vm, int value);

    int GetCallName(VirtualMachine* vm, std::string* name);

    int GetParamInt(VirtualMachine* vm, int* param);

    int GetParamString(VirtualMachine* vm, std::string* param);

    Program* CreateProgram();

    void ResetProgram(Program* program);

    int GetProgram(Program* program, unsigned char** programData);

    void ReleaseProgram(Program* program);

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

    void EmitIf(Program* program);

    void EmitEndIf(Program* program);
    
    void EmitFormat(Program* program);

    void EmitDone(Program* program);
}
