#include "SunScript.h"
#include <fstream>
#include <stack>
#include <vector>
#include <unordered_map>
#include <sstream>

using namespace SunScript;

constexpr unsigned int BR_EMPTY = 0x0;
constexpr unsigned int BR_FROZEN = 0x1;
constexpr unsigned int BR_DISABLED = 0x2;
constexpr unsigned int BR_EXECUTED = 0x4;
constexpr unsigned int BR_ELSE_IF = 0x8;

namespace SunScript
{
    struct JITCacheEntry
    {
        void* data;
        int size;
    };

    struct Value
    {
        unsigned char type;
        unsigned int index;
    };

    struct StackFrame
    {
        int debugLine;
        int returnAddress;
        int stackBounds;
        std::string functionName;
        std::stack<int> branches;
        std::unordered_map<std::string, Value> locals;
        std::vector<std::string> strings;
        std::vector<int> integers;
        std::stack<int> loops;
    };

    struct Function
    {
        int offset;
        int size;
        int numArgs;
        std::vector<std::string> args;
        std::vector<std::string> fields;
    };

    struct VirtualMachine
    {
        unsigned char* program;
        unsigned int programCounter;
        unsigned int programOffset;
        bool running;
        int statusCode;
        int errorCode;
        int resumeCode;
        std::int64_t timeout;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock clock;
        int instructionsExecuted;
        int debugLine;
        int stackBounds;
        int callNumArgs;
        int comparer;
        std::string callName;
        std::stack<int> branches;
        std::stack<int> loops;
        std::stack<StackFrame> frames;
        std::stack<Value> stack;
        std::unordered_map<int, int> debugLines;
        std::unordered_map<std::string, Function> functions;
        std::unordered_map<std::string, Value> locals;
        std::vector<std::string> strings;
        std::vector<int> integers;
        int (*handler)(VirtualMachine* vm);
        Jit jit;
        void* jit_instance;
        void* _userData;
    };

    struct ProgramBlock
    {
        std::string name;
        int numLines;
        int numArgs;
        int numLabels;
        bool topLevel;
        std::vector<std::string> args;
        std::vector<std::string> fields;
        std::vector<unsigned char> debug;
        std::vector<unsigned char> data;
    };

    struct Program
    {
        std::vector<unsigned char> debug;
        std::vector<unsigned char> data;
        std::vector<unsigned char> functions;
        int numFunctions;
        int numLines;
    };
}

//===================

Callstack* SunScript::GetCallStack(VirtualMachine* vm)
{
    Callstack* stack = new Callstack();
    Callstack* tail = stack;
    std::stack<StackFrame> frames(vm->frames);

    int pc = vm->programCounter;
    int debugLine = vm->debugLine;
    while (frames.size() > 0)
    {
        auto& frame = frames.top();
        tail->functionName = frame.functionName;
        tail->numArgs = vm->functions[frame.functionName].numArgs;
        tail->debugLine = debugLine;
        tail->programCounter = pc;
        
        frames.pop();
        debugLine = frame.debugLine;
        pc = frame.returnAddress;
        tail->next = new Callstack();
        tail = tail->next;
    }

    tail->functionName = "main";
    tail->numArgs = 0;
    tail->debugLine = debugLine;
    tail->programCounter = pc;

    return stack;
}

void SunScript::DestroyCallstack(Callstack* stack)
{
    while (stack)
    {
        Callstack* next = stack->next;
        delete stack;
        stack = next;
    }
}

VirtualMachine* SunScript::CreateVirtualMachine()
{
    VirtualMachine* vm = new VirtualMachine();
    vm->handler = nullptr;
    vm->_userData = nullptr;
    vm->program = nullptr;
    vm->comparer = 0;
    std::memset(&vm->jit, 0, sizeof(vm->jit));
    return vm;
}

void SunScript::ShutdownVirtualMachine(VirtualMachine* vm)
{
    if (vm->jit.jit_shutdown)
    {
        vm->jit.jit_shutdown(vm->jit_instance);
    }

    delete vm;
}

