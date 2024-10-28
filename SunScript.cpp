#include "SunScript.h"
#include <fstream>
#include <stack>
#include <unordered_map>
#include <sstream>
#include <iostream>
#include <cstring>
#include <assert.h>
#include <array>
#include <cmath>
#include <algorithm>

using namespace SunScript;

#define VM_ALIGN_16(x) ((x + 0xf) & ~(0xf)) 

namespace SunScript
{
//===================
// MemoryManager
//===================

    MemoryManager::MemoryManager() {}

    void* MemoryManager::New(uint64_t size, char type)
    {
        const uint64_t totalSize = VM_ALIGN_16(size + sizeof(Header));
        if (_segments.size() == 0)
        {
            // Allocate 8KB to start with
            auto& sg = _segments.emplace_back();
            sg._pos = 0;
            sg._totalSize = 8 * 1024;
            sg._memory = new unsigned char[sg._totalSize];
            std::memset(sg._memory, 0, sg._totalSize);
        }
        else if (_segments[_segments.size() - 1]._pos + totalSize >= _segments[_segments.size() - 1]._totalSize)
        {
            // Create a new segment twice the size of the last
            uint64_t size = _segments[_segments.size() - 1]._totalSize * 2;
            auto& sg = _segments.emplace_back();
            sg._pos = 0;
            sg._totalSize = size;
            sg._memory = new unsigned char[sg._totalSize];
            std::memset(sg._memory, 0, sg._totalSize);
        }

        auto& sg = _segments[_segments.size() - 1];

        Header* header = reinterpret_cast<Header*>(sg._memory + sg._pos);
        header->_refCount = 1ULL;
        header->_size = totalSize;
        header->_type = type;
        sg._pos += sizeof(Header);
        unsigned char* mem = sg._memory + sg._pos;
        sg._pos += totalSize;
        return mem;
    }

    void MemoryManager::Dump()
    {
        // Dump memory to the console.
        std::cout << std::hex << std::endl;

        for (auto& sg : _segments)
        for (int i = 0; i < sg._pos; i++)
        {
            std::cout << int(sg._memory[i]) << " ";
            if ((i + 1) % 16 == 0)
            {
                std::cout << std::endl;
            }
        }

        std::cout << std::dec;
    }

    void MemoryManager::AddRef(void* mem)
    {
        for (auto& sg : _segments)
        {
            unsigned char* end = sg._memory + sg._totalSize;
            if (mem >= sg._memory && mem < end)
            {
                Header* header = reinterpret_cast<Header*>((char*)mem - sizeof(Header));
                header->_refCount++;
                break;
            }
        }   
    }

    void MemoryManager::Release(void* mem)
    {
        for (auto& sg : _segments)
        {
            unsigned char* end = sg._memory + sg._totalSize;
            if (mem >= sg._memory && mem < end)
            {
                Header* header = reinterpret_cast<Header*>((char*)mem - sizeof(Header));
                header->_refCount--;

                // TODO: add to free list.
                break;
            }
        }
    }

    char MemoryManager::GetType(void* mem) const
    {
        for (auto& sg : _segments)
        {
            unsigned char* end = sg._memory + sg._totalSize;
            if (mem >= sg._memory && mem < end)
            {
                Header* header = reinterpret_cast<Header*>((char*)mem - sizeof(Header));
                return header->_type;
            }
        }

        return TY_VOID;
    }

    char MemoryManager::GetTypeUnsafe(void* mem)
    {
        Header* header = reinterpret_cast<Header*>((char*)mem - sizeof(Header));
        return header->_type;
    }

    void MemoryManager::Reset()
    {
        for (auto& seg : _segments)
        {
            seg._pos = 0;
        }
    }

    MemoryManager::~MemoryManager()
    {
        for (auto& seg : _segments)
        {
            delete[] seg._memory;
            seg._memory = nullptr;
        }
    }

    //===================
    // Snapshot
    //===================

    Snapshot::Snapshot(int numValues, MemoryManager* mm)
        :
        _numValues(numValues),
        _index(0)
    {
        _values = reinterpret_cast<Value*>(mm->New(numValues * sizeof(Value), TY_OBJECT));
    }

    void Snapshot::Add(int ref, int64_t value)
    {
        Snapshot::Value& val = _values[_index++];
        val._ref = ref;
        val._value = value;
    }

    void Snapshot::Get(int idx, int* ref, int64_t* value) const
    {
        const Snapshot::Value& val = _values[idx];
        *ref = val._ref;
        *value = val._value;
    }

    //=====================
    // ActivationRecord
    //=====================

    ActivationRecord::ActivationRecord(int numItems, MemoryManager* mm)
    {
        _buffer = reinterpret_cast<unsigned char*>(mm->New(numItems * 16, TY_OBJECT));
    }

    void ActivationRecord::Add(int id, int type, void* data)
    {
        const int pos = id * 16;
        *(int64_t*)(_buffer + pos) = id;
        switch (type)
        {
        case TY_INT:
            *(int64_t*)(_buffer + pos + 8) = *(int64_t*)data;
            break;
        case TY_STRING:
            *(char**)(_buffer + pos + 8) = (char*)data;
            break;
        }
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

//===========================

    struct LoopStat
    {
        unsigned int pc;
        int offset;
    };

    struct ReturnStat
    {
        unsigned int pc;
        unsigned int type;
        unsigned int count;
    };

    struct BranchStat
    {
        unsigned int pc;
        unsigned int trueCount;
        unsigned int falseCount;
    };

    struct Statistics
    {
        unsigned int retCount;
        unsigned int branchCount;
        unsigned int loopCount;
        ReturnStat retStats[8];
        BranchStat branchStats[8];
        LoopStat loopStats[8];
    };

    struct FunctionInfo
    {
        unsigned int pc;
        unsigned int size;
        unsigned int counter;
        unsigned int depth;
        Statistics stats;
        std::string name;
        std::vector<std::string> parameters;
        std::vector<std::string> locals;
        std::vector<int> labels;
    };

//============================

    constexpr unsigned int SN_NEEDED = 0x1; // snapshot needed
    constexpr unsigned int MAX_TRACES = 32;
    
    constexpr int HOT_COUNT = 100;          // the number of invokes of script to consider the script 'hot'
    constexpr int MIN_TRACE_SIZE = 12;      // the minimum size of a trace to compile it
    constexpr int MAX_TRACE_SIZE = 200;     // the maximum size of a trace

    struct StackFrame
    {
        int debugLine;
        int returnAddress;
        int stackBounds;
        int localBounds;
        bool discard;
        FunctionInfo* func;
        std::string functionName;

        StackFrame() :
            debugLine(0),
            returnAddress(0),
            stackBounds(0),
            localBounds(0),
            func(nullptr),
            discard(false)
        {}
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

        Function() :
            id(0), blk(0)
        {}
    };

    struct Code
    {
        unsigned char id;
        int flags;
    };

    struct InsData
    {
        unsigned char id;
        union
        {
            int constant;
            int call;
        };
        union
        {
            short offset;
            unsigned char snapCount;
            unsigned char type;
            unsigned char args;
        };
        union
        {
            unsigned char jump;
            char snapId;
            unsigned char local;
        };
    };

    struct TraceNode
    {
        TraceNode* left;        // left node if applicable
        TraceNode* right;       // right node if applicable
        InsData data;
        int flags;              // TN_FLAGS
        int ref;                // ref
        int type;               // the type of the result
        int pc;
    };

    struct TraceGuard
    {
        TraceNode* node;
        unsigned int pc;
    };

    struct TraceLocal
    {
        TraceNode* maxRef; // the last reference within the loop
        TraceNode* minRef; // the last reference before the loop
    };

    struct TraceLoop
    {
        TraceNode* startRef;            // start reference
        TraceNode* endRef;              // end reference
        unsigned int start;             // start program
        unsigned int end;               // end program
        bool active;
        std::vector<TraceGuard> guards;
        std::vector<TraceLocal> locals;

        TraceLoop() :
            startRef(0),
            endRef(0),
            start(0),
            end(0),
            active(false)
        {}
    };

    struct TraceSnapshot
    {
        struct Local
        {
            TraceNode* ref;
            int index;  // index to locals
        };

        unsigned int pc;
        std::vector<StackFrame> frames;
        std::vector<Local> locals;

        TraceSnapshot()
            :
            pc(0)
        {}
    };

    struct Trace
    {
        MemoryManager mm;
        std::vector<TraceNode*> nodes;
        std::vector<TraceNode*> locals;     // local -> ref mapping
        std::vector<TraceNode*> refs;       // stack of refs
        std::vector<TraceSnapshot> snaps;
        std::vector<unsigned char> trace;   // completed trace
        TraceLoop loop;                     // current loop
        int ref;                            // current ref index
        int flags;
        int pc;                             // the pc point where
                                            // the trace starts
        int id;                             // trace id
        void* jit_trace;

        Trace()
            :
            ref(0),
            flags(0),
            pc(0),
            id(0),
            jit_trace(nullptr)
        {}

        void Reset()
        {
            mm.Reset();
            *this = Trace();
        }
    };

    struct TraceTree
    {
        std::array<Trace, MAX_TRACES> traces;
        int numTraces;
        Trace* curTrace;

        TraceTree()
            :
            numTraces(0),
            curTrace(nullptr)
        {}
    };

    struct VirtualMachine
    {
        unsigned char* program;
        unsigned int programCounter;        // the position in the current program
        unsigned int programInstruction;    // the position of the start of the current instruction
        unsigned int programOffset;         // offset in program data where the program starts
        int* debugLines;
        int buildFlags;
        bool running;
        bool tracing;
        bool tracingPaused;
        bool hot;
        int statusCode;
        int errorCode;
        int resumeCode;
        int flags;
        std::int64_t timeout;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock clock;
        int instructionsExecuted;
        int debugLine;
        bool discard;       // whether to discard call return values
        int stackBounds;
        int localBounds;
        int callNumArgs;
        int comparer;
        MemoryManager mm;
        FunctionInfo* main;
        std::string callName;
        std::vector<StackFrame> frames;
        Stack stack;
        std::vector<Block> blocks;
        std::vector<Function> functions;
        std::vector<void*> locals;
        std::vector<unsigned char> traceConstants;
        TraceTree tt;
        int (*handler)(VirtualMachine* vm);
        Jit jit;
        void* jit_instance;
        void* _userData;
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
        int buildFlags;
    };
}

//===================

inline static void RecordLoop(FunctionInfo* info, unsigned int pc, int offset)
{
    const unsigned int numCount = sizeof(info->stats.loopStats) / sizeof(LoopStat);
    for (unsigned int i = 0; i < numCount; i++)
    {
        auto& loop = info->stats.loopStats[i];
        if (i < info->stats.loopCount)
        {
            if (loop.pc == pc)
            {
                loop.offset = offset;
                break;
            }
        }
        else
        {
            loop.pc = pc;
            loop.offset = offset;
            info->stats.loopCount++;
            break;
        }
    }
}

inline static void RecordBranch(FunctionInfo* info, unsigned int pc, bool branchDirection)
{
    const unsigned int numCount = sizeof(info->stats.branchStats) / sizeof(BranchStat);
    for (unsigned int i = 0; i < numCount; i++)
    {
        auto& branch = info->stats.branchStats[i];
        if (i < info->stats.branchCount)
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
            info->stats.branchCount++;
            break;
        }
    }
}

