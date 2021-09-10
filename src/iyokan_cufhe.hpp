#ifndef VIRTUALSECUREPLATFORM_IYOKAN_CUFHE_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_CUFHE_HPP

#include "iyokan.hpp"
#include "iyokan_tfhepp.hpp"
#include "tfhepp_cufhe_wrapper.hpp"

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
    std::vector<std::shared_ptr<cufhe::Ctxt>> ctxts;
};

CEREAL_REGISTER_TYPE(BridgeDepNode<TFHEppWorkerInfo, CUFHEWorkerInfo>);
CEREAL_REGISTER_TYPE(BridgeDepNode<CUFHEWorkerInfo, TFHEppWorkerInfo>);
CEREAL_REGISTER_TYPE(TaskBlackHole<TFHEppWorkerInfo>);
CEREAL_REGISTER_TYPE(TaskBlackHole<CUFHEWorkerInfo>);

using TaskCUFHEGate = Task<TLWELvl0, TLWELvl0, CUFHEWorkerInfo>;

inline void copyCtxt(cufhe::Ctxt& dst, const cufhe::Ctxt& src)
{
    cufhe::CopyOnHost(dst, const_cast<cufhe::Ctxt&>(src));
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
    TLWELvl0 tlwe;
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

    void set(const TLWELvl0& newval)
    {
        output() = newval;
    }

    const TLWELvl0& get() const
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
private:
    Bit initialValue_;

protected:
    void startAsyncImpl(CUFHEWorkerInfo) override
    {
    }

public:
    TaskCUFHEGateDFF() : TaskCUFHEGateMem(1)
    {
        initialValue_ = 0_b;
        setTLWELvl0Trivial0(output());
    }

    TaskCUFHEGateDFF(Bit initValue) : TaskCUFHEGateMem(1)
    {
        initialValue_ = initValue;
        if (initialValue_ == 0_b) {
            setTLWELvl0Trivial0(output());
        }
        else {
            setTLWELvl0Trivial1(output());
        }
    }

    void setInitialValue()
    {
        if (initialValue_ == 0_b) {
            setTLWELvl0Trivial0(output());
        }
        else {
            setTLWELvl0Trivial1(output());
        }
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
        output() = input(0);
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
            // FIXME: Optimization by using stream?
            output() = input(0);
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

#define DEFINE_TASK_GATE(name, numInputs, expr)                  \
    class TaskCUFHEGate##name : public TaskCUFHEGate {           \
    private:                                                     \
        CUFHEWorkerInfo wi_;                                     \
                                                                 \
    private:                                                     \
        cufhe::Ctxt& output()                                    \
        {                                                        \
            return *wi_.ctxts.at(0);                             \
        }                                                        \
        cufhe::Ctxt& input(size_t index)                         \
        {                                                        \
            cufhe::Ctxt& in = *wi_.ctxts.at(index + 1);          \
            in.tlwehost = TaskCUFHEGate::input(index);           \
            return in;                                           \
        }                                                        \
        void startAsyncImpl(CUFHEWorkerInfo wi) override         \
        {                                                        \
            wi_ = wi;                                            \
            auto st = wi_.stream;                                \
            (expr);                                              \
        }                                                        \
                                                                 \
    public:                                                      \
        TaskCUFHEGate##name() : TaskCUFHEGate(numInputs)         \
        {                                                        \
        }                                                        \
        bool hasFinished() const override                        \
        {                                                        \
            return cufhe::StreamQuery(*wi_.stream);              \
        }                                                        \
        void onBeforePropagate() override                        \
        {                                                        \
            TaskCUFHEGate::output() = wi_.ctxts.at(0)->tlwehost; \
        }                                                        \
        template <class Archive>                                 \
        void serialize(Archive& ar)                              \
        {                                                        \
            ar(cereal::base_class<TaskCUFHEGate>(this));         \
        }                                                        \
    };                                                           \
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
DEFINE_TASK_GATE(CONSTONE, 0, TFHEpp::HomCONSTANTONE(output().tlwehost));
DEFINE_TASK_GATE(CONSTZERO, 0, TFHEpp::HomCONSTANTZERO(output().tlwehost));
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
    DEFINE_GATE_IMPL(CONSTONE);
    DEFINE_GATE_IMPL(CONSTZERO);
#undef DEFINE_GATE_IMPL
};

