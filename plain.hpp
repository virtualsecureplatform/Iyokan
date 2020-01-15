#ifndef VIRTUALSECUREPLATFORM_IYOKAN_L2_2_PLAIN_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_L2_2_PLAIN_HPP

#include "main.hpp"

//
#include <thread>

using TaskPlainGate = Task<uint8_t, uint8_t, uint8_t /*dummy*/>;

class TaskPlainGateMem : public TaskPlainGate {
protected:
    uint8_t val_;

public:
    TaskPlainGateMem(int numInputs) : TaskPlainGate(numInputs)
    {
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

// DFF/RAM
class TaskPlainGateDFF : public TaskPlainGateMem {
private:
    void startAsyncImpl(uint8_t) override
    {
    }

public:
    TaskPlainGateDFF() : TaskPlainGateMem(1)
    {
    }

    bool areInputsReady() const override
    {
        // Since areInputsReady() is called after calling of tick(), the
        // input should already be in val_.
        return true;
    }

    void tick() override
    {
        TaskPlainGate::tick();

        val_ = input(0);
        output() = val_;
    }

    bool hasFinished() const override
    {
        // Since hasFinished() is called after calling of tick(), the
        // input should already be in val_.
        return true;
    }
};

// INPUT/OUTPUT/ROM
class TaskPlainGateWIRE : public TaskPlainGateMem {
private:
    AsyncThread thr_;

private:
    void startAsyncImpl(uint8_t) override
    {
        if (inputSize() == 0) {  // INPUT / ROM
            thr_ = [&]() { output() = val_; };
        }
        else if (inputSize() == 1) {  // OUTPUT
            thr_ = [&]() {
                val_ = input(0);
                output() = val_;
            };
        }
        else {
            assert(false);
        }
    }

public:
    TaskPlainGateWIRE(bool inputNeeded) : TaskPlainGateMem(inputNeeded ? 1 : 0)
    {
    }

    bool hasFinished() const override
    {
        return thr_.hasFinished();
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
