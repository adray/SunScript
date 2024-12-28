#pragma once
#include <string>
#include <chrono>
#include <vector>

namespace SunScript
{
    constexpr unsigned char MK_LOOPSTART = 1 << 7;
    constexpr unsigned char MK_TRACESTART = 1 << 6;

    constexpr unsigned char OP_PUSH = 0x0;
    constexpr unsigned char OP_POP = 0x1;
    constexpr unsigned char OP_CALL = 0x2;
    constexpr unsigned char OP_YIELD = 0x3;
    constexpr unsigned char OP_LOCAL = 0x4;
    constexpr unsigned char OP_SET = 0x5;
    constexpr unsigned char OP_CALLD = 0x6;

    constexpr unsigned char OP_DONE = 0x8;
    constexpr unsigned char OP_PUSH_LOCAL = 0x9;
    constexpr unsigned char OP_TABLE_NEW = 0xa;
    constexpr unsigned char OP_TABLE_GET = 0xb;
    constexpr unsigned char OP_TABLE_SET = 0xc;
    constexpr unsigned char OP_UNARY_MINUS = 0xd;
    constexpr unsigned char OP_INCREMENT = 0xe;
    constexpr unsigned char OP_DECREMENT = 0xf;
    constexpr unsigned char OP_ADD = 0x10;
    constexpr unsigned char OP_SUB = 0x1a;
    constexpr unsigned char OP_MUL = 0x1b;
    constexpr unsigned char OP_DIV = 0x1c;

    constexpr unsigned char OP_DUP = 0x20;
    constexpr unsigned char OP_PUSH_FUNC = 0x21;
    constexpr unsigned char OP_FORMAT = 0x22;
    constexpr unsigned char OP_JUMP = 0x23;
    constexpr unsigned char OP_CMP = 0x24;
    constexpr unsigned char OP_RETURN = 0x25;
    constexpr unsigned char OP_CALLO = 0x26;
    constexpr unsigned char OP_CALLM = 0x27;

    constexpr unsigned char OP_LSPUSH = OP_PUSH | MK_LOOPSTART;
    constexpr unsigned char OP_LSPOP = OP_POP | MK_LOOPSTART;
    constexpr unsigned char OP_LSCALL = OP_CALL | MK_LOOPSTART;
    constexpr unsigned char OP_LSYIELD = OP_YIELD | MK_LOOPSTART;
    constexpr unsigned char OP_LSSET = OP_SET | MK_LOOPSTART;
    constexpr unsigned char OP_LSPUSH_LOCAL = OP_PUSH_LOCAL | MK_LOOPSTART;
    constexpr unsigned char OP_LSADD = OP_ADD | MK_LOOPSTART;
    constexpr unsigned char OP_LSSUB = OP_SUB | MK_LOOPSTART;
    constexpr unsigned char OP_LSMUL = OP_MUL | MK_LOOPSTART;
    constexpr unsigned char OP_LSDIV = OP_DIV | MK_LOOPSTART;

    constexpr unsigned char OP_TRPUSH = OP_PUSH | MK_TRACESTART;
    constexpr unsigned char OP_TRPUSH_LOCAL = OP_PUSH_LOCAL | MK_TRACESTART;

    constexpr unsigned char TY_VOID = 0x0;
    constexpr unsigned char TY_INT = 0x1;
    constexpr unsigned char TY_STRING = 0x2;
    constexpr unsigned char TY_REAL = 0x3;
    constexpr unsigned char TY_OBJECT = 0x4;
    constexpr unsigned char TY_FUNC = 0x5;

    constexpr unsigned char JUMP = 0x0;
    constexpr unsigned char JUMP_E = 0x1;
    constexpr unsigned char JUMP_NE = 0x2;
    constexpr unsigned char JUMP_GE = 0x3;
    constexpr unsigned char JUMP_LE = 0x4;
    constexpr unsigned char JUMP_L = 0x5;
    constexpr unsigned char JUMP_G = 0x6;

