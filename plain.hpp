#ifndef VIRTUALSECUREPLATFORM_IYOKAN_L2_2_PLAIN_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_L2_2_PLAIN_HPP

#include "main.hpp"

using TaskPlainGate = Task<uint8_t, uint8_t, uint8_t /*dummy*/>;

// TaskPlainGateMem can be used as INPUT/OUTPUT/ROM/RAM/DFF depending on how to
// connect it to other gates.
class TaskPlainGateMem : public TaskPlainGate {
private:
    bool clockNeeded_;
    uint8_t val_;

private:
    void startAsyncImpl(uint8_t) override
    {
        if (clockNeeded_)
            return;

        if (inputSize() == 0) {  // INPUT / ROM
            output() = val_;
        }
        else if (inputSize() == 1) {  // OUTPUT
            val_ = input(0);
            output() = val_;
        }
        else {
            assert(false);
        }
    }

public:
    TaskPlainGateMem(bool inputNeeded, bool clockNeeded)
        : TaskPlainGate(inputNeeded ? 1 : 0), clockNeeded_(clockNeeded)
    {
    }

    void tick() override
    {
        TaskPlainGate::tick();

        if (clockNeeded_) {  // DFF / RAM
            assert(inputSize() == 1);
            val_ = input(0);
            output() = val_;
        }
    }


    bool hasFinished() const override
    {
        return true;
    }

    void set(uint8_t val)
    {
        val_ = val & 1;
    }

    uint8_t get() const
    {
        return val_;
    }
};

#define DEFINE_TASK_PLAIN_GATE(name, numInputs, expr)    \
    class TaskPlainGate##name : public TaskPlainGate {   \
    private:                                             \
        void startAsyncImpl(uint8_t) override            \
        {                                                \
            output() = (expr)&1;                         \
        }                                                \
                                                         \
    public:                                              \
        TaskPlainGate##name() : TaskPlainGate(numInputs) \
        {                                                \
        }                                                \
        bool hasFinished() const override                \
        {                                                \
            return true;                                 \
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
    : public NetworkBuilder<TaskPlainGate, TaskPlainGateMem,
                            uint8_t /* dummy */> {
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
    PlainWorker(Worker<uint8_t>::QueueType &readyQueue,
                size_t &numFinishedTargets)
        : Worker(readyQueue, numFinishedTargets)
    {
    }
};

#endif
