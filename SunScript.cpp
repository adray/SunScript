#include "SunScript.h"
#include <fstream>
#include <stack>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <iostream>

using namespace SunScript;

#define VM_ALIGN_16(x) ((x + 0xf) & ~(0xf))

namespace SunScript
{
//==================
// MemoryManager
//===================

    MemoryManager::MemoryManager() : _memory(nullptr), _pos(0), _totalSize(0) {}

    void* MemoryManager::New(uint64_t size, char type)
    {
        const uint64_t totalSize = VM_ALIGN_16(size + sizeof(Header));
        if (_memory == nullptr)
        {
            // Allocate 4KB to start with
            _totalSize = 4 * 1024;
            _memory = new unsigned char[_totalSize];
            std::memset(_memory, 0, _totalSize);
        }
        else if (_pos + totalSize >= _totalSize)
        {
            // TODO: reallocate with larger size
            return nullptr;
        }

        Header* header = reinterpret_cast<Header*>(_memory + _pos);
        header->_refCount = 1ULL;
        header->_size = totalSize;
        header->_type = type;
        _pos += 8;
        unsigned char* mem = _memory + _pos;
        _pos += totalSize;
        return mem;
    }

    void MemoryManager::Dump()
    {
        // Dump memory to the console.
        std::cout << std::endl;

        for (int i = 0; i < _pos; i++)
        {
            std::cout << std::format("0x{:x}", _memory[i]) << " ";
            if ((i + 1) % 16 == 0)
            {
                std::cout << std::endl;
            }
        }
    }

    void MemoryManager::AddRef(void* mem)
    {
        unsigned char* end = _memory + _totalSize;
        if (mem >= _memory && mem < end)
        {
            Header* header = reinterpret_cast<Header*>((char*)mem - sizeof(Header));
            header->_refCount++;
        }
    }

    void MemoryManager::Release(void* mem)
    {
        unsigned char* end = _memory + _totalSize;
        if (mem >= _memory && mem < end)
        {
            Header* header = reinterpret_cast<Header*>((char*)mem - sizeof(Header));
            header->_refCount--;

            // TODO: add to free list.
        }
    }

    char MemoryManager::GetType(void* mem)
    {
        unsigned char* end = _memory + _totalSize;
        if (mem >= _memory && mem < end)
        {
            Header* header = reinterpret_cast<Header*>((char*)mem - sizeof(Header));
            return header->_type;
        }

        return TY_VOID;
    }

    void MemoryManager::Reset()
    {
        _pos = 0;
    }

    MemoryManager::~MemoryManager()
    {
        delete[] _memory;
        _memory = nullptr;
    }

//============================

    struct StackFrame
    {
        int debugLine;
        int returnAddress;
        int stackBounds;
        FunctionInfo* func;
        std::string functionName;
        std::stack<int> branches;
        std::unordered_map<std::string, void*> locals;
        std::stack<int> loops;
    };

