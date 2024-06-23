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
constexpr unsigned char OP_OR = 0xa;
constexpr unsigned char OP_AND = 0xb;
constexpr unsigned char OP_LOOP_END = 0xc;
constexpr unsigned char OP_UNARY_MINUS = 0xd;

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
constexpr unsigned char OP_ELSE = 0x27;
constexpr unsigned char OP_ELSE_IF = 0x28;

constexpr unsigned char TY_VOID = 0x0;
constexpr unsigned char TY_INT = 0x1;
constexpr unsigned char TY_STRING = 0x2;

constexpr unsigned int BR_EMPTY = 0x0;
constexpr unsigned int BR_FROZEN = 0x1;
constexpr unsigned int BR_DISABLED = 0x2;
constexpr unsigned int BR_EXECUTED = 0x4;
constexpr unsigned int BR_ELSE_IF = 0x8;

namespace SunScript
{
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
        int numArgs;
    };

    struct VirtualMachine
    {
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
        void (*handler)(VirtualMachine* vm);
        void* _userData;
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

static void Op_Or(VirtualMachine* vm, unsigned char* program)
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

    if (var1.type != TY_INT || var2.type != TY_INT)
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
        return;
    }

    bool result = (vm->integers[var1.index] == 1 || vm->integers[var2.index] == 1);
    
    Value v = {};
    v.index = (unsigned int)vm->integers.size();
    v.type = TY_INT;
    vm->stack.push(v);
    vm->integers.push_back(result ? 1 : 0);
}

