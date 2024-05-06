#include "SunScript.h"
#include <fstream>
#include <stack>
#include <vector>
#include <unordered_map>
#include <sstream>

using namespace SunScript;

constexpr unsigned char OP_PUSH = 0x0;
constexpr unsigned char OP_POP = 0x1;
constexpr unsigned char OP_CALL = 0x2;
constexpr unsigned char OP_YIELD = 0x3;
constexpr unsigned char OP_LOCAL = 0x4;
constexpr unsigned char OP_SET = 0x5;
constexpr unsigned char OP_IF = 0x6;
constexpr unsigned char OP_LOOP = 0x7;
constexpr unsigned char OP_DONE = 0x8;
constexpr unsigned char OP_PUSH_LOCAL = 0x9;
constexpr unsigned char OP_ADD = 0x10;
constexpr unsigned char OP_SUB = 0x1a;
constexpr unsigned char OP_MUL = 0x1b;
constexpr unsigned char OP_DIV = 0x1c;
constexpr unsigned char OP_EQUALS = 0x1d;
constexpr unsigned char OP_NOT_EQUALS = 0x1e;
constexpr unsigned char OP_GREATER_THAN = 0x1f;
constexpr unsigned char OP_LESS_THAN = 0x20;
constexpr unsigned char OP_END_IF = 0x21;
constexpr unsigned char OP_FORMAT = 0x22;
constexpr unsigned char OP_BEGIN_FUNCTION = 0x23;
constexpr unsigned char OP_END_FUNCTION = 0x24;
constexpr unsigned char OP_RETURN = 0x25;
constexpr unsigned char OP_POP_DISCARD = 0x26;

constexpr unsigned char TY_VOID = 0x0;
constexpr unsigned char TY_INT = 0x1;
constexpr unsigned char TY_STRING = 0x2;

namespace SunScript
{
    struct Value
    {
        unsigned char type;
        unsigned int index;
    };

    struct StackFrame
    {
        int returnAddress;
        std::unordered_map<std::string, Value> locals;
        std::vector<std::string> strings;
        std::vector<int> integers;
    };

    struct Function
    {
        int offset;
        int numArgs;
    };

    struct VirtualMachine
    {
        unsigned int programCounter;
        unsigned int disabledCounter;
        unsigned int programOffset;
        bool running;
        int statusCode;
        int errorCode;
        int resumeCode;
        std::int64_t timeout;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock clock;
        int instructionsExecuted;
        std::string callName;
        std::stack<StackFrame> frames;
        std::stack<Value> stack;
        std::unordered_map<std::string, Function> functions;
        std::unordered_map<std::string, Value> locals;
        std::vector<std::string> strings;
        std::vector<int> integers;
        void (*handler)(VirtualMachine* vm);
        void* _userData;
    };

    struct Program
    {
        std::vector<unsigned char> data;
        std::vector<unsigned char> functions;
        int numFunctions;
    };
}

VirtualMachine* SunScript::CreateVirtualMachine()
{
    VirtualMachine* vm = new VirtualMachine();
    vm->handler = nullptr;
    vm->_userData = nullptr;
    return vm;
}

void SunScript::ShutdownVirtualMachine(VirtualMachine* vm)
{
    delete vm;
}

void SunScript::SetHandler(VirtualMachine* vm, void handler(VirtualMachine* vm))
{
    vm->handler = handler;
}

void* SunScript::GetUserData(VirtualMachine* vm)
{
    return vm->_userData;
}

void SunScript::SetUserData(VirtualMachine* vm, void* userData)
{
    vm->_userData = userData;
}

int SunScript::LoadScript(const std::string& filepath, unsigned char** program)
{
    std::ifstream stream(filepath, std::iostream::binary);
    if (stream.good())
    {
        int version;
        stream >> version;
        if (version == 0)
        {
            int size;
            stream >> size;
            *program = new unsigned char[size];
            stream.read((char*)*program, size);
        }
        else
        {
            return 1;
        }
    }

    return 0;
}

static int Read_Int(unsigned char* program, unsigned int* pc)
{
    int a = program[*pc];
    int b = program[(*pc) + 1];
    int c = program[(*pc) + 2];
    int d = program[(*pc) + 3];

    *pc += 4;
    return a | (b << 8) | (c << 16) | (d << 24);
}

