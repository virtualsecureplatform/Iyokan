#ifndef VIRTUALSECUREPLATFORM_IYOKAN_TFHEPP_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_TFHEPP_HPP

#include <bitset>

#include "iyokan.hpp"

//
#include <tfhe++.hpp>

struct TFHEppWorkerInfo {
    const TFHEpp::lweParams params;
    std::shared_ptr<const TFHEpp::GateKey> gateKey;
    std::shared_ptr<const TFHEpp::CircuitKey> circuitKey;
};

using TaskAsyncTFHEpp =
    TaskAsync<TFHEpp::TLWElvl0, TFHEpp::TLWElvl0, TFHEppWorkerInfo>;
using TaskTFHEppGate =
    Task<TFHEpp::TLWElvl0, TFHEpp::TLWElvl0, TFHEppWorkerInfo>;
using TaskTFHEppGateMem =
    TaskMem<TFHEpp::TLWElvl0, TFHEpp::TLWElvl0, TFHEppWorkerInfo>;

class TaskTFHEppGateDFF
    : public TaskDFF<TFHEpp::TLWElvl0, TFHEpp::TLWElvl0, TFHEppWorkerInfo> {
public:
    TaskTFHEppGateDFF()
    {
        TFHEpp::HomCONSTANTZERO(output());
    }
};

