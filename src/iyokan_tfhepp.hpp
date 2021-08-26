#ifndef VIRTUALSECUREPLATFORM_IYOKAN_TFHEPP_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_TFHEPP_HPP

#include <bitset>

#include "iyokan.hpp"

#include "tfhepp_cufhe_wrapper.hpp"

struct TFHEppWorkerInfo {
    std::shared_ptr<const TFHEpp::GateKey> gateKey;
    std::shared_ptr<const CircuitKey> circuitKey;
};

using TaskAsyncTFHEpp = TaskAsync<TLWElvl0, TLWElvl0, TFHEppWorkerInfo>;
using TaskTFHEppGate = Task<TLWElvl0, TLWElvl0, TFHEppWorkerInfo>;
using TaskTFHEppGateMem = TaskMem<TLWElvl0, TLWElvl0, TFHEppWorkerInfo>;

class TaskTFHEppGateDFF : public TaskDFF<TLWElvl0, TLWElvl0, TFHEppWorkerInfo> {
public:
    TaskTFHEppGateDFF()
    {
        TFHEpp::HomCONSTANTZERO(output());
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<TaskDFF<TLWElvl0, TLWElvl0, TFHEppWorkerInfo>>(
            this));
    }
};
CEREAL_REGISTER_TYPE(TaskTFHEppGateDFF);

class TaskTFHEppGateWIRE
    : public TaskMem<TLWElvl0, TLWElvl0, TFHEppWorkerInfo> {
private:
    AsyncThread thr_;

private:
    void startAsyncImpl(TFHEppWorkerInfo, ProgressGraphMaker *graph) override
    {
        if (getInputSize() == 0) {
            if (graph)
                graph->startNode(this->depnode()->label());

            // Nothing to do!
        }
        else if (getInputSize() == 1) {
            thr_ = [graph, this]() {
                if (graph)
                    graph->startNode(this->depnode()->label());
                output() = input(0);
            };
        }
        else {
            assert(false);
        }
    }

public:
    TaskTFHEppGateWIRE()
    {
    }

    TaskTFHEppGateWIRE(bool inputNeeded)
        : TaskMem<TLWElvl0, TLWElvl0, TFHEppWorkerInfo>(inputNeeded ? 1 : 0)
    {
    }

    bool hasFinished() const override
    {
        return getInputSize() == 0 || thr_.hasFinished();
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<TaskMem<TLWElvl0, TLWElvl0, TFHEppWorkerInfo>>(
            this));
    }
};
CEREAL_REGISTER_TYPE(TaskTFHEppGateWIRE);

#define DEFINE_TASK_GATE(name, numInputs, expr)             \
    class TaskTFHEppGate##name : public TaskAsyncTFHEpp {   \
    private:                                                \
        void startSync(TFHEppWorkerInfo wi) override        \
        {                                                   \
            auto gk = wi.gateKey;                           \
            (expr);                                         \
        }                                                   \
                                                            \
    public:                                                 \
        TaskTFHEppGate##name() : TaskAsyncTFHEpp(numInputs) \
        {                                                   \
        }                                                   \
                                                            \
        template <class Archive>                            \
        void serialize(Archive &ar)                         \
        {                                                   \
            ar(cereal::base_class<TaskAsyncTFHEpp>(this));  \
        }                                                   \
    };                                                      \
    CEREAL_REGISTER_TYPE(TaskTFHEppGate##name);
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
    TFHEppWorkerInfo wi_;

private:
    TFHEppWorkerInfo getWorkerInfo()
    {
        return wi_;
    }

public:
    TFHEppWorker(ReadyQueue<TFHEppWorkerInfo> &readyQueue,
                 size_t &numFinishedTargets, TFHEppWorkerInfo wi,
                 std::shared_ptr<ProgressGraphMaker> graph)
        : Worker(readyQueue, numFinishedTargets, graph), wi_(std::move(wi))
    {
    }
};