void SunScript::SetHandler(VirtualMachine* vm, int handler(VirtualMachine* vm))
{
    vm->handler = handler;
}

void SunScript::SetJIT(VirtualMachine* vm, Jit* jit)
{
    vm->jit = *jit;

    if (vm->jit.jit_initialize)
    {
        vm->jit_instance = vm->jit.jit_initialize();
    }
}

std::string SunScript::JITStats(VirtualMachine* vm)
{
    if (vm->jit.jit_stats)
    {
        return vm->jit.jit_stats(vm->jit_instance);
    }

    return "";
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
        stream.read((char*)&version, sizeof(int));
        if (version == 0)
        {
            int size;
            stream.read((char*)&size, sizeof(int));
            *program = new unsigned char[size];
            stream.read((char*)*program, size);
            return 1;
        }
    }

    return 0;
}

static short Read_Short(unsigned char* program, unsigned int* pc)
{
    int a = program[*pc];
    int b = program[(*pc) + 1];

    *pc += 2;
    return a | (b << 8);
}

static unsigned char Read_Byte(unsigned char* program, unsigned int* pc)
{
    unsigned char byte = program[*pc];
    (*pc)++;
    return byte;
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

static void Op_Set(VirtualMachine* vm)
{
    unsigned char type = vm->program[vm->programCounter++];
    const std::string name = Read_String(vm->program, &vm->programCounter);
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
            vm->integers.push_back(Read_Int(vm->program, &vm->programCounter));
        }
        else if (vm->statusCode == VM_PAUSED)
        {
            Read_Int(vm->program, &vm->programCounter);
        }
    }
        break;
    case TY_STRING:
    {
        local.index = (unsigned int)vm->strings.size();
        local.type = TY_STRING;
        if (vm->statusCode == VM_OK)
        {
            vm->strings.push_back(Read_String(vm->program, &vm->programCounter));
        }
        else if (vm->statusCode == VM_PAUSED)
        {
            Read_String(vm->program, &vm->programCounter);
        }
    }
    break;
    default:
        vm->running = false;
        vm->statusCode = VM_ERROR;
        break;
    }
}