static void Op_And(VirtualMachine* vm, unsigned char* program)
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

    if (var1.type != TY_INT || var2.type != TY_INT)
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
        return;
    }

    if (vm->integers[var1.index] == 1 && vm->integers[var2.index] == 1)
    {
        Value v = {};
        v.index = var1.index;
        v.type = TY_INT;
        vm->stack.push(v);
    }
    else
    {
        Value v = {};
        v.index = (unsigned int)vm->integers.size();
        v.type = TY_INT;
        vm->stack.push(v);
        vm->integers.push_back(0);
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

static void Op_Call(VirtualMachine* vm, unsigned char* program)
{
    unsigned char numArgs = Read_Byte(program, &vm->programCounter);
    vm->callName = Read_String(program, &vm->programCounter);
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
                vm->handler(vm);
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

static void Op_Yield(VirtualMachine* vm, unsigned char* program)
{
    if (vm->handler)
    {
        unsigned char numArgs = Read_Byte(program, &vm->programCounter);
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
                int br = BR_EMPTY;
                if (vm->integers[val.index] == 0)
                {
                    br |= BR_DISABLED;
                    vm->statusCode = VM_PAUSED;
                }
                else
                {
                    br |= BR_EXECUTED;
                }

                if (vm->branches.size() > 0)
                {
                    const int br = vm->branches.top();
                    if ((br & BR_ELSE_IF) == BR_ELSE_IF)
                    {
                        vm->branches.pop();
                    }
                }

                vm->branches.push(br);
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
        if (vm->branches.size() > 0)
        {
            int nbr = BR_FROZEN | BR_DISABLED;
            const int br = vm->branches.top();
            if ((br & BR_ELSE_IF) == BR_ELSE_IF)
            {
                vm->branches.pop();
                nbr = br;
                nbr &= ~BR_ELSE_IF;
            }
            vm->branches.push(nbr);
        }
        else
        {
            vm->branches.push(BR_FROZEN | BR_DISABLED);
        }
    }
}

static void Op_Else(VirtualMachine* vm, unsigned char* program)
{
    if (vm->branches.size() == 0)
    {
        vm->statusCode = VM_ERROR;
        vm->running = false;
        return;
    }

    int br = vm->branches.top();
    if ((br & BR_EXECUTED) == BR_EXECUTED)
    {
        // If the branch has been executed then we don't need to run.
        vm->statusCode = VM_PAUSED;

        vm->branches.pop();
        br |= BR_DISABLED;
        vm->branches.push(br);
    }
    else if (br == BR_DISABLED)
    {
        // Otherwise we should execute the ELSE clause.
        vm->statusCode = VM_OK;

        vm->branches.pop();
        br &= ~BR_DISABLED;
        vm->branches.push(br);
    }
}

static void Op_Else_If(VirtualMachine* vm, unsigned char* program)
{
    if (vm->branches.size() == 0)
    {
        vm->statusCode = VM_ERROR;
        vm->running = false;
        return;
    }

    int br = vm->branches.top();
    if ((br & BR_EXECUTED) == BR_EXECUTED)
    {
        // If the branch has been executed then we don't need to run.
        vm->statusCode = VM_PAUSED;

        vm->branches.pop();
        br |= BR_DISABLED | BR_ELSE_IF;
        vm->branches.push(br);
    }
    else if (br == BR_DISABLED)
    {
        // Otherwise we should execute the ELSE IF expression (and possibly the ELSE IF clause).
        vm->statusCode = VM_OK;

        vm->branches.pop();
        br &= ~BR_DISABLED;
        br |= BR_ELSE_IF;
        vm->branches.push(br);
    }
    else if ((br & BR_FROZEN) == BR_FROZEN)
    {
        // We need to a the ELSE IF flag.
        vm->branches.pop();
        br |= BR_ELSE_IF;
        vm->branches.push(br);
    }
}

static void Op_EndIf(VirtualMachine* vm, unsigned char* program)
{
    if (vm->branches.size() == 0)
    {
        vm->statusCode = VM_ERROR;
        vm->running = false;
        return;
    }

    vm->branches.pop();
    if (vm->branches.size() == 0)
    {
        vm->statusCode = VM_OK;
    }
    else
    {
        const int br = vm->branches.top();
        if ((br & BR_DISABLED) == BR_DISABLED)
        {
            vm->statusCode = VM_PAUSED;
        }
        else
        {
            vm->statusCode = VM_OK;
        }
    }
}

static void Op_Loop(VirtualMachine* vm, unsigned char* program)
{
    if (vm->statusCode == VM_OK)
    {
        // Loop should continue directly after this instruction.
        vm->loops.push(vm->programCounter);
    }
}

static void Op_Loop_End(VirtualMachine* vm, unsigned char* program)
{
    if (vm->statusCode == VM_OK)
    {
        // Jump to loop address
        const int addr = vm->loops.top();
        vm->programCounter = addr;
        vm->branches.pop();
    }
    else if (vm->statusCode == VM_PAUSED)
    {
        int br = vm->branches.top();
        if ((br & BR_FROZEN) != BR_FROZEN)
        {
            vm->loops.pop();
        }

        Op_EndIf(vm, program);
    }
}

static void Op_Pop_Discard(VirtualMachine* vm, unsigned char* program)
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

static void Op_Unary_Minus(VirtualMachine* vm, unsigned char* program)
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
    vm->programOffset = 0;
    vm->stackBounds = 0;
    vm->errorCode = 0;
    vm->instructionsExecuted = 0;
    vm->timeout = 0;
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
        const int numArgs = Read_Int(program, &vm->programCounter);
        const std::string name = Read_String(program, &vm->programCounter);
        
        Function func = {};
        func.numArgs = numArgs;
        func.offset = functionOffset;

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

int SunScript::RunScript(VirtualMachine* vm, unsigned char* program, unsigned char* debugData, std::chrono::duration<int, std::nano> timeout)
{
    ResetVM(vm);
    ScanFunctions(vm, program);
    ScanDebugData(vm, debugData);

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

int SunScript::ResumeScript(VirtualMachine* vm, unsigned char* program)
{
    vm->running = true;
    vm->statusCode = vm->resumeCode;
    vm->resumeCode = VM_OK;
    vm->startTime = vm->clock.now();
    vm->instructionsExecuted = 0;

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
            else
            {
                vm->statusCode = VM_ERROR;
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
        case OP_UNARY_MINUS:
            Op_Unary_Minus(vm, program);
            break;
        case OP_AND:
            Op_And(vm, program);
            break;
        case OP_OR:
            Op_Or(vm, program);
            break;
        case OP_IF:
            Op_If(vm, program);
            break;
        case OP_ELSE:
            Op_Else(vm, program);
            break;
        case OP_ELSE_IF:
            Op_Else_If(vm, program);
            break;
        case OP_END_IF:
            Op_EndIf(vm, program);
            break;
        case OP_LOOP:
            Op_Loop(vm, program);
            break;
        case OP_LOOP_END:
            Op_Loop_End(vm, program);
            break;
        case OP_FORMAT:
            Op_Format(vm, program);
            break;
        case OP_RETURN:
            Op_Return(vm, program);
            break;
        case OP_BEGIN_FUNCTION:
            vm->branches.push(BR_FROZEN | BR_DISABLED);
            vm->statusCode = VM_PAUSED;
            break;
        case OP_END_FUNCTION:
            if (vm->frames.size() > 0)
            {
                // If we are executing a function and we don't hit a return statement, implictly return.
                Op_Return(vm, program);
            }
            else
            {
                vm->branches.pop();
                vm->statusCode = VM_OK;
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
    prog->numLines = 0;
    return prog;
}

void SunScript::ResetProgram(Program* program)
{
    program->data.clear();
    program->functions.clear();
    program->debug.clear();
    program->numFunctions = 0;
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
        case OP_BEGIN_FUNCTION:
            ss << "OP_BEGIN_FUNCTION" << std::endl;
            break;
        case OP_END_FUNCTION:
            ss << "OP_END_FUNCTION" << std::endl;
            break;
        case OP_AND:
            ss << "OP_AND" << std::endl;
            break;
        case OP_OR:
            ss << "OP_OR" << std::endl;
            break;
        case OP_UNARY_MINUS:
            ss << "OP_UNARY_MINUS" << std::endl;
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
        case OP_ELSE:
            ss << "OP_ELSE" << std::endl;
            break;
        case OP_ELSE_IF:
            ss << "OP_ELSE_IF" << std::endl;
            break;
        case OP_END_IF:
            ss << "OP_END_IF" << std::endl;
            break;
        case OP_EQUALS:
            ss << "OP_EQUALS" << std::endl;
            break;
        case OP_FORMAT:
            ss << "OP_FORMAT" << std::endl;
            break;
        case OP_GREATER_THAN:
            ss << "OP_GREATER_THAN" << std::endl;
            break;
        case OP_IF:
            ss << "OP_IF" << std::endl;
            break;
        case OP_LESS_THAN:
            ss << "OP_LESS_THAN" << std::endl;
            break;
        case OP_LOCAL:
            ss << "OP_LOCAL " << Read_String(programData, &vm->programCounter) << std::endl;
            break;
        case OP_LOOP:
            ss << "OP_LOOP" << std::endl;
            break;
        case OP_LOOP_END:
            ss << "OP_LOOP_END" << std::endl;
            break;
        case OP_NOT_EQUALS:
            ss << "OP_NOT_EQUALS" << std::endl;
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

void SunScript::EmitYield(Program* program, const std::string& name, unsigned char numArgs)
{
    program->data.push_back(OP_YIELD);
    program->data.push_back(numArgs);
    EmitString(program->data, name);
}

void SunScript::EmitCall(Program* program, const std::string& name, unsigned char numArgs)
{
    program->data.push_back(OP_CALL);
    program->data.push_back(numArgs);
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

void SunScript::EmitAnd(Program* program)
{
    program->data.push_back(OP_AND);
}

void SunScript::EmitOr(Program* program)
{
    program->data.push_back(OP_OR);
}

void SunScript::EmitIf(Program* program)
{
    program->data.push_back(OP_IF);
}

void SunScript::EmitElse(Program* program)
{
    program->data.push_back(OP_ELSE);
}

void SunScript::EmitElseIf(Program* program)
{
    program->data.push_back(OP_ELSE_IF);
}

void SunScript::EmitEndIf(Program* program)
{
    program->data.push_back(OP_END_IF);
}

void SunScript::EmitFormat(Program* program)
{
    program->data.push_back(OP_FORMAT);
}

void SunScript::EmitUnaryMinus(Program* program)
{
    program->data.push_back(OP_UNARY_MINUS);
}

void SunScript::EmitLoop(Program* program)
{
    program->data.push_back(OP_LOOP);
}

void SunScript::EmitEndLoop(Program* program)
{
    program->data.push_back(OP_LOOP_END);
}

void SunScript::EmitDone(Program* program)
{
    program->data.push_back(OP_DONE);
}

void SunScript::EmitDebug(Program* program, int line)
{
    EmitInt(program->debug, int(program->data.size()));
    EmitInt(program->debug, line);
    program->numLines++;
}