class TaskTFHEppCB : public Task<TLWElvl0, TRGSWFFTlvl1, TFHEppWorkerInfo> {
private:
    AsyncThread thrs_[TFHEpp::lvl1param::l];

private:
    void startAsyncImpl(TFHEppWorkerInfo wi, ProgressGraphMaker *graph) override
    {
        for (size_t i = 0; i < TFHEpp::lvl1param::l; i++)
            thrs_[i] = [i, wi, this, graph] {
                if (i == 0 && graph)
                    graph->startNode(this->depnode()->label());

                auto ck = wi.circuitKey;
                assert(ck);

                TRLWElvl1 trgswupper, trgswlower;
                TFHEpp::CircuitBootstrappingPartial(trgswupper, trgswlower,
                                                    input(0), *ck, i);
                for (size_t j = 0; j < 2; j++) {
                    TFHEpp::TwistIFFT<Lvl1>(output()[i][j], trgswupper[j]);
                    TFHEpp::TwistIFFT<Lvl1>(
                        output()[i + TFHEpp::lvl1param::l][j], trgswlower[j]);
                }
            };
    }

public:
    TaskTFHEppCB() : Task<TLWElvl0, TRGSWFFTlvl1, TFHEppWorkerInfo>(1)
    {
    }

    bool hasFinished() const override
    {
        for (size_t i = 0; i < TFHEpp::lvl1param::l; i++)
            if (!thrs_[i].hasFinished())
                return false;
        return true;
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<Task<TLWElvl0, TRGSWFFTlvl1, TFHEppWorkerInfo>>(
            this));
    }
};
CEREAL_REGISTER_TYPE(TaskTFHEppCB);

class TaskTFHEppCBInv : public Task<TLWElvl0, TRGSWFFTlvl1, TFHEppWorkerInfo> {
private:
    AsyncThread thrs_[TFHEpp::lvl1param::l];
    TLWElvl0 invtlwe_;

private:
    void startAsyncImpl(TFHEppWorkerInfo wi, ProgressGraphMaker *graph) override
    {
        for (size_t i = 0; i <= TFHEpp::lvl0param::n; i++)
            invtlwe_[i] = -input(0)[i];

        for (size_t i = 0; i < TFHEpp::lvl1param::l; i++)
            thrs_[i] = [i, wi, this, graph] {
                if (i == 0 && graph) {
                    graph->startNode(this->depnode()->label());
                }

                auto ck = wi.circuitKey;
                assert(ck);

                TRLWElvl1 trgswupper, trgswlower;
                TFHEpp::CircuitBootstrappingPartial(trgswupper, trgswlower,
                                                    invtlwe_, *ck, i);
                for (size_t j = 0; j < 2; j++) {
                    TFHEpp::TwistIFFT<Lvl1>(output()[i][j], trgswupper[j]);
                    TFHEpp::TwistIFFT<Lvl1>(
                        output()[i + TFHEpp::lvl1param::l][j], trgswlower[j]);
                }
            };
    }

public:
    TaskTFHEppCBInv() : Task<TLWElvl0, TRGSWFFTlvl1, TFHEppWorkerInfo>(1)
    {
    }

    bool hasFinished() const override
    {
        for (size_t i = 0; i < TFHEpp::lvl1param::l; i++)
            if (!thrs_[i].hasFinished())
                return false;
        return true;
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<Task<TLWElvl0, TRGSWFFTlvl1, TFHEppWorkerInfo>>(
            this));
    }
};
CEREAL_REGISTER_TYPE(TaskTFHEppCBInv);