inline static void RecordReturn(FunctionInfo* info, unsigned int pc, char type)
{
    const unsigned int numCount = sizeof(info->stats.retStats) / sizeof(ReturnStat);
    for (unsigned int i = 0; i < numCount; i++)
    {
        auto& ret = info->stats.retStats[i];
        if (i < info->stats.retCount)
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
            info->stats.retCount++;
            break;
        }
    }
}

//============================
// TRACING IR (SSA form)
//============================

constexpr int INS_LEFT = 0x1;
constexpr int INS_RIGHT = 0x2;
constexpr int INS_CONSTANT = 0x4;
constexpr int INS_JUMP = 0x8;
constexpr int INS_OFFSET = 0x10;
constexpr int INS_SNAP = 0x20;
constexpr int INS_TYPE = 0x40;
constexpr int INS_CALL = 0x80;
constexpr int INS_ARGS = 0x100;
constexpr int INS_LOCAL = 0x200;

// Instructions should be ordered by the IR value.

static std::vector<Code> Instructions = {
    Code({ IR_LOAD_INT, INS_CONSTANT }),
    Code({ IR_LOAD_STRING, INS_CONSTANT }),
    Code({ IR_LOAD_REAL, INS_CONSTANT }),
    Code({ IR_LOAD_INT_LOCAL, INS_LOCAL }),
    Code({ IR_LOAD_STRING_LOCAL, INS_LOCAL }),
    Code({ IR_LOAD_REAL_LOCAL, INS_LOCAL }),
    Code({ IR_CALL, INS_CALL | INS_ARGS }),
    Code({ IR_YIELD, INS_CALL | INS_ARGS }),
    Code({ IR_INT_ARG, INS_LEFT }),
    Code({ IR_STRING_ARG, INS_LEFT }),
    Code({ IR_REAL_ARG, INS_LEFT }),
    Code({ IR_INCREMENT_INT, INS_LEFT }),
    Code({ IR_DECREMENT_INT, INS_LEFT }),
    Code({ IR_INCREMENT_REAL, INS_LEFT }),
    Code({ IR_DECREMENT_REAL, INS_LEFT }),
    Code({ IR_ADD_INT, INS_LEFT | INS_RIGHT }),
    Code({ IR_SUB_INT, INS_LEFT | INS_RIGHT }),
    Code({ IR_MUL_INT, INS_LEFT | INS_RIGHT }),
    Code({ IR_DIV_INT, INS_LEFT | INS_RIGHT }),
    Code({ IR_UNARY_MINUS_INT, INS_LEFT }),
    Code({ IR_ADD_REAL, INS_LEFT | INS_RIGHT }),
    Code({ IR_SUB_REAL, INS_LEFT | INS_RIGHT }),
    Code({ IR_MUL_REAL, INS_LEFT | INS_RIGHT }),
    Code({ IR_DIV_REAL, INS_LEFT | INS_RIGHT }),
    Code({ IR_UNARY_MINUS_REAL, INS_LEFT }),
    Code({ IR_APP_INT_STRING, INS_LEFT | INS_RIGHT }),
    Code({ IR_APP_STRING_INT, INS_LEFT | INS_RIGHT }),
    Code({ IR_APP_STRING_STRING, INS_LEFT | INS_RIGHT }),
    Code({ IR_APP_STRING_REAL, INS_LEFT | INS_RIGHT }),
    Code({ IR_APP_REAL_STRING, INS_LEFT | INS_RIGHT }),
    Code({ IR_GUARD, INS_JUMP }),
    Code({ IR_CMP_INT, INS_LEFT | INS_RIGHT }),
    Code({ IR_CMP_STRING, INS_LEFT | INS_RIGHT }),
    Code({ IR_CMP_REAL, INS_LEFT | INS_RIGHT }),
    Code({ IR_LOOPBACK, INS_JUMP | INS_OFFSET }),
    Code({ IR_LOOPSTART, 0 }),
    Code({ IR_LOOPEXIT, INS_JUMP | INS_OFFSET }),
    Code({ IR_PHI, INS_LEFT | INS_RIGHT }),
    Code({ IR_SNAP, INS_SNAP }),
    Code({ IR_UNBOX, INS_LEFT | INS_TYPE }),
    Code({ IR_CONV_INT_TO_REAL, INS_LEFT })
};

inline static TraceNode* TTOP(VirtualMachine* vm) { return vm->tt.curTrace->refs.at(vm->tt.curTrace->refs.size() - 1); }
inline static TraceNode* TNEXT(VirtualMachine* vm) { return vm->tt.curTrace->refs.at(vm->tt.curTrace->refs.size() - 2); }
inline static void TPOP(VirtualMachine* vm) { vm->tt.curTrace->refs.resize(vm->tt.curTrace->refs.size() - 1); }
inline static void TPOP2(VirtualMachine* vm) { vm->tt.curTrace->refs.resize(vm->tt.curTrace->refs.size() - 2); }
inline static void TPUSH(VirtualMachine* vm, TraceNode* node) { vm->tt.curTrace->refs.push_back(node); }
inline static void TINC(VirtualMachine* vm) { vm->tt.curTrace->ref++; }

inline static TraceNode* Trace_CreateNode(VirtualMachine* vm, const int type)
{
    TraceNode* node = reinterpret_cast<TraceNode*>(vm->tt.curTrace->mm.New(sizeof(TraceNode), TY_OBJECT));
    std::memset(node, 0, sizeof(TraceNode));
    node->ref = vm->tt.curTrace->ref;
    node->flags = 0;
    node->pc = vm->programCounter;
    node->type = type;
    vm->tt.curTrace->nodes.push_back(node);

    return node;
}

inline static TraceNode* Trace_Instruction(VirtualMachine* vm, const int type, const InsData& ins)
{
    TraceNode* node = reinterpret_cast<TraceNode*>(vm->tt.curTrace->mm.New(sizeof(TraceNode), TY_OBJECT));
    std::memset(node, 0, sizeof(TraceNode));
    node->ref = vm->tt.curTrace->ref;
    node->flags = 0;
    node->pc = vm->programCounter;
    node->type = type;
    node->data = ins;
    vm->tt.curTrace->nodes.push_back(node);

    return node;
}

inline static void Trace_Suspend(VirtualMachine* vm)
{
    Trace* trace = vm->tt.curTrace;
    vm->tracingPaused = true;
}

inline static void Trace_Start(VirtualMachine* vm)
{
    vm->tt.curTrace = &vm->tt.traces[vm->tt.numTraces++];
    vm->tt.curTrace->ref = 0;
    vm->tt.curTrace->flags = SN_NEEDED;
    vm->tt.curTrace->pc = vm->programInstruction;
    vm->tt.curTrace->refs.clear();
    vm->tt.curTrace->locals.resize(vm->locals.size());
    vm->tt.curTrace->snaps.clear();
    vm->tt.curTrace->nodes.clear();
    vm->tt.curTrace->id = vm->tt.numTraces - 1;

    vm->tracing = true;
    vm->tracingPaused = false;
    vm->traceConstants.clear();
    
    // Setup locals for the new trace.
    for (size_t i = 0; i < vm->locals.size(); i++)
    {
        void* local = vm->locals[i];
        if (local)
        {
            const int type = vm->mm.GetType(local);
            TraceNode* node = nullptr;
            switch (type)
            {
                case TY_STRING:
                    node = Trace_Instruction(vm, type, {  .id = IR_LOAD_STRING_LOCAL, .local = static_cast<unsigned char>(i) });
                    break;
                case TY_INT:
                    node = Trace_Instruction(vm, type, { .id = IR_LOAD_INT_LOCAL, .local = static_cast<unsigned char>(i) });
                    break;
                case TY_REAL:
                    node = Trace_Instruction(vm, type, { .id = IR_LOAD_REAL_LOCAL, .local = static_cast<unsigned char>(i) });
                    break;
                default:
                    // Unable to start the trace.
                    vm->tracing = false;
                    break;
            }
            vm->tt.curTrace->locals[i] = node;
            vm->tt.curTrace->ref++;
        }
    }
}

