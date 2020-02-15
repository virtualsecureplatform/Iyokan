#ifndef VIRTUALSECUREPLATFORM_IYOKAN_CUFHE_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_CUFHE_HPP

#include <cufhe.h>
#include <cufhe_gpu.cuh>

#include "iyokan.hpp"
#include "iyokan_tfhepp.hpp"

class CUFHEStream {
private:
    std::unique_ptr<cufhe::Stream> st_;

public:
    CUFHEStream() : st_(std::make_unique<cufhe::Stream>())
    {
        st_->Create();
    }

    ~CUFHEStream()
    {
        st_->Destroy();
    }

    operator cufhe::Stream() const
    {
        return *st_;
    }
};

struct CUFHEWorkerInfo {
    std::shared_ptr<CUFHEStream> stream;
};

using TaskCUFHEGate = Task<cufhe::Ctxt, cufhe::Ctxt, CUFHEWorkerInfo>;

inline void copyCtxt(cufhe::Ctxt& dst, const cufhe::Ctxt& src,
                     std::shared_ptr<CUFHEStream> stream = nullptr)
{
    if (stream) {
        cufhe::Copy(dst, src, *stream);
        cufhe::Synchronize();
    }
    else {
        cufhe::CopyOnHost(dst, src);
    }
}

class TaskCUFHEGateMem : public TaskCUFHEGate {
public:
    TaskCUFHEGateMem(int numInputs) : TaskCUFHEGate(numInputs)
    {
    }

    void set(const cufhe::Ctxt& newval)
    {
        copyCtxt(output(), newval);
    }

    const cufhe::Ctxt& get() const
    {
        return output();
    }
};

class TaskCUFHEGateDFF : public TaskCUFHEGateMem {
protected:
    void startAsyncImpl(CUFHEWorkerInfo) override
    {
    }

public:
    TaskCUFHEGateDFF() : TaskCUFHEGateMem(1)
    {
        cufhe::ConstantZero(output());
    }

    bool areInputsReady() const override
    {
        // Since areInputsReady() is called after calling of tick(), the
        // input should already be in val_.
        return true;
    }

    void tick() override
    {
        TaskCUFHEGateMem::tick();
        copyCtxt(output(), input(0));
    }

    bool hasFinished() const override
    {
        // Since hasFinished() is called after calling of tick(), the
        // input should already be in val_.
        return true;
    }
};

class TaskCUFHEGateWIRE : public TaskCUFHEGateMem {
private:
    CUFHEWorkerInfo wi_;

private:
    void startAsyncImpl(CUFHEWorkerInfo wi) override
    {
        wi_ = std::move(wi);

        switch (getInputSize()) {
        case 0:
            // Nothing to do!
            break;
        case 1:
            copyCtxt(output(), input(0), wi_.stream);
            break;
        default:
            assert(false);
        }
    }

public:
    TaskCUFHEGateWIRE(bool inputNeeded) : TaskCUFHEGateMem(inputNeeded ? 1 : 0)
    {
    }

    bool hasFinished() const override
    {
        return getInputSize() == 0 || cufhe::StreamQuery(*wi_.stream);
    }
};

#define DEFINE_TASK_GATE(name, numInputs, expr)          \
    class TaskCUFHEGate##name : public TaskCUFHEGate {   \
    private:                                             \
        CUFHEWorkerInfo wi_;                             \
                                                         \
    private:                                             \
        void startAsyncImpl(CUFHEWorkerInfo wi) override \
        {                                                \
            wi_ = std::move(wi);                         \
            auto st = wi_.stream;                        \
            (expr);                                      \
        }                                                \
                                                         \
    public:                                              \
        TaskCUFHEGate##name() : TaskCUFHEGate(numInputs) \
        {                                                \
        }                                                \
        bool hasFinished() const override                \
        {                                                \
            return cufhe::StreamQuery(*wi_.stream);      \
        }                                                \
    };
