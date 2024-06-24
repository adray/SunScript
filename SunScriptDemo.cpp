#include "SunScriptDemo.h"
#include "SunScript.h"
#include "Sun.h"
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

    SunScript::EmitPush(_program, "Hello, from sunbeam.");
    SunScript::EmitCall(_program, "Print", 1);
    SunScript::EmitPush(_program, 42);
    SunScript::EmitPush(_program, _42);
    SunScript::EmitEquals(_program);
    SunScript::EmitIf(_program);
    SunScript::EmitPush(_program, "10 times 10 is:");
    SunScript::EmitCall(_program, "Print", 1);
    SunScript::EmitPush(_program, 10);
    SunScript::EmitPush(_program, 10);
    SunScript::EmitMul(_program);
    SunScript::EmitCall(_program, "Print", 1);
    SunScript::EmitEndIf(_program);
    SunScript::EmitPush(_program, "Bye, from sunbeam.");
    SunScript::EmitCall(_program, "Print", 1);
    SunScript::EmitDone(_program);

    unsigned char* programData;
    GetProgram(_program, &programData);

    VirtualMachine* vm = CreateVirtualMachine();
    SetHandler(vm, Handler);
    RunScript(vm, programData);
    
    delete[] programData;
    ShutdownVirtualMachine(vm);
    ReleaseProgram(_program);
}

void SunScript::Demo2()
{
    auto _program = CreateProgram();

    SunScript::EmitPush(_program, 11);
    SunScript::EmitPush(_program, 11);
    SunScript::EmitEquals(_program);
    SunScript::EmitPush(_program, "Hello");
    SunScript::EmitPush(_program, "Hello");
    SunScript::EmitEquals(_program);
    SunScript::EmitAnd(_program);
    SunScript::EmitIf(_program);
    SunScript::EmitPush(_program, "11 == 11 && \"Hello\" == \"Hello\"");
    SunScript::EmitCall(_program, "Print", 1);
    SunScript::EmitDone(_program);

    unsigned char* programData;
    GetProgram(_program, &programData);

    VirtualMachine* vm = CreateVirtualMachine();
    SetHandler(vm, Handler);
    RunScript(vm, programData);

    delete[] programData;
    ShutdownVirtualMachine(vm);
    ReleaseProgram(_program);
}

void SunScript::Demo3()
{
    auto _program = CreateProgram();

    SunScript::EmitLocal(_program, "x");
    SunScript::EmitSet(_program, "x", 0);
    SunScript::EmitLoop(_program);
    SunScript::EmitPush(_program, 10);
    SunScript::EmitPushLocal(_program, "x");
    SunScript::EmitLessThan(_program);
    SunScript::EmitIf(_program);
    SunScript::EmitPushLocal(_program, "x");
    SunScript::EmitPush(_program, 1);
    SunScript::EmitAdd(_program);
    SunScript::EmitPop(_program, "x");
    SunScript::EmitPushLocal(_program, "x");
    SunScript::EmitCall(_program, "Print", 1);
    SunScript::EmitEndLoop(_program);
    SunScript::EmitDone(_program);

    unsigned char* programData;
    GetProgram(_program, &programData);

    VirtualMachine* vm = CreateVirtualMachine();
    SetHandler(vm, Handler);
    RunScript(vm, programData);

    delete[] programData;
    ShutdownVirtualMachine(vm);
    ReleaseProgram(_program);
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

static void RunDemoScript(const std::string& filename, const std::string& str)
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
            const int status = SunScript::RunScript(vm, programData, debugData);
            if (status == VM_ERROR)
            {
                std::cout << "Error running demo script." << std::endl;
                DumpCallstack(vm);
            }
            else if (status == VM_OK)
            {
                std::cout << "Script completed." << std::endl;
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
    RunDemoScript("Demo4.txt", stream.str());
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

    RunDemoScript("Demo5.txt", ss.str());
}