static std::string Read_String(unsigned char* program, unsigned int* pc)
{
    std::string str;
    int index = 0;
    char ch = (char)program[*pc];
    (*pc)++;
    while (ch != 0)
    {
        str = str.append(1, ch);
        ch = (char)program[*pc];
        (*pc)++;
    }
    return str;
}

static void Push_Int(VirtualMachine* vm, int val)
{
    if (vm->statusCode == VM_OK)
    {
        Value v = {};
        v.index = (unsigned int)vm->integers.size();
        v.type = TY_INT;
        vm->stack.push(v);
        vm->integers.push_back(val);
    }
}

static void Push_String(VirtualMachine* vm, const std::string& val)
{
    if (vm->statusCode == VM_OK)
    {
        Value v = {};
        v.index = (unsigned int)vm->strings.size();
        v.type = TY_STRING;
        vm->stack.push(v);
        vm->strings.push_back(val);
    }
}

static bool Compare_Int(unsigned char op, VirtualMachine* vm, Value& var1, Value& var2)
{
    bool result = false;
    switch (op)
    {
    case OP_EQUALS:
        result = vm->integers[var1.index] == vm->integers[var2.index];
        break;
    case OP_NOT_EQUALS:
        result = vm->integers[var1.index] != vm->integers[var2.index];
        break;
    case OP_GREATER_THAN:
        result = vm->integers[var1.index] > vm->integers[var2.index];
        break;
    case OP_LESS_THAN:
        result = vm->integers[var1.index] < vm->integers[var2.index];
        break;
    }

    return result;
}

static bool Compare_String(unsigned char op, VirtualMachine* vm, Value& var1, Value& var2)
{
    bool result = false;
    switch (op)
    {
    case OP_EQUALS:
        result = vm->strings[var1.index] == vm->strings[var2.index];
        break;
    case OP_NOT_EQUALS:
        result = vm->strings[var1.index] != vm->strings[var2.index];
        break;
    case OP_GREATER_THAN:
        result = vm->strings[var1.index] > vm->strings[var2.index];
        break;
    case OP_LESS_THAN:
        result = vm->strings[var1.index] < vm->strings[var2.index];
        break;
    }

    return result;
}

static void Op_Compare(unsigned char op, VirtualMachine* vm, unsigned char* program)
{
    if (vm->statusCode != VM_OK)
    {
        return;
    }

    if (vm->stack.size() < 2)
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
        return;
    }

    Value var1 = vm->stack.top();
    vm->stack.pop();
    Value var2 = vm->stack.top();
    vm->stack.pop();

    // ..perhaps provide auto-conversion
    if (var1.type != var2.type)
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
        return;
    }

    bool result = false;
    switch (var1.type)
    {
    case TY_INT:
        result = Compare_Int(op, vm, var1, var2);
        break;
    case TY_STRING:
        result = Compare_String(op, vm, var1, var2);
        break;
    default:
        vm->running = false;
        vm->statusCode = VM_ERROR;
        break;
    }

    if (vm->statusCode == VM_OK)
    {
        Value v = {};
        v.index = (unsigned int)vm->integers.size();
        v.type = TY_INT;
        vm->stack.push(v);
        vm->integers.push_back(result ? 1 : 0);
    }
}

static void Op_Set(VirtualMachine* vm, unsigned char* program)
{
    unsigned char type = program[vm->programCounter++];
    const std::string name = Read_String(program, &vm->programCounter);
    auto& local = vm->locals[name];

    switch (type)
    {
    case TY_VOID:
        vm->running = false;
        vm->statusCode = VM_ERROR;
        break;
    case TY_INT:
    {
        local.index = (unsigned int)vm->integers.size();
        local.type = TY_INT;
        if (vm->statusCode == VM_OK)
        {
            vm->integers.push_back(Read_Int(program, &vm->programCounter));
        }
        else if (vm->statusCode == VM_PAUSED)
        {
            Read_Int(program, &vm->programCounter);
        }
    }
        break;
    case TY_STRING:
    {
        local.index = (unsigned int)vm->strings.size();
        local.type = TY_STRING;
        if (vm->statusCode == VM_OK)
        {
            vm->strings.push_back(Read_String(program, &vm->programCounter));
        }
        else if (vm->statusCode == VM_PAUSED)
        {
            Read_String(program, &vm->programCounter);
        }
    }
    break;
    default:
        vm->running = false;
        vm->statusCode = VM_ERROR;
        break;
    }
}

