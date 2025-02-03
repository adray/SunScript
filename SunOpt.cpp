#include "SunOpt.h"
#include "SunScript.h"

using namespace SunScript;

inline static void Trace_Constant(std::vector<unsigned char>& constants, int val)
{
    constants.push_back(static_cast<unsigned char>(val & 0xFF));
    constants.push_back(static_cast<unsigned char>((val >> 8) & 0xFF));
    constants.push_back(static_cast<unsigned char>((val >> 16) & 0xFF));
    constants.push_back(static_cast<unsigned char>((val >> 24) & 0xFF));
}

inline static void Trace_Constant(std::vector<unsigned char>& constants, real val)
{
    unsigned char* data = reinterpret_cast<unsigned char*>(&val);
    for (int i = 0; i < SUN_REAL_SIZE; i += 4)
    {
        constants.push_back(data[i]);
        constants.push_back(data[i + 1]);
        constants.push_back(data[i + 2]);
        constants.push_back(data[i + 3]);
    }
}

//================
// IR Buffer
//================

IRBuffer::IRBuffer() : _head(0), _tail(0)
{
    std::memset(_buffer, 0, sizeof(_buffer));
    std::memset(_buffer_left, 0, sizeof(_buffer_left));
    std::memset(_buffer_right, 0, sizeof(_buffer_right));
}

void IRBuffer::write(const InsData& data, int left, int right)
{
    const int pos = _head % BUFFER_SIZE;
    _buffer[pos] = data;
    _buffer_left[pos] = left;
    _buffer_right[pos] = right;
    _head++;
}

void IRBuffer::at(int ref, InsData* data, int* left, int* right)
{
    const int pos = ref % BUFFER_SIZE;
    *data = _buffer[pos];
    *left = _buffer_left[pos];
    *right = _buffer_right[pos];
}

void IRBuffer::read(InsData* data, int* left, int* right)
{
    const int pos = _tail % BUFFER_SIZE;
    *data = _buffer[pos];
    *left = _buffer_left[pos];
    *right = _buffer_right[pos];
    _tail++;
}

//============================
// TRACING OPTIMIZER
//============================

static void Opt_ConstantFold_Filter(OptFilter& filter, std::vector<unsigned char>& constants)
{
    InsData data;
    int left;
    int right;

    // Read next instruction
    if (filter._input.empty())
    {
        return;
    }

    // The output cannot be written to at this time
    if (filter._output.full())
    {
        return;
    }

    filter._input.read(&data, &left, &right);

    // If the buffer is full then flush to the output
    if (filter._buffer.full())
    {
        InsData data;
        int left;
        int right;
        filter._buffer.read(&data, &left, &right);
        filter._output.write(data, left, right);
    }

    switch (data.id)
    {
    case IR_ADD_INT:
    case IR_SUB_INT:
    case IR_MUL_INT:
    case IR_DIV_INT: {
        InsData lData; int ll; int lr;
        filter._buffer.at(left, &lData, &ll, &lr);

        InsData rData; int rl; int rr;
        filter._buffer.at(right, &rData, &rl, &rr);

        if (filter._buffer.exists(left) && filter._buffer.exists(right) &&
            lData.id == IR_LOAD_INT && rData.id == IR_LOAD_INT)
        {
            int* valLeft = reinterpret_cast<int*>(&constants.data()[lData.constant]);
            int* valRight = reinterpret_cast<int*>(&constants.data()[rData.constant]);

            int result = 0;

            switch (data.id)
            {
            case IR_ADD_INT:
                result = *valRight + *valLeft;
                break;
            case IR_SUB_INT:
                result = *valRight - *valLeft;
                break;
            case IR_MUL_INT:
                result = *valRight * *valLeft;
                break;
            case IR_DIV_INT:
                result = *valRight / *valLeft;
                break;
            }

            InsData ins = { .id = IR_LOAD_INT, .constant = int(constants.size()) };
            filter._buffer.write(ins, 0, 0);

            Trace_Constant(constants, result);
        }
        else
        {
            filter._buffer.write(data, left, right);
        }
    }
        break;
    case IR_ADD_REAL:
    case IR_SUB_REAL:
    case IR_MUL_REAL:
    case IR_DIV_REAL: {
        InsData lData; int ll; int lr;
        filter._buffer.at(left, &lData, &ll, &lr);

        InsData rData; int rl; int rr;
        filter._buffer.at(right, &rData, &rl, &rr);

        if (filter._buffer.exists(left) && filter._buffer.exists(right) &&
            lData.id == IR_LOAD_INT && rData.id == IR_LOAD_INT)
        {
            real* valLeft = reinterpret_cast<real*>(&constants.data()[lData.constant]);
            real* valRight = reinterpret_cast<real*>(&constants.data()[rData.constant]);

            real result = 0;

            switch (data.id)
            {
            case IR_ADD_REAL:
                result = *valRight + *valLeft;
                break;
            case IR_SUB_REAL:
                result = *valRight - *valLeft;
                break;
            case IR_MUL_REAL:
                result = *valRight * *valLeft;
                break;
            case IR_DIV_REAL:
                result = *valRight / *valLeft;
                break;
            }

            InsData ins = { .id = IR_LOAD_INT, .constant = int(constants.size()) };
            filter._buffer.write(ins, 0, 0);

            Trace_Constant(constants, result);
        }
        else
        {
            filter._buffer.write(data, left, right);
        }
    }
        break;
    default:
        filter._buffer.write(data, left, right);
        break;
    }
}

