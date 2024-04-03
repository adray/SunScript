#include "SunScriptDemo.h"
#include "SunScript.h"
#include <iostream>

using namespace SunScript;

void Handler(VirtualMachine* vm)
{
    std::string name;
    GetCallName(vm, &name);

    if (name == "Print")
    {
        std::string param;
        int intParam;
        if (VM_OK == GetParamString(vm, &param))
        {
            std::cout << param << std::endl;
        }
        else if (VM_OK == GetParamInt(vm, &intParam))
        {
            std::cout << intParam << std::endl;
        }
    }
}

void SunScript::Demo(int _42)
{
    auto _program = CreateProgram();

    SunScript::EmitPush(_program, "Hello, from sunbeam.");
    SunScript::EmitCall(_program, "Print");
    SunScript::EmitPush(_program, 42);
    SunScript::EmitPush(_program, _42);
    SunScript::EmitEquals(_program);
    SunScript::EmitIf(_program);
    SunScript::EmitPush(_program, "10 times 10 is:");
    SunScript::EmitCall(_program, "Print");
    SunScript::EmitPush(_program, 10);
    SunScript::EmitPush(_program, 10);
    SunScript::EmitMul(_program);
    SunScript::EmitCall(_program, "Print");
    SunScript::EmitEndIf(_program);
    SunScript::EmitPush(_program, "Bye, from sunbeam.");
    SunScript::EmitCall(_program, "Print");
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
