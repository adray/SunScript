#include "SunScriptDemo.h"
#include "SunScript.h"
#include "Sun.h"
#include "SunJIT.h"
#include <iostream>
#include <fstream>

using namespace SunScript;

int Handler(VirtualMachine* vm)
{
    std::string name;
    GetCallName(vm, &name);

    int numArgs;
    GetCallNumArgs(vm, &numArgs);

    if (name == "Print")
    {
        std::string param;
        int intParam;
        if (VM_OK == GetParamString(vm, &param))
        {
            std::cout << param << std::endl;
            return VM_OK;
        }
        else if (VM_OK == GetParamInt(vm, &intParam))
        {
            std::cout << intParam << std::endl;
            return VM_OK;
        }
    }

    return VM_ERROR;
}

void SunScript::Demo1(int _42)
{
    auto _program = CreateProgram();
    int main = CreateFunction(_program);
    int print = CreateFunction(_program);
    auto _block = CreateProgramBlock(true, "main", 0);
    SunScript::Label label;

    SunScript::EmitPush(_block, "Hello, from sunbeam.");
    SunScript::EmitCall(_block, print, 1);
    SunScript::EmitPush(_block, 42);
    SunScript::EmitPush(_block, _42);
    SunScript::EmitCompare(_block);
    SunScript::EmitJump(_block, JUMP_NE, &label);
    SunScript::EmitPush(_block, "10 times 10 is:");
    SunScript::EmitCall(_block, print, 1);
    SunScript::EmitPush(_block, 10);
    SunScript::EmitPush(_block, 10);
    SunScript::EmitMul(_block);
    SunScript::EmitCall(_block, print, 1);
    SunScript::EmitLabel(_block, &label);
    SunScript::EmitPush(_block, "Bye, from sunbeam.");
    SunScript::EmitCall(_block, print, 1);
    SunScript::EmitDone(_block);
    SunScript::EmitProgramBlock(_program, _block);

    SunScript::EmitInternalFunction(_program, _block, main);
    SunScript::EmitExternalFunction(_program, print, "Print");
    SunScript::FlushBlocks(_program);

    unsigned char* programData;
    GetProgram(_program, &programData);

    VirtualMachine* vm = CreateVirtualMachine();
    SetHandler(vm, Handler);
    LoadProgram(vm, programData);
    RunScript(vm);
    
    delete[] programData;
    ShutdownVirtualMachine(vm);
    ReleaseProgram(_program);
    ReleaseProgramBlock(_block);
}

void SunScript::Demo2()
{
    auto _program = CreateProgram();
    int main = CreateFunction(_program);
    int print = CreateFunction(_program);
    auto _block = CreateProgramBlock(true, "main", 0);

    Label label;

    SunScript::EmitPush(_block, 11);
    SunScript::EmitPush(_block, 11);
    SunScript::EmitCompare(_block);
    SunScript::EmitJump(_block, JUMP_NE, &label);
    SunScript::EmitPush(_block, "Hello");
    SunScript::EmitPush(_block, "Hello");
    SunScript::EmitCompare(_block);
    SunScript::EmitJump(_block, JUMP_NE, &label);
    SunScript::EmitPush(_block, "11 == 11 && \"Hello\" == \"Hello\"");
    SunScript::EmitCall(_block, print, 1);
    SunScript::EmitLabel(_block, &label);
    SunScript::EmitDone(_block);
    SunScript::EmitProgramBlock(_program, _block);

    SunScript::EmitInternalFunction(_program, _block, main);
    SunScript::EmitExternalFunction(_program, print, "Print");
    SunScript::FlushBlocks(_program);

    unsigned char* programData;
    GetProgram(_program, &programData);

    VirtualMachine* vm = CreateVirtualMachine();
    SetHandler(vm, Handler);
    LoadProgram(vm, programData);
    RunScript(vm);

    delete[] programData;
    ShutdownVirtualMachine(vm);
    ReleaseProgram(_program);
    ReleaseProgramBlock(_block);
}

void SunScript::Demo3()
{
    auto _program = CreateProgram();
    int main = CreateFunction(_program);
    int print = CreateFunction(_program);
    auto _block = CreateProgramBlock(true, "main", 0);

    Label loopStart;
    Label loopEnd;

    const int x = 0;

    SunScript::EmitLocal(_block, "x");
    SunScript::EmitSet(_block, x, 0);
    SunScript::MarkLabel(_block, &loopStart);
    SunScript::EmitPush(_block, 10);
    SunScript::EmitPushLocal(_block, x);
    SunScript::EmitCompare(_block);
    SunScript::EmitJump(_block, JUMP_GE, &loopEnd);
    SunScript::EmitPushLocal(_block, x);
    SunScript::EmitPush(_block, 1);
    SunScript::EmitAdd(_block);
    SunScript::EmitPop(_block, x);
    SunScript::EmitPushLocal(_block, x);
    SunScript::EmitCall(_block, print, 1);
    SunScript::EmitJump(_block, JUMP, &loopStart);
    SunScript::EmitLabel(_block, &loopEnd);
    SunScript::EmitDone(_block);
    SunScript::EmitMarkedLabel(_block, &loopStart);
    SunScript::EmitProgramBlock(_program, _block);

    SunScript::EmitInternalFunction(_program, _block, main);
    SunScript::EmitExternalFunction(_program, print, "Print");
    SunScript::FlushBlocks(_program);

    unsigned char* programData;
    GetProgram(_program, &programData);

    VirtualMachine* vm = CreateVirtualMachine();
    SetHandler(vm, Handler);
    LoadProgram(vm, programData);
    RunScript(vm);

    delete[] programData;
    ShutdownVirtualMachine(vm);
    ReleaseProgram(_program);
    ReleaseProgramBlock(_block);
}

