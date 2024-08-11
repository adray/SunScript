#pragma once
#include <string>

//
// SunScript Just-In-Time compilation.

namespace SunScript
{
    struct VirtualMachine;
    struct FunctionInfo;
    struct Jit;

    void JIT_Setup(Jit* jit);
    void* JIT_Initialize();
    //void* JIT_Compile(void* instance, VirtualMachine* vm, unsigned char* program, FunctionInfo* info, const std::string& signature);
    int JIT_ExecuteTrace(void* instance, void* data);
    int JIT_Resume(void* instance);
    //void* JIT_SearchCache(void* instance, const std::string& key);
    //int JIT_CacheData(void* instance, const std::string& key, void* data);
    void JIT_Free(void* data);
    //std::string JIT_Stats(void* data);
    void JIT_Shutdown(void* instance);

    void* JIT_CompileTrace(void* instance, VirtualMachine* vm, unsigned char* trace, int size);
}
