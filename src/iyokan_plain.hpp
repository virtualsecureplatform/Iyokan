#ifndef VIRTUALSECUREPLATFORM_IYOKAN_PLAIN_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_PLAIN_HPP

#include "iyokan.hpp"
#include "packet.hpp"

struct PlainWorkerInfo {
};

using TaskPlainGate = Task<Bit, Bit, PlainWorkerInfo>;
using TaskPlainGateMem = TaskMem<Bit, Bit, PlainWorkerInfo>;

class TaskPlainGateDFF : public TaskDFF<Bit, Bit, PlainWorkerInfo> {
private:
    Bit initialValue_;

public:
    TaskPlainGateDFF()
    {
        initialValue_ = 0_b;
        output() = initialValue_;
    }

    TaskPlainGateDFF(Bit initValue)
    {
        initialValue_ = initValue;
        output() = initialValue_;
    }

    void setInitialValue()
    {
        output() = initialValue_;
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<TaskDFF<Bit, Bit, PlainWorkerInfo>>(this));
    }
};
CEREAL_REGISTER_TYPE(TaskPlainGateDFF);

class TaskPlainGateWIRE : public TaskMem<Bit, Bit, PlainWorkerInfo> {
private:
    void startAsyncImpl(PlainWorkerInfo) override
    {
        if (getInputSize() == 0) {
            // Nothing to do!
        }
        else if (getInputSize() == 1) {
            output() = input(0);
        }
        else {
            assert(false);
        }
    }

public:
    TaskPlainGateWIRE()
    {
    }

    TaskPlainGateWIRE(bool inputNeeded)
        : TaskMem<Bit, Bit, PlainWorkerInfo>(inputNeeded ? 1 : 0)
    {
    }

    bool hasFinished() const override
    {
        return true;
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<TaskMem<Bit, Bit, PlainWorkerInfo>>(this));
    }
};
CEREAL_REGISTER_TYPE(TaskPlainGateWIRE);

#define DEFINE_TASK_PLAIN_GATE(name, numInputs, expr)    \
    class TaskPlainGate##name : public TaskPlainGate {   \
    private:                                             \
        void startAsyncImpl(PlainWorkerInfo) override    \
        {                                                \
            output() = (expr);                           \
        }                                                \
                                                         \
    public:                                              \
        TaskPlainGate##name() : TaskPlainGate(numInputs) \
        {                                                \
        }                                                \
                                                         \
        bool hasFinished() const override                \
        {                                                \
            return true;                                 \
        }                                                \
                                                         \
        template <class Archive>                         \
        void serialize(Archive &ar)                      \
        {                                                \
            ar(cereal::base_class<TaskPlainGate>(this)); \
        }                                                \
    };                                                   \
    CEREAL_REGISTER_TYPE(TaskPlainGate##name);
DEFINE_TASK_PLAIN_GATE(AND, 2, (input(0) & input(1)));
DEFINE_TASK_PLAIN_GATE(NAND, 2, ~(input(0) & input(1)));
DEFINE_TASK_PLAIN_GATE(ANDNOT, 2, (input(0) & ~input(1)));
DEFINE_TASK_PLAIN_GATE(OR, 2, (input(0) | input(1)));
DEFINE_TASK_PLAIN_GATE(NOR, 2, ~(input(0) | input(1)));
DEFINE_TASK_PLAIN_GATE(ORNOT, 2, (input(0) | ~input(1)));
DEFINE_TASK_PLAIN_GATE(XOR, 2, (input(0) ^ input(1)));
DEFINE_TASK_PLAIN_GATE(XNOR, 2, ~(input(0) ^ input(1)));
DEFINE_TASK_PLAIN_GATE(MUX, 3, input(2) == 0_b ? input(0) : input(1));
DEFINE_TASK_PLAIN_GATE(NOT, 1, ~input(0));
DEFINE_TASK_PLAIN_GATE(CONSTONE, 0, 1_b);
DEFINE_TASK_PLAIN_GATE(CONSTZERO, 0, 0_b);
#undef DEFINE_TASK_PLAIN_GATE

class PlainNetworkBuilder
    : public NetworkBuilder<TaskPlainGate, TaskPlainGateMem, TaskPlainGateDFF,
                            TaskPlainGateWIRE, PlainWorkerInfo> {
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
    DEFINE_GATE_IMPL(CONSTONE);
    DEFINE_GATE_IMPL(CONSTZERO);
#undef DEFINE_GATE_IMPL
};

using PlainNetwork = PlainNetworkBuilder::NetworkType;

class PlainWorker : public Worker<PlainWorkerInfo> {
private:
    PlainWorkerInfo getWorkerInfo()
    {
        return {};
    }

public:
    PlainWorker(ReadyQueue<PlainWorkerInfo> &readyQueue,
                size_t &numFinishedTargets,
                std::shared_ptr<ProgressGraphMaker> graph)
        : Worker(readyQueue, numFinishedTargets, graph)
    {
    }
};

class TaskPlainROMUX : public Task<Bit, Bit, PlainWorkerInfo> {
private:
    size_t inAddrWidth_;
    std::vector<Bit> data_;

private:
    void startAsyncImpl(PlainWorkerInfo) override
    {
        size_t addr = 0;
        for (int i = 0; i < inAddrWidth_; i++)
            addr |= (input(i) == 1_b ? 1u : 0u) << i;
        output() = data_.at(addr);
    }

public:
    TaskPlainROMUX()
    {
    }

    TaskPlainROMUX(size_t inAddrWidth)
        : Task<Bit, Bit, PlainWorkerInfo>(inAddrWidth),
          inAddrWidth_(inAddrWidth),
          data_(1 << inAddrWidth)
    {
    }

    size_t size() const
    {
        return data_.size();
    }

    void set(size_t addr, Bit val)
    {
        data_.at(addr) = val;
    }

    Bit get(size_t addr)
    {
        return data_.at(addr);
    }

    bool hasFinished() const override
    {
        return true;
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<Task<Bit, Bit, PlainWorkerInfo>>(this),
           inAddrWidth_, data_);
    }
};
CEREAL_REGISTER_TYPE(TaskPlainROMUX);

