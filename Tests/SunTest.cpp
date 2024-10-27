#include "SunTest.h"
#include "../SunScript.h"
#include "../Sun.h"
#include "../SunJIT.h"
#include <string>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <chrono>

using namespace SunScript;

class SunTestSuite;
class SunTest;

static int RunTest(SunTestSuite* suite, SunTest* test);

class SunTest
{
public:
    SunTest(const std::string& filename, bool jit) :
        _failed(false),
        _filename(filename),
        _jit(jit)
    {
    }

    bool _jit;
    bool _failed;
    std::string _filename;
    std::string _failureMessage;
};

class SunTestSuite
{
public:
    SunTestSuite() : _numFailures(0), _dumpTrace(false) {}

    inline void EnableDumpTrace(bool enabled) { _dumpTrace = enabled; }
    
    void AddTest(const std::string& filename)
    {
        _tests.push_back(SunTest(filename, false));
        _tests.push_back(SunTest(filename, true));
    }

    void RunTests()
    {
        for (auto& test : _tests)
        {
            if (RunTest(this, &test) == VM_ERROR)
            {
                _numFailures++;
            }
        }
    }

    int NumFailures() const { return _numFailures; }
    bool DumpTrace() const { return _dumpTrace; }

private:
    std::vector<SunTest> _tests;
    int _numFailures;
    bool _dumpTrace;
};

static int Handler(VirtualMachine* vm)
{
    SunTest* test = reinterpret_cast<SunTest*>(GetUserData(vm));
    
    std::string callName;
    int numArgs;
    SunScript::GetCallName(vm, &callName);
    SunScript::GetCallNumArgs(vm, &numArgs);

    if (callName == "assertFalse")
    {
        test->_failed = true;
        test->_failureMessage = "Assert failure";
        return VM_ERROR;
    }
    else if (callName == "assert")
    {
        int intParam1;
        int intParam2;
        if (VM_OK == GetParamInt(vm, &intParam1) &&
            VM_OK == GetParamInt(vm, &intParam2))
        {
            if (intParam1 != intParam2)
            {
                std::stringstream ss;
                ss << "Assert failure: Expected " << intParam1 << " but was " << intParam2;

                test->_failed = true;
                test->_failureMessage = ss.str();
                return VM_ERROR;
            }
            return VM_OK;
        }
    }
    else if (callName == "Rnd")
    {
        int intParam1;
        if (VM_OK == GetParamInt(vm, &intParam1))
        {
            const int rnd = rand() % intParam1;
            SunScript::PushReturnValue(vm, rnd);
            return VM_OK;
        }
    }
    else if (callName == "DebugLog")
    {
        real param;
        std::string str;
        if (VM_OK == GetParamReal(vm, &param))
        {
            std::cout << param << std::endl;
            return VM_OK;
        }
        else if (VM_OK == GetParamString(vm, &str))
        {
            std::cout << str << std::endl;
            return VM_OK;
        }
    }
    
    return VM_ERROR;
}

static void DumpStack(Callstack* s)
{
    std::cout << s->functionName << " " << s->programCounter << " Line: " << s->debugLine << std::endl;
    if (s->next)
    {
        DumpStack(s->next);
    }
}

static void* CompileTrace(void* instance, VirtualMachine* vm, unsigned char* trace, int size, int traceId)
{
    const std::string line = "=======================";
    std::cout << std::endl << line << std::endl << "Trace " << traceId << std::endl;
    JIT_DumpTrace(trace, size);
    std::cout << line << std::endl;

    return JIT_CompileTrace(instance, vm, trace, size, traceId);
}

static int RunTest(SunTestSuite* suite, SunTest* test)
{
    std::cout << "Running test for " << test->_filename;

    VirtualMachine* vm = CreateVirtualMachine();
    SetHandler(vm, Handler);
    SetUserData(vm, test);

    if (test->_jit)
    {
        Jit jit;
        JIT_Setup(&jit);
        if (suite->DumpTrace())
        {
            jit.jit_compile_trace = CompileTrace;
        }
        SetJIT(vm, &jit);
    }

    unsigned char* program;
    unsigned char* debug;
    int programSize;
    int debugSize;
    std::string compile;
    CompileFile(test->_filename, &program, &debug, &programSize, &debugSize, &compile);

    std::chrono::steady_clock clock;
    auto startTime = clock.now().time_since_epoch();

    const int runCount = 10000;
    if (program)
    {
        LoadProgram(vm, program, debug, programSize);
        for (int i = 0; i < runCount; i++)
        {
            int errorCode = RunScript(vm);
            while (errorCode == VM_YIELDED)
            {
                errorCode = ResumeScript(vm);
            }

            if (errorCode == VM_ERROR)
            {
                test->_failureMessage = "RunScript returned VM_ERROR.";
                test->_failed = true;

                Callstack* s = GetCallStack(vm);
                DumpStack(s);
                DestroyCallstack(s);
                break;
            }
        }
    }
    else
    {
        test->_failed = true;
    }

    auto elapsedTime = clock.now().time_since_epoch() - startTime;
    elapsedTime /= runCount;

    delete[] program;
    ShutdownVirtualMachine(vm);

    if (test->_failed)
    {
        std::cout << " FAILED" << std::endl;
        std::cout << test->_failureMessage << std::endl;
        return VM_ERROR;
    }
    else
    {
        std::cout << " SUCCESS " << elapsedTime.count() << "ns";
        if (test->_jit) {
            std::cout << " [JIT]";
        }
        std::cout << std::endl;

        return VM_OK;
    }
}

static void PrintCaps()
{
    char vendor[13];
    const int flags = JIT_Capabilities(vendor);

    std::cout << vendor;
    if ((flags & SUN_CAPS_SSE3) == SUN_CAPS_SSE3)
    {
        std::cout << " SSE3";
    }
    if ((flags & SUN_CAPS_SSE4_1) == SUN_CAPS_SSE4_1)
    {
        std::cout << " SSE4.1";
    }
    if ((flags & SUN_CAPS_SSE4_2) == SUN_CAPS_SSE4_2)
    {
        std::cout << " SSE4.2";
    }
    std::cout << std::endl;
}

void SunScript::RunTestSuite(const std::string& path, int opts)
{
    PrintCaps();

    std::cout << "Running test suite" << std::endl;

    SunTestSuite* suite = new SunTestSuite();
    suite->EnableDumpTrace(opts & OPT_DUMPTRACE);
    if (std::filesystem::is_directory(path))
    {
        std::filesystem::directory_iterator it(path);
        for (auto& entry : it)
        {
            if (entry.is_regular_file() &&
                entry.path().has_extension() &&
                entry.path().extension() == ".txt")
            {
                suite->AddTest(entry.path().string());
            }
        }
    }
    else
    {
        suite->AddTest(path);
    }

    suite->RunTests();
    std::cout << "Failed: " << suite->NumFailures() << std::endl;
}
