#include "SunOpt.h"
#include "SunScript.h"
#include <cstring>
#include <iostream>
#include <assert.h>

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

inline static int Read_Int(const std::vector<unsigned char>& constants, const int index)
{
    return int(constants[index]) |
        int(constants[index + 1] << 8) |
        int(constants[index + 2] << 16) |
        int(constants[index + 1] << 24);
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

constexpr int UNROLL_LIMIT = 10;

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

static TraceNode* Opt_Extract_Loop_Stride(std::vector<TraceNode*>& nodes, const int inductionVar)
{
    TraceNode* node = nodes[inductionVar];
    assert(node->left && node->right);

    const int leftId = node->left->data.id;
    if (leftId == IR_ADD_INT || leftId == IR_INCREMENT_INT)
    {
        auto& left = node->left;
        if (left->left == node)
        {
            return left;
        }
        if (left->right == node)
        {
            return left;
        }
    }

    const int rightId = node->right->data.id;
    if (rightId == IR_ADD_INT || rightId == IR_INCREMENT_INT)
    {
        auto& right = node->right;
        if (right->left == node)
        {
            return right;
        }
        if (right->right == node)
        {
            return right;
        }
    }

    return nullptr;
}

inline static int Opt_Extract_Induction_Variable(const std::vector<TraceNode*>& nodes, const size_t cmp)
{
    int inductionVar = -1;
    if (nodes[cmp]->left->data.id == IR_PHI)
    {
        inductionVar = nodes[cmp]->left->ref;
    }
    else if (nodes[cmp]->right->data.id == IR_PHI)
    {
        inductionVar = nodes[cmp]->right->ref;
    }
    return inductionVar;
}

inline static int Opt_Extract_Loop_Limit(const std::vector<TraceNode*>& nodes, const size_t cmp)
{
    int loopLimit = -1;
    if (nodes[cmp]->left->data.id == IR_LOAD_INT)
    {
        loopLimit = nodes[cmp]->left->ref;
    }
    else if (nodes[cmp]->right->data.id == IR_LOAD_INT)
    {
        loopLimit = nodes[cmp]->right->ref;
    }
    return loopLimit;
}

inline static TraceNode* Opt_Extract_Loop_Init(const std::vector<TraceNode*>& nodes, const int inductionVar)
{
    TraceNode* node = nodes[inductionVar];
    assert(node->left && node->right);
    if (node->left->data.id == IR_LOAD_INT)
    {
        return node->left;
    }
    if (node->right->data.id == IR_LOAD_INT)
    {
        return node->right;
    }

    return nullptr;
}

static void Opt_LoopRemap(const size_t offset, const size_t start, const size_t end, std::vector<SunScript::TraceNode*>& nodes, 
    std::vector<TraceSnapshot>& snaps, std::vector<int>& remap)
{
    // De-duplicate inserted nodes
    // The remap vector is a map from the IR ref to the
    // last duplicated node in the chain
    //      remap[ref] = remapped ref
    // 
    // e.g. 5 IR_INCREMENT_INT 2
    //      5 IR_INCREMENT_INT 2
    //              =>
    //      5 IR_INCREMENT_INT 2
    //      6 IR_INCREMENT_INT 5
    // 
    //      remap[5-offset] = 5
    //      remap[5-offset] = 6
    // 
    // 
    //    if remap["cur ref"] is set:
    //      if node->left is nodes["cur ref"]->left:
    //          node->left := nodes["cur ref"]
    //      [repeat for right node]
    //    update node->ref
    //    remap["cur ref"] := node->ref

    for (size_t i = start; i < end; i++)
    {
        TraceNode* node = nodes[i];
        const size_t curRef = node->ref - offset;
        if (!remap[curRef])
        {
            remap[curRef] = node->ref;
        }
        else
        {
            if (node->left && nodes[node->ref]->left == node->left)
            {
                node->left = nodes[remap[curRef]];
            }
            if (node->right && nodes[node->ref]->right == node->right)
            {
                node->right = nodes[remap[curRef]];
            }
            node->ref = int(i);
            remap[curRef] = node->ref;
        }
    }
}

static void Opt_Partial_Unroll_Loop(MemoryManager* mm, std::vector<unsigned char>& constants, std::vector<TraceNode*>& nodes, 
    std::vector<TraceSnapshot>& snaps, const size_t start, const size_t end)
{
    size_t exit = 0;
    size_t cmp = 0;

    // 1) Find the loop condition

    for (size_t pos = start; pos < end; pos++)
    {
        auto& node = nodes[pos];

        switch (node->data.id)
        {
        case IR_LOOPEXIT:
            exit = pos;
            break;
        case IR_CMP_INT:
        case IR_CMP_REAL:
        case IR_CMP_STRING:
        case IR_CMP_TABLE:
            cmp = pos; /* needs to be the cmp associated with loop exit, but as guards are not allowed there will not be more compares.. */
            break;
        case IR_CALL:
            return; /* don't unroll if there are calls as the JIT generates quite a lot of machine code for these */
        case IR_GUARD:
            break; /* don't unroll if there are guards for now */
        }
    }

    TraceNode* exitNode = nodes[exit];
    TraceNode* loopback = nodes[end];

    // 2) Determine if the loop condition can be unrolled
    // To unroll it, it must have a constant loop count.
    // See SSA book Chapter 10.2 for more ideas

    if (!cmp || !exit)
    {
        return;
    }

    const int loopLimitVar = Opt_Extract_Loop_Limit(nodes, cmp);

    if (loopLimitVar == -1)
    {
        return;
    }

    const int inductionVar = Opt_Extract_Induction_Variable(nodes, cmp);

    if (inductionVar == -1)
    {
        return;
    }

    const TraceNode* initVar = Opt_Extract_Loop_Init(nodes, inductionVar);

    if (initVar == nullptr)
    {
        return;
    }

    const TraceNode* strideVar = Opt_Extract_Loop_Stride(nodes, inductionVar);

    if (strideVar == nullptr)
    {
        return;
    }

    int stride;
    switch (strideVar->data.id)
    {
    case IR_INCREMENT_INT:
        stride = 1;
        break;
    case IR_ADD_INT:
        // The stride is the node which isn't the induction node
        if (strideVar->left == nodes[inductionVar])
        {
            stride = Read_Int(constants, strideVar->right->data.constant);
        }
        else if (strideVar->right == nodes[inductionVar])
        {
            stride = Read_Int(constants, strideVar->left->data.constant);
        }
        break;
    default:
        return; /* unsupported stride */
    }

    // 3) Duplicate the loop body N times + anything additional + adjust the loop counter
    // This will leave it in a SSA form requiring reconstruction.

    const int loopCondition = nodes[exit]->data.jump;
    const int loopLimit = Read_Int(constants, nodes[loopLimitVar]->data.constant);
    const int init = Read_Int(constants, initVar->data.constant);
    int numIterations = 0;

    switch (loopCondition)
    {
    case JUMP_GE: /*for (var i = I; i < C; i += S)*/
        if (stride > 0)
        {
            numIterations = (loopLimit - init) / stride;
            // TODO: if C doesn't divide by S exactly
            break;
        }
        return; /* we don't know hoe to deal with negative or zero size strides */
    case JUMP_E: /*for (var i = I; i != C; i += S)*/
        return;
    case JUMP_NE: /*for (var i = I; i == C; i += S)*/
        return;
    default:
        return;
    }

    const int unrollFactor = 4;
    if (numIterations <= unrollFactor)
    {
        return;
    }

    // Construct a set of phis. In a loop body there will be a chain of instructions
    // which resembles a loop coming from a phi node, passing through several nodes
    // and returning to the same phi node. When we generate a snapshot using a phi
    // node and we have an epilogue it should use the value from the epilogue
    // in the snapshot.
    std::vector<int> phis(start);
    for (size_t i = 0; i < start; i++)
    {
        TraceNode* node = nodes[i];
        if (node->data.id == IR_PHI)
        {
            phis[i] = int(i);
        }
    }

    const int epilogueSize = numIterations % unrollFactor;

    // Adjust the loop limit e.g. for (var i = 0; i < 10; i++) => for (var i = 0; i < 8; i += 4)
    TraceNode* limitNode = nodes[loopLimitVar];
    if (limitNode->data.id == IR_LOAD_INT)
    {
        int* val = reinterpret_cast<int*>(constants.data() + limitNode->data.constant);
        *val = *val - epilogueSize * stride;
    }

    // Duplicate the loop body
    int nodesInserted = 0;
    for (size_t j = 0; j < unrollFactor-1; j++)
    {
        for (size_t i = exit + 1; i < end; i++)
        {
            TraceNode* cur = nodes[i];
            TraceNode* node = reinterpret_cast<TraceNode*>(mm->New(sizeof(TraceNode), TY_OBJECT));
            node->data = cur->data;
            node->flags = cur->flags;
            node->pc = cur->pc;
            node->ref = cur->ref;
            node->type = cur->type;
            node->left = cur->left;
            node->right = cur->right;
            nodes.insert(nodes.begin() + end + nodesInserted, node);

            nodesInserted++;
        }
    }

    // Update loopback ref
    loopback->ref += nodesInserted;

    // Patch jumps
    exitNode->data.offset += nodesInserted;
    loopback->data.offset -= nodesInserted;

    // Generate epilogue by cloning the loop body
    for (int j = 0; j < epilogueSize; j++)
    {
        for (size_t i = exit + 1; i < end; i++)
        {
            TraceNode* cur = nodes[i];
            TraceNode* node = reinterpret_cast<TraceNode*>(mm->New(sizeof(TraceNode), TY_OBJECT));
            node->data = cur->data;
            node->flags = cur->flags;
            node->pc = cur->pc;
            node->ref = cur->ref;
            node->type = cur->type;
            node->left = cur->left;
            node->right = cur->right;
            nodes.insert(nodes.begin() + end + nodesInserted + 1, node);

            nodesInserted++;
        }
    }

    // Remap the loop body
    const size_t base = start;
    const size_t size = end + nodesInserted + 1 - base;
    std::vector<int> remap(size);
    Opt_LoopRemap(base, start, loopback->ref, nodes, snaps, remap);

    // Update PHI nodes (possibly store of a list of phi nodes and iterate on them...)
    // PHI nodes are located *before* the loop
    for (size_t i = 0; i < start; i++)
    {
        TraceNode* node = nodes[i];
        if (node->data.id == IR_PHI)
        {
            const int left = node->left->ref - int(start);
            if (left >= 0)
            {
                node->left = nodes[remap[left]];
            }

            const int right = node->right->ref - int(start);
            if (right >= 0)
            {
                node->right = nodes[remap[right]];
            }
        }
    }

    // Reset mapping of loop body nodes as the epilogue nodes should point to the
    // PHI nodes and not the nodes in the loop body
    for (size_t i = exit + 1; i < end; i++)
    {
        TraceNode* node = nodes[i];
        if (node->left && node->left->data.id == IR_PHI)
        {
            remap[i - base] = node->left->ref;
        }
    }

    // Remap the loop epilogue
    Opt_LoopRemap(base, loopback->ref, end + nodesInserted + 1, nodes, snaps, remap);

    // Update refs following the loop epilogue
    for (size_t i = end + nodesInserted + 1; i < nodes.size(); i++)
    {
        nodes[i]->ref = int(i);
    }

    // Remap snapshots:
    // 
    // e.g. 
    // A IR_PHI X F
    // B --
    // C --
    // D --
    // E IR_INCREMENT_INT A
    // F IR_INCREMENT_INT E
    // G --
    // H IR_INCREMENT_INT A
    // I IR_INCREMENT_INT H
    // J IR_SNAP[A]
    //
    // Remap: J IR_SNAP[A] -> J IR_SNAP[I]
    for (size_t i = end + nodesInserted + 1; i < nodes.size(); i++)
    {
        TraceNode* node = nodes[i];
        if (node->data.id == IR_SNAP)
        {
            // Remap the snapshot
            TraceSnapshot& snap = snaps[node->data.snapId];
            for (auto& local : snap.locals)
            {
                if (local.ref->data.id == IR_PHI)
                {
                    //const int ref = local.ref->right->ref;
                    //TraceNode* phi = nodes[remap[ref - int(start)]];
                    //assert(phi->data.id == IR_PHI);
                    //local.ref = phi;

                    //local.ref = nodes[remap[local.ref->ref - int(base)]];
                }
            }
        }
    }
}

static void Opt_Optimize_Loop(MemoryManager* mm,
    std::vector<unsigned char>& constants,
    std::vector<TraceNode*>& nodes, 
    std::vector<TraceSnapshot>& snaps, size_t& pos)
{
    const size_t start = pos;
    for (; pos < nodes.size(); pos++)
    {
        if (nodes[pos]->data.id == IR_LOOPBACK)
        {
            if (pos - start <= UNROLL_LIMIT)
            {
                Opt_Partial_Unroll_Loop(mm, constants, nodes, snaps, start, pos);
            }
            break;
        }
    }
}

void SunScript::Opt_Optimize_Loops(MemoryManager* mm,
    std::vector<unsigned char>& constants,
    std::vector<TraceNode*>& nodes,
    std::vector<TraceSnapshot>& snaps)
{
    for (size_t i = 0; i < nodes.size(); i++)
    {
        if (nodes[i]->data.id == IR_LOOPSTART)
        {
            Opt_Optimize_Loop(mm, constants, nodes, snaps, i);
        }
    }
}

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