inline static void Trace_Abort(VirtualMachine* vm)
{
    vm->tracing = false;

    Trace* trace = vm->tt.curTrace;
    trace->Reset();

    vm->tt.numTraces--;
}

inline static void Trace_Restore(VirtualMachine* vm, Trace* trace)
{
    vm->tt.curTrace = trace;
}

inline static void Trace_Real(VirtualMachine* vm, real val)
{
    unsigned char* data = reinterpret_cast<unsigned char*>(&val);
    for (int i = 0; i < SUN_REAL_SIZE; i+=4)
    {
        vm->tt.curTrace->trace.push_back(data[i]);
        vm->tt.curTrace->trace.push_back(data[i + 1]);
        vm->tt.curTrace->trace.push_back(data[i + 2]);
        vm->tt.curTrace->trace.push_back(data[i + 3]);
    }
}

inline static void Trace_Int(VirtualMachine* vm, int val)
{
    vm->tt.curTrace->trace.push_back(static_cast<unsigned char>(val & 0xFF));
    vm->tt.curTrace->trace.push_back(static_cast<unsigned char>((val >> 8) & 0xFF));
    vm->tt.curTrace->trace.push_back(static_cast<unsigned char>((val >> 16) & 0xFF));
    vm->tt.curTrace->trace.push_back(static_cast<unsigned char>((val >> 24) & 0xFF));
}

inline static void Trace_String(VirtualMachine* vm, const char* str)
{
    while (*str != 0) {
        vm->tt.curTrace->trace.push_back(static_cast<unsigned char>(*str));
        str++;
    }
    vm->tt.curTrace->trace.push_back(0);
}

inline static void Trace_Constant(VirtualMachine* vm, real val)
{
    unsigned char* data = reinterpret_cast<unsigned char*>(&val);
    for (int i = 0; i < SUN_REAL_SIZE; i += 4)
    {
        vm->traceConstants.push_back(data[i]);
        vm->traceConstants.push_back(data[i + 1]);
        vm->traceConstants.push_back(data[i + 2]);
        vm->traceConstants.push_back(data[i + 3]);
    }
}

inline static void Trace_Constant(VirtualMachine* vm, int val)
{
    vm->traceConstants.push_back(static_cast<unsigned char>(val & 0xFF));
    vm->traceConstants.push_back(static_cast<unsigned char>((val >> 8) & 0xFF));
    vm->traceConstants.push_back(static_cast<unsigned char>((val >> 16) & 0xFF));
    vm->traceConstants.push_back(static_cast<unsigned char>((val >> 24) & 0xFF));
}

inline static void Trace_Constant(VirtualMachine* vm, const char* str)
{
    while (*str != 0) {
        vm->traceConstants.push_back(static_cast<unsigned char>(*str));
        str++;
    }
    vm->traceConstants.push_back(0);
}

static void Trace_Node(VirtualMachine* vm,  TraceNode* node)
{
    auto& ins = node->data;

    const auto& code = std::lower_bound(Instructions.begin(), Instructions.end(), ins.id, [](const Code& c, const int val) { return c.id < val; });
    assert(code->id == ins.id);

    vm->tt.curTrace->trace.push_back(ins.id);
    if ((code->flags & INS_LEFT) == INS_LEFT)
    {
        Trace_Int(vm, node->left->ref);
    }
    if ((code->flags & INS_RIGHT) == INS_RIGHT)
    {
        Trace_Int(vm, node->right->ref);
    }
    if ((code->flags & INS_JUMP) == INS_JUMP)
    {
        vm->tt.curTrace->trace.push_back(ins.jump & 0xFF);
    }
    if ((code->flags & INS_OFFSET) == INS_OFFSET)
    {
        vm->tt.curTrace->trace.push_back(ins.offset & 0xFF);
        vm->tt.curTrace->trace.push_back((ins.offset >> 8) & 0xFF);
    }
    if ((code->flags & INS_CONSTANT) == INS_CONSTANT)
    {
        Trace_Int(vm, ins.constant);
    }
    if ((code->flags & INS_LOCAL) == INS_LOCAL)
    {
        vm->tt.curTrace->trace.push_back(ins.local);
    }
    if ((code->flags & INS_CALL) == INS_CALL)
    {
        Trace_Int(vm, ins.call);
    }
    if ((code->flags & INS_ARGS) == INS_ARGS)
    {
        vm->tt.curTrace->trace.push_back(ins.args);
    }
    if ((code->flags & INS_TYPE) == INS_TYPE)
    {
        vm->tt.curTrace->trace.push_back(ins.type);
    }
    if ((code->flags & INS_SNAP) == INS_SNAP)
    {
        vm->tt.curTrace->trace.push_back(ins.snapId);
        vm->tt.curTrace->trace.push_back(ins.snapCount);

        for (auto& local : vm->tt.curTrace->snaps[ins.snapId].locals)
        {
            vm->tt.curTrace->trace.push_back(static_cast<unsigned char>(local.ref->ref));
        }
    }
}

inline static void Trace_LoadC_Int(VirtualMachine* vm, int val)
{
    TraceNode* node = Trace_Instruction(vm, TY_INT, {.id = IR_LOAD_INT, .constant = int(vm->traceConstants.size())});
    TPUSH(vm, node);
    TINC(vm);

    Trace_Constant(vm, val);
}

inline static void Trace_LoadC_Real(VirtualMachine* vm, real val)
{
    TraceNode* node = Trace_Instruction(vm, TY_REAL, { .id = IR_LOAD_REAL, .constant = int(vm->traceConstants.size()) });
    TPUSH(vm, node);
    TINC(vm);

    Trace_Constant(vm, val);
}

inline static void Trace_LoadC_String(VirtualMachine* vm, const char* str)
{
    TraceNode* node = Trace_Instruction(vm, TY_STRING, { .id = IR_LOAD_STRING, .constant = int(vm->traceConstants.size()) });
    TPUSH(vm, node);
    TINC(vm);

    Trace_Constant(vm, str);
}

inline static void Trace_Conv_Int_To_Real(VirtualMachine* vm, int index)
{
    TraceNode* node = Trace_Instruction(vm, TY_REAL, { .id = IR_CONV_INT_TO_REAL });
    node->left = vm->tt.curTrace->refs.at(vm->tt.curTrace->refs.size() + index);
    
    TPOP(vm);
    TPUSH(vm, node);
    TINC(vm);
}

inline static void Trace_Push_Local(VirtualMachine* vm, int local)
{
    // Push the ref currently associated with the local to the stack.
    vm->tt.curTrace->refs.push_back(vm->tt.curTrace->locals[local]);
}

inline static void Trace_Pop(VirtualMachine* vm, int local)
{
    // Update the local to point to the ref on the top of the stack.
    vm->tt.curTrace->locals[local] = TTOP(vm);
    TPOP(vm);
    vm->tt.curTrace->flags |= SN_NEEDED;
}

inline static void Trace_Pop_Discard(VirtualMachine* vm)
{
    // Discard the top of the stack.
    TPOP(vm);
}

inline static void Trace_Arg_String(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_VOID, { .id = IR_STRING_ARG });
    node->left = TTOP(vm);

    TPOP(vm);
    TINC(vm);
}

inline static void Trace_Arg_Int(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_VOID, { .id = IR_INT_ARG });
    node->left = TTOP(vm);

    TPOP(vm);
    TINC(vm);
}

inline static void Trace_Arg_Real(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_VOID, { .id = IR_REAL_ARG });
    node->left = TTOP(vm);

    TPOP(vm);
    TINC(vm);
}

inline static void Trace_Call(VirtualMachine* vm, int call, int args)
{
    TraceNode* node = Trace_Instruction(vm, TY_OBJECT, { .id = IR_CALL, .call = call, .args = static_cast<unsigned char>(args) });
    TINC(vm);
}

inline static void Trace_Yield(VirtualMachine* vm, int call, int args)
{
    TraceNode* node = Trace_Instruction(vm, TY_VOID, { .id = IR_YIELD, .call = call, .args = static_cast<unsigned char>(args) });
    TINC(vm);
}

inline static void Trace_Increment_Int(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_INT, { .id = IR_INCREMENT_INT });
    node->left = TTOP(vm);

    TPOP(vm);
    TPUSH(vm, node);
    TINC(vm);
}

inline static void Trace_Decrement_Int(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_INT, { .id = IR_DECREMENT_INT });
    node->left = TTOP(vm);

    TPOP(vm);
    TPUSH(vm, node);
    TINC(vm);
}

inline static void Trace_Add_Real(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_REAL, { .id = IR_ADD_REAL });
    node->left = TTOP(vm);
    node->right = TNEXT(vm);

    TPOP2(vm);
    TPUSH(vm, node);
    TINC(vm);
}