//static void Opt_Guard_Filter(Optimizer& opt, std::vector<unsigned char>& constants)
//{
//    auto& filter = opt.guard;
//
//    switch (data.id)
//    {
//    case IR_CMP_INT:
//    case IR_CMP_REAL:
//    case IR_CMP_STRING: {
//        bool found = false;
//        for (int i = 0; i < Optimizer::BUFFER_SIZE; i++)
//        {
//            if (opt._buffer_left[i] == left &&
//                opt._buffer_right[i] == right)
//            {
//                found = true;
//                break;
//            }
//        }
//
//        if (!found)
//        {
//            opt._buffer[opt._pos] = data;
//            opt._buffer_left[opt._pos] = left;
//            opt._buffer_right[opt._pos] = right;
//
//            opt._pos = (opt._pos + 1) % Optimizer::BUFFER_SIZE;
//        }
//    }
//        break;
//    case IR_GUARD: {
//        bool found = false;
//        for (int i = 0; i < Optimizer::BUFFER_SIZE; i++)
//        {
//            if (opt._buffer[i].jump == data.jump)
//            {
//                found = true;
//                break;
//            }
//        }
//
//        if (!found)
//        {
//            opt._buffer[opt._pos] = data;
//            opt._buffer_left[opt._pos] = -1;
//            opt._buffer_right[opt._pos] = -1;
//
//            opt._pos = (opt._pos + 1) % Optimizer::BUFFER_SIZE;
//        }
//    }
//        break;
//    }
//}

void SunScript::Opt_Optimize_Forward(Optimizer& opt, std::vector<unsigned char>& constants, TraceNode* node)
{
    opt._buffer.write(node->data, node->left ? node->left->ref : 0, node->right ? node->right->ref : 0);

    InsData data;
    int left;
    int right;

    //if (opt.guard._enabled)
    //{
    //    Opt_Guard_Filter(opt, constants);
    //}

    if (opt.fold._enabled)
    {
        opt._buffer.read(&data, &left, &right);
        opt.fold._input.write(data, left, right);
        Opt_ConstantFold_Filter(opt.fold, constants);
        if (!opt.fold._output.empty())
        {
            opt.fold._output.read(&data, &left, &right);
            opt.output.write(data, left, right);

            if (data.id == IR_PHI)
            {
                opt.dead._used.insert(left);
                opt.dead._used.insert(right);
            }
        }
    }
    else
    {
        opt._buffer.read(&data, &left, &right);
        opt.output.write(data, left, right);

        if (data.id == IR_PHI)
        {
            opt.dead._used.insert(left);
            opt.dead._used.insert(right);
        }
    }
}

void SunScript::Opt_Optimize_Backward(Optimizer& opt, std::vector<unsigned char>& constants, TraceNode* node)
{
    // TODO: snapshots if we remove the code which sets a variable.
    // When we restore a snapshot, this variable won't have a value?
    // Will this cause a problem?

    switch (node->data.id)
    {
    case IR_LOAD_INT:
    case IR_ADD_INT:
    case IR_SUB_INT:
    case IR_MUL_INT:
    case IR_DIV_INT:
    case IR_LOAD_REAL:
    case IR_ADD_REAL:
    case IR_SUB_REAL:
    case IR_MUL_REAL:
    case IR_DIV_REAL:
        if (opt.dead._used.find(node->ref) == opt.dead._used.end())
        {
            // Instruction never used, output an IR_NOP
            InsData data = { .id = IR_NOP };
            opt.output.write(data, -1, -1);
        }
        else
        {
            if (node->left) { opt.dead._used.insert(node->left->ref); }
            if (node->right) { opt.dead._used.insert(node->right->ref); }
            opt.output.write(node->data, node->left ? node->left->ref : -1, node->right ? node->right->ref : -1);
        }
        break;
    default:
        if (node->left) { opt.dead._used.insert(node->left->ref); }
        if (node->right) { opt.dead._used.insert(node->right->ref); }
        opt.output.write(node->data, node->left ? node->left->ref : -1, node->right ? node->right->ref : -1);
        break;
    }
}

void SunScript::Opt_Drain(Optimizer& opt, std::vector<unsigned char>& constants)
{
    while (!opt.fold._buffer.empty() && !opt.fold._output.full())
    {
        InsData data; int left; int right;
        opt.fold._buffer.read(&data, &left, &right);
        opt.fold._output.write(data, left, right);
    }

    while (!opt.output.full() && !opt.fold._output.empty())
    {
        InsData data; int left; int right;
        opt.fold._output.read(&data, &left, &right);
        opt.output.write(data, left, right);

        if (data.id == IR_PHI)
        {
            opt.dead._used.insert(left);
            opt.dead._used.insert(right);
        }
    }
}