    constexpr unsigned char IR_LOAD_INT = 0x0;
    constexpr unsigned char IR_LOAD_STRING = 0x1;
    constexpr unsigned char IR_LOAD_REAL = 0x2;
    constexpr unsigned char IR_LOAD_TABLE = 0x3;
    constexpr unsigned char IR_LOAD_INT_LOCAL = 0x10;
    constexpr unsigned char IR_LOAD_STRING_LOCAL = 0x11;
    constexpr unsigned char IR_LOAD_REAL_LOCAL = 0x12;
    constexpr unsigned char IR_LOAD_TABLE_LOCAL = 0x13;
    constexpr unsigned char IR_CALL = 0x20;
    constexpr unsigned char IR_YIELD = 0x21;
    constexpr unsigned char IR_INT_ARG = 0x25;
    constexpr unsigned char IR_STRING_ARG = 0x26;
    constexpr unsigned char IR_REAL_ARG = 0x27;
    constexpr unsigned char IR_TABLE_ARG = 0x28;
    constexpr unsigned char IR_INCREMENT_INT = 0x30;
    constexpr unsigned char IR_DECREMENT_INT = 0x31;
    constexpr unsigned char IR_INCREMENT_REAL = 0x32;
    constexpr unsigned char IR_DECREMENT_REAL = 0x33;
    constexpr unsigned char IR_ADD_INT = 0x34;
    constexpr unsigned char IR_SUB_INT = 0x35;
    constexpr unsigned char IR_MUL_INT = 0x36;
    constexpr unsigned char IR_DIV_INT = 0x37;
    constexpr unsigned char IR_UNARY_MINUS_INT = 0x38;
    constexpr unsigned char IR_ADD_REAL = 0x39;
    constexpr unsigned char IR_SUB_REAL = 0x3a;
    constexpr unsigned char IR_MUL_REAL = 0x3b;
    constexpr unsigned char IR_DIV_REAL = 0x3c;
    constexpr unsigned char IR_UNARY_MINUS_REAL = 0x3d;
    constexpr unsigned char IR_APP_INT_STRING = 0x47;
    constexpr unsigned char IR_APP_STRING_INT = 0x48;
    constexpr unsigned char IR_APP_STRING_STRING = 0x49;
    constexpr unsigned char IR_APP_STRING_REAL = 0x4a;
    constexpr unsigned char IR_APP_REAL_STRING = 0x4b;
    constexpr unsigned char IR_GUARD = 0x50;
    constexpr unsigned char IR_CMP_INT = 0x51;
    constexpr unsigned char IR_CMP_STRING = 0x52;
    constexpr unsigned char IR_CMP_REAL = 0x53;
    constexpr unsigned char IR_CMP_TABLE = 0x54;
    constexpr unsigned char IR_LOOPBACK = 0x60;
    constexpr unsigned char IR_LOOPSTART = 0x61;
    constexpr unsigned char IR_LOOPEXIT = 0x62;
    constexpr unsigned char IR_PHI = 0x63;
    constexpr unsigned char IR_SNAP = 0x64;
    constexpr unsigned char IR_UNBOX = 0x65;
    constexpr unsigned char IR_NOP = 0x66;
    constexpr unsigned char IR_CONV_INT_TO_REAL = 0x70;
    constexpr unsigned char IR_TABLE_NEW = 0x80;
    constexpr unsigned char IR_TABLE_HGET = 0x81;
    constexpr unsigned char IR_TABLE_AGET = 0x82;
    constexpr unsigned char IR_TABLE_HSET = 0x83;
    constexpr unsigned char IR_TABLE_ASET = 0x84;
    constexpr unsigned char IR_TABLE_AREF = 0x85;
    constexpr unsigned char IR_TABLE_HREF = 0x86;

    constexpr int BUILD_FLAG_SINGLE = 0x1;
    constexpr int BUILD_FLAG_DOUBLE = 0x2;

#ifdef USE_SUN_FLOAT
    typedef float real;
    constexpr int SUN_REAL_SIZE = 4;
#else
    typedef double real;
    constexpr int SUN_REAL_SIZE = 8;
#endif

    struct VirtualMachine;
    struct Program;
    struct ProgramBlock;
    struct FunctionInfo;

    class MemoryManager
    {
    private:
        struct Header
        {
            int64_t _refCount;
            int64_t _size;
            char _type;
        };

        struct Segment
        {
            unsigned char* _memory;
            uint64_t _pos;
            uint64_t _totalSize;
        };

    public:

