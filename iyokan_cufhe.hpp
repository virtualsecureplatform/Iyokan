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

CEREAL_REGISTER_TYPE(BridgeDepNode<TFHEppWorkerInfo, CUFHEWorkerInfo>);
CEREAL_REGISTER_TYPE(BridgeDepNode<CUFHEWorkerInfo, TFHEppWorkerInfo>);
CEREAL_REGISTER_TYPE(TaskBlackHole<TFHEppWorkerInfo>);
CEREAL_REGISTER_TYPE(TaskBlackHole<CUFHEWorkerInfo>);

using TaskCUFHEGate = Task<cufhe::Ctxt, cufhe::Ctxt, CUFHEWorkerInfo>;

inline void copyCtxt(cufhe::Ctxt& dst, const cufhe::Ctxt& src,
                     std::shared_ptr<CUFHEStream> stream = nullptr)
{
    if (stream) {
        cufhe::Copy(dst, src, *stream);
    }
    else {
        cufhe::CopyOnHost(dst, src);
    }
}

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

namespace cufhe {
template <class Archive>
void save(Archive& ar, const Ctxt& ctxt)
{
    ar(cufhe2tfhepp(ctxt));
}

template <class Archive>
void load(Archive& ar, Ctxt& ctxt)
{
    TFHEpp::TLWElvl0 tlwe;
    ar(tlwe);
    copyCtxt(ctxt, *tfhepp2cufhe(tlwe));
}

template <class Archive>
void serialize(Archive& ar, cuFHETRLWElvl1& trlwe)
{
    ar(trlwe.trlwehost);
}

}  // namespace cufhe

class TaskCUFHEGateMem : public TaskCUFHEGate {
public:
    TaskCUFHEGateMem()
    {
    }

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

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(cereal::base_class<TaskCUFHEGate>(this));
    }
};
CEREAL_REGISTER_TYPE(TaskCUFHEGateMem);

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

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(cereal::base_class<TaskCUFHEGateMem>(this));
    }
};
CEREAL_REGISTER_TYPE(TaskCUFHEGateDFF);

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
    TaskCUFHEGateWIRE()
    {
    }

    TaskCUFHEGateWIRE(bool inputNeeded) : TaskCUFHEGateMem(inputNeeded ? 1 : 0)
    {
    }

    bool hasFinished() const override
    {
        return getInputSize() == 0 || cufhe::StreamQuery(*wi_.stream);
    }

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(cereal::base_class<TaskCUFHEGateMem>(this));
    }
};
CEREAL_REGISTER_TYPE(TaskCUFHEGateWIRE);

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
        template <class Archive>                         \
        void serialize(Archive& ar)                      \
        {                                                \
            ar(cereal::base_class<TaskCUFHEGate>(this)); \
        }                                                \
    };                                                   \
    CEREAL_REGISTER_TYPE(TaskCUFHEGate##name);
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

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(cereal::base_class<
            TaskAsync<cufhe::Ctxt, TFHEpp::TLWElvl0, TFHEppWorkerInfo>>(this));
    }
};
CEREAL_REGISTER_TYPE(TaskCUFHE2TFHEpp);

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

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(cereal::base_class<
            TaskAsync<TFHEpp::TLWElvl0, cufhe::Ctxt, TFHEppWorkerInfo>>(this));
    }
};
CEREAL_REGISTER_TYPE(TaskTFHEpp2CUFHE);

class TaskTFHEppRAMCMUXsForCUFHE : public TaskBase<TFHEppWorkerInfo> {
private:
    size_t numReadyInputs_;
    std::shared_ptr<cufhe::cuFHETRLWElvl1> output_;
    std::vector<std::weak_ptr<const TRGSWFFTlvl1Pair>> inputAddrs_;
    std::weak_ptr<const TFHEpp::TRLWElvl1> inputWritten_;

    std::weak_ptr<cufhe::cuFHETRLWElvl1> mem_;
    std::bitset<TaskTFHEppRAMUX::ADDRESS_BIT> addrBitset_;