static void DumpCallstack(VirtualMachine* vm)
{
    SunScript::Callstack* callstack = GetCallStack(vm);

    while (callstack)
    {
        std::cout << callstack->functionName << "(" << callstack->numArgs << ") PC: " << callstack->programCounter << " Line: " << callstack->debugLine << std::endl;
        callstack = callstack->next;
    }

    DestroyCallstack(callstack);
}

static void RunDemoScript(const std::string& filename, const std::string& str, bool jit_enabled)
{
    std::ofstream stream(filename);
    if (stream.good())
    {
        stream << str;
        stream.close();

        std::cout << "Compiling demo script." << std::endl;

        unsigned char* programData;
        unsigned char* debugData;
        std::string error;
        SunScript::CompileFile(filename, &programData, &debugData, &error);

        if (programData && debugData)
        {
            std::cout << "Running demo script." << std::endl;

            auto vm = SunScript::CreateVirtualMachine();
            SunScript::SetHandler(vm, &Handler);

            if (jit_enabled)
            {
                Jit jit = {};
                SunScript::JIT_Setup(&jit);
                SunScript::SetJIT(vm, &jit);
            }

        int status = LoadProgram(vm, programData, debugData);
            if (status == VM_ERROR)
        {
            std::cout << "Error load demo script." << std::endl;
        }
            else if (status == VM_OK)
        {
            status = SunScript::RunScript(vm);
                if (status == VM_ERROR)
                {
                    std::cout << "Error running demo script." << std::endl;
                    DumpCallstack(vm);
                }
                else if (status == VM_OK)
                {
                    std::cout << "Script completed." << std::endl;
                }

                if (jit_enabled)
                {
                    std::cout << SunScript::JITStats(vm) << std::endl;
                }
        }

            ShutdownVirtualMachine(vm);
        }
        else
        {
            std::cout << "Unable to compile demo script: " << error << std::endl;
        }
    }
    else
    {
        std::cout << "Unable to write to file." << std::endl;
    }
}

void SunScript::Demo4()
{
    std::stringstream stream;
    stream << "var foo = -10;" << std::endl;
    stream << "Print(foo);" << std::endl;
    stream << "function Test1() {" << std::endl;
    stream << "    Print(\"Test\");" << std::endl;
    stream << "    return 5;" << std::endl;
    stream << "}" << std::endl;
    stream << "function Test2(x) {" << std::endl;
    stream << "    Print(\"Foo: \" + x);" << std::endl;
    stream << "}" << std::endl;
    stream << "function Test3(x) {" << std::endl;
    stream << "    return x;" << std::endl;
    stream << "}" << std::endl;
    stream << "1 + Test3(1);" << std::endl;
    stream << "var x = 2 + Test1();" << std::endl;
    stream << "Print(x);" << std::endl;
    stream << "Test2(Test1() + 5);" << std::endl;
    RunDemoScript("Demo4.txt", stream.str(), false);
}

void SunScript::Demo5()
{
    std::stringstream ss;
    ss << "var x = 5;" << std::endl;
    ss << "x--;" << std::endl;
    ss << "Print(x);" << std::endl;
    ss << "var y = (7 + x)++;" << std::endl;
    ss << "Print(y);" << std::endl;
    ss << "var z = 6;" << std::endl;
    ss << "z -= 2;" << std::endl;
    ss << "z += 7;" << std::endl;
    ss << "z *= 2;" << std::endl;
    ss << "Print(z);" << std::endl;
    ss << "if (z >= 5) { Print(\"Foo\"); }" << std::endl;

    RunDemoScript("Demo5.txt", ss.str(), false);
}

void SunScript::Demo6()
{
    std::stringstream ss;
    ss << "function test() {" << std::endl;
    ss << "    Print(\"Test\");" << std::endl;
    ss << "}" << std::endl;
    ss << "function add(x) {" << std::endl;
    ss << "    var y = 10;" << std::endl;
    ss << "    Print(x * y * 2);" << std::endl;
    ss << "    Print(\"Adding..\");" << std::endl;
    ss << "    Print(\"Adding2..\");" << std::endl;
    ss << "    test();" << std::endl;
    ss << "    test();" << std::endl;
    ss << "}" << std::endl;
    ss << "var j = \"Foo\";" << std::endl;
    ss << "add(4);" << std::endl;
    ss << "add(5);" << std::endl;
    ss << "if (6 == 5 && (10 == 10 || 12 == 12)) {" << std::endl;
    ss << "    Print(j + j);" << std::endl;
    ss << "} else if (5 == 5 || (10 == 10 && 12 == 12)) {" << std::endl;
    ss << "    Print(j + j + j);" << std::endl;
    ss << "} else {" << std::endl;
    ss << "    Print(j);" << std::endl;
    ss << "}" << std::endl;

    RunDemoScript("Demo6.txt", ss.str(), true);
}