        MemoryManager();
        void* New(uint64_t size, char type);
        void Dump();
        void AddRef(void* mem);
        void Release(void* mem);
        char GetType(void* mem) const;
        void Reset();
        static char GetTypeUnsafe(void* mem);
        ~MemoryManager();

    private:
        std::vector<Segment> _segments;
    };

    class Snapshot
    {
    private:
        struct Value
        {
            int _ref;
            int64_t _value;
        };

    public:
        Snapshot(int numValues, MemoryManager* mm);
        void Add(int ref, int64_t value);
        void Get(int idx, int* ref, int64_t* value) const;
        inline size_t Count() const { return _numValues; }

    private:
        Value* _values;
        int _numValues;
        int _index;
    };

    class ActivationRecord
    {
    public:
        ActivationRecord(int numItems, MemoryManager* mm);
        void Add(int id, int type, void* data);
        inline unsigned char* GetBuffer() { return _buffer; }

    private:
        unsigned char* _buffer;
    };

    struct Label
    {
        int pos;
        std::vector<int> jumps;
    };

    struct Callstack
    {
        std::string functionName;
        int numArgs = 0;
        int debugLine = 0;
        int programCounter = 0;

        Callstack* next = nullptr;
    };

    struct Jit
    {
        void* (*jit_initialize) (void);
        void* (*jit_compile_trace) (void* instance, VirtualMachine* vm, unsigned char* trace, int size, int traceId);
        int (*jit_execute) (void* instance, void* data, unsigned char* record);
        int (*jit_resume) (void* instance);
        void (*jit_shutdown) (void* instance);
    };

    constexpr int VM_OK = 0;
    constexpr int VM_ERROR = 1;
    constexpr int VM_YIELDED = 2;
    constexpr int VM_PAUSED = 3;
    constexpr int VM_TIMEOUT = 4;

    constexpr int ERR_NONE = 0;
    constexpr int ERR_INTERNAL = 1;

    /* Gets the call stack. */
    Callstack* GetCallStack(VirtualMachine* vm);
    
    /* Destroys the call stack. */
    void DestroyCallstack(Callstack* callstack);

    /*
    * Creates an instance of a Virtual Machine to
    * execute scripts.
    */
    VirtualMachine* CreateVirtualMachine();

    /*
    * Shuts down an instance of a Virtual Machine.
    */
    void ShutdownVirtualMachine(VirtualMachine* vm);

    /*
    * Sets the optimization level of the JIT compilier.
    */
    void SetOptimizationLevel(VirtualMachine* vm, int level);

    /*
    * Sets a handler function which will handle functions
    * defined by the host program.
    * 
    * The handler should return VM_OK in the case the method was handled correctly.
    * Otherwise it should return VM_ERROR.
    * 
    * The handler can use the following functions to retrieve state information for the function call:
    * - GetCallNumArgs
    * - GetCallName
    * - GetParamInt
    * - GetParamString
    */
    void SetHandler(VirtualMachine* vm, int handler(VirtualMachine* vm));

    /*
    * This function sets the handlers for JIT compilation. 
    */
    void SetJIT(VirtualMachine* vm, Jit* jit);

    /*
    * Gets user data associated with the Virtual Machine.
    */
    void* GetUserData(VirtualMachine* vm);

    /*
    * Sets the user data associated with the Virtual Machine.
    */
    void SetUserData(VirtualMachine* vm, void* userData);

    /*
    * Loads a script for the given filepath.
    * The program data should be freed by the caller.
    */
    int LoadScript(const std::string& filepath, unsigned char** program);

    /* Loads a program into the virtual machine. */
    int LoadProgram(VirtualMachine* vm, unsigned char* program, int programSize);

    /* Loads a program into the virtual machine. */
    int LoadProgram(VirtualMachine* vm, unsigned char* program, unsigned char* debugData, int programSize);

    int RunScript(VirtualMachine* vm);

    int RunScript(VirtualMachine* vm, std::chrono::duration<int, std::nano> timeout);

    int ResumeScript(VirtualMachine* vm);

    MemoryManager* GetMemoryManager(VirtualMachine* vm);

    void* CreateTable(MemoryManager* mm);

    void* GetTableArray(void* table, int index);

    void* GetTableHash(void* table, const std::string& key);

    void SetTableArray(void* table, int index, void* value);