class TaskTFHEppROMUX
    : public TaskAsync<TRGSWFFTlvl1, TRLWElvl1, TFHEppWorkerInfo> {
private:
    size_t inAddrWidth_, log2NumWordsPerTRLWE_;
    std::vector<TRLWElvl1> data_;

    // Workplace for UROMUX()
    std::vector<TRLWElvl1> UROMUX_temp;

private:
    void UROMUX(TRLWElvl1 &result)
    {
        if (inAddrWidth_ <= log2NumWordsPerTRLWE_) {
            result = data_[0];
            return;
        }

        const size_t numTRLWE = data_.size();
        const size_t log2NumTRLWE = inAddrWidth_ - log2NumWordsPerTRLWE_;
        assert(numTRLWE == (1 << log2NumTRLWE));

        std::vector<TRLWElvl1> &temp = UROMUX_temp;
        temp.resize(numTRLWE / 2);

        for (size_t bit = 0; bit < log2NumTRLWE; bit++) {
            size_t numCMUX = (numTRLWE >> (bit + 1));
            for (size_t i = 0; i < numCMUX; i++) {
                auto &in = (bit == 0 ? data_ : temp);
                auto &out = (bit == log2NumTRLWE - 1 ? result : temp[i]);
                TFHEpp::CMUXFFT<Lvl1>(out, input(log2NumWordsPerTRLWE_ + bit),
                                      in[2 * i], in[2 * i + 1]);
            }
        }
    }

    void LROMUX(const uint32_t N, const TRLWElvl1 &data)
    {
        TRLWElvl1 &acc = output();
        acc = data;

        TRLWElvl1 temp;
        for (uint32_t bit = 1; bit <= log2NumWordsPerTRLWE_; bit++) {
            if (log2NumWordsPerTRLWE_ - bit >= inAddrWidth_)
                continue;
            TFHEpp::PolynomialMulByXaiMinusOne<Lvl1>(temp[0], acc[0],
                                                     2 * N - (N >> bit));
            TFHEpp::PolynomialMulByXaiMinusOne<Lvl1>(temp[1], acc[1],
                                                     2 * N - (N >> bit));
            TFHEpp::trgswfftExternalProduct<Lvl1>(
                temp, temp, input(log2NumWordsPerTRLWE_ - bit));
            for (uint32_t i = 0; i < N; i++) {
                acc[0][i] += temp[0][i];
                acc[1][i] += temp[1][i];
            }
        }
    }

    void startSync(TFHEppWorkerInfo /* wi */) override
    {
        TRLWElvl1 data;
        UROMUX(data);
        LROMUX(TFHEpp::lvl1param::n, data);
    }

public:
    TaskTFHEppROMUX()
    {
    }

    TaskTFHEppROMUX(size_t inAddrWidth, size_t log2OutRdataWidth)
        : TaskAsync<TRGSWFFTlvl1, TRLWElvl1, TFHEppWorkerInfo>(inAddrWidth),
          inAddrWidth_(inAddrWidth)
    {
        assert(TFHEpp::lvl1param::nbit >= log2OutRdataWidth);

        log2NumWordsPerTRLWE_ = TFHEpp::lvl1param::nbit - log2OutRdataWidth;
        if (inAddrWidth < log2NumWordsPerTRLWE_)
            data_.resize(1);
        else
            data_.resize(1 << (inAddrWidth - log2NumWordsPerTRLWE_));
    }

    size_t size() const
    {
        return data_.size();
    }

    void set(size_t index, TRLWElvl1 val)
    {
        data_.at(index) = val;
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<
               TaskAsync<TRGSWFFTlvl1, TRLWElvl1, TFHEppWorkerInfo>>(this),
           inAddrWidth_, log2NumWordsPerTRLWE_, data_);
    }
};
CEREAL_REGISTER_TYPE(TaskTFHEppROMUX);

class TaskTFHEppSEI : public TaskAsync<TRLWElvl1, TLWElvl0, TFHEppWorkerInfo> {
private:
    size_t index_;

private:
    void startSync(TFHEppWorkerInfo wi) override
    {
        const KeySwitchingKey &ksk = wi.gateKey->ksk;

        TLWElvl1 reslvl1;
        TFHEpp::SampleExtractIndex<Lvl1>(reslvl1, input(0), index_);
        TFHEpp::IdentityKeySwitch<Lvl10>(output(), reslvl1, ksk);
    }

public:
    TaskTFHEppSEI()
    {
    }

    TaskTFHEppSEI(size_t index)
        : TaskAsync<TRLWElvl1, TLWElvl0, TFHEppWorkerInfo>(1), index_(index)
    {
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<TaskAsync<TRLWElvl1, TLWElvl0, TFHEppWorkerInfo>>(
               this),
           index_);
    }
};
CEREAL_REGISTER_TYPE(TaskTFHEppSEI);

struct TRGSWFFTlvl1Pair {
    TRGSWFFTlvl1 normal, inverted;

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(normal, inverted);
    }
};

class TaskTFHEppCBWithInv
    : public Task<TLWElvl0, TRGSWFFTlvl1Pair, TFHEppWorkerInfo> {
private:
    AsyncThread thrs_[TFHEpp::lvl1param::l];

private:
    void startAsyncImpl(TFHEppWorkerInfo wi, ProgressGraphMaker *graph) override
    {
        for (size_t i = 0; i < TFHEpp::lvl1param::l; i++)
            thrs_[i] = [i, wi, this, graph] {
                if (i == 0 && graph)
                    graph->startNode(this->depnode()->label());

                auto ck = wi.circuitKey;
                assert(ck);

                TFHEpp::CircuitBootstrappingFFTwithInvPartial(
                    output().normal[i],
                    output().normal[i + TFHEpp::lvl1param::l],
                    output().inverted[i],
                    output().inverted[i + TFHEpp::lvl1param::l], input(0), *ck,
                    i);
            };
    }

public:
    TaskTFHEppCBWithInv()
        : Task<TLWElvl0, TRGSWFFTlvl1Pair, TFHEppWorkerInfo>(1)
    {
    }

    bool hasFinished() const override
    {
        for (size_t i = 0; i < TFHEpp::lvl1param::l; i++)
            if (!thrs_[i].hasFinished())
                return false;
        return true;
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<
            Task<TLWElvl0, TRGSWFFTlvl1Pair, TFHEppWorkerInfo>>(this));
    }
};
CEREAL_REGISTER_TYPE(TaskTFHEppCBWithInv);

