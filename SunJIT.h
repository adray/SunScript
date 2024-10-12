#pragma once
#include <string>

//
// SunScript Just-In-Time compilation.

namespace SunScript
{
    struct VirtualMachine;
    struct Jit;

    constexpr int SUN_CAPS_NONE = 0x0;
    constexpr int SUN_CAPS_SSE3 = 0x1;
    constexpr int SUN_CAPS_SSE4_1 = 0x2;
    constexpr int SUN_CAPS_SSE4_2 = 0x4;

    int JIT_Capabilities(char vendor[13]);
    void JIT_Setup(Jit* jit);
    void* JIT_Initialize();
    void JIT_DumpTrace(unsigned char* trace, unsigned int size);
    void JIT_DisassembleTrace(void* data);
    void* JIT_CompileTrace(void* instance, VirtualMachine* vm, unsigned char* trace, int size, int traceId);
    int JIT_ExecuteTrace(void* instance, void* data, unsigned char* record);
    int JIT_Resume(void* instance);
    void JIT_Free(void* data);
    void JIT_Shutdown(void* instance);
}