class TaskPlainRAMReader : public Task<Bit, Bit, PlainWorkerInfo> {
private:
    std::shared_ptr<std::vector<Bit>> data_;

public:
    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<Task<Bit, Bit, PlainWorkerInfo>>(this), data_);
    }

private:
    void startAsyncImpl(PlainWorkerInfo) override
    {
        const size_t addrWidth = getAddressWidth();
        size_t addr = 0;
        for (size_t i = 0; i < addrWidth; i++)
            addr |= static_cast<size_t>(input(i)) << i;
        output() = data_->at(addr);
    }

public:
    TaskPlainRAMReader()
    {
    }

    TaskPlainRAMReader(size_t addressWidth)
        : Task<Bit, Bit, PlainWorkerInfo>(addressWidth)
    {
        data_ = std::make_shared<std::vector<Bit>>(1 << addressWidth);
    }

    size_t getAddressWidth() const
    {
        return utility::ctz(data_->size());
    }

    size_t size() const
    {
        return (1 << getAddressWidth());
    }

    void set(size_t addr, Bit val)
    {
        data_->at(addr) = val;
    }

    Bit get(size_t addr)
    {
        return data_->at(addr);
    }

    const std::shared_ptr<std::vector<Bit>> &data()
    {
        return data_;
    }

    bool hasFinished() const override
    {
        return true;
    }
};
CEREAL_REGISTER_TYPE(TaskPlainRAMReader);

class TaskPlainRAMWriter
    : public Task<Bit, uint8_t /* dummy */, PlainWorkerInfo> {
private:
    std::weak_ptr<std::vector<Bit>> data_;

public:
    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<Task<Bit, uint8_t, PlainWorkerInfo>>(this),
           data_);
    }

private:
    void startAsyncImpl(PlainWorkerInfo) override
    {
        std::shared_ptr<std::vector<Bit>> data = data_.lock();
        assert(data);
        const size_t addrWidth = getAddressWidth();
        if (input(addrWidth) == 1_b) {  // wren is asserted
            // Make address
            size_t addr = 0;
            for (size_t i = 0; i < addrWidth; i++)
                addr |= static_cast<size_t>(input(i)) << i;
            // Write in the data
            data->at(addr) = input(addrWidth + 1);
        }

        // NOTE: input(addrWidth + 1 + 1) (that is rdata) is not used at all
        // here, but it is used to make dependence relationship between
        // TaskPlainRAMReader and TaskPlainRAMWriter. TaskPlainRAMWriter MUST be
        // started AFTER TaskPlainRAMReader has finished so that the value from
        // the RAM will not be corrupted.
    }

public:
    TaskPlainRAMWriter()
    {
    }

    TaskPlainRAMWriter(size_t addressWidth,
                       const std::shared_ptr<std::vector<Bit>> &data)
        : Task<Bit, uint8_t, PlainWorkerInfo>(/* addr */ addressWidth +
                                              /* wren */ 1 +
                                              /* wdata */ 1 +
                                              /* rdata */ 1),
          data_(data)
    {
    }

    size_t getAddressWidth() const
    {
        std::shared_ptr<std::vector<Bit>> data = data_.lock();
        assert(data);
        return utility::ctz(data->size());
    }

    bool hasFinished() const override
    {
        return true;
    }
};
CEREAL_REGISTER_TYPE(TaskPlainRAMWriter);