class TaskTFHEppRAMUX
    : public TaskAsync<TRGSWFFTlvl1Pair, TRLWElvl1, TFHEppWorkerInfo> {
private:
    std::vector<std::shared_ptr<TRLWElvl1>> data_;
    std::vector<TRLWElvl1> temp_;  // temporary workspace for RAMUX()

private:
    void RAMUX()
    {
        const size_t addrWidth = getAddressWidth();
        const uint32_t num_trlwe = 1 << addrWidth;
        temp_.resize(num_trlwe / 2);
        auto addr = [this](size_t i) -> const TRGSWFFTlvl1 & {
            return input(i).inverted;
        };

        for (uint32_t index = 0; index < num_trlwe / 2; index++) {
            TFHEpp::CMUXFFT<Lvl1>(temp_[index], addr(0), *data_[2 * index],
                                  *data_[2 * index + 1]);
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
    TaskTFHEppRAMUX()
    {
    }

    TaskTFHEppRAMUX(size_t addressWidth)
        : TaskAsync<TRGSWFFTlvl1Pair, TRLWElvl1, TFHEppWorkerInfo>(
              addressWidth),
          data_(1 << addressWidth)
    {
        for (auto &p : data_)
            p = std::make_shared<TRLWElvl1>();
    }

    size_t getAddressWidth() const
    {
        return getInputSize();
    }

    size_t size() const
    {
        return 1 << getAddressWidth();
    }

    std::shared_ptr<const TRLWElvl1> get(size_t addr) const
    {
        return data_.at(addr);
    }

    std::shared_ptr<TRLWElvl1> &get(size_t addr)
    {
        return data_.at(addr);
    }

    void set(size_t addr, TRLWElvl1 val)
    {
        *data_.at(addr) = std::move(val);
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<
               TaskAsync<TRGSWFFTlvl1Pair, TRLWElvl1, TFHEppWorkerInfo>>(this),
           data_);
    }
};
CEREAL_REGISTER_TYPE(TaskTFHEppRAMUX);

class TaskTFHEppGateMUXWoSE
    : public TaskAsync<TLWElvl0, TRLWElvl1, TFHEppWorkerInfo> {
private:
    void startSync(TFHEppWorkerInfo wi) override
    {
        auto gk = wi.gateKey;
        TFHEpp::HomMUXwoSE<Lvl01>(output(), input(2), input(1), input(0),
                                  gk->bkfftlvl01);
    }

public:
    TaskTFHEppGateMUXWoSE()
        : TaskAsync<TLWElvl0, TRLWElvl1, TFHEppWorkerInfo>(3)
    {
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<TaskAsync<TLWElvl0, TRLWElvl1, TFHEppWorkerInfo>>(
            this));
    }
};
CEREAL_REGISTER_TYPE(TaskTFHEppGateMUXWoSE);

class TaskTFHEppRAMCMUXs : public TaskBase<TFHEppWorkerInfo> {
private:
    size_t numReadyInputs_, memIndex_;
    std::shared_ptr<TRLWElvl1> output_;
    std::vector<std::weak_ptr<const TRGSWFFTlvl1Pair>> inputAddrs_;
    std::weak_ptr<const TRLWElvl1> inputWritten_;

    std::weak_ptr<const TRLWElvl1> mem_;

    AsyncThread thr_;

public:
    TaskTFHEppRAMCMUXs()
    {
    }

    TaskTFHEppRAMCMUXs(size_t addressWidth, std::weak_ptr<const TRLWElvl1> mem,
                       size_t memIndex)
        : numReadyInputs_(0),
          memIndex_(memIndex),
          output_(std::make_shared<TRLWElvl1>()),
          inputAddrs_(addressWidth),
          mem_(std::move(mem))
    {
    }

