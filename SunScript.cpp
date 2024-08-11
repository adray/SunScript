#include "SunScript.h"
#include <fstream>
#include <stack>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <iostream>
#include <cstring>
#include <assert.h>

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
            // Allocate 8KB to start with
            _totalSize = 8 * 1024;
            _memory = new unsigned char[_totalSize];
            std::memset(_memory, 0, _totalSize);
        }
        else if (_pos + totalSize >= _totalSize)
        {
            // TODO: reallocate with larger size
	    abort();
            return nullptr;
        }

        Header* header = reinterpret_cast<Header*>(_memory + _pos);
        header->_refCount = 1ULL;
        header->_size = totalSize;
        header->_type = type;
        _pos += sizeof(Header);
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

    class Stack
    {
    public:
        inline Stack();

        inline void push(void* data);
        inline void* pop();
        inline size_t size() { return _pos; }
        inline void* top() { return _array[_pos - 1]; }
        inline bool empty() { return _pos == 0; }

    private:
        void** _array;
        unsigned int _size;
        unsigned int _pos;
    };

    Stack::Stack()
        : _size(32), _pos(0)
    {
        _array = new void* [_size];
    };

    void Stack::push(void* data)
    {
        _array[_pos++] = data;
    }

    void* Stack::pop()
    {
        return _array[--_pos];
    }