static void Op_Push_Local(VirtualMachine* vm, unsigned char* program)
{
    std::string name = Read_String(program, &vm->programCounter);

    if (vm->statusCode == VM_OK)
    {
        const auto& it = vm->locals.find(name);
        if (it != vm->locals.end())
        {
            auto& local = vm->locals[name];
            vm->stack.push(local);
        }
        else
        {
            vm->statusCode = VM_ERROR;
            vm->running = false;
        }
    }
}

static void Op_Push(VirtualMachine* vm, unsigned char* program)
{
    unsigned char type = program[vm->programCounter++];
    switch (type)
    {
    case TY_INT:
        Push_Int(vm, Read_Int(program, &vm->programCounter));
        break;
    case TY_STRING:
        Push_String(vm, Read_String(program, &vm->programCounter));
        break;
    }
}

static void Op_Return(VirtualMachine* vm, unsigned char* program)
{
    if (vm->statusCode == VM_OK)
    {
        if (vm->frames.size() == 0)
        {
            vm->statusCode = VM_ERROR;
            vm->running = false;
            return;
        }

        StackFrame frame = vm->frames.top();

        // Copy the return value into the stack frame.
        if (vm->stack.size() > 0)
        {
            Value retValue = vm->stack.top();
            vm->stack.pop();

            Value remapped = {};
            remapped.type = retValue.type;
            switch (retValue.type)
            {
            case TY_INT:
                frame.integers.push_back(vm->integers[retValue.index]);
                remapped.index = int(frame.integers.size()) - 1;
                break;
            case TY_STRING:
                frame.strings.push_back(vm->strings[retValue.index]);
                remapped.index = int(frame.strings.size()) - 1;
                break;
            }
            vm->stack.push(remapped);
        }

        vm->locals = frame.locals;
        vm->integers = frame.integers;
        vm->strings = frame.strings;
        vm->frames.pop();
        vm->programCounter = frame.returnAddress;
    }
}

static void CreateStackFrame(VirtualMachine* vm, StackFrame& frame, int numArguments)
{
    frame.returnAddress = vm->programCounter;
    frame.integers = vm->integers;
    frame.locals = vm->locals;
    frame.strings = vm->strings;

    vm->strings.clear();
    vm->integers.clear();
    vm->locals.clear();

    std::vector<Value> imStack;

    for (int i = 0; i < numArguments; i++)
    {
        auto& top = vm->stack.top();

        Value val = {};
        val.type = top.type;

        switch (top.type)
        {
        case TY_INT:
            vm->integers.push_back(frame.integers[top.index]);
            val.index = int(vm->integers.size()) - 1;
            break;
        case TY_STRING:
            vm->strings.push_back(frame.strings[top.index]);
            val.index = int(vm->strings.size()) - 1;
            break;
        }

        imStack.push_back(val);
        vm->stack.pop();
    }

    for (int i = int(imStack.size()) - 1; i >= 0; i--)
    {
        auto& val = imStack[i];
        vm->stack.push(val);
    }
}

static void Op_Call(VirtualMachine* vm, unsigned char* program)
{
    if (vm->handler)
    {
        // Calls out to a handler
        // parameters can be accessed via GetParamInt() etc
        vm->callName = Read_String(program, &vm->programCounter);
        if (vm->statusCode == VM_OK)
        {
            const auto& it = vm->functions.find(vm->callName);
            if (it != vm->functions.end())
            {
                const int address = it->second.offset + vm->programOffset;
                StackFrame frame = {};
                CreateStackFrame(vm, frame, it->second.numArgs);
                vm->frames.push(frame);
                vm->programCounter = address;
            }
            else
            {
                vm->handler(vm);
            }
        }
    }
    else
    {
        // this is an error
        vm->running = false;
        vm->statusCode = VM_ERROR;
    }
}

static void Op_Yield(VirtualMachine* vm, unsigned char* program)
{
    if (vm->handler)
    {
        // Calls out to a handler
        // parameters can be accessed via GetParamInt() etc
        vm->callName = Read_String(program, &vm->programCounter);
        if (vm->statusCode == VM_OK)
        {
            vm->handler(vm);

            vm->running = false;
            vm->statusCode = VM_YIELDED;
        }
    }
    else
    {
        // this is an error
        vm->running = false;
        vm->statusCode = VM_ERROR;
    }
}