inline TaskNetwork<PlainWorkerInfo> makePlainRAMNetwork(
    size_t addressWidth, size_t dataWidth, const std::string &ramPortName)
{
    NetworkBuilderBase<PlainWorkerInfo> builder;

    // Create inputs
    std::vector<std::shared_ptr<TaskPlainGateWIRE>> inputsAddr, inputsWdata;
    for (size_t i = 0; i < addressWidth; i++) {
        auto taskINPUT = builder.addINPUT<TaskPlainGateWIRE>("addr", i, false);
        inputsAddr.push_back(taskINPUT);
    }
    auto taskWriteEnabled =
        builder.addINPUT<TaskPlainGateWIRE>("wren", 0, false);
    for (size_t i = 0; i < dataWidth; i++) {
        auto taskINPUT = builder.addINPUT<TaskPlainGateWIRE>("wdata", i, false);
        inputsWdata.push_back(taskINPUT);
    }

    // Create outputs
    std::vector<std::shared_ptr<TaskPlainGateWIRE>> outputsRdata;
    for (size_t i = 0; i < dataWidth; i++) {
        auto taskOUTPUT =
            builder.addOUTPUT<TaskPlainGateWIRE>("rdata", i, true);
        outputsRdata.push_back(taskOUTPUT);
    }

    // Create and connect components for each data bit
    for (size_t i = 0; i < dataWidth; i++) {
        // Create RAMReader and RAMWriter
        auto taskRAMReader = builder.emplaceTask<TaskPlainRAMReader>(
            NodeLabel{"RAMReader", fmt::format("[{}]", i)}, addressWidth);
        builder.registerTask("ram", ramPortName, i, taskRAMReader);
        auto taskRAMWriter = builder.emplaceTask<TaskPlainRAMWriter>(
            NodeLabel{"RAMWriter", fmt::format("[{}]", i)}, addressWidth,
            taskRAMReader->data());

        // Connect addr to RAMReader and RAMWriter
        for (size_t i = 0; i < addressWidth; i++) {
            connectTasks(inputsAddr.at(i), taskRAMReader);
            connectTasks(inputsAddr.at(i), taskRAMWriter);
        }
        // Connect RAMReader to rdata
        connectTasks(taskRAMReader, outputsRdata.at(i));
        // Connect wren, wdata and rdata to RAMWriter
        connectTasks(taskWriteEnabled, taskRAMWriter);
        connectTasks(inputsWdata.at(i), taskRAMWriter);
        connectTasks(outputsRdata.at(i), taskRAMWriter);
    }

    return TaskNetwork<PlainWorkerInfo>(std::move(builder));
}

inline TaskNetwork<PlainWorkerInfo> makePlainROMNetwork(size_t inAddrWidth,
                                                        size_t outRdataWidth)
{
    /*
       INPUT (addr[0])  ------------+-----+
                                    |  R  |
       INPUT (addr[1])  ------------+  O  +----- OUTPUT (rdata[i])
                                    |  M  |
       INPUT (addr[2])  ------------+  U  +
                                    |  X  |
                                    | [i] |
                                      ...
                                    |     |
       INPUT (addr[inAddrWidth]) ---+-----+
    */

    NetworkBuilderBase<PlainWorkerInfo> builder;

    // Create inputs.
    std::vector<std::shared_ptr<TaskPlainGateWIRE>> inputs;
    for (int i = 0; i < inAddrWidth; i++) {
        auto taskINPUT = builder.addINPUT<TaskPlainGateWIRE>("addr", i, false);
        inputs.push_back(taskINPUT);
    }

    // Create and connect ROMUX for each rdata's bit.
    for (int i = 0; i < outRdataWidth; i++) {
        auto taskROMUX = builder.emplaceTask<TaskPlainROMUX>(
            NodeLabel{"ROM", "body"}, inAddrWidth);
        builder.registerTask("rom", "all", i, taskROMUX);

        auto output = builder.addOUTPUT<TaskPlainGateWIRE>("rdata", i, true);

        for (auto &&in : inputs)
            connectTasks(in, taskROMUX);
        connectTasks(taskROMUX, output);
    }

    return TaskNetwork<PlainWorkerInfo>(std::move(builder));
}

bool isSerializedPlainFrontend(const std::string &filepath);
void doPlain(const Options &opt);
void processAllGates(PlainNetwork &net, int numWorkers,
                     std::shared_ptr<ProgressGraphMaker> graph = nullptr);

#endif
