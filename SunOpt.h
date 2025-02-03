#pragma once
#include <vector>
#include <unordered_set>

namespace SunScript
{
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

    void Opt_Optimize_Forward(Optimizer& opt, std::vector<unsigned char>& constants, TraceNode* node);
    void Opt_Optimize_Backward(Optimizer& opt, std::vector<unsigned char>& constants, TraceNode* node);
    void Opt_Drain(Optimizer& opt, std::vector<unsigned char>& constants);

}