    virtual ~TaskTFHEppRAMCMUXs()
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

    void checkValid(error::Stack &err) override
    {
        assert(this->depnode());

        const NodeLabel &label = this->depnode()->label();
        if (!std::all_of(inputAddrs_.begin(), inputAddrs_.end(),
                         [](auto &&in) { return in.use_count() != 0; }) ||
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

    void addInputPtr(const std::shared_ptr<const TRGSWFFTlvl1Pair> &input)
    {
        auto it = std::find_if(inputAddrs_.begin(), inputAddrs_.end(),
                               [](auto &&in) { return in.use_count() == 0; });
        assert(it != inputAddrs_.end());
        *it = input;
    }

    void addInputPtr(const std::shared_ptr<const TRLWElvl1> &input)
    {
        assert(inputWritten_.use_count() == 0);
        inputWritten_ = input;
    }

    std::shared_ptr<const TRLWElvl1> getOutputPtr() const
    {
        return output_;
    }

    void startAsync(TFHEppWorkerInfo, ProgressGraphMaker *graph) override
    {
        thr_ = [this, graph] {
            if (graph)
                graph->startNode(this->depnode()->label());

            *output_ = *inputWritten_.lock();
            for (size_t j = 0; j < getAddressWidth(); j++) {
                const TRGSWFFTlvl1 &in = (memIndex_ >> j) & 1u
                                             ? inputAddrs_[j].lock()->normal
                                             : inputAddrs_[j].lock()->inverted;
                TFHEpp::CMUXFFT<Lvl1>(*output_, in, *output_, *mem_.lock());
            }
        };
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<TaskBase<TFHEppWorkerInfo>>(this),
           numReadyInputs_, memIndex_, output_, inputAddrs_, inputWritten_,
           mem_);
    }
};
CEREAL_REGISTER_TYPE(TaskTFHEppRAMCMUXs);

class TaskTFHEppRAMGateBootstrapping
    : public TaskAsync<TLWElvl0, uint8_t /* dummy */, TFHEppWorkerInfo> {
private:
    std::weak_ptr<TRLWElvl1> mem_;

private:
    void startSync(TFHEppWorkerInfo wi) override
    {
        const TFHEpp::GateKey &gk = *wi.gateKey;
        TFHEpp::GateBootstrappingTLWE2TRLWEFFT<Lvl01>(*mem_.lock(), input(0),
                                                      gk.bkfftlvl01);
    }

public:
    TaskTFHEppRAMGateBootstrapping()
    {
    }

    TaskTFHEppRAMGateBootstrapping(std::weak_ptr<TRLWElvl1> mem)
        : TaskAsync<TLWElvl0, uint8_t /* dummy */, TFHEppWorkerInfo>(1),
          mem_(std::move(mem))
    {
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<TaskAsync<TLWElvl0, uint8_t, TFHEppWorkerInfo>>(
               this),
           mem_);
    }
};
CEREAL_REGISTER_TYPE(TaskTFHEppRAMGateBootstrapping);