static void Op_If(VirtualMachine* vm, unsigned char* program)
{
    if (vm->statusCode == VM_OK)
    {
        if (!vm->stack.empty())
        {
            auto& val = vm->stack.top();
            if (val.type != TY_INT)
            {
                vm->statusCode = VM_ERROR;
                vm->running = false;
            }
            else
            {
                if (vm->integers[val.index] == 0)
                {
                    vm->disabledCounter++;
                    vm->statusCode = VM_PAUSED;
                }

                vm->stack.pop();
            }
        }
        else
        {
            vm->running = false;
            vm->statusCode = VM_ERROR;
        }
    }
    else if (vm->statusCode == VM_PAUSED)
    {
        vm->disabledCounter++;
    }
}

static void Op_EndIf(VirtualMachine* vm, unsigned char* program)
{
    if (vm->statusCode == VM_PAUSED)
    {
        if (vm->disabledCounter == 0)
        {
            vm->statusCode = VM_ERROR;
            vm->running = false;
        }
        vm->disabledCounter--;
        if (vm->disabledCounter == 0)
        {
            vm->statusCode = VM_OK;
        }
    }
}

static void Op_Pop_Discard(VirtualMachine* vm, unsigned char* program)
{
    if (vm->statusCode == VM_OK && vm->stack.size() > 0)
    {
        vm->stack.pop();
    }
}

static void Op_Pop(VirtualMachine* vm, unsigned char* program)
{
    const std::string name = Read_String(program, &vm->programCounter);

    if (vm->statusCode == VM_OK)
    {
        if (!vm->stack.empty())
        {
            auto& local = vm->locals[name];
            local.index = vm->stack.top().index;
            local.type = vm->stack.top().type;

            vm->stack.pop();
        }
        else
        {
            vm->running = false;
            vm->statusCode = VM_ERROR;
        }
    }
}

static void Op_Local(VirtualMachine* vm, unsigned char* program)
{
    const std::string name = Read_String(program, &vm->programCounter);
    if (vm->statusCode == VM_OK)
    {
        Value val = {};
        vm->locals.insert(std::pair<std::string, Value>(name, val));
    }
}

static void Add_String(VirtualMachine* vm, Value& v1, Value& v2)
{
    std::stringstream result;
    result << vm->strings[v1.index];
    switch (v2.type)
    {
    case TY_STRING:
        result << vm->strings[v2.index];
        break;
    case TY_INT:
        result << vm->integers[v2.index];
        break;
    default:
        vm->running = false;
        vm->statusCode = VM_ERROR;
        break;
    }

    if (vm->statusCode == VM_OK)
    {
        Push_String(vm, result.str());
    }
}

static void Add_Int(VirtualMachine* vm, Value& v1, Value& v2)
{
    int result = vm->integers[v1.index];
    
    if (v2.type == TY_INT)
    {
        result += vm->integers[v2.index];
    }
    else
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
    }

    if (vm->statusCode == VM_OK)
    {
        Push_Int(vm, result);
    }
}

static void Sub_Int(VirtualMachine* vm, Value& v1, Value& v2)
{
    int result = vm->integers[v1.index];

    if (v2.type == TY_INT)
    {
        result -= vm->integers[v2.index];
    }
    else
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
    }

    if (vm->statusCode == VM_OK)
    {
        Push_Int(vm, result);
    }
}

static void Mul_Int(VirtualMachine* vm, Value& v1, Value& v2)
{
    int result = vm->integers[v1.index];

    if (v2.type == TY_INT)
    {
        result *= vm->integers[v2.index];
    }
    else
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
    }

    if (vm->statusCode == VM_OK)
    {
        Push_Int(vm, result);
    }
}

static void Div_Int(VirtualMachine* vm, Value& v1, Value& v2)
{
    int result = vm->integers[v1.index];

    if (v2.type == TY_INT)
    {
        result /= vm->integers[v2.index];
    }
    else
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
    }

    if (vm->statusCode == VM_OK)
    {
        Push_Int(vm, result);
    }
}