    AsyncThread thr_;

public:
    TaskTFHEppRAMCMUXsForCUFHE()
    {
    }

    TaskTFHEppRAMCMUXsForCUFHE(std::weak_ptr<cufhe::cuFHETRLWElvl1> mem,
                               size_t memIndex)
        : numReadyInputs_(0),
          output_(std::make_shared<cufhe::cuFHETRLWElvl1>()),
          inputAddrs_(TaskTFHEppRAMUX::ADDRESS_BIT),
          mem_(std::move(mem)),
          addrBitset_(memIndex)
    {
    }

    virtual ~TaskTFHEppRAMCMUXsForCUFHE()
    {
    }

    size_t getInputSize() const override
    {
        return TaskTFHEppRAMUX::ADDRESS_BIT + 1;
    }

    void checkValid(error::Stack& err) override
    {
        assert(this->depnode());

        const NodeLabel& label = this->depnode()->label();
        if (!std::all_of(inputAddrs_.begin(), inputAddrs_.end(),
                         [](auto&& in) { return in.use_count() != 0; }) ||
            inputWritten_.use_count() == 0)
            err.add("Not enough inputs: ", label.str());
    }

    void tick() override
    {
        numReadyInputs_ = 0;
    }

    void notifyOneInputReady() override
    {
        numReadyInputs_++;
        assert(numReadyInputs_ <= TaskTFHEppRAMUX::ADDRESS_BIT + 1);
    }

    bool areInputsReady() const override
    {
        return numReadyInputs_ == TaskTFHEppRAMUX::ADDRESS_BIT + 1;
    }

    bool hasFinished() const override
    {
        return thr_.hasFinished();
    }

    void addInputPtr(const std::shared_ptr<const TRGSWFFTlvl1Pair>& input)
    {
        auto it = std::find_if(inputAddrs_.begin(), inputAddrs_.end(),
                               [](auto&& in) { return in.use_count() == 0; });
        assert(it != inputAddrs_.end());
        *it = input;
    }

    void addInputPtr(const std::shared_ptr<const TFHEpp::TRLWElvl1>& input)
    {
        assert(inputWritten_.use_count() == 0);
        inputWritten_ = input;
    }

    std::shared_ptr<const cufhe::cuFHETRLWElvl1> getOutputPtr() const
    {
        return output_;
    }

    void startAsync(TFHEppWorkerInfo) override
    {
        thr_ = [this] {
            output_->trlwehost = *inputWritten_.lock();
            for (size_t j = 0; j < TaskTFHEppRAMUX::ADDRESS_BIT; j++) {
                const TFHEpp::TRGSWFFTlvl1& in =
                    addrBitset_[j] != 0 ? inputAddrs_[j].lock()->normal
                                        : inputAddrs_[j].lock()->inverted;
                TFHEpp::CMUXFFTlvl1(output_->trlwehost, in, output_->trlwehost,
                                    mem_.lock()->trlwehost);
            }
        };
    }

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(cereal::base_class<TaskBase<TFHEppWorkerInfo>>(this),
           numReadyInputs_, output_, inputAddrs_, inputWritten_, mem_,
           addrBitset_);
    }
};
CEREAL_REGISTER_TYPE(TaskTFHEppRAMCMUXsForCUFHE);

class TaskTFHEpp2CUFHETRLWElvl1
    : public TaskAsync<TFHEpp::TRLWElvl1, cufhe::cuFHETRLWElvl1,
                       TFHEppWorkerInfo> {
private:
    void startSync(TFHEppWorkerInfo) override
    {
        output().trlwehost = input(0);
    }

public:
    TaskTFHEpp2CUFHETRLWElvl1()
        : TaskAsync<TFHEpp::TRLWElvl1, cufhe::cuFHETRLWElvl1, TFHEppWorkerInfo>(
              1)
    {
    }

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(cereal::base_class<TaskAsync<
               TFHEpp::TRLWElvl1, cufhe::cuFHETRLWElvl1, TFHEppWorkerInfo>>(
            this));
    }
};
CEREAL_REGISTER_TYPE(TaskTFHEpp2CUFHETRLWElvl1);

