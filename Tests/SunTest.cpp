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

private:
    std::vector<SunTest> _tests;
    int _numFailures;
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

    return VM_ERROR;
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
        SetJIT(vm, &jit);
    }

    unsigned char* program;
    CompileFile(test->_filename, &program);

    std::chrono::steady_clock clock;
    auto startTime = clock.now().time_since_epoch();

    const int runCount = 10000;
    if (program)
    {
        for (int i = 0; i < runCount; i++)
        {
            int errorCode = RunScript(vm, program);
            while (errorCode == VM_YIELDED)
            {
                errorCode = ResumeScript(vm, program);
            }

            if (errorCode == VM_ERROR)
            {
                test->_failureMessage = "RunScript returned VM_ERROR.";
                test->_failed = true;
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

void SunScript::RunTestSuite(const std::string& directory)
{
    std::cout << "Running test suite" << std::endl;

    SunTestSuite* suite = new SunTestSuite();
    std::filesystem::directory_iterator it(directory);
    for (auto& entry : it)
    {
        if (entry.is_regular_file() &&
            entry.path().has_extension() &&
            entry.path().extension() == ".txt")
        {
            suite->AddTest(entry.path().string());
        }
    }

    suite->RunTests();
    std::cout << "Failed: " << suite->NumFailures() << std::endl;
}