using CUFHENetwork = CUFHENetworkBuilder::NetworkType;

class CUFHEWorker : public Worker<CUFHEWorkerInfo> {
    const static size_t WORKER_INFO_CTXTS_SIZE = 10;

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
        for (size_t i = 0; i < WORKER_INFO_CTXTS_SIZE; i++)
            wi_.ctxts.push_back(std::make_shared<cufhe::Ctxt>());
    }
};

class TaskCUFHE2TFHEpp
    : public TaskAsync<cufhe::Ctxt, TLWELvl0, TFHEppWorkerInfo> {
private:
    void startSync(TFHEppWorkerInfo) override
    {
        cufhe2tfheppInPlace(output(), input(0));
    }

public:
    TaskCUFHE2TFHEpp() : TaskAsync<cufhe::Ctxt, TLWELvl0, TFHEppWorkerInfo>(1)
    {
    }

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(cereal::base_class<
            TaskAsync<cufhe::Ctxt, TLWELvl0, TFHEppWorkerInfo>>(this));
    }
};
CEREAL_REGISTER_TYPE(TaskCUFHE2TFHEpp);

class TaskTFHEpp2CUFHE
    : public TaskAsync<TLWELvl0, cufhe::Ctxt, TFHEppWorkerInfo> {
private:
    void startSync(TFHEppWorkerInfo) override
    {
        tfhepp2cufheInPlace(output(), input(0));
    }

public:
    TaskTFHEpp2CUFHE() : TaskAsync<TLWELvl0, cufhe::Ctxt, TFHEppWorkerInfo>(1)
    {
    }

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(cereal::base_class<
            TaskAsync<TLWELvl0, cufhe::Ctxt, TFHEppWorkerInfo>>(this));
    }
};
CEREAL_REGISTER_TYPE(TaskTFHEpp2CUFHE);

class TaskTFHEppRAMCMUXsForCUFHE : public TaskBase<TFHEppWorkerInfo> {
private:
    size_t numReadyInputs_, memFirstIndex_;
    std::shared_ptr<std::vector<std::shared_ptr<cufhe::cuFHETRLWElvl1>>>
        outputs_;
    std::vector<std::weak_ptr<const TRGSWLvl1FFTPair>> inputAddrs_;
    std::weak_ptr<const TRLWELvl1> inputWritten_;

    std::vector<std::weak_ptr<cufhe::cuFHETRLWElvl1>> mem_;

    AsyncThread thr_;

public:
    TaskTFHEppRAMCMUXsForCUFHE()
    {
    }

    TaskTFHEppRAMCMUXsForCUFHE(
        size_t addressWidth,
        std::vector<std::weak_ptr<cufhe::cuFHETRLWElvl1>> mem,
        size_t memFirstIndex, size_t blockSize)
        : numReadyInputs_(0),
          memFirstIndex_(memFirstIndex),
          inputAddrs_(addressWidth),
          mem_(std::move(mem))
    {
        assert(mem_.size() == blockSize);

        outputs_ = std::make_shared<
            std::vector<std::shared_ptr<cufhe::cuFHETRLWElvl1>>>();
        for (size_t i = 0; i < blockSize; i++)
            outputs_->push_back(std::make_shared<cufhe::cuFHETRLWElvl1>());
    }

    virtual ~TaskTFHEppRAMCMUXsForCUFHE()
    {
    }

    size_t getAddressWidth() const
    {
        return inputAddrs_.size();
    }