DEFINE_TASK_GATE(AND, 2, cufhe::And(output(), input(0), input(1), *st));
DEFINE_TASK_GATE(NAND, 2, cufhe::Nand(output(), input(0), input(1), *st));
DEFINE_TASK_GATE(ANDNOT, 2, cufhe::AndYN(output(), input(0), input(1), *st));
DEFINE_TASK_GATE(OR, 2, cufhe::Or(output(), input(0), input(1), *st));
DEFINE_TASK_GATE(NOR, 2, cufhe::Nor(output(), input(0), input(1), *st));
DEFINE_TASK_GATE(ORNOT, 2, cufhe::OrYN(output(), input(0), input(1), *st));
DEFINE_TASK_GATE(XOR, 2, cufhe::Xor(output(), input(0), input(1), *st));
DEFINE_TASK_GATE(XNOR, 2, cufhe::Xnor(output(), input(0), input(1), *st));
DEFINE_TASK_GATE(MUX, 3,
                 cufhe::Mux(output(), input(2), input(1), input(0), *st));
DEFINE_TASK_GATE(NOT, 1, cufhe::Not(output(), input(0), *st));
#undef DEFINE_TASK_GATE

class CUFHENetworkBuilder
    : public NetworkBuilder<TaskCUFHEGate, TaskCUFHEGateMem, TaskCUFHEGateDFF,
                            TaskCUFHEGateWIRE, CUFHEWorkerInfo> {
private:
#define DEFINE_GATE_IMPL(name)                           \
    std::shared_ptr<TaskCUFHEGate> name##Impl() override \
    {                                                    \
        return std::make_shared<TaskCUFHEGate##name>();  \
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

using CUFHENetwork = CUFHENetworkBuilder::NetworkType;

class CUFHEWorker : public Worker<CUFHEWorkerInfo> {
private:
    CUFHEWorkerInfo wi_;

private:
    CUFHEWorkerInfo getWorkerInfo() override
    {
        return wi_;
    }

public:
    CUFHEWorker(ReadyQueue<CUFHEWorkerInfo>& readyQueue,
                size_t& numFinishedTargets,
                std::shared_ptr<ProgressGraphMaker> graph)
        : Worker(readyQueue, numFinishedTargets, graph)
    {
        wi_.stream = std::make_shared<CUFHEStream>();
    }
};

inline TFHEpp::TLWElvl0 cufhe2tfhepp(const cufhe::Ctxt& src)
{
    // FIXME: Check if TFHEpp's parameters are the same as cuFHE's.
    const int32_t n = cufhe::GetDefaultParam()->lwe_n_;

    TFHEpp::TLWElvl0 tlwe;
    for (int i = 0; i < n + 1; i++)
        tlwe[i] = src.lwe_sample_->data()[i];

    return tlwe;
}

inline std::shared_ptr<cufhe::Ctxt> tfhepp2cufhe(const TFHEpp::TLWElvl0& src)
{
    // FIXME: Check if TFHEpp's parameters are the same as cuFHE's.
    const int32_t n = cufhe::GetDefaultParam()->lwe_n_;

    auto ctxt = std::make_shared<cufhe::Ctxt>();
    for (int i = 0; i < n + 1; i++)
        ctxt->lwe_sample_->data()[i] = src[i];

    return ctxt;
}

class TaskCUFHE2TFHEpp
    : public TaskAsync<cufhe::Ctxt, TFHEpp::TLWElvl0, TFHEppWorkerInfo> {
private:
    void startSync(TFHEppWorkerInfo) override
    {
        output() = cufhe2tfhepp(input(0));
    }

public:
    TaskCUFHE2TFHEpp()
        : TaskAsync<cufhe::Ctxt, TFHEpp::TLWElvl0, TFHEppWorkerInfo>(1)
    {
    }
};

class TaskTFHEpp2CUFHE
    : public TaskAsync<TFHEpp::TLWElvl0, cufhe::Ctxt, TFHEppWorkerInfo> {
private:
    void startSync(TFHEppWorkerInfo) override
    {
        copyCtxt(output(), *tfhepp2cufhe(input(0)));
    }

public:
    TaskTFHEpp2CUFHE()
        : TaskAsync<TFHEpp::TLWElvl0, cufhe::Ctxt, TFHEppWorkerInfo>(1)
    {
    }
};

using CUFHE2TFHEppBridge = BridgeDepNode<CUFHEWorkerInfo, TFHEppWorkerInfo>;
using TFHEpp2CUFHEBridge = BridgeDepNode<TFHEppWorkerInfo, CUFHEWorkerInfo>;

class CUFHENetworkRunner {
private:
    template <class WorkerInfo, class WorkerType>
    struct Runner {
        std::shared_ptr<ReadyQueue<WorkerInfo>> readyQueue;
        size_t numFinishedTargets;
        std::vector<WorkerType> workers;
        std::vector<std::shared_ptr<TaskNetwork<WorkerInfo>>> nets;

        Runner()
            : readyQueue(std::make_shared<ReadyQueue<WorkerInfo>>()),
              numFinishedTargets(0)
        {
        }

        bool isValid()
        {
            for (auto&& net : nets)
                if (!net->isValid())
                    return false;
            return true;
        }

        template <class... Args>
        void addWorker(Args&&... args)
        {
            workers.emplace_back(*readyQueue, numFinishedTargets,
                                 std::forward<Args>(args)...);
        }

        void prepareToRun()
        {
            assert(readyQueue->empty());
            assert(nets.empty() || workers.size() > 0);

            numFinishedTargets = 0;
            for (auto&& net : nets)
                net->pushReadyTasks(*readyQueue);
        }

        size_t numNodes() const
        {
            size_t ret = 0;
            for (auto&& net : nets)
                ret += net->numNodes();
            return ret;
        }

        bool isRunning() const
        {
            return std::any_of(workers.begin(), workers.end(),
                               [](auto&& w) { return w.isWorking(); }) ||
                   !readyQueue->empty();
        }

        void update()
        {
            for (auto&& w : workers)
                w.update();
        }

        void tick()
        {
            for (auto&& net : nets)
                net->tick();
        }
    };

    Runner<CUFHEWorkerInfo, CUFHEWorker> cufhe_;
    Runner<TFHEppWorkerInfo, TFHEppWorker> tfhepp_;
    std::vector<std::shared_ptr<CUFHE2TFHEppBridge>> bridges0_;
    std::vector<std::shared_ptr<TFHEpp2CUFHEBridge>> bridges1_;

public:
    CUFHENetworkRunner(int numCUFHEWorkers, int numTFHEppWorkers,
                       TFHEppWorkerInfo wi)
    {
        for (int i = 0; i < numCUFHEWorkers; i++)
            cufhe_.addWorker(nullptr);
        for (int i = 0; i < numTFHEppWorkers; i++)
            tfhepp_.addWorker(wi, nullptr);
    }

    bool isValid()
    {
        if (!cufhe_.isValid())
            return false;
        if (!tfhepp_.isValid())
            return false;
        for (auto&& b : bridges0_)
            if (!b->task()->isValid())
                return false;
        for (auto&& b : bridges1_)
            if (!b->task()->isValid())
                return false;
        return true;
    }

    void addNetwork(std::shared_ptr<CUFHENetwork> net)
    {
        cufhe_.nets.push_back(net);
    }

    void addNetwork(std::shared_ptr<TFHEppNetwork> net)
    {
        tfhepp_.nets.push_back(net);
    }

    void addBridge(
        std::shared_ptr<BridgeDepNode<CUFHEWorkerInfo, TFHEppWorkerInfo>>
            bridge)
    {
        bridge->setReadyQueue(tfhepp_.readyQueue);
        bridges0_.push_back(bridge);
    }

    void addBridge(
        std::shared_ptr<BridgeDepNode<TFHEppWorkerInfo, CUFHEWorkerInfo>>
            bridge)
    {
        bridge->setReadyQueue(cufhe_.readyQueue);
        bridges1_.push_back(bridge);
    }

    void run()
    {
        cufhe_.prepareToRun();
        tfhepp_.prepareToRun();

        size_t numNodes = cufhe_.numNodes() + tfhepp_.numNodes() +
                          bridges0_.size() + bridges1_.size();

        while (cufhe_.numFinishedTargets + tfhepp_.numFinishedTargets <
               numNodes) {
            assert((cufhe_.isRunning() || tfhepp_.isRunning()) &&
                   "Detected infinite loop");
            cufhe_.update();
            tfhepp_.update();
        }
    }

    void tick()
    {
        cufhe_.tick();
        tfhepp_.tick();
        for (auto&& bridge : bridges0_)
            bridge->tick();
        for (auto&& bridge : bridges1_)
            bridge->tick();
    }
};

void doCUFHE(const Options& opt);
void processAllGates(CUFHENetwork& net, int numWorkers,
                     std::shared_ptr<ProgressGraphMaker> graph = nullptr);
#endif