inline static void Trace_Sub_Real(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_REAL, { .id = IR_SUB_REAL });
    node->left = TTOP(vm);
    node->right = TNEXT(vm);

    TPOP2(vm);
    TPUSH(vm, node);
    TINC(vm);
}

inline static void Trace_Mul_Real(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_REAL, { .id = IR_MUL_REAL });
    node->left = TTOP(vm);
    node->right = TNEXT(vm);

    TPOP2(vm);
    TPUSH(vm, node);
    TINC(vm);
}

inline static void Trace_Div_Real(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_REAL, { .id = IR_DIV_REAL });
    node->left = TTOP(vm);
    node->right = TNEXT(vm);

    TPOP2(vm);
    TPUSH(vm, node);
    TINC(vm);
}

inline static void Trace_Add_Int(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_INT, { .id = IR_ADD_INT });
    node->left = TTOP(vm);
    node->right = TNEXT(vm);

    TPOP2(vm);
    TPUSH(vm, node);
    TINC(vm);
}

inline static void Trace_Sub_Int(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_INT, { .id = IR_SUB_INT });
    node->left = TTOP(vm);
    node->right = TNEXT(vm);

    TPOP2(vm);
    TPUSH(vm, node);
    TINC(vm);
}

inline static void Trace_Mul_Int(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_INT, { .id = IR_MUL_INT });
    node->left = TTOP(vm);
    node->right = TNEXT(vm);

    TPOP2(vm);
    TPUSH(vm, node);
    TINC(vm);
}

inline static void Trace_Div_Int(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_INT, { .id = IR_DIV_INT });
    node->left = TTOP(vm);
    node->right = TNEXT(vm);

    TPOP2(vm);
    TPUSH(vm, node);
    TINC(vm);
}

inline static void Trace_App_String_Int(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_STRING, { .id = IR_APP_STRING_INT });
    node->left = TNEXT(vm);
    node->right = TTOP(vm);

    TPOP2(vm);
    TPUSH(vm, node);
    TINC(vm);
}

inline static void Trace_App_Int_String(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_STRING, { .id = IR_APP_INT_STRING });
    node->left = TNEXT(vm);
    node->right = TTOP(vm);

    TPOP2(vm);
    TPUSH(vm, node);
    TINC(vm);
}

inline static void Trace_App_String_String(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_STRING, { .id = IR_APP_STRING_STRING });
    node->left = TNEXT(vm);
    node->right = TTOP(vm);

    TPOP2(vm);
    TPUSH(vm, node);
    TINC(vm);
}

inline static void Trace_App_Real_String(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_STRING, { .id = IR_APP_REAL_STRING });
    node->left = TNEXT(vm);
    node->right = TTOP(vm);

    TPOP2(vm);
    TPUSH(vm, node);
    TINC(vm);
}

inline static void Trace_App_String_Real(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_STRING, { .id = IR_APP_STRING_REAL });
    node->left = TNEXT(vm);
    node->right = TTOP(vm);

    TPOP2(vm);
    TPUSH(vm, node);
    TINC(vm);
}

inline static void Trace_LoopStart(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_VOID, { .id = IR_LOOPSTART });
    TINC(vm);
}

inline static void Trace_LoopBack(VirtualMachine* vm, int jump, short offset)
{
    TraceNode* node = Trace_Instruction(vm, TY_VOID, { .id = IR_LOOPBACK, .offset = offset, .jump = static_cast<unsigned char>(jump) });
    TINC(vm);
}

inline static void Trace_PromoteGuard(VirtualMachine* vm, TraceNode* node, TraceNode* exit)
{
    assert(node->data.id == IR_GUARD);
    node->data.id = IR_LOOPEXIT;
    node->data.offset = short(exit->ref - node->ref);
}

inline static void Trace_Guard(VirtualMachine* vm, int jump)
{
    TraceNode* node = Trace_Instruction(vm, TY_VOID, { .id = IR_GUARD, .jump = static_cast<unsigned char>(jump) });
    vm->tt.curTrace->ref++;
}

inline static void Trace_Cmp_Int(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_VOID, { .id = IR_CMP_INT });
    node->left = TNEXT(vm);
    node->right = TTOP(vm);

    TPOP2(vm);
    vm->tt.curTrace->ref++;
}

inline static void Trace_Cmp_Real(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_VOID, { .id = IR_CMP_REAL });
    node->left = TNEXT(vm);
    node->right = TTOP(vm);

    TPOP2(vm);
    TINC(vm);
}

inline static void Trace_Cmp_String(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_VOID, { .id = IR_CMP_STRING });
    node->left = TTOP(vm);
    node->right = TNEXT(vm);

    TPOP2(vm);
    TINC(vm);
}

inline static void Trace_Unary_Minus_Real(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_REAL, { .id = IR_UNARY_MINUS_REAL });
    node->left = TTOP(vm);

    TPOP(vm);
    TPUSH(vm, node);
    TINC(vm);
}

inline static void Trace_Unary_Minus_Int(VirtualMachine* vm)
{
    TraceNode* node = Trace_Instruction(vm, TY_INT, { .id = IR_UNARY_MINUS_INT });
    node->left = TTOP(vm);

    TPOP(vm);
    TPUSH(vm, node);
    TINC(vm);
}

inline static void Trace_Done(VirtualMachine* vm)
{
    // Take a snapshot:
    // However this is end of the program, so no variables
    // need to be captured we just need need to report
    // it is the end.

    TraceSnapshot& snap = vm->tt.curTrace->snaps.emplace_back();
    snap.pc = vm->programInstruction;

    TraceNode* node = Trace_Instruction(vm, TY_VOID, { .id = IR_SNAP,
        .snapCount = static_cast<unsigned char>(snap.locals.size()),
        .snapId = char(vm->tt.curTrace->snaps.size() - 1) });
    TINC(vm);
}

inline static void Trace_Snap(VirtualMachine* vm)
{
    if ((vm->tt.curTrace->flags & SN_NEEDED) == SN_NEEDED)
    {
        vm->tt.curTrace->flags &= ~SN_NEEDED;

        // Take a snapshot:
        // We need to record all variables as we don't know
        // which ones we need down a different branch.

        TraceSnapshot& snap = vm->tt.curTrace->snaps.emplace_back();
        snap.pc = vm->programInstruction;
        for (auto& frame : vm->frames)
        {
            snap.frames.emplace_back(frame);
        }
        
        std::vector<bool> usedRefs;
        usedRefs.resize(vm->tt.curTrace->ref);

        for (size_t i = 0; i < vm->tt.curTrace->locals.size(); i++)
        {
            TraceNode* node = vm->tt.curTrace->locals[i];

            if (node && !usedRefs[node->ref])
            {
                auto& local = snap.locals.emplace_back();
                local.index = int(i);
                local.ref = node;
                usedRefs[node->ref] = true;
            }
        }

        TraceNode* node = Trace_Instruction(vm, TY_VOID, { .id = IR_SNAP,
            .snapCount = static_cast<unsigned char>(snap.locals.size()),
            .snapId = char(vm->tt.curTrace->snaps.size() - 1)} );

        TINC(vm);
    }
}

inline static void Trace_Unbox(VirtualMachine* vm, int type)
{
    TraceNode* left = vm->tt.curTrace->nodes[vm->tt.curTrace->nodes.size() - 1];

    TraceNode* n = Trace_Instruction(vm, type, { .id = IR_UNBOX, .type = static_cast<unsigned char>(type) });

    while (left->data.id != IR_CALL)
    {
        left = vm->tt.curTrace->nodes[left->ref - 1];
    }
    n->left = left;

    TINC(vm);
}

inline static void Trace_ReturnValue(VirtualMachine* vm, int type)
{
    Trace_Snap(vm);
    Trace_Unbox(vm, type);

    // Push the ref which has pushed onto the stack by the function call.
    vm->tt.curTrace->refs.push_back(vm->tt.curTrace->nodes[vm->tt.curTrace->ref - 1]);
}

static void Trace_Finalize(VirtualMachine* vm)
{
    //
    // Process the nodes and copy the data over to a new buffer.
    // The nodes may have be rearranged and thus the data need reordering.

    const size_t size = vm->traceConstants.size() + sizeof(int);
    const int constantSize = int(vm->traceConstants.size());

    vm->tt.curTrace->trace.resize(size);
    
    size_t pos = 0;
    std::memcpy(vm->tt.curTrace->trace.data(), &constantSize, sizeof(int));
    pos += sizeof(int);
    std::memcpy(vm->tt.curTrace->trace.data() + pos, vm->traceConstants.data(), constantSize);
    pos += constantSize;

    for (TraceNode* node : vm->tt.curTrace->nodes)
    {
        Trace_Node(vm, node);
    }
}

static void Trace_Compile(VirtualMachine* vm)
{
    for (size_t i = 0; i < vm->tt.numTraces; i++)
    {
        Trace* trace = &vm->tt.traces[i];
        if (trace->nodes.size() >= MIN_TRACE_SIZE)
        {
            trace->jit_trace = vm->jit.jit_compile_trace(
                vm->jit_instance,
                vm,
                trace->trace.data(),
                int(trace->trace.size()),
                int(i)
            );

            // Set the instruction to trigger executing the trace.
            vm->program[trace->pc] = (~MK_LOOPSTART & vm->program[trace->pc]) | MK_TRACESTART;
        }
    }
}