static void Op_Operator(unsigned char op, VirtualMachine* vm, unsigned char* program)
{
    if (vm->statusCode != VM_OK)
    {
        return;
    }

    if (vm->stack.size() < 2)
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
        return;
    }

    Value var1 = vm->stack.top();
    vm->stack.pop();
    Value var2 = vm->stack.top();
    vm->stack.pop();

    if (op == OP_ADD)
    {
        switch (var1.type)
        {
        case TY_STRING:
            Add_String(vm, var1, var2);
            break;
        case TY_INT:
            Add_Int(vm, var1, var2);
            break;
        default:
            vm->running = false;
            vm->statusCode = VM_ERROR;
            break;
        }
    }
    else if (op == OP_SUB)
    {
        switch (var1.type)
        {
        case TY_INT:
            Sub_Int(vm, var1, var2);
            break;
        default:
            vm->running = false;
            vm->statusCode = VM_ERROR;
            break;
        }
    }
    else if (op == OP_MUL)
    {
        switch (var1.type)
        {
        case TY_INT:
            Mul_Int(vm, var1, var2);
            break;
        default:
            vm->running = false;
            vm->statusCode = VM_ERROR;
            break;
        }
    }
    else if (op == OP_DIV)
    {
        switch (var1.type)
        {
        case TY_INT:
            Div_Int(vm, var1, var2);
            break;
        default:
            vm->running = false;
            vm->statusCode = VM_ERROR;
            break;
        }
    }
}

static void Op_Format(VirtualMachine* vm, unsigned char* program)
{
    if (vm->statusCode == VM_OK)
    {
        const Value& formatVal = vm->stack.top();
        if (formatVal.type != TY_STRING)
        {
            vm->statusCode = VM_ERROR;
            vm->running = false;
            return;
        }

        const std::string format = vm->strings[formatVal.index];
        vm->stack.pop();

        std::stringstream formatted;

        size_t offset = 0;
        while (offset < format.size())
        {
            size_t pos = format.find('{', offset);
            formatted << format.substr(offset, pos - offset);
            if (pos == std::string::npos) { break; }
            size_t end = format.find('}', pos);

            std::string name = format.substr(pos + 1, end - pos - 1);
            const Value& local = vm->locals[name];
            switch (local.type)
            {
            case TY_INT:
                formatted << vm->integers[local.index];
                break;
            case TY_STRING:
                formatted << vm->strings[local.index];
                break;
            default:
                vm->statusCode = VM_ERROR;
                vm->running = false;
                break;
            }

            offset = end + 1;
        }

        Push_String(vm, formatted.str());
    }
}

static void ResetVM(VirtualMachine* vm)
{
    vm->programCounter = 0;
    vm->disabledCounter = 0;
    vm->programOffset = 0;
    vm->errorCode = 0;
    vm->instructionsExecuted = 0;
    vm->timeout = 0;
    vm->resumeCode = VM_OK;
    while (!vm->stack.empty()) { vm->stack.pop(); }
    vm->integers.clear();
    vm->strings.clear();
    vm->locals.clear();
}

static void ScanFunctions(VirtualMachine* vm, unsigned char* program)
{
    const int numFunctions = Read_Int(program, &vm->programCounter);
    for (int i = 0; i < numFunctions; i++)
    {
        const int functionOffset = Read_Int(program, &vm->programCounter);
        const int numArgs = Read_Int(program, &vm->programCounter);
        const std::string name = Read_String(program, &vm->programCounter);
        
        Function func = {};
        func.numArgs = numArgs;
        func.offset = functionOffset;

        vm->functions.insert(std::pair<std::string, Function>(name, func));
    }

    vm->programOffset = vm->programCounter;
}

int SunScript::RunScript(VirtualMachine* vm, unsigned char* program)
{
    return RunScript(vm, program, std::chrono::duration<int, std::nano>::zero());
}

int SunScript::RunScript(VirtualMachine* vm, unsigned char* program, std::chrono::duration<int, std::nano> timeout)
{
    ResetVM(vm);
    ScanFunctions(vm, program);

    // Convert timeout to nanoseconds (or whatever it may be specified in)
    vm->timeout = std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout).count();

    return ResumeScript(vm, program);
}

inline static void CheckForTimeout(VirtualMachine* vm)
{
    if (vm->timeout > 0 && vm->instructionsExecuted % 50 == 0)
    {
        std::chrono::steady_clock::time_point curTime = vm->clock.now();
        if (curTime.time_since_epoch().count() >= vm->startTime.time_since_epoch().count() + vm->timeout)
        {
            vm->resumeCode = vm->statusCode;
            vm->statusCode = VM_TIMEOUT;
            vm->running = false;
        }
    }
}

