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

class TaskTFHEppROMUX : public TaskAsync<TFHEpp::TRGSWFFTlvl1,
                                         TFHEpp::TRLWElvl1, TFHEppWorkerInfo> {
private:
    const static size_t ADDRESS_BIT = 7, WIDTH_BIT = 5;
    std::vector<TFHEpp::TRLWElvl1> data_;

private:
    TFHEpp::TRLWElvl1 UROMUX()
    {
        const uint32_t num_trlwe = 1 << (ADDRESS_BIT - WIDTH_BIT);
        std::array<TFHEpp::TRLWElvl1, num_trlwe / 2> temp;

        for (uint32_t index = 0; index < num_trlwe / 2; index++)
            TFHEpp::CMUXFFTlvl1(temp[index], input(WIDTH_BIT),
                                data_[2 * index + 1], data_[2 * index]);

        for (uint32_t bit = 0; bit < (ADDRESS_BIT - WIDTH_BIT - 2); bit++) {
            const uint32_t stride = 1 << bit;
            const uint32_t offset = (1 << bit) - 1;
            for (uint32_t index = 0; index < (num_trlwe >> (bit + 1));
                 index++) {
                TFHEpp::CMUXFFTlvl1(temp[(2 * index + 1) * stride + offset],
                                    input(WIDTH_BIT + bit + 1),
                                    temp[(2 * index + 1) * stride + offset],
                                    temp[(2 * index) * stride + offset]);
            }
        }
        const uint32_t stride = 1 << (ADDRESS_BIT - WIDTH_BIT - 2);
        const uint32_t offset = (1 << (ADDRESS_BIT - WIDTH_BIT - 2)) - 1;
        TFHEpp::TRLWElvl1 res;
        TFHEpp::CMUXFFTlvl1(res, input(ADDRESS_BIT - 1), temp[stride + offset],
                            temp[offset]);
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
        TFHEpp::trgswfftExternalProductlvl1(temp, temp, input(WIDTH_BIT - 1));
        for (uint32_t i = 0; i < N; i++) {
            acc[0][i] = temp[0][i] + data[0][i];
            acc[1][i] = temp[1][i] + data[1][i];
        }

        for (uint32_t bit = 2; bit <= WIDTH_BIT; bit++) {
            TFHEpp::PolynomialMulByXaiMinusOnelvl1(temp[0], acc[0],
                                                   2 * N - (N >> bit));
            TFHEpp::PolynomialMulByXaiMinusOnelvl1(temp[1], acc[1],
                                                   2 * N - (N >> bit));
            TFHEpp::trgswfftExternalProductlvl1(temp, temp,
                                                input(WIDTH_BIT - bit));
            for (uint32_t i = 0; i < N; i++) {
                acc[0][i] += temp[0][i];
                acc[1][i] += temp[1][i];
            }
        }
    }

    void startSync(TFHEppWorkerInfo wi) override
    {
        // input(0) : TRGSWFFTlvl1 -> data : TRLWElvl1
        TFHEpp::TRLWElvl1 data = UROMUX();
        // data : TRLWElvl1 -> output() : TRLWElvl1
        LROMUX(wi.params.N, data);
    }

public:
    TaskTFHEppROMUX()
        : TaskAsync<TFHEpp::TRGSWFFTlvl1, TFHEpp::TRLWElvl1, TFHEppWorkerInfo>(
              ADDRESS_BIT),
          data_(1 << (ADDRESS_BIT - WIDTH_BIT))
    {
    }

    void set128le(size_t addr, TFHEpp::TRLWElvl1 val)
    {
        assert((addr & 0b1111111) == 0);
        data_.at(addr >> 7) = std::move(val);
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

    bool isValid() override
    {
        return this->depnode() &&
               std::all_of(inputAddrs_.begin(), inputAddrs_.end(),
                           [](auto &&in) { return in.use_count() != 0; }) &&
               inputWritten_.use_count() != 0;
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
            NodeLabel{builder.genid(), "CBWithInv", detail::fok("[", i, "]")},
            0, taskCB);
        builder.connectTasks(taskINPUT, taskCB);
        cbs.push_back(taskCB);
    }

    // Create RAMUX.
    auto taskRAMUX = std::make_shared<TaskTFHEppRAMUX>();
    builder.addTask(NodeLabel{builder.genid(), "RAMUX", ""}, 0, taskRAMUX);
    builder.registerTask("ram", ramPortName, indexByte, taskRAMUX);

    // Connect CBs and RAMUX.
    for (auto &&cb : cbs)
        builder.connectTasks(cb, taskRAMUX);

    // Create SEIs and connect with CBs.
    auto taskSEI0 = std::make_shared<TaskTFHEppSEI>(0);
    builder.addTask(NodeLabel{builder.genid(), "SEI", "[0]"}, 0, taskSEI0);
    builder.connectTasks(taskRAMUX, taskSEI0);

    // Create output for read-out data and connect.
    auto taskOutputReadData =
        builder.getTask<TaskTFHEppGateWIRE>("output", "rdata", indexByte);
    builder.connectTasks(taskSEI0, taskOutputReadData);

    // Create input for write-in data.
    auto taskInputWriteData =
        builder.getTask<TaskTFHEppGateWIRE>("input", "wdata", indexByte);
    auto taskInputWriteEnabled =
        builder.getTask<TaskTFHEppGateWIRE>("input", "wren", 0);

    // Create MUXWoSE and connect.
    auto taskMUXWoSE = std::make_shared<TaskTFHEppGateMUXWoSE>();
    builder.addTask(NodeLabel{builder.genid(), "MUXWoSE", ""}, 0, taskMUXWoSE);
    builder.connectTasks(taskSEI0, taskMUXWoSE);
    builder.connectTasks(taskInputWriteData, taskMUXWoSE);
    builder.connectTasks(taskInputWriteEnabled, taskMUXWoSE);

    // Create links of CMUXs -> SEI -> GateBootstrapping.
    for (int i = 0; i < (1 << TaskTFHEppRAMUX::ADDRESS_BIT); i++) {
        // Create components...
        auto taskCMUXs =
            std::make_shared<TaskTFHEppRAMCMUXs>(taskRAMUX->get(i), i);
        builder.addTask(
            NodeLabel{builder.genid(), "CMUXs", detail::fok("[", i, "]")}, 0,
            taskCMUXs);

        auto taskSEI = std::make_shared<TaskTFHEppSEI>(0);
        builder.addTask(
            NodeLabel{builder.genid(), "SEI", detail::fok("[", i, "]")}, 0,
            taskSEI);

        auto taskGB =
            std::make_shared<TaskTFHEppRAMGateBootstrapping>(taskRAMUX->get(i));
        builder.addTask(
            NodeLabel{builder.genid(), "GB", detail::fok("[", i, "]")}, 0,
            taskGB);

        // ... and connect them.
        builder.connectTasks(taskMUXWoSE, taskCMUXs);
        for (auto &&cb : cbs)
            builder.connectTasks(cb, taskCMUXs);
        builder.connectTasks(taskCMUXs, taskSEI);
        builder.connectTasks(taskSEI, taskGB);
    }
}