class TaskCUFHERAMUX
    : public TaskAsync<TRGSWFFTlvl1Pair, TFHEpp::TRLWElvl1, TFHEppWorkerInfo> {
public:
    const static size_t ADDRESS_BIT = 8;

private:
    std::vector<std::shared_ptr<cufhe::cuFHETRLWElvl1>> data_;

private:
    void RAMUX()
    {
        const uint32_t num_trlwe = 1 << ADDRESS_BIT;
        std::array<TFHEpp::TRLWElvl1, num_trlwe / 2> temp;
        auto addr = [this](size_t i) -> const TFHEpp::TRGSWFFTlvl1& {
            return input(i).inverted;
        };

        for (uint32_t index = 0; index < num_trlwe / 2; index++) {
            TFHEpp::CMUXFFTlvl1(temp[index], addr(0),
                                data_[2 * index]->trlwehost,
                                data_[2 * index + 1]->trlwehost);
        }

        for (uint32_t bit = 0; bit < (ADDRESS_BIT - 2); bit++) {
            const uint32_t stride = 1 << bit;
            for (uint32_t index = 0; index < (num_trlwe >> (bit + 2));
                 index++) {
                TFHEpp::CMUXFFTlvl1(temp[(2 * index) * stride], addr(bit + 1),
                                    temp[(2 * index) * stride],
                                    temp[(2 * index + 1) * stride]);
            }
        }

        const uint32_t stride = 1 << (ADDRESS_BIT - 2);
        TFHEpp::CMUXFFTlvl1(output(), addr(ADDRESS_BIT - 1), temp[0],
                            temp[stride]);
    }

    void startSync(TFHEppWorkerInfo) override
    {
        RAMUX();
    }

public:
    TaskCUFHERAMUX()
        : TaskAsync<TRGSWFFTlvl1Pair, TFHEpp::TRLWElvl1, TFHEppWorkerInfo>(
              ADDRESS_BIT),
          data_(1 << ADDRESS_BIT)
    {
        for (auto&& p : data_)
            p = std::make_shared<cufhe::cuFHETRLWElvl1>();
    }

    size_t size() const
    {
        return 1 << ADDRESS_BIT;
    }

    std::shared_ptr<const cufhe::cuFHETRLWElvl1> get(size_t addr) const
    {
        return data_.at(addr);
    }

    std::shared_ptr<cufhe::cuFHETRLWElvl1> get(size_t addr)
    {
        return data_.at(addr);
    }

    void set(size_t addr, TFHEpp::TRLWElvl1 val)
    {
        data_.at(addr)->trlwehost = std::move(val);
    }

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(cereal::base_class<TaskAsync<TRGSWFFTlvl1Pair, TFHEpp::TRLWElvl1,
                                        TFHEppWorkerInfo>>(this),
           data_);
    }
};
CEREAL_REGISTER_TYPE(TaskCUFHERAMUX);

class TaskCUFHERAMSEIAndKS
    : public Task<cufhe::cuFHETRLWElvl1, cufhe::Ctxt, CUFHEWorkerInfo> {
private:
    CUFHEWorkerInfo wi_;

private:
    void startAsyncImpl(CUFHEWorkerInfo wi) override
    {
        wi_ = std::move(wi);
        cufhe::SampleExtractAndKeySwitch(output(), input(0), *wi_.stream);
    }

public:
    TaskCUFHERAMSEIAndKS()
        : Task<cufhe::cuFHETRLWElvl1, cufhe::Ctxt, CUFHEWorkerInfo>(1)
    {
    }

    bool hasFinished() const override
    {
        return cufhe::StreamQuery(*wi_.stream);
    }

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(cereal::base_class<
            Task<cufhe::cuFHETRLWElvl1, cufhe::Ctxt, CUFHEWorkerInfo>>(this));
    }
};
CEREAL_REGISTER_TYPE(TaskCUFHERAMSEIAndKS);