class TaskTFHEppGateWIRE
    : public TaskMem<TFHEpp::TLWElvl0, TFHEpp::TLWElvl0, TFHEppWorkerInfo> {
private:
    AsyncThread thr_;

private:
    void startAsyncImpl(TFHEppWorkerInfo) override
    {
        if (getInputSize() == 0) {
            // Nothing to do!
        }
        else if (getInputSize() == 1) {
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
        return getInputSize() == 0 || thr_.hasFinished();
    }
};

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

class TaskTFHEppCB : public TaskAsync<TFHEpp::TLWElvl0, TFHEpp::TRGSWFFTlvl1,
                                      TFHEppWorkerInfo> {
private:
    void startSync(TFHEppWorkerInfo wi) override
    {
        auto ck = wi.circuitKey;
        assert(ck);
        TFHEpp::CircuitBootstrappingFFT(output(), input(0), *ck);
    }

public:
    TaskTFHEppCB()
        : TaskAsync<TFHEpp::TLWElvl0, TFHEpp::TRGSWFFTlvl1, TFHEppWorkerInfo>(1)
    {
    }
};

class TaskTFHEppCBInv : public TaskAsync<TFHEpp::TLWElvl0, TFHEpp::TRGSWFFTlvl1,
                                         TFHEppWorkerInfo> {
private:
    void startSync(TFHEppWorkerInfo wi) override
    {
        auto ck = wi.circuitKey;
        assert(ck);
        TFHEpp::CircuitBootstrappingFFTInv(output(), input(0), *ck);
    }

public:
    TaskTFHEppCBInv()
        : TaskAsync<TFHEpp::TLWElvl0, TFHEpp::TRGSWFFTlvl1, TFHEppWorkerInfo>(1)
    {
    }
};

class TaskTFHEppROMUX : public TaskAsync<TFHEpp::TRGSWFFTlvl1,
                                         TFHEpp::TRLWElvl1, TFHEppWorkerInfo> {
private:
    size_t inAddrWidth_, numWordsPerTRLWE_, log2NumWordsPerTRLWE_;
    std::vector<TFHEpp::TRLWElvl1> data_;

    // Workplace for UROMUX()
    std::vector<TFHEpp::TRLWElvl1> UROMUX_temp;

private:
    TFHEpp::TRLWElvl1 UROMUX()
    {
        std::vector<TFHEpp::TRLWElvl1> &temp = UROMUX_temp;
        temp.resize(data_.size() / 2);

        uint32_t log2NumTRLWE = inAddrWidth_ - log2NumWordsPerTRLWE_;
        assert(data_.size() == (1 << log2NumTRLWE));

        if (log2NumTRLWE == 0)  // data_.size() == 1
            return data_[0];

        for (int index = 0; index < data_.size() / 2; index++)
            TFHEpp::CMUXFFTlvl1(temp[index], input(log2NumWordsPerTRLWE_),
                                data_[2 * index], data_[2 * index + 1]);

        if (log2NumTRLWE == 1)  // data_.size() == 2
            return temp[0];

        for (uint32_t bit = 0; bit < log2NumTRLWE - 2; bit++) {
            uint32_t stride = 1 << bit;
            for (int index = 0; index < (data_.size() >> (bit + 2)); index++) {
                TFHEpp::CMUXFFTlvl1(temp[(2 * index) * stride],
                                    input(log2NumWordsPerTRLWE_ + bit + 1),
                                    temp[(2 * index) * stride],
                                    temp[(2 * index + 1) * stride]);
            }
        }

        TFHEpp::TRLWElvl1 res;
        uint32_t stride = 1 << (inAddrWidth_ - log2NumWordsPerTRLWE_ - 2);
        TFHEpp::CMUXFFTlvl1(res, input(inAddrWidth_ - 1), temp[0],
                            temp[stride]);
        return res;
    }

    void LROMUX(const uint32_t N, const TFHEpp::TRLWElvl1 &data)
    {
        TFHEpp::TRLWElvl1 temp;
        TFHEpp::TRLWElvl1 &acc = output();
        TFHEpp::PolynomialMulByXaiMinusOnelvl1(temp[0], data[0],
                                               2 * N - (N >> 1));
        TFHEpp::PolynomialMulByXaiMinusOnelvl1(temp[1], data[1],
                                               2 * N - (N >> 1));
        TFHEpp::trgswfftExternalProductlvl1(temp, temp,
                                            input(log2NumWordsPerTRLWE_ - 1));
        for (uint32_t i = 0; i < N; i++) {
            acc[0][i] = temp[0][i] + data[0][i];
            acc[1][i] = temp[1][i] + data[1][i];
        }

        for (uint32_t bit = 2; bit <= log2NumWordsPerTRLWE_; bit++) {
            TFHEpp::PolynomialMulByXaiMinusOnelvl1(temp[0], acc[0],
                                                   2 * N - (N >> bit));
            TFHEpp::PolynomialMulByXaiMinusOnelvl1(temp[1], acc[1],
                                                   2 * N - (N >> bit));
            TFHEpp::trgswfftExternalProductlvl1(
                temp, temp, input(log2NumWordsPerTRLWE_ - bit));
            for (uint32_t i = 0; i < N; i++) {
                acc[0][i] += temp[0][i];
                acc[1][i] += temp[1][i];
            }
        }
    }

    void startSync(TFHEppWorkerInfo wi) override
    {
        TFHEpp::TRLWElvl1 data = UROMUX();
        LROMUX(wi.params.N, data);
    }

public:
    TaskTFHEppROMUX(size_t inAddrWidth, size_t log2OutRdataWidth)
        : TaskAsync<TFHEpp::TRGSWFFTlvl1, TFHEpp::TRLWElvl1, TFHEppWorkerInfo>(
              inAddrWidth),
          inAddrWidth_(inAddrWidth)
    {
        assert(TFHEpp::DEF_Nbit >= log2OutRdataWidth);
        assert(inAddrWidth >= TFHEpp::DEF_Nbit - log2OutRdataWidth);

        log2NumWordsPerTRLWE_ = TFHEpp::DEF_Nbit - log2OutRdataWidth;
        numWordsPerTRLWE_ = 1 << log2NumWordsPerTRLWE_;
        data_.resize(1 << (inAddrWidth - log2NumWordsPerTRLWE_));
    }

    size_t size() const
    {
        return data_.size();
    }

    void set(size_t index, TFHEpp::TRLWElvl1 val)
    {
        data_.at(index) = val;
    }
};

class TaskTFHEppSEI
    : public TaskAsync<TFHEpp::TRLWElvl1, TFHEpp::TLWElvl0, TFHEppWorkerInfo> {
private:
    const static size_t ADDRESS_BIT = 7, WIDTH_BIT = 5;
    size_t index_;

private:
    void startSync(TFHEppWorkerInfo wi) override
    {
        const TFHEpp::KeySwitchingKey &ksk = wi.gateKey->ksk;

        TFHEpp::TLWElvl1 reslvl1;
        TFHEpp::SampleExtractIndexlvl1(reslvl1, input(0), index_);
        TFHEpp::IdentityKeySwitchlvl10(output(), reslvl1, ksk);
    }

public:
    TaskTFHEppSEI(size_t index)
        : TaskAsync<TFHEpp::TRLWElvl1, TFHEpp::TLWElvl0, TFHEppWorkerInfo>(1),
          index_(index)
    {
    }
};

struct TRGSWFFTlvl1Pair {
    TFHEpp::TRGSWFFTlvl1 normal, inverted;
};

class TaskTFHEppCBWithInv
    : public TaskAsync<TFHEpp::TLWElvl0, TRGSWFFTlvl1Pair, TFHEppWorkerInfo> {
private:
    void startSync(TFHEppWorkerInfo wi) override
    {
        auto ck = wi.circuitKey;
        assert(ck);
        TFHEpp::CircuitBootstrappingFFTwithInv(
            output().normal, output().inverted, input(0), *ck);
    }

public:
    TaskTFHEppCBWithInv()
        : TaskAsync<TFHEpp::TLWElvl0, TRGSWFFTlvl1Pair, TFHEppWorkerInfo>(1)
    {
    }
};

class TaskTFHEppRAMUX
    : public TaskAsync<TRGSWFFTlvl1Pair, TFHEpp::TRLWElvl1, TFHEppWorkerInfo> {
public:
    const static size_t ADDRESS_BIT = 8;

private:
    std::vector<TFHEpp::TRLWElvl1> data_;

private:
    void RAMUX()
    {
        const uint32_t num_trlwe = 1 << ADDRESS_BIT;
        std::array<TFHEpp::TRLWElvl1, num_trlwe / 2> temp;
        auto addr = [this](size_t i) -> const TFHEpp::TRGSWFFTlvl1 & {
            return input(i).inverted;
        };

        for (uint32_t index = 0; index < num_trlwe / 2; index++) {
            TFHEpp::CMUXFFTlvl1(temp[index], addr(0), data_[2 * index],
                                data_[2 * index + 1]);
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
    TaskTFHEppRAMUX()
        : TaskAsync<TRGSWFFTlvl1Pair, TFHEpp::TRLWElvl1, TFHEppWorkerInfo>(
              ADDRESS_BIT),
          data_(1 << ADDRESS_BIT)
    {
    }

    size_t size() const
    {
        return 1 << ADDRESS_BIT;
    }

    const TFHEpp::TRLWElvl1 &get(size_t addr) const
    {
        return data_.at(addr);
    }

    TFHEpp::TRLWElvl1 &get(size_t addr)
    {
        return data_.at(addr);
    }

    void set(size_t addr, TFHEpp::TRLWElvl1 val)
    {
        data_.at(addr) = std::move(val);
    }
};

class TaskTFHEppGateMUXWoSE
    : public TaskAsync<TFHEpp::TLWElvl0, TFHEpp::TRLWElvl1, TFHEppWorkerInfo> {
private:
    void startSync(TFHEppWorkerInfo wi) override
    {
        auto gk = wi.gateKey;
        TFHEpp::HomMUXwoSE(output(), input(2), input(1), input(0), *gk);
    }

public:
    TaskTFHEppGateMUXWoSE()
        : TaskAsync<TFHEpp::TLWElvl0, TFHEpp::TRLWElvl1, TFHEppWorkerInfo>(3)
    {
    }
};

class TaskTFHEppRAMCMUXs : public TaskBase<TFHEppWorkerInfo> {
private:
    size_t numReadyInputs_;
    std::shared_ptr<TFHEpp::TRLWElvl1> output_;
    std::vector<std::weak_ptr<const TRGSWFFTlvl1Pair>> inputAddrs_;
    std::weak_ptr<const TFHEpp::TRLWElvl1> inputWritten_;

    const TFHEpp::TRLWElvl1 &mem_;
    const std::bitset<TaskTFHEppRAMUX::ADDRESS_BIT> addrBitset_;

    AsyncThread thr_;

public:
    TaskTFHEppRAMCMUXs(const TFHEpp::TRLWElvl1 &mem, size_t memIndex)
        : numReadyInputs_(0),
          output_(std::make_shared<TFHEpp::TRLWElvl1>()),
          inputAddrs_(TaskTFHEppRAMUX::ADDRESS_BIT),
          mem_(mem),
          addrBitset_(memIndex)
    {
    }

    virtual ~TaskTFHEppRAMCMUXs()
    {
    }

    size_t getInputSize() const override
    {
        return TaskTFHEppRAMUX::ADDRESS_BIT + 1;
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

    void addInputPtr(const std::shared_ptr<const TRGSWFFTlvl1Pair> &input)
    {
        auto it = std::find_if(inputAddrs_.begin(), inputAddrs_.end(),
                               [](auto &&in) { return in.use_count() == 0; });
        assert(it != inputAddrs_.end());
        *it = input;
    }

    void addInputPtr(const std::shared_ptr<const TFHEpp::TRLWElvl1> &input)
    {
        assert(inputWritten_.use_count() == 0);
        inputWritten_ = input;
    }

    std::shared_ptr<const TFHEpp::TRLWElvl1> getOutputPtr() const
    {
        return output_;
    }

    void startAsync(TFHEppWorkerInfo) override
    {
        thr_ = [this] {
            *output_ = *inputWritten_.lock();
            for (size_t j = 0; j < TaskTFHEppRAMUX::ADDRESS_BIT; j++) {
                const TFHEpp::TRGSWFFTlvl1 &in =
                    addrBitset_[j] != 0 ? inputAddrs_[j].lock()->normal
                                        : inputAddrs_[j].lock()->inverted;
                TFHEpp::CMUXFFTlvl1(*output_, in, *output_, mem_);
            }
        };
    }
};

class TaskTFHEppRAMGateBootstrapping
    : public TaskAsync<TFHEpp::TLWElvl0, uint8_t /* dummy */,
                       TFHEppWorkerInfo> {
private:
    TFHEpp::TRLWElvl1 &mem_;

private:
    void startSync(TFHEppWorkerInfo wi) override
    {
        const TFHEpp::GateKey &gk = *wi.gateKey;
        TFHEpp::GateBootstrappingTLWE2TRLWEFFTlvl01(mem_, input(0), gk);
    }

public:
    TaskTFHEppRAMGateBootstrapping(TFHEpp::TRLWElvl1 &mem)
        : TaskAsync<TFHEpp::TLWElvl0, uint8_t /* dummy */, TFHEppWorkerInfo>(1),
          mem_(mem)
    {
    }
};

inline void makeTFHEppRAMNetworkImpl(
    NetworkBuilderBase<TFHEppWorkerInfo> &builder,
    const std::string &ramPortName, int indexByte)
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

    // Create inputs and CBs.
    std::vector<std::shared_ptr<TaskTFHEppCBWithInv>> cbs;
    for (size_t i = 0; i < TaskTFHEppRAMUX::ADDRESS_BIT; i++) {
        auto taskINPUT =
            builder.getTask<TaskTFHEppGateWIRE>("input", "addr", i);
        auto taskCB = std::make_shared<TaskTFHEppCBWithInv>();
        builder.addTask(
            NodeLabel{detail::genid(), "CBWithInv", utility::fok("[", i, "]")},
            taskCB);
        connectTasks(taskINPUT, taskCB);
        cbs.push_back(taskCB);
    }

    // Create RAMUX.
    auto taskRAMUX = std::make_shared<TaskTFHEppRAMUX>();
    builder.addTask(NodeLabel{detail::genid(), "RAMUX", ""}, taskRAMUX);
    builder.registerTask("ram", ramPortName, indexByte, taskRAMUX);

    // Connect CBs and RAMUX.
    for (auto &&cb : cbs)
        connectTasks(cb, taskRAMUX);

    // Create SEIs and connect with CBs.
    auto taskSEI0 = std::make_shared<TaskTFHEppSEI>(0);
    builder.addTask(NodeLabel{detail::genid(), "SEI", "[0]"}, taskSEI0);
    connectTasks(taskRAMUX, taskSEI0);

    // Create output for read-out data and connect.
    auto taskOutputReadData =
        builder.getTask<TaskTFHEppGateWIRE>("output", "rdata", indexByte);
    connectTasks(taskSEI0, taskOutputReadData);

    // Create input for write-in data.
    auto taskInputWriteData =
        builder.getTask<TaskTFHEppGateWIRE>("input", "wdata", indexByte);
    auto taskInputWriteEnabled =
        builder.getTask<TaskTFHEppGateWIRE>("input", "wren", 0);

    // Create MUXWoSE and connect.
    auto taskMUXWoSE = std::make_shared<TaskTFHEppGateMUXWoSE>();
    builder.addTask(NodeLabel{detail::genid(), "MUXWoSE", ""}, taskMUXWoSE);
    connectTasks(taskSEI0, taskMUXWoSE);
    connectTasks(taskInputWriteData, taskMUXWoSE);
    connectTasks(taskInputWriteEnabled, taskMUXWoSE);

    // Create links of CMUXs -> SEI -> GateBootstrapping.
    for (int i = 0; i < (1 << TaskTFHEppRAMUX::ADDRESS_BIT); i++) {
        // Create components...
        auto taskCMUXs =
            std::make_shared<TaskTFHEppRAMCMUXs>(taskRAMUX->get(i), i);
        builder.addTask(
            NodeLabel{detail::genid(), "CMUXs", utility::fok("[", i, "]")},
            taskCMUXs);

        auto taskSEI = std::make_shared<TaskTFHEppSEI>(0);
        builder.addTask(
            NodeLabel{detail::genid(), "SEI", utility::fok("[", i, "]")},
            taskSEI);

        auto taskGB =
            std::make_shared<TaskTFHEppRAMGateBootstrapping>(taskRAMUX->get(i));
        builder.addTask(
            NodeLabel{detail::genid(), "GB", utility::fok("[", i, "]")},
            taskGB);

        // ... and connect them.
        connectTasks(taskMUXWoSE, taskCMUXs);
        for (auto &&cb : cbs)
            connectTasks(cb, taskCMUXs);
        connectTasks(taskCMUXs, taskSEI);
        connectTasks(taskSEI, taskGB);
    }
}

inline TaskNetwork<TFHEppWorkerInfo> makeTFHEppRAMNetwork(
    const std::string &ramPortName)
{
    NetworkBuilderBase<TFHEppWorkerInfo> builder;

    // Inputs for address.
    for (size_t i = 0; i < TaskTFHEppRAMUX::ADDRESS_BIT; i++)
        builder.addINPUT<TaskTFHEppGateWIRE>(detail::genid(), "addr", i, false);

    // Input for write-in flag.
    builder.addINPUT<TaskTFHEppGateWIRE>(detail::genid(), "wren", 0, false);

    for (int indexByte = 0; indexByte < 8; indexByte++) {
        // Input for data to write into RAM.
        builder.addINPUT<TaskTFHEppGateWIRE>(detail::genid(), "wdata",
                                             indexByte, false);
        // Output for data to be read from RAM.
        builder.addOUTPUT<TaskTFHEppGateWIRE>(detail::genid(), "rdata",
                                              indexByte, true);

        makeTFHEppRAMNetworkImpl(builder, ramPortName, indexByte);
    }

    return TaskNetwork<TFHEppWorkerInfo>(std::move(builder));
}

inline TaskNetwork<TFHEppWorkerInfo> makeTFHEppROMNetwork(
    size_t inAddrWidth, size_t log2OutRdataWidth)
{
    size_t outRdataWidth = 1 << log2OutRdataWidth,
           log2NumWordsPerTRLWE = TFHEpp::DEF_Nbit - log2OutRdataWidth;

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
        NodeLabel{detail::genid(), "ROMUX", ""}, inAddrWidth,
        log2OutRdataWidth);
    builder.registerTask("rom", "all", 0, taskROMUX);

    // Create inputs and CB/CBInvs, and connect them to ROMUX.
    for (int i = 0; i < log2NumWordsPerTRLWE; i++) {
        auto taskINPUT = builder.addINPUT<TaskTFHEppGateWIRE>(detail::genid(),
                                                              "addr", i, false);
        auto taskCB = builder.emplaceTask<TaskTFHEppCB>(
            NodeLabel{detail::genid(), "CB", utility::fok("[", i, "]")});
        connectTasks(taskINPUT, taskCB);
        connectTasks(taskCB, taskROMUX);
    }
    for (int i = log2NumWordsPerTRLWE; i < inAddrWidth; i++) {
        auto taskINPUT = builder.addINPUT<TaskTFHEppGateWIRE>(detail::genid(),
                                                              "addr", i, false);
        auto taskCBInv = builder.emplaceTask<TaskTFHEppCBInv>(
            NodeLabel{detail::genid(), "CBInv", utility::fok("[", i, "]")});
        connectTasks(taskINPUT, taskCBInv);
        connectTasks(taskCBInv, taskROMUX);
    }

    // Create SEIs and outputs, and connect them properly
    for (int i = 0; i < outRdataWidth; i++) {
        auto taskSEI = std::make_shared<TaskTFHEppSEI>(i);
        builder.addTask(
            NodeLabel{detail::genid(), "SEI", utility::fok("[", i, "]")},
            taskSEI);
        connectTasks(taskROMUX, taskSEI);

        auto taskOUTPUT = builder.addOUTPUT<TaskTFHEppGateWIRE>(
            detail::genid(), "rdata", i, true);
        connectTasks(taskSEI, taskOUTPUT);
    }

    return TaskNetwork<TFHEppWorkerInfo>(std::move(builder));
}

void doTFHE(const Options &opt);
void processAllGates(TFHEppNetwork &net, int numWorkers, TFHEppWorkerInfo wi,
                     std::shared_ptr<ProgressGraphMaker> graph = nullptr);

#endif