    size_t getInputSize() const override
    {
        return getAddressWidth() + 1;
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
        assert(numReadyInputs_ <= getAddressWidth() + 1);
    }

    bool areInputsReady() const override
    {
        return numReadyInputs_ == getAddressWidth() + 1;
    }

    bool hasFinished() const override
    {
        return thr_.hasFinished();
    }

    void addInputPtr(const std::shared_ptr<const TRGSWLvl1FFTPair>& input)
    {
        auto it = std::find_if(inputAddrs_.begin(), inputAddrs_.end(),
                               [](auto&& in) { return in.use_count() == 0; });
        assert(it != inputAddrs_.end());
        *it = input;
    }

    void addInputPtr(const std::shared_ptr<const TRLWELvl1>& input)
    {
        assert(inputWritten_.use_count() == 0);
        inputWritten_ = input;
    }

    std::shared_ptr<const std::vector<std::shared_ptr<cufhe::cuFHETRLWElvl1>>>
    getOutputPtr() const
    {
        return outputs_;
    }

    void startAsync(TFHEppWorkerInfo, ProgressGraphMaker* graph) override
    {
        thr_ = [this, graph] {
            if (graph)
                graph->startNode(this->depnode()->label());

            assert(outputs_);
            for (size_t i = 0; i < outputs_->size(); i++) {
                auto& output = *(*outputs_)[i];
                output.trlwehost = *inputWritten_.lock();
                auto& mem = mem_[i];
                for (size_t j = 0; j < getAddressWidth(); j++) {
                    const TRGSWLvl1FFT& in =
                        ((memFirstIndex_ + i) >> j) & 1u
                            ? inputAddrs_[j].lock()->normal
                            : inputAddrs_[j].lock()->inverted;
                    TFHEpp::CMUXFFT<Lvl1>(output.trlwehost, in,
                                          output.trlwehost,
                                          mem.lock()->trlwehost);
                }
            }
        };
    }

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(cereal::base_class<TaskBase<TFHEppWorkerInfo>>(this),
           numReadyInputs_, memFirstIndex_, outputs_, inputAddrs_,
           inputWritten_, mem_);
    }
};
CEREAL_REGISTER_TYPE(TaskTFHEppRAMCMUXsForCUFHE);

class TaskTFHEpp2CUFHETRLWELvl1
    : public TaskAsync<TRLWELvl1, cufhe::cuFHETRLWElvl1, TFHEppWorkerInfo> {
private:
    void startSync(TFHEppWorkerInfo) override
    {
        output().trlwehost = input(0);
    }

public:
    TaskTFHEpp2CUFHETRLWELvl1()
        : TaskAsync<TRLWELvl1, cufhe::cuFHETRLWElvl1, TFHEppWorkerInfo>(1)
    {
    }

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(cereal::base_class<
            TaskAsync<TRLWELvl1, cufhe::cuFHETRLWElvl1, TFHEppWorkerInfo>>(
            this));
    }
};
CEREAL_REGISTER_TYPE(TaskTFHEpp2CUFHETRLWELvl1);