static void Op_Push_Local(VirtualMachine* vm)
{
    std::string name = Read_String(vm->program, &vm->programCounter);

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

static void Op_Push(VirtualMachine* vm)
{
    unsigned char type = vm->program[vm->programCounter++];
    switch (type)
    {
    case TY_INT:
        Push_Int(vm, Read_Int(vm->program, &vm->programCounter));
        break;
    case TY_STRING:
        Push_String(vm, Read_String(vm->program, &vm->programCounter));
        break;
    }
}

static void Op_Return(VirtualMachine* vm)
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
        vm->branches = frame.branches;
        vm->loops = frame.loops;
        vm->stackBounds = frame.stackBounds;
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
    frame.branches = vm->branches;
    frame.loops = vm->loops;
    frame.stackBounds = vm->stackBounds;

    vm->strings.clear();
    vm->integers.clear();
    vm->locals.clear();
    vm->loops = std::stack<int>();
    vm->branches = std::stack<int>();
    vm->stackBounds = int(vm->stack.size()) - numArguments;

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

static void Op_Call(VirtualMachine* vm)
{
    unsigned char numArgs = Read_Byte(vm->program, &vm->programCounter);
    vm->callName = Read_String(vm->program, &vm->programCounter);
    vm->callNumArgs = numArgs;
    if (vm->statusCode == VM_OK)
    {
        const auto& it = vm->functions.find(vm->callName);
        if (it != vm->functions.end())
        {
            if (it->second.numArgs == numArgs)
            {
                const int address = it->second.offset + vm->programOffset;
                StackFrame frame = {};
                frame.functionName = vm->callName;
                frame.debugLine = vm->debugLine;
                CreateStackFrame(vm, frame, numArgs);
                vm->frames.push(frame);
                vm->programCounter = address;
            }
            else
            {
                vm->running = false;
                vm->statusCode = VM_ERROR;
            }
        }
        else
        {
            if (vm->handler)
            {
                // Calls out to a handler
                // parameters can be accessed via GetParamInt() etc
                vm->statusCode = vm->handler(vm);
                vm->running = vm->statusCode == VM_OK;
            }
            else
            {
                // no handler defined
                vm->running = false;
                vm->statusCode = VM_ERROR;
            }
        }
    }
}

static void Op_Yield(VirtualMachine* vm)
{
    if (vm->handler)
    {
        unsigned char numArgs = Read_Byte(vm->program, &vm->programCounter);
        // Calls out to a handler
        // parameters can be accessed via GetParamInt() etc
        vm->callName = Read_String(vm->program, &vm->programCounter);
        vm->callNumArgs = numArgs;
        if (vm->statusCode == VM_OK)
        {
            if (vm->handler(vm) == VM_ERROR)
            {
                vm->running = false;
                vm->statusCode = VM_ERROR;
            }
            else
            {
                vm->running = false;
                vm->statusCode = VM_YIELDED;
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

static void Op_Pop_Discard(VirtualMachine* vm)
{
    if (vm->statusCode == VM_OK && vm->stack.size() > vm->stackBounds)
    {
        if (vm->stack.size() == 0)
        {
            vm->statusCode = VM_ERROR;
            vm->running = false;
        }
        else
        {
            vm->stack.pop();
        }
    }
}

static void Op_Pop(VirtualMachine* vm)
{
    const std::string name = Read_String(vm->program, &vm->programCounter);

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

static void Op_Local(VirtualMachine* vm)
{
    const std::string name = Read_String(vm->program, &vm->programCounter);
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

static void Op_Unary_Minus(VirtualMachine* vm)
{
    if (vm->statusCode != VM_OK)
    {
        return;
    }

    if (vm->stack.size() < 1)
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
        return;
    }

    Value var1 = vm->stack.top();
    vm->stack.pop();

    if (var1.type == TY_INT)
    {
        Push_Int(vm, -vm->integers[var1.index]);
    }
    else
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
    }
}

static void Op_Operator(unsigned char op, VirtualMachine* vm)
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

static void Op_Format(VirtualMachine* vm)
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

static void Op_Increment(VirtualMachine* vm)
{
    if (vm->statusCode == VM_OK)
    {
        if (vm->stack.size() == 0)
        {
            vm->running = false;
            vm->statusCode = VM_ERROR;
            return;
        }

        const SunScript::Value value = vm->stack.top();
        
        if (value.type == TY_INT)
        {
            vm->integers[value.index]++;
        }
        else
        {
            vm->running = false;
            vm->statusCode = VM_ERROR;
        }
    }
}

static void Op_Decrement(VirtualMachine* vm)
{
    if (vm->statusCode == VM_OK)
    {
        if (vm->stack.size() == 0)
        {
            vm->running = false;
            vm->statusCode = VM_ERROR;
            return;
        }

        const SunScript::Value value = vm->stack.top();

        if (value.type == TY_INT)
        {
            vm->integers[value.index]--;
        }
        else
        {
            vm->running = false;
            vm->statusCode = VM_ERROR;
        }
    }
}

static void Op_Jump(VirtualMachine* vm)
{
    const char type = Read_Byte(vm->program, &vm->programCounter);
    const short offset = Read_Short(vm->program, &vm->programCounter);

    switch (type)
    {
    case JUMP:
        vm->programCounter += offset;
        break;
    case JUMP_E:
        if (vm->comparer == 0)
        {
            vm->programCounter += offset;
        }
        break;
    case JUMP_GE:
        if (vm->comparer >= 0)
        {
            vm->programCounter += offset;
        }
        break;
    case JUMP_LE:
        if (vm->comparer <= 0)
        {
            vm->programCounter += offset;
        }
        break;
    case JUMP_NE:
        if (vm->comparer != 0)
        {
            vm->programCounter += offset;
        }
        break;
    case JUMP_L:
        if (vm->comparer < 0)
        {
            vm->programCounter += offset;
        }
        break;
    case JUMP_G:
        if (vm->comparer > 0)
        {
            vm->programCounter += offset;
        }
        break;
    }
}

static void Op_Compare(VirtualMachine* vm)
{
    if (vm->stack.size() < 2)
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
        return;
    }

    Value item1 = vm->stack.top();
    vm->stack.pop();

    Value item2 = vm->stack.top();
    vm->stack.pop();

    if (item1.type == TY_INT && item2.type == TY_INT)
    {
        vm->comparer = vm->integers[item1.index] - vm->integers[item2.index];
    }
    else if (item1.type == TY_STRING && item2.type == TY_STRING)
    {
        vm->comparer = strcmp(vm->strings[item1.index].c_str(), vm->strings[item2.index].c_str());
    }
    else
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
    }
}

static void ResetVM(VirtualMachine* vm)
{
    vm->program = nullptr;
    vm->programCounter = 0;
    vm->programOffset = 0;
    vm->stackBounds = 0;
    vm->errorCode = 0;
    vm->instructionsExecuted = 0;
    vm->timeout = 0;
    vm->callNumArgs = 0;
    vm->resumeCode = VM_OK;
    while (!vm->stack.empty()) { vm->stack.pop(); }
    while (!vm->frames.empty()) { vm->frames.pop(); }
    vm->integers.clear();
    vm->strings.clear();
    vm->locals.clear();
    vm->debugLines.clear();
    vm->functions.clear();
    while (!vm->branches.empty()) {
        vm->branches.pop();
    }
    while (!vm->loops.empty()) {
        vm->loops.pop();
    }
}

static void ScanFunctions(VirtualMachine* vm, unsigned char* program)
{
    const int numFunctions = Read_Int(program, &vm->programCounter);
    for (int i = 0; i < numFunctions; i++)
    {
        const int functionOffset = Read_Int(program, &vm->programCounter);
        const int functionSize = Read_Int(program, &vm->programCounter);
        const std::string name = Read_String(program, &vm->programCounter);
        const int numArgs = Read_Int(program, &vm->programCounter);
        
        Function func = {};
        func.numArgs = numArgs;
        func.offset = functionOffset;
        func.size = functionSize;

        for (int i = 0; i < numArgs; i++)
        {
            const std::string name = Read_String(program, &vm->programCounter);
            func.args.push_back(name);
        }

        const int numFields = Read_Int(program, &vm->programCounter);
        for (int i = 0; i < numFields; i++)
        {
            const std::string name = Read_String(program, &vm->programCounter);
            func.fields.push_back(name);
        }

        vm->functions.insert(std::pair<std::string, Function>(name, func));
    }

    vm->programOffset = vm->programCounter;
}

static void ScanDebugData(VirtualMachine* vm, unsigned char* debugData)
{
    if (debugData)
    {
        unsigned int pos = 0;
        const int numLines = Read_Int(debugData, &pos);
        for (int i = 0; i < numLines; i++)
        {
            const int pc = Read_Int(debugData, &pos);
            const int line = Read_Int(debugData, &pos);
            vm->debugLines.insert(std::pair<int, int>(pc, line));
        }
    }
}

int SunScript::RunScript(VirtualMachine* vm, unsigned char* program)
{
    return RunScript(vm, program, nullptr);
}

int SunScript::RunScript(VirtualMachine* vm, unsigned char* program, unsigned char* debugData)
{
    return RunScript(vm, program, debugData, std::chrono::duration<int, std::nano>::zero());
}

int SunScript::RunScript(VirtualMachine* vm, unsigned char* program, std::chrono::duration<int, std::nano> timeout)
{
    return RunScript(vm, program, nullptr, timeout);
}

static void StartVM(VirtualMachine* vm, unsigned char* program)
{
    vm->running = true;
    vm->statusCode = vm->resumeCode;
    vm->resumeCode = VM_OK;
    vm->startTime = vm->clock.now();
    vm->instructionsExecuted = 0;
    vm->program = program;
}

static int RunJIT(VirtualMachine* vm)
{
    const std::string cacheKey = "@main_";

    void* data = vm->jit.jit_search_cache(vm->jit_instance, cacheKey);
    if (!data)
    {
        FunctionInfo info;
        FindFunction(vm, "main", info);

        data = vm->jit.jit_compile(vm->jit_instance, vm, vm->program, info, "");
        if (data != nullptr)
        {
            vm->jit.jit_cache(vm->jit_instance, cacheKey, data);
        }
    }

    if (data)
    {
        const int status = vm->jit.jit_execute(data);
        if (status == VM_ERROR)
        {
            vm->running = false;
            vm->statusCode = VM_ERROR;
        }
        return status;
    }

    return VM_ERROR;
}

int SunScript::RunScript(VirtualMachine* vm, unsigned char* program, unsigned char* debugData, std::chrono::duration<int, std::nano> timeout)
{
    ResetVM(vm);
    ScanFunctions(vm, program);
    ScanDebugData(vm, debugData);

    // Convert timeout to nanoseconds (or whatever it may be specified in)
    vm->timeout = std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout).count();

    if (vm->jit_instance)
    {
        StartVM(vm, program);
        return RunJIT(vm);
    }

    FunctionInfo info;
    FindFunction(vm, "main", info);
    vm->programCounter = info.pc;

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

int SunScript::ResumeScript(VirtualMachine* vm, unsigned char* program)
{
    StartVM(vm, program);

    while (vm->running)
    {
        const auto& lineIt = vm->debugLines.find(vm->programCounter - vm->programOffset);
        if (lineIt != vm->debugLines.end())
        {
            vm->debugLine = lineIt->second;
        }

        const unsigned char op = program[vm->programCounter++];

        switch (op)
        {
        case OP_PUSH:
            Op_Push(vm);
            break;
        case OP_PUSH_LOCAL:
            Op_Push_Local(vm);
            break;
        case OP_SET:
            Op_Set(vm);
            break;
        case OP_POP:
            Op_Pop(vm);
            break;
        case OP_CALL:
            Op_Call(vm);
            break;
        case OP_DONE:
            if (vm->statusCode == VM_OK)
            {
                vm->running = false;
            }
            else
            {
                vm->statusCode = VM_ERROR;
                vm->running = false;
            }
            break;
        case OP_YIELD:
            Op_Yield(vm);
            break;
        case OP_LOCAL:
            Op_Local(vm);
            break;
        case OP_CMP:
            Op_Compare(vm);
            break;
        case OP_JUMP:
            Op_Jump(vm);
            break;
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
            Op_Operator(op, vm);
            break;
        case OP_UNARY_MINUS:
            Op_Unary_Minus(vm);
            break;
        case OP_FORMAT:
            Op_Format(vm);
            break;
        case OP_RETURN:
            Op_Return(vm);
            break;
        case OP_POP_DISCARD:
            Op_Pop_Discard(vm);
            break;
        case OP_INCREMENT:
            Op_Increment(vm);
            break;
        case OP_DECREMENT:
            Op_Decrement(vm);
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

int SunScript::GetCallNumArgs(VirtualMachine* vm, int* numArgs)
{
    *numArgs = vm->callNumArgs;
    return VM_OK;
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

int SunScript::PushParamString(VirtualMachine* vm, const std::string& param)
{
    vm->strings.push_back(param);

    Value value = { .type = TY_STRING, .index = unsigned int(vm->strings.size()) - 1 };
    vm->stack.push(value);
    return VM_OK;
}

int SunScript::PushParamInt(VirtualMachine* vm, int param)
{
    vm->integers.push_back(param);

    Value value = { .type = TY_INT, .index = unsigned int(vm->integers.size()) - 1 };
    vm->stack.push(value);
    return VM_OK;
}

void SunScript::InvokeHandler(VirtualMachine* vm, const std::string& callName, int numParams)
{
    vm->callName = callName;
    vm->callNumArgs = numParams;

    // Now, invoke.
    vm->handler(vm);
}

int SunScript::FindFunction(VirtualMachine* vm, const std::string& callName, FunctionInfo& info)
{
    const auto& it = vm->functions.find(callName);
    if (it != vm->functions.end())
    {
        info.pc = it->second.offset + vm->programOffset;
        info.size = it->second.size;
        info.locals = it->second.fields;
        info.parameters = it->second.args;
        info.name = callName;
        return VM_OK;
    }

    return VM_ERROR;
}

unsigned char* SunScript::GetLoadedProgram(VirtualMachine* vm)
{
    return vm->program;
}

Program* SunScript::CreateProgram()
{
    Program* prog = new Program();
    prog->numFunctions = 0;
    prog->numLines = 0;
    return prog;
}

ProgramBlock* SunScript::CreateProgramBlock(bool topLevel, const std::string& name, int numArgs)
{
    ProgramBlock* block = new ProgramBlock();
    block->numArgs = numArgs;
    block->numLines = 0;
    block->topLevel = topLevel;
    block->name = name;
    block->numLabels = 0;

    return block;
}

void SunScript::ReleaseProgramBlock(ProgramBlock* block)
{
    delete block;
}

void SunScript::ResetProgram(Program* program)
{
    program->data.clear();
    program->functions.clear();
    program->debug.clear();
    program->numLines = 0;
}

int SunScript::GetProgram(Program* program, unsigned char** programData)
{
    const int size = int(program->data.size() + program->functions.size() + sizeof(std::int32_t));
    *programData = new unsigned char[size];
    const int numFunctions = program->numFunctions;
    (*programData)[0] = (unsigned char)(numFunctions & 0xFF);
    (*programData)[1] = (unsigned char)((numFunctions >> 8) & 0xFF);
    (*programData)[2] = (unsigned char)((numFunctions >> 16) & 0xFF);
    (*programData)[3] = (unsigned char)((numFunctions >> 24) & 0xFF);
    std::memcpy(*programData + sizeof(std::int32_t), program->functions.data(), program->functions.size());
    std::memcpy(*programData + program->functions.size() + sizeof(std::int32_t), program->data.data(), program->data.size());
    return size;
}

int SunScript::GetDebugData(Program* program, unsigned char** debug)
{
    *debug = new unsigned char[program->debug.size() + 4];
    const int numItems = program->numLines;
    (*debug)[0] = (unsigned char)(numItems & 0xFF);
    (*debug)[1] = (unsigned char)((numItems >> 8) & 0xFF);
    (*debug)[2] = (unsigned char)((numItems >> 16) & 0xFF);
    (*debug)[3] = (unsigned char)((numItems >> 24) & 0xFF);
    std::memcpy(&(*debug)[4], program->debug.data(), program->debug.size());
    return int(program->debug.size() + 4);
}

void SunScript::ReleaseProgram(Program* program)
{
    delete program;
}

void SunScript::Disassemble(std::stringstream& ss, unsigned char* programData, unsigned char* debugData)
{
    VirtualMachine* vm = CreateVirtualMachine();
    ResetVM(vm);

    ScanFunctions(vm, programData);
    ScanDebugData(vm, debugData);
    vm->running = true;

    ss << "======================" << std::endl;
    ss << "Functions" << std::endl;
    ss << "======================" << std::endl;
    if (vm->functions.size() > 0)
    {
        for (auto& func : vm->functions)
        {
            ss << (func.second.offset + vm->programOffset) << " " << func.first << "(" << func.second.numArgs << ")" << std::endl;
        }
    }
    else
    {
        ss << "No functions" << std::endl;
    }

    ss << "======================" << std::endl;
    ss << "Program" << std::endl;
    ss << "======================" << std::endl;
    while (vm->running)
    {
        ss << vm->programCounter << " ";
        
        const char op = programData[vm->programCounter++];

        switch (op)
        {
        case OP_UNARY_MINUS:
            ss << "OP_UNARY_MINUS" << std::endl;
            break;
        case OP_INCREMENT:
            ss << "OP_INCREMENT" << std::endl;
            break;
        case OP_DECREMENT:
            ss << "OP_DECREMENT" << std::endl;
            break;
        case OP_ADD:
            ss << "OP_ADD" << std::endl;
            break;
        case OP_SUB:
            ss << "OP_SUB" << std::endl;
            break;
        case OP_MUL:
            ss << "OP_MUL" << std::endl;
            break;
        case OP_DIV:
            ss << "OP_DIV" << std::endl;
            break;
        case OP_CMP:
            ss << "OP_CMP" << std::endl;
            break;
        case OP_JUMP:
            ss << "OP_JUMP " << int(Read_Byte(programData, &vm->programCounter)) << " " << int(Read_Short(programData, &vm->programCounter)) << std::endl;
            break;
        case OP_LOCAL:
            ss << "OP_LOCAL " << Read_String(programData, &vm->programCounter) << std::endl;
            break;
        case OP_POP:
            ss << "OP_POP " << Read_String(programData, &vm->programCounter) << std::endl;
            break;
        case OP_POP_DISCARD:
            ss << "OP_POP_DISCARD" << std::endl;
            break;
        case OP_PUSH:
        {
            const unsigned char ty = programData[vm->programCounter++];
            if (ty == TY_INT)
            {
                ss << "OP_PUSH " << Read_Int(programData, &vm->programCounter) << std::endl;
            }
            else if (ty == TY_STRING)
            {
                ss << "OP_PUSH \"" << Read_String(programData, &vm->programCounter) << "\"" << std::endl;
            }
        }
            break;
        case OP_PUSH_LOCAL:
            ss << "OP_PUSH_LOCAL " << Read_String(programData, &vm->programCounter) << std::endl;
            break;
        case OP_RETURN:
            ss << "OP_RETURN" << std::endl;
            break;
        case OP_SET:
        {
            const unsigned char ty = programData[vm->programCounter++];
            if (ty == TY_INT)
            {
                ss << "OP_SET " << Read_String(programData, &vm->programCounter) << " " << Read_Int(programData, &vm->programCounter) << std::endl;
            }
            else if (ty == TY_STRING)
            {
                ss << "OP_SET " << Read_String(programData, &vm->programCounter) << " \"" << Read_String(programData, &vm->programCounter) << "\"" << std::endl;
            }
        }
            break;
        case OP_YIELD:
            ss << "OP_YIELD " << Read_String(programData, &vm->programCounter) << std::endl;
            break;
        case OP_CALL:
            ss << "OP_CALL " << int(Read_Byte(programData, &vm->programCounter)) << " " << Read_String(programData, &vm->programCounter) << std::endl;
            break;
        case OP_DONE:
            ss << "OP_DONE" << std::endl;
            vm->running = false;
            break;
        }
    }
    
    ShutdownVirtualMachine(vm);
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

void SunScript::EmitProgramBlock(Program* program, ProgramBlock* block)
{
    const int offset = int(program->data.size());
    const int size = int(block->data.size());

    EmitInt(program->functions, offset);
    EmitInt(program->functions, size);
    EmitString(program->functions, block->name);
    EmitInt(program->functions, block->numArgs);
    for (auto& arg : block->args)
    {
        EmitString(program->functions, arg);
    }
    EmitInt(program->functions, int(block->fields.size()));
    for (auto& field : block->fields)
    {
        EmitString(program->functions, field);
    }

    program->data.insert(program->data.end(), block->data.begin(), block->data.end());
    program->debug.insert(program->debug.end(), block->debug.begin(), block->debug.end());
    program->numLines += block->numLines;
    program->numFunctions++;
}

void SunScript::EmitReturn(ProgramBlock* program)
{
    program->data.push_back(OP_RETURN);
}

void SunScript::EmitParameter(ProgramBlock* program, const std::string& name)
{
    program->args.push_back(name);
}

void SunScript::EmitLocal(ProgramBlock* program, const std::string& name)
{
    program->data.push_back(OP_LOCAL);
    EmitString(program->data, name);

    program->fields.push_back(name);
}

void SunScript::EmitSet(ProgramBlock* program, const std::string& name, int value)
{
    program->data.push_back(OP_SET);
    program->data.push_back(TY_INT);
    EmitString(program->data, name);
    EmitInt(program->data, value);
}

void SunScript::EmitSet(ProgramBlock* program, const std::string& name, const std::string& value)
{
    program->data.push_back(OP_SET);
    program->data.push_back(TY_STRING);
    EmitString(program->data, name);
    EmitString(program->data, value);
}

void SunScript::EmitPushLocal(ProgramBlock* program, const std::string& localName)
{
    program->data.push_back(OP_PUSH_LOCAL);
    EmitString(program->data, localName);
}

void SunScript::EmitPush(ProgramBlock* program, int value)
{
    program->data.push_back(OP_PUSH);
    program->data.push_back(TY_INT);
    EmitInt(program->data, value);
}

void SunScript::EmitPush(ProgramBlock* program, const std::string& value)
{
    program->data.push_back(OP_PUSH);
    program->data.push_back(TY_STRING);
    EmitString(program->data, value);
}

void SunScript::EmitPop(ProgramBlock* program, const std::string& localName)
{
    program->data.push_back(OP_POP);
    EmitString(program->data, localName);
}

void SunScript::EmitPop(ProgramBlock* program)
{
    program->data.push_back(OP_POP_DISCARD);
}

void SunScript::EmitYield(ProgramBlock* program, const std::string& name, unsigned char numArgs)
{
    program->data.push_back(OP_YIELD);
    program->data.push_back(numArgs);
    EmitString(program->data, name);
}

void SunScript::EmitCall(ProgramBlock* program, const std::string& name, unsigned char numArgs)
{
    program->data.push_back(OP_CALL);
    program->data.push_back(numArgs);
    EmitString(program->data, name);
}

void SunScript::EmitAdd(ProgramBlock* program)
{
    program->data.push_back(OP_ADD);
}

void SunScript::EmitSub(ProgramBlock* program)
{
    program->data.push_back(OP_SUB);
}

void SunScript::EmitDiv(ProgramBlock* program)
{
    program->data.push_back(OP_DIV);
}

void SunScript::EmitMul(ProgramBlock* program)
{
    program->data.push_back(OP_MUL);
}

void SunScript::EmitFormat(ProgramBlock* program)
{
    program->data.push_back(OP_FORMAT);
}

void SunScript::EmitUnaryMinus(ProgramBlock* program)
{
    program->data.push_back(OP_UNARY_MINUS);
}

void SunScript::EmitIncrement(ProgramBlock* program)
{
    program->data.push_back(OP_INCREMENT);
}

void SunScript::EmitDecrement(ProgramBlock* program)
{
    program->data.push_back(OP_DECREMENT);
}

void SunScript::MarkLabel(ProgramBlock* program, Label* label)
{
    label->pos = int(program->data.size()) - 2;
}

void SunScript::EmitMarkedLabel(ProgramBlock* program, Label* label)
{
    for (const int jump : label->jumps)
    {
        const int offset = label->pos - jump;
        program->data[jump] = offset & 0xFF;
        program->data[jump + 1] = (offset >> 8) & 0xFF;
    }
}

void SunScript::EmitLabel(ProgramBlock* program, Label* label)
{
    for (const int jump : label->jumps)
    {
        const int offset = int(program->data.size()) - jump - 2;
        program->data[jump] = offset & 0xFF;
        program->data[jump + 1] = (offset >> 8) & 0xFF;
    }
}

void SunScript::EmitCompare(ProgramBlock* program)
{
    program->data.push_back(OP_CMP);
}

void SunScript::EmitJump(ProgramBlock* program, char type, Label* label)
{
    program->data.push_back(OP_JUMP);
    program->data.push_back(type);

    // Reserve 16 bits for the jump offset.
    program->data.push_back(0);
    program->data.push_back(0);

    label->jumps.push_back(int(program->data.size()) - 2);
}

void SunScript::EmitDone(ProgramBlock* program)
{
    program->data.push_back(OP_DONE);
}

void SunScript::EmitDebug(ProgramBlock* program, int line)
{
    EmitInt(program->debug, int(program->data.size()));
    EmitInt(program->debug, line);
    program->numLines++;
}