//===================

Callstack* SunScript::GetCallStack(VirtualMachine* vm)
{
    Callstack* stack = new Callstack();
    Callstack* tail = stack;
    
    size_t id = vm->frames.size();
    int pc = vm->programCounter;
    int debugLine = vm->debugLine;
    while (id > 1)
    {
        auto& frame = vm->frames[id];
        tail->functionName = frame.functionName;
        tail->numArgs = int(frame.func->parameters.size());
        tail->debugLine = debugLine;
        tail->programCounter = pc;
        
        id--;
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
    std::memset(&vm->jit, 0, sizeof(vm->jit));
    return vm;
}

void SunScript::ShutdownVirtualMachine(VirtualMachine* vm)
{
    if (vm->jit.jit_shutdown)
    {
        vm->jit.jit_shutdown(vm->jit_instance);
    }

    delete[] vm->program;
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

static real Read_Real(unsigned char* program, unsigned int* pc)
{
    real* value = reinterpret_cast<real*>(&program[*pc]);
    *pc += SUN_REAL_SIZE;
    return *value;
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

static void Push_Real(VirtualMachine* vm, real val)
{
    assert(vm->statusCode == VM_OK);

    real* data = reinterpret_cast<real*>(vm->mm.New(sizeof(real), TY_REAL));
    *data = val;
    vm->stack.push(data);
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

static void Op_Set(VirtualMachine* vm)
{
    unsigned char type = vm->program[vm->programCounter++];
    const int id = Read_Byte(vm->program, &vm->programCounter) + vm->localBounds;
    auto& local = vm->locals[id];

    //if (vm->tracing)
    //{
    //    vm->trace.push_back(OP_SET);
    //    vm->trace.push_back(static_cast<unsigned char>(id & 0xFF));
    //}

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
        if (vm->tracing) { Trace_Int(vm, *data); }
    }
        break;
    case TY_STRING:
    {
        assert (vm->statusCode == VM_OK);
        
        char* str = Read_String(vm->program, &vm->programCounter);
        char* data = reinterpret_cast<char*>(vm->mm.New(sizeof(int), TY_INT));
        std::memcpy(data, str, strlen(str) + 1);
        vm->stack.push(data);
        if (vm->tracing) { Trace_String(vm, str); }
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

    if (vm->statusCode == VM_OK)
    {
        if (vm->locals.size() > id)
        {
            vm->stack.push(vm->locals[id]);

            if (vm->tracing)
            {
                Trace_Push_Local(vm, id);
            }
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
    {
        const int val = Read_Int(vm->program, &vm->programCounter);
        Push_Int(vm, val);
        if (vm->tracing) { Trace_LoadC_Int(vm, val); }
    }
        break;
    case TY_STRING:
    {
         const char* str = Read_String(vm->program, &vm->programCounter);
         Push_String(vm, str);
         if (vm->tracing) { Trace_LoadC_String(vm, str); }
    }
        break;
    case TY_REAL:
    {
        const real val = Read_Real(vm->program, &vm->programCounter);
        Push_Real(vm, val);
        if (vm->tracing) { Trace_LoadC_Real(vm, val); }
    }
        break;
    }
}

static void Discard(VirtualMachine* vm)
{
    if (vm->discard && vm->stack.size() > vm->stackBounds)
    {
        if (vm->stack.size() == 0)
        {
            vm->statusCode = VM_ERROR;
            vm->running = false;
        }
        else
        {
            vm->stack.pop();

            if (vm->tracing) { Trace_Pop_Discard(vm); }
        }
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

    Discard(vm);

    StackFrame& frame = vm->frames[vm->frames.size() - 1];

#if DEBUG
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
#endif

    frame.func->depth--;

    vm->locals.resize(vm->localBounds);
    vm->stackBounds = frame.stackBounds;
    vm->localBounds = frame.localBounds;
    vm->frames.resize(vm->frames.size() - 1);
    vm->programCounter = frame.returnAddress;
    vm->discard = frame.discard;
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

static void Op_Call(VirtualMachine* vm, bool discard)
{
    assert (vm->statusCode == VM_OK);
    
    const unsigned char numArgs = Read_Byte(vm->program, &vm->programCounter);
    const int id = Read_Int(vm->program, &vm->programCounter);
    auto& func = vm->functions[id];
    vm->callName = func.name;
    vm->callNumArgs = numArgs;

    if (func.blk != -1)
    {
        auto& blk = vm->blocks[func.blk];
        if (blk.numArgs == numArgs)
        {
            const int address = blk.info.pc + vm->programOffset;
            StackFrame& frame = vm->frames.emplace_back();
            frame.functionName = vm->callName;
            frame.debugLine = vm->debugLine;
            frame.func = &blk.info;
            CreateStackFrame(vm, frame, numArgs, int(blk.info.locals.size()));
            vm->programCounter = address;
            vm->discard = discard;

            if (vm->tracing)
            {
                vm->tt.curTrace->locals.resize(vm->locals.size());
                vm->tt.curTrace->flags |= SN_NEEDED; // we need a new snapshot to reflect the change in frames

                if (blk.info.depth >= 1)
                {
                    // Recursive functions don't work for now.. abort the trace
                    Trace_Abort(vm);
                }
            }

            blk.info.counter++;
            blk.info.depth++;
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
            if (vm->tracing)
            {
                Trace_Call(vm, id, numArgs);
            }

            // Calls out to a handler
            // parameters can be accessed via GetParamInt() etc
            vm->statusCode = vm->handler(vm);
            vm->running = vm->statusCode == VM_OK;

            Discard(vm);
        }
        else
        {
            // no handler defined
            vm->running = false;
            vm->statusCode = VM_ERROR;
        }
    }    
}

static void Op_CallX(VirtualMachine* vm)
{
    Op_Call(vm, true);
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
            Trace_Yield(vm, id, numArgs);
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

static void Op_Pop(VirtualMachine* vm)
{
    assert(vm->statusCode == VM_OK);
    
    const int id = Read_Byte(vm->program, &vm->programCounter) + vm->localBounds;

    if (!vm->stack.empty())
    {
        vm->locals[id] = vm->stack.top();

        if (vm->tracing)
        {
            Trace_Pop(vm, id);
        }

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
    char type = vm->mm.GetType(v2);
    switch (type)
    {
    case TY_STRING:
        result << reinterpret_cast<char*>(v2);
        if (vm->tracing) { Trace_App_String_String(vm); }
        break;
    case TY_INT:
        result << *reinterpret_cast<int*>(v2);
        if (vm->tracing) { Trace_App_Int_String(vm); }
        break;
    case TY_REAL:
        result << *reinterpret_cast<real*>(v2);
        if (vm->tracing) { Trace_App_Real_String(vm); }
        break;
    default:
        vm->running = false;
        vm->statusCode = VM_ERROR;
        break;
    }

    result << v1;

    if (vm->statusCode == VM_OK)
    {
        Push_String(vm, result.str().c_str());
    }
}

static void Add_Real(VirtualMachine* vm, real* v1, void* v2)
{
    real result = *v1;
    const char type = vm->mm.GetType(v2);

    switch (type)
    {
    case TY_REAL:
        result += *reinterpret_cast<real*>(v2);
        if (vm->tracing) { Trace_Add_Real(vm); }
        Push_Real(vm, result);
        break;
    case TY_INT:
        result += real(*reinterpret_cast<int*>(v2));
        if (vm->tracing) { Trace_Conv_Int_To_Real(vm, -2); Trace_Add_Real(vm); }
        Push_Real(vm, result);
        break;
    case TY_STRING:
    {
        std::stringstream ss;
        ss << reinterpret_cast<char*>(v2);
        ss << result;
        if (vm->tracing) { Trace_App_String_Real(vm); }
        Push_String(vm, ss.str().c_str());
    }
        break;
    default:
        vm->running = false;
        vm->statusCode = VM_ERROR;
        break;
    }
}

static void Add_Int(VirtualMachine* vm, int* v1, void* v2)
{
    int result = *v1;
    const char type = vm->mm.GetType(v2);

    switch (type)
    {
    case TY_INT:
        result += *reinterpret_cast<int*>(v2);
        if (vm->tracing) { Trace_Add_Int(vm); }
        Push_Int(vm, result);
        break;
    case TY_STRING:
    {
        std::stringstream ss;
        ss << reinterpret_cast<char*>(v2);
        ss << result;
        if (vm->tracing) { Trace_App_String_Int(vm); }
        Push_String(vm, ss.str().c_str());
    }
        break;
    case TY_REAL:
    {
        real res = real(result) + *reinterpret_cast<real*>(v2);
        if (vm->tracing) { Trace_Conv_Int_To_Real(vm, -1); Trace_Add_Real(vm); }
        Push_Real(vm, res);
    }
        break;
    default:
        vm->running = false;
        vm->statusCode = VM_ERROR;
        break;
    }
}

static void Sub_Real(VirtualMachine* vm, real* v1, void* v2)
{
    real result = *v1;
    const char type = vm->mm.GetType(v2);

    if (type == TY_REAL)
    {
        result = *reinterpret_cast<real*>(v2) - result;

        if (vm->tracing) { Trace_Sub_Real(vm); }
    }
    else if (type == TY_INT)
    {
        result = *reinterpret_cast<int*>(v2) - result;

        if (vm->tracing) { Trace_Conv_Int_To_Real(vm, -2); Trace_Sub_Real(vm); }
    }
    else
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
    }

    if (vm->statusCode == VM_OK)
    {
        Push_Real(vm, result);
    }
}

static void Sub_Int(VirtualMachine* vm, int* v1, void* v2)
{
    int result = *v1;
    const char type = vm->mm.GetType(v2);

    if (type == TY_INT)
    {
        Push_Int(vm, result = *reinterpret_cast<int*>(v2) - result);

        if (vm->tracing) { Trace_Sub_Int(vm); }
    }
    else if (type == TY_REAL)
    {
        Push_Real(vm, *reinterpret_cast<real*>(v2) - result);

        if (vm->tracing) { Trace_Conv_Int_To_Real(vm, -1); Trace_Sub_Real(vm); }
    }
    else
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
    }
}

static void Mul_Real(VirtualMachine* vm, real* v1, void* v2)
{
    real result = *v1;
    const char type = vm->mm.GetType(v2);

    if (type == TY_REAL)
    {
        result *= *reinterpret_cast<real*>(v2);

        if (vm->tracing) { Trace_Mul_Real(vm); }
    }
    else if (type == TY_INT)
    {
        result *= *reinterpret_cast<int*>(v2);

        if (vm->tracing) { Trace_Conv_Int_To_Real(vm, -2); Trace_Mul_Real(vm); }
    }
    else
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
    }

    if (vm->statusCode == VM_OK)
    {
        Push_Real(vm, result);
    }
}

static void Mul_Int(VirtualMachine* vm, int* v1, void* v2)
{
    int result = *v1;
    const char type = vm->mm.GetType(v2);

    if (type == TY_INT)
    {
        Push_Int(vm, result * *reinterpret_cast<int*>(v2));

        if (vm->tracing) { Trace_Mul_Int(vm); }
    }
    else if (type == TY_REAL)
    {
        Push_Real(vm, result * *reinterpret_cast<real*>(v2));

        if (vm->tracing) { Trace_Conv_Int_To_Real(vm, -1); Trace_Mul_Real(vm); }
    }
    else
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
    }
}

static void Div_Real(VirtualMachine* vm, real* v1, void* v2)
{
    real result = *v1;
    const char type = vm->mm.GetType(v2);

    if (type == TY_REAL)
    {
        result = *reinterpret_cast<real*>(v2) / result;

        if (vm->tracing) { Trace_Div_Real(vm); }
    }
    else if (type == TY_INT)
    {
        result = *reinterpret_cast<int*>(v2) / result;

        if (vm->tracing) { Trace_Conv_Int_To_Real(vm, -2); Trace_Div_Real(vm); }
    }
    else
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
    }

    if (vm->statusCode == VM_OK)
    {
        Push_Real(vm, result);
    }
}

static void Div_Int(VirtualMachine* vm, int* v1, void* v2)
{
    int result = *v1;
    const char type = vm->mm.GetType(v2);

    if (type == TY_INT)
    {
        Push_Int(vm, *reinterpret_cast<int*>(v2) / result);

        if (vm->tracing) { Trace_Div_Int(vm); }
    }
    else if (type == TY_REAL)
    {
        Push_Real(vm, *reinterpret_cast<real*>(v2) / result);

        if (vm->tracing) { Trace_Conv_Int_To_Real(vm, -1); Trace_Div_Real(vm); }
    }
    else
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
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

    void* var1 = vm->stack.top();
    vm->stack.pop();

    if (vm->mm.GetType(var1) == TY_INT)
    {
        Push_Int(vm, -*reinterpret_cast<int*>(var1));
        
        if (vm->tracing) { Trace_Unary_Minus_Int(vm); }
    }
    else if (vm->mm.GetType(var1) == TY_REAL)
    {
        Push_Real(vm, -*reinterpret_cast<real*>(var1));

        if (vm->tracing) { Trace_Unary_Minus_Real(vm); }
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
        case TY_REAL:
            Add_Real(vm, reinterpret_cast<real*>(var1), var2);
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
        case TY_REAL:
            Sub_Real(vm, reinterpret_cast<real*>(var1), var2);
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
        case TY_REAL:
            Mul_Real(vm, reinterpret_cast<real*>(var1), var2);
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
        case TY_REAL:
            Div_Real(vm, reinterpret_cast<real*>(var1), var2);
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

    void* value = vm->stack.top();
        
    if (vm->mm.GetType(value) == TY_INT)
    {
        (*reinterpret_cast<int*>(value))++;
    
        if (vm->tracing) { Trace_Increment_Int(vm); }
    }
    else if (vm->mm.GetType(value) == TY_REAL)
    {
        (*reinterpret_cast<real*>(value))++;

        if (vm->tracing) { Trace_LoadC_Real(vm, 1.0); Trace_Add_Real(vm); }
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

    
    void* value = vm->stack.top();

    if (vm->mm.GetType(value) == TY_INT)
    {
        (*reinterpret_cast<int*>(value))--;
    
        if (vm->tracing) { Trace_Decrement_Int(vm); }
    }
    else if (vm->mm.GetType(value) == TY_REAL)
    {
        (*reinterpret_cast<real*>(value))--;

        if (vm->tracing) { Trace_LoadC_Real(vm, 1.0); Trace_Sub_Real(vm); }
    }
    else
    {
        vm->running = false;
        vm->statusCode = VM_ERROR;
    }
}

static char Flip(char jump)
{
    switch (jump)
    {
    case JUMP_E:
        return JUMP_NE;
    case JUMP_NE:
        return JUMP_E;
    case JUMP_G:
        return JUMP_LE;
    case JUMP_GE:
        return JUMP_L;
    case JUMP_L:
        return JUMP_GE;
    case JUMP_LE:
        return JUMP_G;
    }

    return JUMP;
}

static void ComputePhis(VirtualMachine* vm, TraceLoop& loop)
{
   // Find variables modified by the loop.
    
    int numNodes = 0;
    for (size_t i = 0; i < loop.locals.size(); i++)
    {
        auto& local = loop.locals[i];
        if (local.minRef && local.maxRef && local.minRef->ref < local.maxRef->ref)
        {
            numNodes++;
        }
    }

    if (numNodes > 0)
    {
        int numPhiNodesInserted = 0;
        for (size_t i = 0; i < loop.locals.size(); i++)
        {
            auto& local = loop.locals[i];
            if (local.minRef && local.maxRef && local.minRef->ref < local.maxRef->ref)
            {
                // Generate a Phi node
                TraceNode* phi = reinterpret_cast<TraceNode*>(vm->tt.curTrace->mm.New(sizeof(TraceNode), TY_OBJECT));
                phi->data.id = IR_PHI;
                phi->left = local.minRef;
                phi->right = local.maxRef;
                phi->ref = loop.startRef->ref + numPhiNodesInserted;
                phi->flags = 0;
                phi->pc = vm->programCounter;
                phi->type = TY_VOID;
                vm->tt.curTrace->nodes.insert(vm->tt.curTrace->nodes.begin() + loop.startRef->ref + numPhiNodesInserted, phi);

                vm->tt.curTrace->ref++;

                numPhiNodesInserted++;
            }
        }

        // Update the nodes after the PHI nodes were inserted at
        for (size_t i = loop.startRef->ref + numNodes; i < vm->tt.curTrace->nodes.size(); i++)
        {
            TraceNode* node = vm->tt.curTrace->nodes[i];
            node->ref += numNodes;
        }

        for (size_t i = loop.startRef->ref - numNodes; i < loop.startRef->ref; i++) // loop over the PHI nodes
        {
            TraceNode* phi = vm->tt.curTrace->nodes[i];

            // We need to update the nodes left/right so we are inserted between
            for (size_t j = loop.startRef->ref; j < vm->tt.curTrace->nodes.size(); j++) // nodes from after the PHI nodes to the end
            {
                TraceNode* node = vm->tt.curTrace->nodes[j];
                if (node->left == phi->left || node->left == phi->right)
                {
                    node->left = phi;
                }
                else if (node->right == phi->left || node->right == phi->right)
                {
                    node->right = phi;
                }
            }
        }
    }
}

static void HotLoop(VirtualMachine* vm, int type, int pc, int offset, bool branchDir)
{
    if (vm->tracing)
    {
        if (offset < 0)
        {
            auto& loop = vm->tt.curTrace->loop;
            if (loop.active)
            {
                loop.endRef = vm->tt.curTrace->nodes[vm->tt.curTrace->ref - 1];
                loop.end = pc;

                Trace_LoopBack(vm, type, loop.startRef->ref - vm->tt.curTrace->ref);

                // Record the last used instance of variables.
                for (size_t i = 0; i < loop.locals.size(); i++)
                {
                    loop.locals[i].maxRef = vm->tt.curTrace->locals[i];
                }

                ComputePhis(vm, loop);

                // Pause the tracing for the duration of the loop.
                vm->tracing = false;
                vm->tracingPaused = true;
            }
            else
            {
                Trace_Abort(vm);

            }
        }
        else if (type != JUMP)
        {
            Trace_Snap(vm);

            if (branchDir)
            {
                Trace_Guard(vm, Flip(type));
            }
            else
            {
                Trace_Guard(vm, type);
            }

            if (vm->tt.curTrace->loop.active)
            {
                auto& loop = vm->tt.curTrace->loop;
                auto& guard = loop.guards.emplace_back();
                guard.pc = pc;
                guard.node = vm->tt.curTrace->nodes[vm->tt.curTrace->ref - 1];
            }
        }
    }
    else if (vm->tracingPaused)
    {
        // Check if we are leaving the loop
        if (branchDir)
        {
            // To be certain we need to check the pc is now beyond the
            // end of the loop
            auto& loop = vm->tt.curTrace->loop;
            if (loop.active && vm->programCounter > loop.end)
            {
                // We need to promote the guard to a loop exit
                TraceNode* guardNode = nullptr;
                for (size_t i = 0; i < loop.guards.size(); i++)
                {
                    auto& guard = loop.guards[i];
                    if (guard.pc == pc)
                    {
                        guardNode = guard.node;
                        break;
                    }
                }

                // Disable the loop now it is done.
                loop.active = false;

                const int exitRef = loop.endRef->ref + 1;
                assert(guardNode);
                Trace_PromoteGuard(vm, guardNode, vm->tt.curTrace->nodes[exitRef]);
                vm->tt.curTrace->flags = SN_NEEDED;

                // Complete tracing the loop and start a new trace.
                vm->programInstruction = vm->programCounter;
                Trace_Snap(vm);
                Trace_Finalize(vm);
                Trace_Start(vm);
            }
        }
    }

    if (offset > 0 && branchDir && !vm->tracing && !vm->tracingPaused)
    {
        // Tracing startpoint. This may be loop exit or an jump
        // within an if. We are not interested in the later, but it doesn't
        // harm creating a trace starting there.

        vm->programInstruction = vm->programCounter;
        Trace_Start(vm);
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
        const auto& frame = vm->frames[vm->frames.size()-1];
#if DEBUG
        RecordBranch(frame.func, pc, branchDir);
#endif

        // Record a loop
        if (offset < 0)
        {
#if DEBUG
            RecordLoop(frame.func, vm->programCounter, offset);
#endif
            vm->program[vm->programCounter] |= MK_LOOPSTART;
        }
    }
    else
    {
#if DEBUG
        RecordBranch(vm->main, pc, branchDir);
#endif

        // Record a loop
        if (offset < 0)
        {
#if DEBUG
            RecordLoop(vm->main, vm->programCounter, offset);
#endif
            vm->program[vm->programCounter] |= MK_LOOPSTART;
        }
    }

    if (vm->hot)
    {
        HotLoop(vm, type, pc, offset, branchDir);
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
        vm->comparer = *reinterpret_cast<int*>(item2) - *reinterpret_cast<int*>(item1);
        if (vm->tracing) { Trace_Cmp_Int(vm); }
    }
    else if (vm->mm.GetType(item1) == TY_STRING && vm->mm.GetType(item2) == TY_STRING)
    {
        vm->comparer = strcmp(reinterpret_cast<char*>(item2), reinterpret_cast<char*>(item1));
        if (vm->tracing) { Trace_Cmp_String(vm); }
    }
    else if (vm->mm.GetType(item1) == TY_REAL && vm->mm.GetType(item2) == TY_REAL)
    {
        real cmp = *reinterpret_cast<real*>(item2) - *reinterpret_cast<real*>(item1);
        if (cmp == 0.0 || std::isnan(cmp))
        {
            vm->comparer = 0;
        }
        else if (cmp < 0)
        {
            vm->comparer = -1;
        }
        else if (cmp > 0)
        {
            vm->comparer = 1;
        }
        else
        {
            abort();
        }

        if (vm->tracing) { Trace_Cmp_Real(vm); }
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
    vm->tracingPaused = false;
    vm->hot = false;
    vm->stackBounds = 0;
    vm->errorCode = 0;
    vm->flags = 0;
    vm->instructionsExecuted = 0;
    vm->timeout = 0;
    vm->callNumArgs = 0;
    vm->resumeCode = VM_OK;
    while (!vm->stack.empty()) { vm->stack.pop(); }
    vm->frames.clear();
    vm->locals.clear();
}

static void ScanFunctions(VirtualMachine* vm, unsigned char* program)
{
    const int numBlocks = Read_Int(program, &vm->programCounter);
    const int numEntries = Read_Int(program, &vm->programCounter);
    vm->buildFlags = Read_Int(program, &vm->programCounter);
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
        func.info.depth = 0;
        std::memset(&func.info.stats, 0, sizeof(func.info.stats));

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

static void LoopStart(VirtualMachine* vm)
{
    if (vm->tracing)
    {
        if (vm->tt.curTrace->loop.active)
        {
            // If there is already a loop in this trace abort the trace
            // and attempt to trace the inner loop.
            Trace_Abort(vm);
            Trace_Start(vm);
        }
        else
        {
            // Otherwise finish the current trace and start a new trace for the loop.
            Trace_Snap(vm);
            Trace_Finalize(vm);
            Trace_Start(vm);
        }

        Trace_LoopStart(vm);

        auto& pt = vm->tt.curTrace->loop;
        pt.startRef = vm->tt.curTrace->nodes[vm->tt.curTrace->ref - 1];    // point to the loop start
        pt.endRef = nullptr;
        pt.start = vm->programCounter;
        pt.active = true;
        pt.guards.clear();
        pt.locals.clear();

        // Take a snapshot of the local variable state.
        for (size_t i = 0; i < vm->tt.curTrace->locals.size(); i++)
        {
            auto& local = pt.locals.emplace_back();
            local.minRef = local.maxRef = vm->tt.curTrace->locals[i];
        }
    }
}

static void ExecuteTrace(VirtualMachine* vm)
{
    for (int i = 0; i < vm->tt.numTraces; i++)
    {
        Trace* trace = &vm->tt.traces[i];
        if (trace->pc == vm->programInstruction)
        {
            ActivationRecord record(int(vm->locals.size()), &vm->mm);
            for (size_t i = 0; i < vm->locals.size(); i++)
            {
                record.Add(int(i), vm->mm.GetType(vm->locals[i]), vm->locals[i]);
            }
            unsigned char* buffer = record.GetBuffer();

            vm->tt.curTrace = trace;

            const int state = vm->jit.jit_execute(vm->jit_instance, trace->jit_trace, buffer);
            break;
        }
    }
}

static void CheckBuildFlags(VirtualMachine* vm)
{
#ifdef USE_SUN_FLOAT
    if ((vm->buildFlags & BUILD_FLAG_SINGLE) != BUILD_FLAG_SINGLE)
    {
        vm->statusCode = VM_ERROR;
        vm->running = false;
    }
#else
    if ((vm->buildFlags & BUILD_FLAG_DOUBLE) != BUILD_FLAG_DOUBLE)
    {
        vm->statusCode = VM_ERROR;
        vm->running = false;
    }
#endif
}

static int ResumeScript2(VirtualMachine* vm)
{
    StartVM(vm);
    CheckBuildFlags(vm);

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

        vm->programInstruction = vm->programCounter;
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
            Op_Call(vm, false);
            break;
        case OP_CALLX:
            Op_CallX(vm);
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
        case OP_INCREMENT:
            Op_Increment(vm);
            break;
        case OP_DECREMENT:
            Op_Decrement(vm);
            break;
        case OP_LSADD:
        case OP_LSSUB:
        case OP_LSMUL:
        case OP_LSDIV:
            LoopStart(vm);
            Op_Operator(op, vm);
            break;
        case OP_LSCALL:
            LoopStart(vm);
            Op_Call(vm, false);
            break;
        case OP_LSSET:
            LoopStart(vm);
            Op_Set(vm);
            break;
        case OP_LSPOP:
            LoopStart(vm);
            Op_Pop(vm);
            break;
        case OP_LSPUSH:
            LoopStart(vm);
            Op_Push(vm);
            break;
        case OP_LSPUSH_LOCAL:
            LoopStart(vm);
            Op_Push_Local(vm);
            break;
        case OP_LSYIELD:
            LoopStart(vm);
            Op_Yield(vm);
            break;
        case OP_TRPUSH:
        case OP_TRPUSH_LOCAL:
            ExecuteTrace(vm);
            break;
        default:
            abort();
        }

        vm->instructionsExecuted++;
        CheckForTimeout(vm);
    }

    return vm->statusCode;
}

int SunScript::RunScript(VirtualMachine* vm)
{
    return RunScript(vm, std::chrono::duration<int, std::nano>::zero());
}

int SunScript::LoadProgram(VirtualMachine* vm, unsigned char* program, unsigned char* debugData, int programSize)
{
    delete[] vm->program;

    vm->program = new unsigned char[programSize];
    std::memcpy(vm->program, program, programSize);
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

int SunScript::LoadProgram(VirtualMachine* vm, unsigned char* program, int size)
{
    return LoadProgram(vm, program, nullptr, size);
}

int SunScript::RunScript(VirtualMachine* vm, std::chrono::duration<int, std::nano> timeout)
{
    ResetVM(vm);

    // Convert timeout to nanoseconds (or whatever it may be specified in)
    vm->timeout = std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout).count();
    
    if (vm->tt.numTraces > 0 && vm->tt.traces[0].jit_trace)
    {
        vm->tt.curTrace = &vm->tt.traces[0];

        ActivationRecord record(int(vm->locals.size()), &vm->mm);
        for (size_t i = 0; i < vm->locals.size(); i++)
        {
            record.Add(int(i), vm->mm.GetType(vm->locals[i]), vm->locals[i]);
        }
        unsigned char* buffer = record.GetBuffer();

        const int state = vm->jit.jit_execute(vm->jit_instance, vm->tt.curTrace->jit_trace, buffer);
        if (state == VM_YIELDED)
        {
            return state;
        }

        return ResumeScript2(vm);
    }

    vm->programCounter = vm->main->pc + vm->programOffset;
    vm->programInstruction = vm->programCounter;
    vm->locals.resize(vm->main->locals.size() + vm->main->parameters.size());
    vm->main->counter++;

    if (vm->main->counter == HOT_COUNT && vm->jit_instance)
    {
        // Run with trace
        vm->hot = true;
        Trace_Start(vm);
        const int state = ResumeScript2(vm);
        if (state == VM_OK && vm->tracing)
        {
            // End tracing and JIT compile
            Trace_Done(vm);
            Trace_Finalize(vm);
            Trace_Compile(vm);
            vm->tracing = false;
        }
        return state;
    }

    return ResumeScript2(vm);
}

int SunScript::ResumeScript(VirtualMachine* vm)
{
    if (vm->jit_instance && vm->tt.numTraces > 0 && vm->tt.traces[0].jit_trace)
    {
        const int state = vm->jit.jit_resume(vm->jit_instance);
        if (state == VM_YIELDED)
        {
            return state;
        }

        return ResumeScript2(vm);
    }

    const int state = ResumeScript2(vm);
    if (state == VM_OK && vm->tracing)
    {
        // End tracing and JIT compile
        Trace_Done(vm);
        Trace_Finalize(vm);
        Trace_Compile(vm);
        vm->tracing = false;
    }
    return state;
}

MemoryManager* SunScript::GetMemoryManager(VirtualMachine* vm)
{
    return &vm->mm;
}

int SunScript::RestoreSnapshot(VirtualMachine* vm, const Snapshot& snap, int number, int ref)
{
    if (number < 0 || number >= vm->tt.curTrace->snaps.size())
    {
        return VM_ERROR;
    }

    if (ref < 0 || ref >= vm->tt.curTrace->nodes.size())
    {
        return VM_ERROR;
    }

    const auto& sn = vm->tt.curTrace->snaps[number];
    vm->programCounter = sn.pc;
    size_t numLocals = vm->main->locals.size();
    size_t lastFrameNumLocals = numLocals;
    for (auto& fr : sn.frames)
    {
        vm->frames.emplace_back(fr);
        numLocals += fr.func->locals.size();
        lastFrameNumLocals = fr.func->locals.size();
    }
    vm->localBounds = int(numLocals - lastFrameNumLocals);
    vm->locals.resize(numLocals);
    vm->stackBounds = 0;
    for (int i = 0 ; i < snap.Count(); i++)
    {
        int ref;
        int64_t val;
        
        snap.Get(i, &ref, &val);

        const int type = vm->tt.curTrace->nodes[ref]->type;

        bool ok = false;
        for (auto& local : sn.locals)
        {
            if (local.ref->ref == ref)
            {
                void* data;
                switch (type)
                {
                case TY_INT:
                    data = vm->mm.New(sizeof(int), TY_INT);
                    *reinterpret_cast<int*>(data) = int(val);
                    vm->locals[local.index] = data;
                    break;
                case TY_STRING:
                case TY_OBJECT:
                    vm->locals[local.index] = reinterpret_cast<void*>(val); // boxed
                    break;
                case TY_REAL:
                    data = vm->mm.New(sizeof(real), TY_REAL);
                    *reinterpret_cast<real*>(data) = *reinterpret_cast<real*>(&val);
                    vm->locals[local.index] = data;
                    break;
                }

                ok = true;
                break;
            }
        }

        if (!ok)
        {
            // Temporary (may never be used)
            void* data;
            switch (type)
            {
                case TY_INT:
                    data = vm->mm.New(sizeof(int), TY_INT);
                    *reinterpret_cast<int*>(data) = int(val);
                    vm->stack.push(reinterpret_cast<int*>(data));
                    break;
                case TY_STRING:
                case TY_OBJECT:
                    vm->stack.push(reinterpret_cast<void*>(val)); // boxed
                    break;
                case TY_REAL:
                    data = vm->mm.New(sizeof(real), TY_REAL);
                    *reinterpret_cast<real*>(data) = *reinterpret_cast<real*>(&val);
                    vm->stack.push(data);
                    break;
            }
            vm->stackBounds++;
        }
    }

    return VM_OK;
}

void SunScript::PushReturnValue(VirtualMachine* vm, const std::string& value)
{
    if (vm->statusCode == VM_OK)
    {
        Push_String(vm, value.c_str());
        if (vm->tracing) { Trace_ReturnValue(vm, TY_STRING); }
    }
}

void SunScript::PushReturnValue(VirtualMachine* vm, int value)
{
    if (vm->statusCode == VM_OK)
    {
        Push_Int(vm, value);
        if (vm->tracing) { Trace_ReturnValue(vm, TY_INT); }
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

int SunScript::GetParam(VirtualMachine* vm, void** param)
{
    if (vm->stack.size() == 0) { return VM_ERROR; }
    *param = vm->stack.top();
    vm->stack.pop();
    return VM_OK;
}

int SunScript::GetParamReal(VirtualMachine* vm, real* param)
{
    if (vm->stack.size() == 0) { return VM_ERROR; }

    void* val = vm->stack.top();
    if (vm->mm.GetType(val) == TY_REAL)
    {
        *param = *reinterpret_cast<real*>(val);

        vm->stack.pop();

        if (vm->tracing)
        {
            Trace_Arg_Real(vm);
        }

        return VM_OK;
    }
    else
    {
        return VM_ERROR;
    }
}

int SunScript::GetParamInt(VirtualMachine* vm, int* param)
{
    if (vm->stack.size() == 0) { return VM_ERROR; }
    
    void* val = vm->stack.top();
    if (vm->mm.GetType(val) == TY_INT)
    {
        *param = *reinterpret_cast<int*>(val);

        vm->stack.pop();

        if (vm->tracing)
        {
            Trace_Arg_Int(vm);
        }

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

        if (vm->tracing)
        {
            Trace_Arg_String(vm);
        }

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

int SunScript::PushParamReal(VirtualMachine* vm, real param)
{
    Push_Real(vm, param);
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
    prog->buildFlags = 0;
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
    program->blocks.clear();
    program->numLines = 0;
    program->numFunctions = 0;
    program->buildFlags = 0;
}

int SunScript::GetProgram(Program* program, unsigned char** programData)
{
    const int size = int(program->data.size() + program->functions.size() + program->entries.size() + sizeof(std::int32_t) * 3);
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

    const int buildFlags = program->buildFlags;
    (*programData)[8] = (unsigned char)(buildFlags & 0xFF);
    (*programData)[9] = (unsigned char)((buildFlags >> 8) & 0xFF);
    (*programData)[10] = (unsigned char)((buildFlags >> 16) & 0xFF);
    (*programData)[11] = (unsigned char)((buildFlags >> 24) & 0xFF);

    const int offset = sizeof(std::int32_t) * 3;

    std::memcpy(*programData + offset, program->functions.data(), program->functions.size());
    std::memcpy(*programData + program->functions.size() + offset, program->entries.data(), program->entries.size());
    std::memcpy(*programData + program->functions.size() + program->entries.size() + offset, program->data.data(), program->data.size());
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
    ss << "Build" << std::endl;
    ss << "======================" << std::endl;
    if ((vm->buildFlags & BUILD_FLAG_DOUBLE) == BUILD_FLAG_DOUBLE)
    {
        ss << "BUILD_FLAG_DOUBLE" << std::endl;
    }
    else if ((vm->buildFlags & BUILD_FLAG_SINGLE) == BUILD_FLAG_SINGLE)
    {
        ss << "BUILD_FLAG_SINGLE" << std::endl;
    }

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
                ss << (blk.info.pc + vm->programOffset) << " " << blk.info.name << "(" << blk.numArgs << ")"<< "/" << func.id << std::endl;
            }
            else
            {
                ss << func.name << "/" << func.id << " [External]" << std::endl;
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
            else if (ty == TY_REAL)
            {
                ss << "OP_PUSH " << Read_Real(programData, &vm->programCounter) << "D" << std::endl;
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
        case OP_CALLX:
            ss << "OP_CALLX " << int(Read_Byte(programData, &vm->programCounter)) << " " << Read_Int(programData, &vm->programCounter) << std::endl;
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

static void EmitReal(std::vector<unsigned char>& data, const real value)
{
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&value);

    for (int i = 0; i < SUN_REAL_SIZE; i+=4)
    {
        data.push_back(bytes[i]);
        data.push_back(bytes[i+1]);
        data.push_back(bytes[i+2]);
        data.push_back(bytes[i+3]);
    }
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

void SunScript::EmitPush(ProgramBlock* program, real value)
{
    program->data.push_back(OP_PUSH);
    program->data.push_back(TY_REAL);
    EmitReal(program->data, value);
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

void SunScript::EmitYield(ProgramBlock* program, int func, unsigned char numArgs)
{
    program->data.push_back(OP_YIELD);
    program->data.push_back(numArgs);
    EmitInt(program->data, func);
}

void SunScript::EmitCallX(ProgramBlock* program, int func, unsigned char numArgs)
{
    program->data.push_back(OP_CALLX);
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

void SunScript::EmitBuildFlags(Program* program, int flags)
{
    program->buildFlags |= flags;
}