class TaskCUFHERAMUX
    : public TaskAsync<TRGSWLvl1FFTPair, TRLWELvl1, TFHEppWorkerInfo> {
private:
    std::vector<std::shared_ptr<cufhe::cuFHETRLWElvl1>> data_;
    std::vector<TRLWELvl1> temp_;  // temporary workspace for RAMUX()

private:
    void RAMUX()
    {
        const size_t addrWidth = getAddressWidth();
        const uint32_t num_trlwe = 1 << addrWidth;
        temp_.resize(num_trlwe / 2);
        auto addr = [this](size_t i) -> const TRGSWLvl1FFT& {
            return input(i).inverted;
        };

        for (uint32_t index = 0; index < num_trlwe / 2; index++) {
            TFHEpp::CMUXFFT<Lvl1>(temp_[index], addr(0),
                                  data_[2 * index]->trlwehost,
                                  data_[2 * index + 1]->trlwehost);
        }

        for (uint32_t bit = 0; bit < (addrWidth - 2); bit++) {
            const uint32_t stride = 1 << bit;
            for (uint32_t index = 0; index < (num_trlwe >> (bit + 2));
                 index++) {
                TFHEpp::CMUXFFT<Lvl1>(temp_[(2 * index) * stride],
                                      addr(bit + 1),
                                      temp_[(2 * index) * stride],
                                      temp_[(2 * index + 1) * stride]);
            }
        }

        const uint32_t stride = 1 << (addrWidth - 2);
        TFHEpp::CMUXFFT<Lvl1>(output(), addr(addrWidth - 1), temp_[0],
                              temp_[stride]);
    }

    void startSync(TFHEppWorkerInfo) override
    {
        RAMUX();
    }

public:
    TaskCUFHERAMUX()
    {
    }

    TaskCUFHERAMUX(size_t addressWidth)
        : TaskAsync<TRGSWLvl1FFTPair, TRLWELvl1, TFHEppWorkerInfo>(
              addressWidth),
          data_(1 << addressWidth)
    {
        for (auto&& p : data_)
            p = std::make_shared<cufhe::cuFHETRLWElvl1>();
    }

    size_t getAddressWidth() const
    {
        return getInputSize();
    }

    size_t size() const
    {
        return 1 << getAddressWidth();
    }

    std::shared_ptr<const cufhe::cuFHETRLWElvl1> get(size_t addr) const
    {
        return data_.at(addr);
    }

    std::shared_ptr<cufhe::cuFHETRLWElvl1> get(size_t addr)
    {
        return data_.at(addr);
    }

    void set(size_t addr, TRLWELvl1 val)
    {
        data_.at(addr)->trlwehost = std::move(val);
    }

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(cereal::base_class<
               TaskAsync<TRGSWLvl1FFTPair, TRLWELvl1, TFHEppWorkerInfo>>(this),
           data_);
    }
};
CEREAL_REGISTER_TYPE(TaskCUFHERAMUX);

class TaskCUFHERAMSEIAndKS
    : public Task<std::vector<std::shared_ptr<cufhe::cuFHETRLWElvl1>>,
                  cufhe::Ctxt, CUFHEWorkerInfo> {
private:
    CUFHEWorkerInfo wi_;
    size_t targetIndex_;

private:
    void startAsyncImpl(CUFHEWorkerInfo wi) override
    {
        wi_ = std::move(wi);
        cufhe::SampleExtractAndKeySwitch(
            this->output(), *this->input(0).at(targetIndex_), *wi_.stream);
    }

public:
    TaskCUFHERAMSEIAndKS()
    {
    }

    TaskCUFHERAMSEIAndKS(size_t targetIndex)
        : Task<std::vector<std::shared_ptr<cufhe::cuFHETRLWElvl1>>, cufhe::Ctxt,
               CUFHEWorkerInfo>(1),
          targetIndex_(targetIndex)
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
               Task<std::vector<std::shared_ptr<cufhe::cuFHETRLWElvl1>>,
                    cufhe::Ctxt, CUFHEWorkerInfo>>(this),
           targetIndex_);
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
        cufhe::GateBootstrappingTLWE2TRLWElvl01NTT(
            *mem_.lock(), const_cast<cufhe::Ctxt&>(input(0)), *wi_.stream);
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

    void setSDFFInitialValue()
    {
        cufhe_.setSDFFInitialValue<TaskCUFHEGateDFF>();
        tfhepp_.setSDFFInitialValue<TaskTFHEppGateDFF>();
    }
};

bool isSerializedCUFHEFrontend(const std::string& filepath);
void doCUFHE(const Options& opt);
void processAllGates(CUFHENetwork& net, int numWorkers,
                     std::shared_ptr<ProgressGraphMaker> graph = nullptr);
#endif