class TaskCUFHERAMGateBootstrapping
    : public Task<cufhe::Ctxt, uint8_t /* dummy */, CUFHEWorkerInfo> {
private:
    CUFHEWorkerInfo wi_;
    std::weak_ptr<cufhe::cuFHETRLWElvl1> mem_;

private:
    void startAsyncImpl(CUFHEWorkerInfo wi) override
    {
        wi_ = std::move(wi);
        cufhe::GateBootstrappingTLWE2TRLWElvl01NTT(*mem_.lock(), input(0),
                                                   *wi_.stream);
    }

public:
    TaskCUFHERAMGateBootstrapping()
    {
    }

    TaskCUFHERAMGateBootstrapping(std::weak_ptr<cufhe::cuFHETRLWElvl1> mem)
        : Task<cufhe::Ctxt, uint8_t, CUFHEWorkerInfo>(1), mem_(std::move(mem))
    {
    }

    bool hasFinished() const override
    {
        return cufhe::StreamQuery(*wi_.stream);
    }

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(cereal::base_class<Task<cufhe::Ctxt, uint8_t, CUFHEWorkerInfo>>(
               this),
           mem_);
    }
};
CEREAL_REGISTER_TYPE(TaskCUFHERAMGateBootstrapping);

using CUFHE2TFHEppBridge = BridgeDepNode<CUFHEWorkerInfo, TFHEppWorkerInfo>;
using TFHEpp2CUFHEBridge = BridgeDepNode<TFHEppWorkerInfo, CUFHEWorkerInfo>;

class CUFHENetworkRunner {
private:
    NetworkRunner<CUFHEWorkerInfo, CUFHEWorker> cufhe_;
    NetworkRunner<TFHEppWorkerInfo, TFHEppWorker> tfhepp_;
    std::vector<std::shared_ptr<CUFHE2TFHEppBridge>> bridges0_;
    std::vector<std::shared_ptr<TFHEpp2CUFHEBridge>> bridges1_;
    std::shared_ptr<ProgressGraphMaker> graph_;

public:
    CUFHENetworkRunner(int numCUFHEWorkers, int numTFHEppWorkers,
                       TFHEppWorkerInfo wi,
                       std::shared_ptr<ProgressGraphMaker> graph = nullptr)
        : graph_(std::move(graph))
    {
        for (int i = 0; i < numCUFHEWorkers; i++)
            cufhe_.addWorker(graph_);
        for (int i = 0; i < numTFHEppWorkers; i++)
            tfhepp_.addWorker(wi, graph_);
    }

    void addNetwork(std::shared_ptr<CUFHENetwork> net)
    {
        cufhe_.addNetwork(net);
    }

    void addNetwork(std::shared_ptr<TFHEppNetwork> net)
    {
        tfhepp_.addNetwork(net);
    }

    void addBridge(
        std::shared_ptr<BridgeDepNode<CUFHEWorkerInfo, TFHEppWorkerInfo>>
            bridge)
    {
        bridge->setReadyQueue(tfhepp_.getReadyQueue());
        bridges0_.push_back(bridge);
    }

    void addBridge(
        std::shared_ptr<BridgeDepNode<TFHEppWorkerInfo, CUFHEWorkerInfo>>
            bridge)
    {
        bridge->setReadyQueue(cufhe_.getReadyQueue());
        bridges1_.push_back(bridge);
    }

    void run()
    {
        if (graph_)
            graph_->reset();
        cufhe_.prepareToRun();
        tfhepp_.prepareToRun();

        size_t numNodes = cufhe_.numNodes() + tfhepp_.numNodes() +
                          bridges0_.size() + bridges1_.size();

        while (cufhe_.getNumFinishedTargets() +
                   tfhepp_.getNumFinishedTargets() <
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

bool isSerializedCUFHEFrontend(const std::string& filepath);
void doCUFHE(const Options& opt);
void processAllGates(CUFHENetwork& net, int numWorkers,
                     std::shared_ptr<ProgressGraphMaker> graph = nullptr);
#endif