int SunScript::ResumeScript(VirtualMachine * vm, unsigned char* program)
{
    vm->running = true;
    vm->statusCode = vm->resumeCode;
    vm->resumeCode = VM_OK;
    vm->startTime = vm->clock.now();
    vm->instructionsExecuted = 0;

    while (vm->running)
    {
        unsigned char op = program[vm->programCounter++];

        switch (op)
        {
        case OP_PUSH:
            Op_Push(vm, program);
            break;
        case OP_PUSH_LOCAL:
            Op_Push_Local(vm, program);
            break;
        case OP_SET:
            Op_Set(vm, program);
            break;
        case OP_POP:
            Op_Pop(vm, program);
            break;
        case OP_CALL:
            Op_Call(vm, program);
            break;
        case OP_DONE:
            if (vm->statusCode == VM_OK)
            {
                vm->running = false;
            }
            break;
        case OP_YIELD:
            Op_Yield(vm, program);
            break;
        case OP_LOCAL:
            Op_Local(vm, program);
            break;
        case OP_EQUALS:
        case OP_NOT_EQUALS:
        case OP_GREATER_THAN:
        case OP_LESS_THAN:
            Op_Compare(op, vm, program);
            break;
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
            Op_Operator(op, vm, program);
            break;
        case OP_IF:
            Op_If(vm, program);
            break;
        case OP_END_IF:
            Op_EndIf(vm, program);
            break;
        case OP_FORMAT:
            Op_Format(vm, program);
            break;
        case OP_RETURN:
            Op_Return(vm, program);
            break;
        case OP_BEGIN_FUNCTION:
            vm->disabledCounter++;
            vm->statusCode = VM_PAUSED;
            break;
        case OP_END_FUNCTION:
            if (vm->disabledCounter == 0)
            {
                // If we are executing a function and we don't hit a return statement, implictly return.
                Op_Return(vm, program);
            }
            else
            {
                vm->disabledCounter--;
                vm->statusCode = vm->disabledCounter == 0 ? VM_OK : VM_PAUSED;
            }
            break;
        case OP_POP_DISCARD:
            Op_Pop_Discard(vm, program);
            break;
        }

        vm->instructionsExecuted++;
        CheckForTimeout(vm);
    }

    return vm->statusCode;
}

void SunScript::PushReturnValue(VirtualMachine* vm, const std::string& value)
{
    if (vm->statusCode == VM_OK)
    {
        Push_String(vm, value);
    }
}

void SunScript::PushReturnValue(VirtualMachine* vm, int value)
{
    if (vm->statusCode == VM_OK)
    {
        Push_Int(vm, value);
    }
}

int SunScript::GetCallName(VirtualMachine* vm, std::string* name)
{
    *name = vm->callName;
    return VM_OK;
}

int SunScript::GetParamInt(VirtualMachine* vm, int* param)
{
    Value& val = vm->stack.top();
    if (val.type == TY_INT)
    {
        *param = vm->integers[val.index];

        vm->stack.pop();
        return VM_OK;
    }
    else
    {
        return VM_ERROR;
    }
}

int SunScript::GetParamString(VirtualMachine* vm, std::string* param)
{
    Value& val = vm->stack.top();
    if (val.type == TY_STRING)
    {
        *param = vm->strings[val.index];

        vm->stack.pop();
        return VM_OK;
    }
    else
    {
        return VM_ERROR;
    }
}

Program* SunScript::CreateProgram()
{
    Program* prog = new Program();
    prog->numFunctions = 0;
    return prog;
}

void SunScript::ResetProgram(Program* program)
{
    program->data.clear();
}

int SunScript::GetProgram(Program* program, unsigned char** programData)
{
    *programData = new unsigned char[program->data.size() + program->functions.size() + sizeof(std::int32_t)];
    const int numFunctions = program->numFunctions;
    (*programData)[0] = (unsigned char)(numFunctions & 0xFF);
    (*programData)[1] = (unsigned char)((numFunctions >> 8) & 0xFF);
    (*programData)[2] = (unsigned char)((numFunctions >> 16) & 0xFF);
    (*programData)[3] = (unsigned char)((numFunctions >> 24) & 0xFF);
    std::memcpy(*programData + sizeof(std::int32_t), program->functions.data(), program->functions.size());
    std::memcpy(*programData + program->functions.size() + sizeof(std::int32_t), program->data.data(), program->data.size());
    return (int)program->data.size();
}

