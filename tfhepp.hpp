#ifndef VIRTUALSECUREPLATFORM_IYOKAN_L2_2_TFHEPP_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_L2_2_TFHEPP_HPP

#include "main.hpp"

//
#include <tfhe++.hpp>

using TFHEppWorkerInfo = std::shared_ptr<const TFHEpp::GateKey>;
using TaskTFHEppGate =
    Task<TFHEpp::TLWElvl0, TFHEpp::TLWElvl0, TFHEppWorkerInfo>;
using TaskTFHEppGate =
    Task<TFHEpp::TLWElvl0, TFHEpp::TLWElvl0, TFHEppWorkerInfo>;
using TaskTFHEppGateMem =
    TaskMem<TFHEpp::TLWElvl0, TFHEpp::TLWElvl0, TFHEppWorkerInfo>;
using TaskTFHEppGateDFF =
    TaskDFF<TFHEpp::TLWElvl0, TFHEpp::TLWElvl0, TFHEppWorkerInfo>;

class TaskTFHEppGateWIRE
    : public TaskMem<TFHEpp::TLWElvl0, TFHEpp::TLWElvl0, TFHEppWorkerInfo> {
private:
    AsyncThread thr_;

private:
    void startAsyncImpl(TFHEppWorkerInfo) override
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
    TaskTFHEppGateWIRE(bool inputNeeded)
        : TaskMem<TFHEpp::TLWElvl0, TFHEpp::TLWElvl0, TFHEppWorkerInfo>(
              inputNeeded ? 1 : 0)
    {
    }

    bool hasFinished() const override
    {
        return inputSize() == 0 || thr_.hasFinished();
    }
};

#define DEFINE_TASK_GATE(name, numInputs, expr)            \
    class TaskTFHEppGate##name : public TaskTFHEppGate {   \
    private:                                               \
        AsyncThread thr_;                                  \
                                                           \
    private:                                               \
        void startAsyncImpl(TFHEppWorkerInfo gk) override  \
        {                                                  \
            thr_ = [this, gk]() { (expr); };               \
        }                                                  \
                                                           \
    public:                                                \
        TaskTFHEppGate##name() : TaskTFHEppGate(numInputs) \
        {                                                  \
        }                                                  \
                                                           \
        bool hasFinished() const override                  \
        {                                                  \
            return thr_.hasFinished();                     \
        }                                                  \
    };
DEFINE_TASK_GATE(AND, 2, TFHEpp::HomAND(output(), input(0), input(1), *gk));
DEFINE_TASK_GATE(NAND, 2, TFHEpp::HomNAND(output(), input(0), input(1), *gk));
DEFINE_TASK_GATE(ANDNOT, 2,
                 TFHEpp::HomANDYN(output(), input(0), input(1), *gk));
DEFINE_TASK_GATE(OR, 2, TFHEpp::HomOR(output(), input(0), input(1), *gk));
DEFINE_TASK_GATE(NOR, 2, TFHEpp::HomNOR(output(), input(0), input(1), *gk));
DEFINE_TASK_GATE(ORNOT, 2, TFHEpp::HomORYN(output(), input(0), input(1), *gk));
DEFINE_TASK_GATE(XOR, 2, TFHEpp::HomXOR(output(), input(0), input(1), *gk));
DEFINE_TASK_GATE(XNOR, 2, TFHEpp::HomXNOR(output(), input(0), input(1), *gk));
DEFINE_TASK_GATE(MUX, 3,
                 TFHEpp::HomMUX(output(), input(2), input(1), input(0), *gk));
DEFINE_TASK_GATE(NOT, 1, TFHEpp::HomNOT(output(), input(0)));
#undef DEFINE_TASK_GATE

class TFHEppNetworkBuilder
    : public NetworkBuilder<TaskTFHEppGate, TaskTFHEppGateMem,
                            TaskTFHEppGateDFF, TaskTFHEppGateWIRE,
                            TFHEppWorkerInfo> {
private:
#define DEFINE_GATE_IMPL(name)                            \
    std::shared_ptr<TaskTFHEppGate> name##Impl() override \
    {                                                     \
        return std::make_shared<TaskTFHEppGate##name>();  \
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

using TFHEppNetwork = TFHEppNetworkBuilder::NetworkType;

class TFHEppWorker : public Worker<TFHEppWorkerInfo> {
private:
    TFHEppWorkerInfo gk_;

private:
    TFHEppWorkerInfo getWorkerInfo()
    {
        return gk_;
    }

public:
    TFHEppWorker(TFHEppWorkerInfo gk, ReadyQueue<TFHEppWorkerInfo>& readyQueue,
                 size_t& numFinishedTargets,
                 std::shared_ptr<ProgressGraphMaker> graph)
        : Worker(readyQueue, numFinishedTargets, graph), gk_(std::move(gk))
    {
    }
};

#endif