inline TaskNetwork<TFHEppWorkerInfo> makeTFHEppRAMNetwork(
    const std::string &ramPortName)
{
    NetworkBuilderBase<TFHEppWorkerInfo> builder;

    // Inputs for address.
    for (size_t i = 0; i < TaskTFHEppRAMUX::ADDRESS_BIT; i++)
        builder.addINPUT<TaskTFHEppGateWIRE>(builder.genid(), 0, "addr", i,
                                             false);

    // Input for write-in flag.
    builder.addINPUT<TaskTFHEppGateWIRE>(builder.genid(), 0, "wren", 0, false);

    for (int indexByte = 0; indexByte < 8; indexByte++) {
        // Input for data to write into RAM.
        builder.addINPUT<TaskTFHEppGateWIRE>(builder.genid(), 0, "wdata",
                                             indexByte, false);
        // Output for data to be read from RAM.
        builder.addOUTPUT<TaskTFHEppGateWIRE>(builder.genid(), 0, "rdata",
                                              indexByte, true);

        makeTFHEppRAMNetworkImpl(builder, ramPortName, indexByte);
    }

    return TaskNetwork<TFHEppWorkerInfo>(std::move(builder));
}

inline TaskNetwork<TFHEppWorkerInfo> makeTFHEppROMNetwork()
{
    /*
       INPUT (ROM[0]) -- CB --+-----+  +-- SEI --- OUTPUT (ROM[0])
                              |  R  |  |
       INPUT (ROM[1]) -- CB --+  O  +--+-- SEI --- OUTPUT (ROM[1])
                              |  M  |  |
       INPUT (ROM[2]) -- CB --+  U  +  +-- SEI --- OUTPUT (ROM[2])
                              |  X  |  |
                                ...       ...
                              |     |  |
       INPUT (ROM[6]) -- CB --+-----+  +-- SEI --- OUTPUT (ROM[6])
                                       |
                                       +-- SEI --- OUTPUT (ROM[7])
                                       |
                                          ...
                                       |
                                       +-- SEI --- OUTPUT (ROM[31])
    */

    NetworkBuilderBase<TFHEppWorkerInfo> builder;

    // Create inputs and CBs.
    std::vector<std::shared_ptr<TaskTFHEppCB>> cbs;
    for (int i = 0; i < 7; i++) {
        auto taskINPUT = builder.addINPUT<TaskTFHEppGateWIRE>(
            builder.genid(), 0, "ROM", i, false);
        auto taskCB = std::make_shared<TaskTFHEppCB>();
        builder.addTask(
            NodeLabel{builder.genid(), "CB", detail::fok("[", i, "]")}, 0,
            taskCB);
        builder.connectTasks(taskINPUT, taskCB);
        cbs.push_back(taskCB);
    }

    // Create ROMUX.
    auto taskROMUX = std::make_shared<TaskTFHEppROMUX>();
    builder.addTask(NodeLabel{builder.genid(), "ROMUX", ""}, 0, taskROMUX);
    builder.registerTask("rom", "all", 0, taskROMUX);

    // Connect CBs and ROMUX
    for (auto &&cb : cbs)
        builder.connectTasks(cb, taskROMUX);

    // Create SEIs and outputs, and connect them properly
    for (int i = 0; i < 32; i++) {
        auto taskSEI = std::make_shared<TaskTFHEppSEI>(i);
        builder.addTask(
            NodeLabel{builder.genid(), "SEI", detail::fok("[", i, "]")}, 0,
            taskSEI);
        builder.connectTasks(taskROMUX, taskSEI);

        auto taskOUTPUT = builder.addOUTPUT<TaskTFHEppGateWIRE>(
            builder.genid(), 0, "ROM", i, true);
        builder.connectTasks(taskSEI, taskOUTPUT);
    }

    return TaskNetwork<TFHEppWorkerInfo>(std::move(builder));
}

void doTFHE(const Options &opt);
void processAllGates(TFHEppNetwork &net, int numWorkers, TFHEppWorkerInfo wi,
                     std::shared_ptr<ProgressGraphMaker> graph = nullptr);

#endif