    void SetTableHash(void* table, const std::string& key, void* value);

    int RestoreSnapshot(VirtualMachine* vm, const Snapshot& snap, int number, int ref);

    void PushReturnValue(VirtualMachine* vm, const std::string& value);
    
    void PushReturnValue(VirtualMachine* vm, int value);

    int GetCallNumArgs(VirtualMachine* vm, int* numArgs);

    int GetCallName(VirtualMachine* vm, std::string* name);

    int GetParam(VirtualMachine* vm, void** param);

    int GetParamReal(VirtualMachine* vm, real* param);

    int GetParamInt(VirtualMachine* vm, int* param);

    int GetParamString(VirtualMachine* vm, std::string* param);

    int PushParamString(VirtualMachine* vm, const std::string& param);

    int PushParamInt(VirtualMachine* vm, int param);

    int PushParamReal(VirtualMachine* vm, real param);

    void InvokeHandler(VirtualMachine* vm, const std::string& callName, int numParams);

    int FindFunction(VirtualMachine* vm, int id, FunctionInfo** info);

    const char* FindFunctionName(VirtualMachine* vm, int id);

    unsigned char* GetLoadedProgram(VirtualMachine* vm);

    Program* CreateProgram();

    void FlushBlocks(Program* program);

    int CreateFunction(Program* program);

    void EmitInternalFunction(Program* program, ProgramBlock* blk, int func);

    void EmitExternalFunction(Program* program, int func, const std::string& name);

    ProgramBlock* CreateProgramBlock(bool topLevel, const std::string& name, int numArgs);

    void ReleaseProgramBlock(ProgramBlock* block);
    
    void ResetProgram(Program* program);

    int GetProgram(Program* program, unsigned char** programData);

    int GetDebugData(Program* program, unsigned char** debug);

    void ReleaseProgram(Program* program);

    void Disassemble(std::stringstream& ss, unsigned char* programData, unsigned char* debugData);

    void EmitProgramBlock(Program* program, ProgramBlock* block);

    void EmitReturn(ProgramBlock* program);

    void EmitParameter(ProgramBlock* program, const std::string& name);

    void EmitLocal(ProgramBlock* program, const std::string& name);
    
    void EmitSet(ProgramBlock* program, const unsigned char local, int value);

    void EmitSet(ProgramBlock* program, const unsigned char local, const std::string& value);
    
    void EmitPushLocal(ProgramBlock* program, unsigned char local);
    
    void EmitPush(ProgramBlock* program, int value);

    void EmitPush(ProgramBlock* program, real value);

    void EmitPush(ProgramBlock* program, const std::string& value);

    void EmitPop(ProgramBlock* program, unsigned char local);

    void EmitPushDelegate(ProgramBlock* program, int func);

    void EmitYield(ProgramBlock* program, int func, unsigned char numArgs);

    void EmitCallD(ProgramBlock* program, int func, unsigned char numArgs);

    void EmitCallO(ProgramBlock* program, unsigned char numArgs);

    void EmitCallM(ProgramBlock* program, unsigned char numArgs);

    void EmitCall(ProgramBlock* program, int func, unsigned char numArgs);

    void EmitAdd(ProgramBlock* program);

    void EmitSub(ProgramBlock* program);

    void EmitDiv(ProgramBlock* program);

    void EmitMul(ProgramBlock* program);

    void EmitFormat(ProgramBlock* program);

    void EmitUnaryMinus(ProgramBlock* program);

    void EmitIncrement(ProgramBlock* program);

    void EmitDecrement(ProgramBlock* program);

    void MarkLabel(ProgramBlock* program, Label* label);

    void EmitMarkedLabel(ProgramBlock* program, Label* label);

    void EmitLabel(ProgramBlock* program, Label* label);

    void EmitCompare(ProgramBlock* program);

    void EmitJump(ProgramBlock* program, char type, Label* label);

    void EmitTableNew(ProgramBlock* program);

    void EmitTableGet(ProgramBlock* program);

    void EmitTableSet(ProgramBlock* program);

    void EmitDup(ProgramBlock* program);

    void EmitDone(ProgramBlock* program);

    void EmitDebug(ProgramBlock* program, int line);

    void EmitBuildFlags(Program* program, int flags);
}