//============================

    struct StackFrame
    {
        int debugLine;
        int returnAddress;
        int stackBounds;
        int localBounds;
        FunctionInfo* func;
        std::string functionName;
    };

    struct Block
    {
        int numArgs;
        FunctionInfo info;
    };

    struct Function
    {
        int id;
        int blk;
        std::string name;
    };

    struct VirtualMachine
    {
        unsigned char* program;
        unsigned int programCounter;
        unsigned int programOffset;
        int* debugLines;
        bool running;
	bool tracing;
        int statusCode;
        int errorCode;
        int resumeCode;
        int flags;
        std::int64_t timeout;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock clock;
        int instructionsExecuted;
        int debugLine;
        int stackBounds;
        int localBounds;
        int callNumArgs;
        int comparer;
        MemoryManager mm;
        FunctionInfo* main;
        std::string callName;
        std::stack<StackFrame> frames;
        Stack stack;
        std::vector<Block> blocks;
        std::vector<Function> functions;
        std::vector<void*> locals;
	std::vector<unsigned char> trace;
        int (*handler)(VirtualMachine* vm);
        Jit jit;
        void* jit_instance;
        void* _userData;
	void* jit_trace;
    };

    struct ProgramBlock
    {
        bool topLevel;
        int numLines;
        int numArgs;
        int numLabels;
        int id;
        std::string name;
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
        std::vector<unsigned char> entries;
        std::vector<ProgramBlock*> blocks;
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
        tail->numArgs = int(frame.func->parameters.size());
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
    vm->debugLines = nullptr;
    vm->comparer = 0;
    vm->jit_trace = nullptr;
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

//static std::string Read_String(unsigned char* program, unsigned int* pc)
//{
//    std::string str;
//    int index = 0;
//    char ch = (char)program[*pc];
//    (*pc)++;
//    while (ch != 0)
//    {
//        str = str.append(1, ch);
//        ch = (char)program[*pc];
//        (*pc)++;
//    }
//    return str;
//}

static char* Read_String(unsigned char* program, unsigned int* pc)
{
    char* str = (char*)&program[*pc];
    const size_t len = strlen(str) + 1;
    *pc += static_cast<unsigned int>(len);
    return str;
}

static void Push_Int(VirtualMachine* vm, int val)
{
    assert(vm->statusCode == VM_OK);

    int* data = reinterpret_cast<int*>(vm->mm.New(sizeof(int), TY_INT));
    *data = val;
    vm->stack.push(data);
}

static void Push_String(VirtualMachine* vm, const char* str)
{
    assert(vm->statusCode == VM_OK);

    char* data = reinterpret_cast<char*>(vm->mm.New(sizeof(char*), TY_STRING));
    std::memcpy(data, str, strlen(str) + 1);
    vm->stack.push(data);
}

inline static void Trace_Int(VirtualMachine* vm, int val)
{
    if (vm->tracing)
    {
	vm->trace.push_back(static_cast<unsigned char>(val & 0xFF));
        vm->trace.push_back(static_cast<unsigned char>((val >> 8) & 0xFF));
        vm->trace.push_back(static_cast<unsigned char>((val >> 16) & 0xFF));
        vm->trace.push_back(static_cast<unsigned char>((val >> 24) & 0xFF));
    }
}

inline static void Trace_String(VirtualMachine* vm, const char* str)
{
    if (vm->tracing)
    {
	while (str != 0) { 
            vm->trace.push_back(static_cast<unsigned char>(*str));
	    str++;
	}
    }
}

static void Op_Set(VirtualMachine* vm)
{
    unsigned char type = vm->program[vm->programCounter++];
    const int id = Read_Byte(vm->program, &vm->programCounter) + vm->localBounds;
    auto& local = vm->locals[id];

    if (vm->tracing)
    {
	vm->trace.push_back(OP_SET);
	vm->trace.push_back(static_cast<unsigned char>(id & 0xFF));
    }

    switch (type)
    {
    case TY_VOID:
        vm->running = false;
        vm->statusCode = VM_ERROR;
        break;
    case TY_INT:
    {
        assert (vm->statusCode == VM_OK);
        
        int* data = reinterpret_cast<int*>(vm->mm.New(sizeof(int), TY_INT));
        *data = Read_Int(vm->program, &vm->programCounter);
        vm->stack.push(data);
        Trace_Int(vm, *data);
    }
        break;
    case TY_STRING:
    {
        assert (vm->statusCode == VM_OK);
        
        char* str = Read_String(vm->program, &vm->programCounter);
        char* data = reinterpret_cast<char*>(vm->mm.New(sizeof(int), TY_INT));
        std::memcpy(data, str, strlen(str) + 1);
        vm->stack.push(data);
	Trace_String(vm, str);
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
    const int id = Read_Byte(vm->program, &vm->programCounter) + vm->localBounds;
    
    if (vm->tracing)
    {
	vm->trace.push_back(OP_PUSH_LOCAL);
	vm->trace.push_back(static_cast<unsigned char>(id & 0xFF)); 
    }
    
    if (vm->statusCode == VM_OK)
    {
        if (vm->locals.size() > id)
        {
            vm->stack.push(vm->locals[id]);
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
    
    if (vm->tracing)
    {
	vm->trace.push_back(OP_PUSH);
	vm->trace.push_back(type);
    }

    switch (type)
    {
    case TY_INT:
	{
	    const int val = Read_Int(vm->program, &vm->programCounter);
            Push_Int(vm, val);
	    Trace_Int(vm, val);
	}
        break;
    case TY_STRING:
	{
	     const char* str = Read_String(vm->program, &vm->programCounter);
             Push_String(vm, str);
	     Trace_String(vm, str);
	}
        break;
    }
}

static void Op_Return(VirtualMachine* vm)
{
    assert(vm->statusCode == VM_OK);

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

    if (vm->tracing) { vm->trace.push_back(OP_RETURN); }

    vm->stackBounds = frame.stackBounds;
    vm->localBounds = frame.localBounds;
    vm->frames.pop();
    vm->programCounter = frame.returnAddress;
}

static void CreateStackFrame(VirtualMachine* vm, StackFrame& frame, int numArguments, int numLocals)
{
    frame.returnAddress = vm->programCounter;
    frame.localBounds = vm->localBounds;
    frame.stackBounds = vm->stackBounds;

    vm->stackBounds = int(vm->stack.size()) - numArguments;
    vm->localBounds = int(vm->locals.size());
    vm->locals.resize(numArguments + numLocals + vm->locals.size());

    // TODO: we may need to reverse the stack?
}

static void Op_Call(VirtualMachine* vm)
{
    assert (vm->statusCode == VM_OK);
    
    unsigned char numArgs = Read_Byte(vm->program, &vm->programCounter);
    const int id = Read_Int(vm->program, &vm->programCounter);
    auto& func = vm->functions[id];
    vm->callName = func.name;
    vm->callNumArgs = numArgs;
    
    if (vm->tracing)
    {
	vm->trace.push_back(OP_CALL);
	vm->trace.push_back(numArgs);
	Trace_Int(vm, id);
    }

    if (func.blk != -1)
    {
        auto& blk = vm->blocks[func.blk];
        if (blk.numArgs == numArgs)
        {
            const int address = blk.info.pc + vm->programOffset;
            StackFrame& frame = vm->frames.emplace();
            frame.functionName = vm->callName;
            frame.debugLine = vm->debugLine;
            frame.func = &blk.info;
            CreateStackFrame(vm, frame, numArgs, int(blk.info.locals.size()));
            vm->programCounter = address;

            blk.info.counter++;
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

static void Op_Yield(VirtualMachine* vm)
{
    if (vm->handler)
    {
        unsigned char numArgs = Read_Byte(vm->program, &vm->programCounter);
        // Calls out to a handler
        // parameters can be accessed via GetParamInt() etc
        const int id = Read_Int(vm->program, &vm->programCounter);

	if (vm->tracing)
	{
	    vm->trace.push_back(OP_YIELD);
	    vm->trace.push_back(numArgs);
	    Trace_Int(vm, id);
	}

	vm->callName = vm->functions[id].name;
        vm->callNumArgs = numArgs;

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
    else
    {
        // this is an error
        vm->running = false;
        vm->statusCode = VM_ERROR;
    }
}

static void Op_Pop_Discard(VirtualMachine* vm)
{
    assert(vm->statusCode == VM_OK);
    
    if (vm->tracing) { vm->trace.push_back(OP_POP_DISCARD); }

    if (vm->stack.size() > vm->stackBounds)
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
    assert(vm->statusCode == VM_OK);
    
    const int id = Read_Byte(vm->program, &vm->programCounter) + vm->localBounds;

    if (vm->tracing)
    {
        vm->trace.push_back(OP_POP);
	vm->trace.push_back(static_cast<unsigned char>(id));
    }

    if (!vm->stack.empty())
    {
        vm->locals[id] = vm->stack.top();

        vm->stack.pop();
    }
    else
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
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
        Push_String(vm, result.str().c_str());
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
    assert (vm->statusCode == VM_OK);

    if (vm->stack.size() < 1)
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
        return;
    }

    if (vm->tracing) { vm->trace.push_back(OP_UNARY_MINUS); }

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
    assert(vm->statusCode == VM_OK);

    if (vm->stack.size() < 2)
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
        return;
    }
    
    if (vm->tracing) { vm->trace.push_back(op); }

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

//static void Op_Format(VirtualMachine* vm)
//{
//    if (vm->statusCode == VM_OK)
//    {
//        void* formatVal = vm->stack.top();
//        if (vm->mm.GetType(formatVal) != TY_STRING)
//        {
//            vm->statusCode = VM_ERROR;
//            vm->running = false;
//            return;
//        }
//
//        const std::string format = std::string(reinterpret_cast<char*>(formatVal));
//        vm->stack.pop();
//
//        std::stringstream formatted;
//
//        size_t offset = 0;
//        while (offset < format.size())
//        {
//            size_t pos = format.find('{', offset);
//            formatted << format.substr(offset, pos - offset);
//            if (pos == std::string::npos) { break; }
//            size_t end = format.find('}', pos);
//
//            std::string name = format.substr(pos + 1, end - pos - 1);
//            void* local = //vm->locals[name];
//            switch (vm->mm.GetType(local))
//            {
//            case TY_INT:
//                formatted << *reinterpret_cast<int*>(local);
//                break;
//            case TY_STRING:
//                formatted << reinterpret_cast<char*>(local);
//                break;
//            default:
//                vm->statusCode = VM_ERROR;
//                vm->running = false;
//                break;
//            }
//
//            offset = end + 1;
//        }
//
//        Push_String(vm, formatted.str());
//    }
//}

static void Op_Increment(VirtualMachine* vm)
{
    assert(vm->statusCode == VM_OK);
    
    if (vm->stack.size() == 0)
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
        return;
    }

    if (vm->tracing) { vm->trace.push_back(OP_INCREMENT); }

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

static void Op_Decrement(VirtualMachine* vm)
{
    assert(vm->statusCode == VM_OK);
    
    if (vm->stack.size() == 0)
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
        return;
    }

    if (vm->tracing) { vm->trace.push_back(OP_DECREMENT); }
    
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

static void Op_Jump(VirtualMachine* vm)
{
    unsigned int pc = vm->programCounter;
    const char type = Read_Byte(vm->program, &vm->programCounter);
    const short offset = Read_Short(vm->program, &vm->programCounter);
    bool branchDir = false;
    
    if (vm->tracing)
    {
	vm->trace.push_back(OP_JUMP);
	vm->trace.push_back(type);
	vm->trace.push_back(static_cast<unsigned char>(offset & 0xFF));
	vm->trace.push_back(static_cast<unsigned char>((offset >> 8) & 0xFF));
    }

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
        RecordBranch(vm->main, pc, branchDir);
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

    if (vm->tracing) { vm->trace.push_back(OP_CMP); }

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
    vm->mm.Reset();
    vm->programCounter = 0;
    vm->tracing = false;
    vm->stackBounds = 0;
    vm->errorCode = 0;
    vm->flags = 0;
    vm->instructionsExecuted = 0;
    vm->timeout = 0;
    vm->callNumArgs = 0;
    vm->resumeCode = VM_OK;
    while (!vm->stack.empty()) { vm->stack.pop(); }
    while (!vm->frames.empty()) { vm->frames.pop(); }
    vm->locals.clear();
}

static void ScanFunctions(VirtualMachine* vm, unsigned char* program)
{
    const int numBlocks = Read_Int(program, &vm->programCounter);
    const int numEntries = Read_Int(program, &vm->programCounter);
    for (int i = 0; i < numBlocks; i++)
    {
        const int functionOffset = Read_Int(program, &vm->programCounter);
        const int functionSize = Read_Int(program, &vm->programCounter);
        const std::string name = Read_String(program, &vm->programCounter);
        const int numArgs = Read_Int(program, &vm->programCounter);
        
        Block func = {};
        func.numArgs = numArgs;
        func.info.pc = functionOffset;
        func.info.size = functionSize;
        func.info.name = name;

        for (int i = 0; i < numArgs; i++)
        {
            const std::string name = Read_String(program, &vm->programCounter);
            func.info.parameters.push_back(name);
        }

        const int numFields = Read_Int(program, &vm->programCounter);
        for (int i = 0; i < numFields; i++)
        {
            const std::string name = Read_String(program, &vm->programCounter);
            func.info.locals.push_back(name);
        }

        vm->blocks.push_back(func);
    }

    vm->functions.resize(numEntries);
    for (int i = 0; i < numEntries; i++)
    {
        const int id = Read_Int(program, &vm->programCounter);
        const int blk = Read_Int(program, &vm->programCounter);
        const std::string name = Read_String(program, &vm->programCounter);

        auto& entry = vm->functions[id];
        entry.id = id;
        entry.blk = blk;
        entry.name = name;
    }

    vm->programOffset = vm->programCounter;
}

static void ScanDebugData(VirtualMachine* vm, unsigned char* debugData)
{
    if (debugData)
    {
        unsigned int size = 0;
        for (auto& function : vm->blocks)
        {
            size += function.info.size;
        }

        vm->debugLines = new int[size];
        std::memset(vm->debugLines, 0, size);

        unsigned int pos = 0;
        const int numLines = Read_Int(debugData, &pos);
        for (int i = 0; i < numLines; i++)
        {
            const int pc = Read_Int(debugData, &pos);
            const int line = Read_Int(debugData, &pos);
            vm->debugLines[pc] = line;
        }
    }
}

static void StartVM(VirtualMachine* vm)
{
    vm->running = true;
    vm->statusCode = vm->resumeCode;
    vm->resumeCode = VM_OK;
    vm->startTime = vm->clock.now();
    vm->instructionsExecuted = 0;
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

static int ResumeScript2(VirtualMachine* vm)
{
    StartVM(vm);

    while (vm->running)
    {
        //const auto& lineIt = vm->debugLines.find(vm->programCounter - vm->programOffset);
        //if (lineIt != vm->debugLines.end())
        //{
        //    vm->debugLine = lineIt->second;
        //}
        if (vm->debugLines)
        {
            vm->debugLine = vm->debugLines[vm->programCounter - vm->programOffset];
        }

	const unsigned char op = vm->program[vm->programCounter++];

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
	    if (vm->tracing) { vm->trace.push_back(OP_DONE); }
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
        //case OP_FORMAT:
        //    Op_Format(vm);
        //    break;
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

    if (vm->statusCode == VM_OK && vm->tracing)
    {
	// End tracing and JIT compile
        vm->tracing = false;
	vm->jit_trace = vm->jit.jit_compile_trace(vm->jit_instance, vm, vm->trace.data(), int(vm->trace.size()));
    }

    return vm->statusCode;
}

int SunScript::RunScript(VirtualMachine* vm)
{
    return RunScript(vm, std::chrono::duration<int, std::nano>::zero());
}

/*static int RunJIT(VirtualMachine* vm)
{
    const std::string cacheKey = "@main_";

    void* data = vm->jit.jit_search_cache(vm->jit_instance, cacheKey);
    if (!data)
    {
        FunctionInfo* info = nullptr;
        for (int i = 0; i < vm->blocks.size(); i++)
        {
            if (vm->blocks[i].info.name == "main")
            {
                info = &vm->blocks[i].info;
                break;
            }
        }

        if (!info)
        {
            return VM_ERROR;
        }

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
    else if (status == VM_YIELDED)
    {
        vm->flags = VM_FLAGS_YIELD_JIT;
    }
        return status;
    }

    return VM_DEOPTIMIZE;
}*/

int SunScript::LoadProgram(VirtualMachine* vm, unsigned char* program, unsigned char* debugData)
{
    vm->program = program;
    vm->blocks.clear();
    vm->functions.clear();
    delete[] vm->debugLines;
    ScanFunctions(vm, program);
    ScanDebugData(vm, debugData);

    FunctionInfo* info = nullptr;
    for (int i = 0; i < vm->blocks.size(); i++)
    {
        if (vm->blocks[i].info.name == "main")
        {
            info = &vm->blocks[i].info;
            break;
        }
    }

    if (!info)
    {
        return VM_ERROR;
    }

    vm->main = info;

    return VM_OK;
}

int SunScript::LoadProgram(VirtualMachine* vm, unsigned char* program)
{
    return LoadProgram(vm, program, nullptr);
}

int SunScript::RunScript(VirtualMachine* vm, std::chrono::duration<int, std::nano> timeout)
{
    ResetVM(vm);
    
    if (vm->jit_trace)
    {
	return vm->jit.jit_execute(vm->jit_instance, vm->jit_trace);
    }

    // Convert timeout to nanoseconds (or whatever it may be specified in)
    vm->timeout = std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout).count();

    vm->programCounter = vm->main->pc + vm->programOffset;
    vm->locals.resize(vm->main->locals.size() + vm->main->parameters.size());
    vm->main->counter++;

    if (vm->main->counter == 100 && vm->jit_instance)
    {
	// Run with trace
	vm->tracing = true;
	vm->trace.clear();
    }

    return ResumeScript2(vm);
}

int SunScript::ResumeScript(VirtualMachine* vm)
{
    if (vm->jit_instance && vm->jit_trace)
    {
	const int status = vm->jit.jit_resume(vm->jit_instance);
        return status;
    }

    return ResumeScript2(vm);
}

void SunScript::PushReturnValue(VirtualMachine* vm, const std::string& value)
{
    if (vm->statusCode == VM_OK)
    {
        Push_String(vm, value.c_str());
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
    if (vm->stack.size() == 0) { return VM_ERROR; }
    
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
    if (vm->stack.size() == 0) { return VM_ERROR; }

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
    Push_String(vm, param.c_str());
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

int SunScript::FindFunction(VirtualMachine* vm, int id, FunctionInfo** info)
{
    if (id >= 0 && id < vm->functions.size())
    {
        const auto& func = vm->functions[id];

	if (func.blk != -1)
	{
            *info = &vm->blocks[func.blk].info;
	}
        return VM_OK;
    }

    return VM_ERROR;
}

const char* SunScript::FindFunctionName(VirtualMachine* vm, int id)
{
    if (id >= 0 && id < vm->functions.size())
    {
	return vm->functions[id].name.c_str();
    }

    return nullptr;
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

int SunScript::CreateFunction(Program* program)
{
    return program->numFunctions++;
}

ProgramBlock* SunScript::CreateProgramBlock(bool topLevel, const std::string& name, int numArgs)
{
    ProgramBlock* block = new ProgramBlock();
    block->numArgs = numArgs;
    block->numLines = 0;
    block->topLevel = topLevel;
    block->name = name;
    block->numLabels = 0;
    block->id = -1;

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
    const int size = int(program->data.size() + program->functions.size() + program->entries.size() + sizeof(std::int32_t) * 2);
    *programData = new unsigned char[size];

    const size_t numBlocks = program->blocks.size();
    (*programData)[0] = (unsigned char)(numBlocks & 0xFF);
    (*programData)[1] = (unsigned char)((numBlocks >> 8) & 0xFF);
    (*programData)[2] = (unsigned char)((numBlocks >> 16) & 0xFF);
    (*programData)[3] = (unsigned char)((numBlocks >> 24) & 0xFF);

    const int numFunctions = program->numFunctions;
    (*programData)[4] = (unsigned char)(numFunctions & 0xFF);
    (*programData)[5] = (unsigned char)((numFunctions >> 8) & 0xFF);
    (*programData)[6] = (unsigned char)((numFunctions >> 16) & 0xFF);
    (*programData)[7] = (unsigned char)((numFunctions >> 24) & 0xFF);

    std::memcpy(*programData + sizeof(std::int32_t) * 2, program->functions.data(), program->functions.size());
    std::memcpy(*programData + program->functions.size() + sizeof(std::int32_t) * 2, program->entries.data(), program->entries.size());
    std::memcpy(*programData + program->functions.size() + program->entries.size() + sizeof(std::int32_t) * 2, program->data.data(), program->data.size());
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
            if (func.blk != -1)
            {
                auto& blk = vm->blocks[func.blk];
                ss << (blk.info.pc + vm->programOffset) << " " << blk.info.name << "(" << blk.numArgs << ")" << std::endl;
            }
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
        case OP_POP:
            ss << "OP_POP " << int(Read_Byte(programData, &vm->programCounter)) << std::endl;
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
            ss << "OP_PUSH_LOCAL " << int(Read_Byte(programData, &vm->programCounter)) << std::endl;
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
            ss << "OP_CALL " << int(Read_Byte(programData, &vm->programCounter)) << " " << Read_Int(programData, &vm->programCounter) << std::endl;
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

void SunScript::EmitInternalFunction(Program* program, ProgramBlock* blk, int func)
{
    EmitInt(program->entries, func);
    EmitInt(program->entries, blk->id);
    EmitString(program->entries, blk->name);
}

void SunScript::EmitExternalFunction(Program* program, int func, const std::string& name)
{
    EmitInt(program->entries, func);
    EmitInt(program->entries, -1);
    EmitString(program->entries, name);
}

void SunScript::FlushBlocks(Program* program)
{
    for (int i = 0; i < program->blocks.size(); i++)
    {
        auto& block = program->blocks[i];

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
    }
}

void SunScript::EmitProgramBlock(Program* program, ProgramBlock* block)
{
    block->id = int(program->blocks.size());
    program->blocks.push_back(block);
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
    program->fields.push_back(name);
}

void SunScript::EmitSet(ProgramBlock* program, const unsigned char local, int value)
{
    program->data.push_back(OP_SET);
    program->data.push_back(TY_INT);
    program->data.push_back(local);
    EmitInt(program->data, value);
}

void SunScript::EmitSet(ProgramBlock* program, const unsigned char local, const std::string& value)
{
    program->data.push_back(OP_SET);
    program->data.push_back(TY_STRING);
    program->data.push_back(local);
    EmitString(program->data, value);
}

void SunScript::EmitPushLocal(ProgramBlock* program, unsigned char local)
{
    program->data.push_back(OP_PUSH_LOCAL);
    program->data.push_back(local);
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

void SunScript::EmitPop(ProgramBlock* program, unsigned char local)
{
    program->data.push_back(OP_POP);
    program->data.push_back(local);
}

void SunScript::EmitPop(ProgramBlock* program)
{
    program->data.push_back(OP_POP_DISCARD);
}

void SunScript::EmitYield(ProgramBlock* program, int func, unsigned char numArgs)
{
    program->data.push_back(OP_YIELD);
    program->data.push_back(numArgs);
    EmitInt(program->data, func);
}

void SunScript::EmitCall(ProgramBlock* program, int func, unsigned char numArgs)
{
    program->data.push_back(OP_CALL);
    program->data.push_back(numArgs);
    EmitInt(program->data, func);
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
