#pragma once
#include <vector>
#include <unordered_set>
#include <string>

namespace SunScript
{
    class MemoryManager;
    struct FunctionInfo;

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
        {
        }
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
        {
        }
    };

    class IRBuffer
    {
    public:
        static const int BUFFER_SIZE = 64;

        IRBuffer();
        void write(const InsData& data, int left, int right);
        void at(int ref, InsData* data, int* left, int* right);
        inline bool empty() { return _head == _tail; }
        inline bool full() { return _head - _tail >= BUFFER_SIZE; }
        inline bool exists(int pos) { return pos >= _tail && pos < _head; }
        void read(InsData* data, int* left, int* right);

    private:
        int _head;
        int _tail;
        InsData _buffer[BUFFER_SIZE];
        int _buffer_left[BUFFER_SIZE];
        int _buffer_right[BUFFER_SIZE];
    };

    struct OptFilter
    {
        bool _enabled;
        IRBuffer _input;
        IRBuffer _buffer;
        IRBuffer _output;
    };

    struct OptDeadCodeElim
    {
        std::unordered_set<int> _used;
        OptFilter _filter;
    };

    struct Optimizer
    {
        IRBuffer _buffer;
        IRBuffer output;
        OptFilter guard;
        OptFilter fold;
        OptDeadCodeElim dead;

        Optimizer() {
            fold._enabled = true;
            guard._enabled = true;
        }
    };

    void Opt_Optimize_Loops(MemoryManager* mm,
        std::vector<unsigned char>& constants,
        std::vector<TraceNode*>& nodes,
        std::vector<TraceSnapshot>& snaps);
    void Opt_Optimize_Forward(Optimizer& opt, std::vector<unsigned char>& constants, TraceNode* node);
    void Opt_Optimize_Backward(Optimizer& opt, std::vector<unsigned char>& constants, TraceNode* node);
    void Opt_Drain(Optimizer& opt, std::vector<unsigned char>& constants);

}