inline void makeTFHEppRAMNetworkImpl(
    NetworkBuilderBase<TFHEppWorkerInfo> &builder, size_t addressWidth,
    const std::string &ramPortName,
    const std::vector<std::shared_ptr<TaskTFHEppCBWithInv>> &cbs, int indexBit)
{
    /*
        // Address CB
        for i in [0, address_bit]:
            addr[i] -> CB -> trgswfft[i] ([1])
                          -> invtrgswfft[i] ([0])

        // Read
        invtrgswfft    -> RAMUX                  -> encumemory
        encumemory     -> SampleExtractIndexlvl1 -> encreadreslvl1
        encreadreslvl1 -> IdentityKeySwitchlvl10 -> encreadres

        // Write
        MemWrite, WriteData, encreadres -> HomMUXwoSE -> writed
        for i in [0, memsize]:
            temp = copy writed
            for j in [0, address_bit]:
                (trgswfft/invtrgswfft)[j], temp -> CMUXFFTlvl1 -> temp
            temp -> SampleExtractIndexlvl1 -> temp2
            temp2 -> IdentityKeySwitchlvl10 -> temp3
            temp3 -> GateBootstrappingTLWE2TRLWEFFTlvl01
    */

    /*
                               +------------------------------------>i
                               |
                               |                      +------+
                               |  +-----+   WDATA ->--+ MUX  |
                               |  | RAM |             | woSE |------>A
        ADDR[i] -> CBwithInv --+--+ UX  +-- SEI --+---+      |
                                  |     |         |   +------+
                                  +-----+         |      ^
                                                  |      +--- MemWrite
                                                  |
                                                  +--> RDATA

        ===========================================================================

               +-------+
        i-> ---+ CMUXs |
               |  [0]  +-- SEI -- GateBootstrapping
        A-> ---+       |
               +-------+

               +-------+
        i-> ---+ CMUXs |
               |  [1]  +-- SEI -- GateBootstrapping
        A-> ---+       |
               +-------+

                  ...

    */

    // Create RAMUX.
    auto taskRAMUX = std::make_shared<TaskTFHEppRAMUX>(addressWidth);
    builder.addTask(NodeLabel{"RAMUX", ""}, taskRAMUX);
    builder.registerTask("ram", ramPortName, indexBit, taskRAMUX);

    // Connect CBs and RAMUX.
    for (auto &&cb : cbs)
        connectTasks(cb, taskRAMUX);

    // Create SEIs and connect with CBs.
    auto taskSEI0 = std::make_shared<TaskTFHEppSEI>(0);
    builder.addTask(NodeLabel{"SEI", "[0]"}, taskSEI0);
    connectTasks(taskRAMUX, taskSEI0);

    // Create output for read-out data and connect.
    auto taskOutputReadData =
        builder.getTask<TaskTFHEppGateWIRE>("output", "rdata", indexBit);
    connectTasks(taskSEI0, taskOutputReadData);

    // Create input for write-in data.
    auto taskInputWriteData =
        builder.getTask<TaskTFHEppGateWIRE>("input", "wdata", indexBit);
    auto taskInputWriteEnabled =
        builder.getTask<TaskTFHEppGateWIRE>("input", "wren", 0);

    // Create MUXWoSE and connect.
    auto taskMUXWoSE = std::make_shared<TaskTFHEppGateMUXWoSE>();
    builder.addTask(NodeLabel{"MUXWoSE", ""}, taskMUXWoSE);
    connectTasks(taskSEI0, taskMUXWoSE);
    connectTasks(taskInputWriteData, taskMUXWoSE);
    connectTasks(taskInputWriteEnabled, taskMUXWoSE);

    // Create links of CMUXs -> SEI -> GateBootstrapping.
    for (int i = 0; i < (1 << addressWidth); i++) {
        // Create components...
        auto taskCMUXs = std::make_shared<TaskTFHEppRAMCMUXs>(
            addressWidth, taskRAMUX->get(i), i);
        builder.addTask(NodeLabel{"CMUXs", utility::fok("[", i, "]")},
                        taskCMUXs);

        auto taskSEI = std::make_shared<TaskTFHEppSEI>(0);
        builder.addTask(NodeLabel{"SEI", utility::fok("[", i, "]")}, taskSEI);

        auto taskGB =
            std::make_shared<TaskTFHEppRAMGateBootstrapping>(taskRAMUX->get(i));
        builder.addTask(NodeLabel{"GB", utility::fok("[", i, "]")}, taskGB);

        // ... and connect them.
        connectTasks(taskMUXWoSE, taskCMUXs);
        for (auto &&cb : cbs)
            connectTasks(cb, taskCMUXs);
        connectTasks(taskCMUXs, taskSEI);
        connectTasks(taskSEI, taskGB);
    }
}

inline TaskNetwork<TFHEppWorkerInfo> makeTFHEppRAMNetwork(
    size_t addressWidth, size_t dataWidth, const std::string &ramPortName)
{
    NetworkBuilderBase<TFHEppWorkerInfo> builder;

    // Inputs for address.
    std::vector<std::shared_ptr<TaskTFHEppCBWithInv>> cbs;
    for (size_t i = 0; i < addressWidth; i++) {
        auto taskINPUT = builder.addINPUT<TaskTFHEppGateWIRE>("addr", i, false);
        auto taskCB = std::make_shared<TaskTFHEppCBWithInv>();
        builder.addTask(NodeLabel{"CBWithInv", utility::fok("[", i, "]")},
                        taskCB);
        connectTasks(taskINPUT, taskCB);
        cbs.push_back(taskCB);
    }

    // Input for write-in flag.
    builder.addINPUT<TaskTFHEppGateWIRE>("wren", 0, false);

    for (int indexBit = 0; indexBit < dataWidth; indexBit++) {
        // Input for data to write into RAM.
        builder.addINPUT<TaskTFHEppGateWIRE>("wdata", indexBit, false);
        // Output for data to be read from RAM.
        builder.addOUTPUT<TaskTFHEppGateWIRE>("rdata", indexBit, true);

        makeTFHEppRAMNetworkImpl(builder, addressWidth, ramPortName, cbs,
                                 indexBit);
    }

    return TaskNetwork<TFHEppWorkerInfo>(std::move(builder));
}