    struct Function
    {
        int numArgs;
        FunctionInfo info;
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
        MemoryManager mm;
        std::string callName;
        std::stack<int> branches;
        std::stack<int> loops;
        std::stack<StackFrame> frames;
        std::stack<void*> stack;
        std::unordered_map<int, int> debugLines;
        std::unordered_map<std::string, Function> functions;
        std::unordered_map<std::string, void*> locals;
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

inline static void RecordBranch(FunctionInfo* info, unsigned int pc, bool branchDirection)
{
    const unsigned int numCount = sizeof(info->branchStats) / sizeof(BranchStat);
    for (unsigned int i = 0; i < numCount; i++)
    {
        auto& branch = info->branchStats[i];
        if (i < info->branchCount)
        {
            if (branch.pc == pc)
            {
                if (branchDirection)
                {
                    branch.trueCount++;
                }
                else
                {
                    branch.falseCount++;
                }
                break;
            }
        }
        else
        {
            branch.pc = pc;
            branch.falseCount = branchDirection ? 0 : 1;
            branch.trueCount = branchDirection ? 1 : 0;
            info->branchCount++;
            break;
        }
    }
}

inline static void RecordReturn(FunctionInfo* info, unsigned int pc, char type)
{
    const unsigned int numCount = sizeof(info->retStats) / sizeof(ReturnStat);
    for (unsigned int i = 0; i < numCount; i++)
    {
        auto& ret = info->retStats[i];
        if (i < info->retCount)
        {
            if (ret.pc == pc &&
                ret.type == type)
            {
                ret.count++;
                break;
            }
        }
        else
        {
            ret.pc = pc;
            ret.type = type;
            ret.count = 1;
            info->retCount++;
            break;
        }
    }
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

inline static unsigned char Read_Byte(unsigned char* program, unsigned int* pc)
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
        int* data = reinterpret_cast<int*>(vm->mm.New(sizeof(int), TY_INT));
        *data = val;
        vm->stack.push(data);
    }
}

static void Push_String(VirtualMachine* vm, const std::string& val)
{
    if (vm->statusCode == VM_OK)
    {
        char* data = reinterpret_cast<char*>(vm->mm.New(sizeof(char*), TY_STRING));
        std::memcpy(data, val.c_str(), val.size() + 1);
        vm->stack.push(data);
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
        if (vm->statusCode == VM_OK)
        {
            int* data = reinterpret_cast<int*>(vm->mm.New(sizeof(int), TY_INT));
            *data = Read_Int(vm->program, &vm->programCounter);
            vm->stack.push(data);
        }
        else if (vm->statusCode == VM_PAUSED)
        {
            Read_Int(vm->program, &vm->programCounter);
        }
    }
        break;
    case TY_STRING:
    {
        if (vm->statusCode == VM_OK)
        {
            std::string str = Read_String(vm->program, &vm->programCounter);
            char* data = reinterpret_cast<char*>(vm->mm.New(sizeof(int), TY_INT));
            std::memcpy(data, str.c_str(), str.size() + 1);
            vm->stack.push(data);
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
            vm->stack.push(it->second);
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

        StackFrame& frame = vm->frames.top();

        // Record stats
        if (vm->stack.size() > 0)
        {
            void* retVal = vm->stack.top();
            const char type = vm->mm.GetType(retVal);
            RecordReturn(frame.func, vm->programCounter, type);
        }
        else
        {
            // TY_VOID
            RecordReturn(frame.func, vm->programCounter, TY_VOID);
        }

        vm->locals = frame.locals;
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
    frame.locals = vm->locals;
    frame.branches = vm->branches;
    frame.loops = vm->loops;
    frame.stackBounds = vm->stackBounds;

    vm->locals.clear();
    vm->loops = std::stack<int>();
    vm->branches = std::stack<int>();
    vm->stackBounds = int(vm->stack.size()) - numArguments;

    // TODO: we may need to reverse the stack?
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
                const int address = it->second.info.pc + vm->programOffset;
                StackFrame& frame = vm->frames.emplace();
                frame.functionName = vm->callName;
                frame.debugLine = vm->debugLine;
                frame.func = &it->second.info;
                CreateStackFrame(vm, frame, numArgs);
                vm->programCounter = address;

                it->second.info.counter++;
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
            vm->locals[name] = vm->stack.top();

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
        vm->locals.insert(std::pair<std::string, void*>(name, nullptr));
    }
}

static void Add_String(VirtualMachine* vm, char* v1, void* v2)
{
    std::stringstream result;
    result << v1;
    char type = vm->mm.GetType(v2);
    switch (type)
    {
    case TY_STRING:
        result << reinterpret_cast<char*>(v2);
        break;
    case TY_INT:
        result << *reinterpret_cast<int*>(v2);
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

static void Add_Int(VirtualMachine* vm, int* v1, void* v2)
{
    int result = *v1;
    const char type = vm->mm.GetType(v2);

    if (type == TY_INT)
    {
        result += *reinterpret_cast<int*>(v2);
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

static void Sub_Int(VirtualMachine* vm, int* v1, void* v2)
{
    int result = *v1;
    const char type = vm->mm.GetType(v2);

    if (type == TY_INT)
    {
        result -= *reinterpret_cast<int*>(v2);
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

static void Mul_Int(VirtualMachine* vm, int* v1, void* v2)
{
    int result = *v1;
    const char type = vm->mm.GetType(v2);

    if (type == TY_INT)
    {
        result *= *reinterpret_cast<int*>(v2);
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

static void Div_Int(VirtualMachine* vm, int* v1, void* v2)
{
    int result = *v1;
    const char type = vm->mm.GetType(v2);

    if (type == TY_INT)
    {
        result /= *reinterpret_cast<int*>(v2);
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

    void* var1 = vm->stack.top();
    vm->stack.pop();

    if (vm->mm.GetType(var1) == TY_INT)
    {
        Push_Int(vm, -*reinterpret_cast<int*>(var1));
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

    void* var1 = vm->stack.top();
    vm->stack.pop();
    void* var2 = vm->stack.top();
    vm->stack.pop();

    if (op == OP_ADD)
    {
        switch (vm->mm.GetType(var1))
        {
        case TY_STRING:
            Add_String(vm, reinterpret_cast<char*>(var1), var2);
            break;
        case TY_INT:
            Add_Int(vm, reinterpret_cast<int*>(var1), var2);
            break;
        default:
            vm->running = false;
            vm->statusCode = VM_ERROR;
            break;
        }
    }
    else if (op == OP_SUB)
    {
        switch (vm->mm.GetType(var1))
        {
        case TY_INT:
            Sub_Int(vm, reinterpret_cast<int*>(var1), var2);
            break;
        default:
            vm->running = false;
            vm->statusCode = VM_ERROR;
            break;
        }
    }
    else if (op == OP_MUL)
    {
        switch (vm->mm.GetType(var1))
        {
        case TY_INT:
            Mul_Int(vm, reinterpret_cast<int*>(var1), var2);
            break;
        default:
            vm->running = false;
            vm->statusCode = VM_ERROR;
            break;
        }
    }
    else if (op == OP_DIV)
    {
        switch (vm->mm.GetType(var1))
        {
        case TY_INT:
            Div_Int(vm, reinterpret_cast<int*>(var1), var2);
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
        void* formatVal = vm->stack.top();
        if (vm->mm.GetType(formatVal) != TY_STRING)
        {
            vm->statusCode = VM_ERROR;
            vm->running = false;
            return;
        }

        const std::string format = std::string(reinterpret_cast<char*>(formatVal));
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
            void* local = vm->locals[name];
            switch (vm->mm.GetType(local))
            {
            case TY_INT:
                formatted << *reinterpret_cast<int*>(local);
                break;
            case TY_STRING:
                formatted << reinterpret_cast<char*>(local);
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

        void* value = vm->stack.top();
        
        if (vm->mm.GetType(value) == TY_INT)
        {
            (*reinterpret_cast<int*>(value))++;
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

        void* value = vm->stack.top();

        if (vm->mm.GetType(value) == TY_INT)
        {
            (*reinterpret_cast<int*>(value))--;
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
    unsigned int pc = vm->programCounter;
    const char type = Read_Byte(vm->program, &vm->programCounter);
    const short offset = Read_Short(vm->program, &vm->programCounter);
    bool branchDir = false;

    switch (type)
    {
    case JUMP:
        branchDir = true;
        vm->programCounter += offset;
        break;
    case JUMP_E:
        if (vm->comparer == 0)
        {
            branchDir = true;
            vm->programCounter += offset;
        }
        break;
    case JUMP_GE:
        if (vm->comparer >= 0)
        {
            branchDir = true;
            vm->programCounter += offset;
        }
        break;
    case JUMP_LE:
        if (vm->comparer <= 0)
        {
            branchDir = true;
            vm->programCounter += offset;
        }
        break;
    case JUMP_NE:
        if (vm->comparer != 0)
        {
            branchDir = true;
            vm->programCounter += offset;
        }
        break;
    case JUMP_L:
        if (vm->comparer < 0)
        {
            branchDir = true;
            vm->programCounter += offset;
        }
        break;
    case JUMP_G:
        if (vm->comparer > 0)
        {
            branchDir = true;
            vm->programCounter += offset;
        }
        break;
    }

    // Record branch stats
    if (vm->frames.size() > 0)
    {
        const auto& frame = vm->frames.top();
        RecordBranch(frame.func, pc, branchDir);
    }
    else
    {
        auto& main = vm->functions["main"]; // cache main?
        RecordBranch(&main.info, pc, branchDir);
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

    void* item1 = vm->stack.top();
    vm->stack.pop();

    void* item2 = vm->stack.top();
    vm->stack.pop();

    if (vm->mm.GetType(item1) == TY_INT && vm->mm.GetType(item2) == TY_INT)
    {
        vm->comparer = *reinterpret_cast<int*>(item1) - *reinterpret_cast<int*>(item2);
    }
    else if (vm->mm.GetType(item1) == TY_STRING && vm->mm.GetType(item2) == TY_STRING)
    {
        vm->comparer = strcmp(reinterpret_cast<char*>(item1), reinterpret_cast<char*>(item2));
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
    vm->mm.Reset();
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
        func.info.pc = functionOffset;
        func.info.size = functionSize;

        for (int i = 0; i < numArgs; i++)
        {
            const std::string name = Read_String(program, &vm->programCounter);
            func.info.parameters.push_back(name);
        }

        const int numFields = Read_Int(program, &vm->programCounter);
        for (int i = 0; i < numFields; i++)
        {
            const std::string name = Read_String(program, &vm->programCounter);
            func.info.parameters.push_back(name);
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

static void StartVM(VirtualMachine* vm, unsigned char* program)
{
    vm->running = true;
    vm->statusCode = vm->resumeCode;
    vm->resumeCode = VM_OK;
    vm->startTime = vm->clock.now();
    vm->instructionsExecuted = 0;
    vm->program = program;
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

static int ResumeScript2(VirtualMachine* vm, unsigned char* program)
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

static int RunJIT(VirtualMachine* vm)
{
    const std::string cacheKey = "@main_";

    void* data = vm->jit.jit_search_cache(vm->jit_instance, cacheKey);
    if (!data)
    {
        FunctionInfo* info;
        FindFunction(vm, "main", &info);

        data = vm->jit.jit_compile(vm->jit_instance, vm, vm->program, info, "");
        if (data != nullptr)
        {
            vm->jit.jit_cache(vm->jit_instance, cacheKey, data);
        }
    }

    if (data)
    {
        const int status = vm->jit.jit_execute(vm->jit_instance, data);
        if (status == VM_ERROR)
        {
            vm->running = false;
            vm->statusCode = VM_ERROR;
        }
        return status;
    }

    return VM_DEOPTIMIZE;
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
        const int status = RunJIT(vm);
        if (status != VM_DEOPTIMIZE)
        {
            return status;
        }
    }

    FunctionInfo* info;
    if (FindFunction(vm, "main", &info) == VM_ERROR)
    {
        return VM_ERROR;
    }

    vm->programCounter = info->pc + vm->programOffset;
    info->counter++;

    return ResumeScript2(vm, program);
}

int SunScript::ResumeScript(VirtualMachine* vm, unsigned char* program)
{
    if (vm->jit_instance)
    {
        return vm->jit.jit_resume(vm->jit_instance);
    }

    return ResumeScript2(vm, program);
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
    void* val = vm->stack.top();
    if (vm->mm.GetType(val) == TY_INT)
    {
        *param = *reinterpret_cast<int*>(val);

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
    void* val = vm->stack.top();
    if (vm->mm.GetType(val) == TY_STRING)
    {
        *param = reinterpret_cast<char*>(val);

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
    Push_String(vm, param);
    return VM_OK;
}

int SunScript::PushParamInt(VirtualMachine* vm, int param)
{
    Push_Int(vm, param);
    return VM_OK;
}

void SunScript::InvokeHandler(VirtualMachine* vm, const std::string& callName, int numParams)
{
    vm->callName = callName;
    vm->callNumArgs = numParams;

    // Now, invoke.
    vm->handler(vm);
}

int SunScript::FindFunction(VirtualMachine* vm, const std::string& callName, FunctionInfo** info)
{
    const auto& it = vm->functions.find(callName);
    if (it != vm->functions.end())
    {
        *info = &it->second.info;
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
            ss << (func.second.info.pc + vm->programOffset) << " " << func.first << "(" << func.second.numArgs << ")" << std::endl;
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