void SunScript::ReleaseProgram(Program* program)
{
    delete program;
}

static void EmitInt(std::vector<unsigned char>& data, const int value)
{
    data.push_back(value & 0xFF);
    data.push_back((value >> 8) & 0xFF);
    data.push_back((value >> 16) & 0xFF);
    data.push_back((value >> 24) & 0xFF);
}

static void EmitString(std::vector<unsigned char>& data, const std::string& value)
{
    for (char val : value)
    {
        data.push_back(val);
    }

    data.push_back(0);
}

void SunScript::EmitReturn(Program* program)
{
    program->data.push_back(OP_RETURN);
}

void SunScript::EmitBeginFunction(Program* program, const std::string& name, int numArgs)
{
    program->data.push_back(OP_BEGIN_FUNCTION);

    const int offset = int(program->data.size());
    EmitInt(program->functions, offset);
    EmitInt(program->functions, numArgs);
    EmitString(program->functions, name);
    program->numFunctions++;
}

void SunScript::EmitEndFunction(Program* program)
{
    program->data.push_back(OP_END_FUNCTION);
}

void SunScript::EmitLocal(Program* program, const std::string& name)
{
    program->data.push_back(OP_LOCAL);
    EmitString(program->data, name);
}

void SunScript::EmitSet(Program* program, const std::string& name, int value)
{
    program->data.push_back(OP_SET);
    program->data.push_back(TY_INT);
    EmitString(program->data, name);
    EmitInt(program->data, value);
}

void SunScript::EmitSet(Program* program, const std::string& name, const std::string& value)
{
    program->data.push_back(OP_SET);
    program->data.push_back(TY_STRING);
    EmitString(program->data, name);
    EmitString(program->data, value);
}

void SunScript::EmitPushLocal(Program* program, const std::string& localName)
{
    program->data.push_back(OP_PUSH_LOCAL);
    EmitString(program->data, localName);
}

void SunScript::EmitPush(Program* program, int value)
{
    program->data.push_back(OP_PUSH);
    program->data.push_back(TY_INT);
    EmitInt(program->data, value);
}

void SunScript::EmitPush(Program* program, const std::string& value)
{
    program->data.push_back(OP_PUSH);
    program->data.push_back(TY_STRING);
    EmitString(program->data, value);
}

void SunScript::EmitPop(Program* program, const std::string& localName)
{
    program->data.push_back(OP_POP);
    EmitString(program->data, localName);
}

void SunScript::EmitPop(Program* program)
{
    program->data.push_back(OP_POP_DISCARD);
}

void SunScript::EmitYield(Program* program, const std::string& name)
{
    program->data.push_back(OP_YIELD);
    EmitString(program->data, name);
}

void SunScript::EmitCall(Program* program, const std::string& name)
{
    program->data.push_back(OP_CALL);
    EmitString(program->data, name);
}

void SunScript::EmitAdd(Program* program)
{
    program->data.push_back(OP_ADD);
}

void SunScript::EmitSub(Program* program)
{
    program->data.push_back(OP_SUB);
}

void SunScript::EmitDiv(Program* program)
{
    program->data.push_back(OP_DIV);
}

void SunScript::EmitMul(Program* program)
{
    program->data.push_back(OP_MUL);
}

void SunScript::EmitEquals(Program* program)
{
    program->data.push_back(OP_EQUALS);
}

void SunScript::EmitNotEquals(Program* program)
{
    program->data.push_back(OP_NOT_EQUALS);
}

void SunScript::EmitGreaterThan(Program* program)
{
    program->data.push_back(OP_GREATER_THAN);
}

void SunScript::EmitLessThan(Program* program)
{
    program->data.push_back(OP_LESS_THAN);
}

void SunScript::EmitIf(Program* program)
{
    program->data.push_back(OP_IF);
}

void SunScript::EmitEndIf(Program* program)
{
    program->data.push_back(OP_END_IF);
}

void SunScript::EmitFormat(Program* program)
{
    program->data.push_back(OP_FORMAT);
}

void SunScript::EmitDone(Program* program)
{
    program->data.push_back(OP_DONE);
}
