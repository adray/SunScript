#pragma once
#include <string>

//
// SunScript Just-In-Time compilation.

namespace SunScript
{
    struct VirtualMachine;
    struct Jit;

    void JIT_Setup(Jit* jit);
    void* JIT_Initialize();
    void JIT_DumpTrace(unsigned char* trace, unsigned int size);
    void* JIT_CompileTrace(void* instance, VirtualMachine* vm, unsigned char* trace, int size);
    int JIT_ExecuteTrace(void* instance, void* data);
    int JIT_Resume(void* instance);
    void JIT_Free(void* data);
    void JIT_Shutdown(void* instance);
}