inline TaskNetwork<TFHEppWorkerInfo> makeTFHEppROMNetwork(
    size_t inAddrWidth, size_t log2OutRdataWidth)
{
    size_t outRdataWidth = 1 << log2OutRdataWidth,
           log2NumWordsPerTRLWE = TFHEpp::lvl1param::nbit - log2OutRdataWidth;
    if (inAddrWidth < log2NumWordsPerTRLWE)
        log2NumWordsPerTRLWE = inAddrWidth;

    /*
       INPUT (addr[0])  ----------- CB -----+-----+  +-- SEI --- OUTPUT
                                            |  R  |  |            (rdata[0])
       INPUT (addr[1])  ----------- CB -----+  O  +--+-- SEI --- OUTPUT
                                            |  M  |  |            (rdata[1])
       INPUT (addr[2])  ----------- CB -----+  U  |  +-- SEI --- OUTPUT
                                            |  X  |  |            (rdata[2])
                                              ...
                                            |     |  +-- SEI
                                            |     |       |
                                            |     |     OUTPUT
       INPUT            ----------- CB -----+     |  (rdata[outRdataWidth])
        (addr[log2NumWordsPerTRLWE-1])      |     |
                                            |     |
       INPUT            ----------- CBInv --+     |
        (addr[log2NumWordsPerTRLWE])        |     |
                                            |     |
       INPUT            ----------- CBInv --+     |
        (addr[log2NumWordsPerTRLWE+1])      |     |
                                              ...
                                            |     |
       INPUT            ----------- CBInv --+-----+
        (addr[inAddrWidth])
    */

    NetworkBuilderBase<TFHEppWorkerInfo> builder;

    // Create ROMUX.
    auto taskROMUX = builder.emplaceTask<TaskTFHEppROMUX>(
        NodeLabel{"ROMUX", ""}, inAddrWidth, log2OutRdataWidth);
    builder.registerTask("rom", "all", 0, taskROMUX);

    // Create inputs and CB/CBInvs, and connect them to ROMUX.
    for (int i = 0; i < log2NumWordsPerTRLWE; i++) {
        auto taskINPUT = builder.addINPUT<TaskTFHEppGateWIRE>("addr", i, false);
        auto taskCB = builder.emplaceTask<TaskTFHEppCB>(
            NodeLabel{"CB", utility::fok("[", i, "]")});
        connectTasks(taskINPUT, taskCB);
        connectTasks(taskCB, taskROMUX);
    }
    for (int i = log2NumWordsPerTRLWE; i < inAddrWidth; i++) {
        auto taskINPUT = builder.addINPUT<TaskTFHEppGateWIRE>("addr", i, false);
        auto taskCBInv = builder.emplaceTask<TaskTFHEppCBInv>(
            NodeLabel{"CBInv", utility::fok("[", i, "]")});
        connectTasks(taskINPUT, taskCBInv);
        connectTasks(taskCBInv, taskROMUX);
    }

    // Create SEIs and outputs, and connect them properly
    for (int i = 0; i < outRdataWidth; i++) {
        auto taskSEI = std::make_shared<TaskTFHEppSEI>(i);
        builder.addTask(NodeLabel{"SEI", utility::fok("[", i, "]")}, taskSEI);
        connectTasks(taskROMUX, taskSEI);

        auto taskOUTPUT =
            builder.addOUTPUT<TaskTFHEppGateWIRE>("rdata", i, true);
        connectTasks(taskSEI, taskOUTPUT);
    }

    return TaskNetwork<TFHEppWorkerInfo>(std::move(builder));
}

bool isSerializedTFHEppFrontend(const std::string &filepath);
void doTFHE(const Options &opt);
void processAllGates(TFHEppNetwork &net, int numWorkers, TFHEppWorkerInfo wi,
                     std::shared_ptr<ProgressGraphMaker> graph = nullptr);

#endif
