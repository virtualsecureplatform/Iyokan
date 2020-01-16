#ifndef VIRTUALSECUREPLATFORM_IYOKAN_PLAIN_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_PLAIN_HPP

#include "main.hpp"

using TaskPlainGate = Task<uint8_t, uint8_t, uint8_t /*dummy*/>;
using TaskPlainGateMem = TaskMem<uint8_t, uint8_t, uint8_t /* dummy */>;
using TaskPlainGateDFF = TaskDFF<uint8_t, uint8_t, uint8_t /* dummy */>;

class TaskPlainGateWIRE
    : public TaskMem<uint8_t, uint8_t, uint8_t /* dummy */> {
private:
    AsyncThread thr_;

private:
    void startAsyncImpl(uint8_t) override
    {
        if (inputSize() == 0) {
            // Nothing to do!
        }
        else if (inputSize() == 1) {
            thr_ = [&]() { output() = input(0); };
        }
        else {
            assert(false);
        }
    }

public:
    TaskPlainGateWIRE(bool inputNeeded)
        : TaskMem<uint8_t, uint8_t, uint8_t /* dummy */>(inputNeeded ? 1 : 0)
    {
    }

    bool hasFinished() const override
    {
        return inputSize() == 0 || thr_.hasFinished();
    }
};

#define DEFINE_TASK_PLAIN_GATE(name, numInputs, expr)    \
    class TaskPlainGate##name : public TaskPlainGate {   \
    private:                                             \
        AsyncThread thr_;                                \
                                                         \
    private:                                             \
        void startAsyncImpl(uint8_t) override            \
        {                                                \
            thr_ = [&]() { output() = (expr)&1; };       \
        }                                                \
                                                         \
    public:                                              \
        TaskPlainGate##name() : TaskPlainGate(numInputs) \
        {                                                \
        }                                                \
                                                         \
        bool hasFinished() const override                \
        {                                                \
            return thr_.hasFinished();                   \
        }                                                \
    };
DEFINE_TASK_PLAIN_GATE(AND, 2, (input(0) & input(1)));
DEFINE_TASK_PLAIN_GATE(NAND, 2, ~(input(0) & input(1)));
DEFINE_TASK_PLAIN_GATE(ANDNOT, 2, (input(0) & ~input(1)));
DEFINE_TASK_PLAIN_GATE(OR, 2, (input(0) | input(1)));
DEFINE_TASK_PLAIN_GATE(NOR, 2, ~(input(0) | input(1)));
DEFINE_TASK_PLAIN_GATE(ORNOT, 2, (input(0) | ~input(1)));
DEFINE_TASK_PLAIN_GATE(XOR, 2, (input(0) ^ input(1)));
DEFINE_TASK_PLAIN_GATE(XNOR, 2, ~(input(0) ^ input(1)));
DEFINE_TASK_PLAIN_GATE(MUX, 3, input(2) == 0 ? input(0) : input(1));
DEFINE_TASK_PLAIN_GATE(NOT, 1, ~input(0));
#undef DEFINE_TASK_PLAIN_GATE

class PlainNetworkBuilder
    : public NetworkBuilder<TaskPlainGate, TaskPlainGateMem, TaskPlainGateDFF,
                            TaskPlainGateWIRE, uint8_t /* dummy */> {
private:
#define DEFINE_GATE_IMPL(name)                           \
    std::shared_ptr<TaskPlainGate> name##Impl() override \
    {                                                    \
        return std::make_shared<TaskPlainGate##name>();  \
    }
    DEFINE_GATE_IMPL(AND);
    DEFINE_GATE_IMPL(NAND);
    DEFINE_GATE_IMPL(ANDNOT);
    DEFINE_GATE_IMPL(OR);
    DEFINE_GATE_IMPL(NOR);
    DEFINE_GATE_IMPL(ORNOT);
    DEFINE_GATE_IMPL(XOR);
    DEFINE_GATE_IMPL(XNOR);
    DEFINE_GATE_IMPL(MUX);
    DEFINE_GATE_IMPL(NOT);
#undef DEFINE_GATE_IMPL
};

using PlainNetwork = PlainNetworkBuilder::NetworkType;

class PlainWorker : public Worker<uint8_t /* dummy */> {
private:
    uint8_t getWorkerInfo()
    {
        return 0;  // dummy
    }

public:
    PlainWorker(ReadyQueue<uint8_t> &readyQueue, size_t &numFinishedTargets,
                std::shared_ptr<ProgressGraphMaker> graph)
        : Worker(readyQueue, numFinishedTargets, graph)
    {
    }
};

#endif
