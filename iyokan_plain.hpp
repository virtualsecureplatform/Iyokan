#ifndef VIRTUALSECUREPLATFORM_IYOKAN_PLAIN_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_PLAIN_HPP

#include "iyokan.hpp"
#include "packet.hpp"

struct PlainWorkerInfo {
};

using TaskPlainGate = Task<Bit, Bit, PlainWorkerInfo>;
using TaskPlainGateMem = TaskMem<Bit, Bit, PlainWorkerInfo>;

class TaskPlainGateDFF : public TaskDFF<Bit, Bit, PlainWorkerInfo> {
public:
    TaskPlainGateDFF()
    {
        output() = 0_b;
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

class TaskPlainSplitter : public Task<uint32_t, Bit, PlainWorkerInfo> {
private:
    size_t index_;

private:
    void startAsyncImpl(PlainWorkerInfo) override
    {
        output() = Bit((input(0) >> index_) & 1u);
    }

public:
    TaskPlainSplitter()
    {
    }

    TaskPlainSplitter(size_t index)
        : Task<uint32_t, Bit, PlainWorkerInfo>(1), index_(index)
    {
    }

    bool hasFinished() const override
    {
        return true;
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<Task<uint32_t, Bit, PlainWorkerInfo>>(this),
           index_);
    }
};
CEREAL_REGISTER_TYPE(TaskPlainSplitter);

class TaskPlainRAM
    : public Task<Bit, uint32_t /* only 8 Bit used */, PlainWorkerInfo> {
public:
    const static size_t ADDRESS_BIT = 8;

private:
    std::vector<uint8_t> data_;

private:
    void startAsyncImpl(PlainWorkerInfo) override
    {
        size_t addr = 0;
        for (size_t i = 0; i < ADDRESS_BIT; i++)
            addr |= static_cast<size_t>(input(i)) << i;
        output() = data_.at(addr);

        if (input(ADDRESS_BIT) == 1_b) {
            uint8_t val = 0;
            for (int i = 0; i < 8; i++)
                val |= static_cast<uint8_t>(input(ADDRESS_BIT + 1 + i)) << i;
            data_.at(addr) = val;
        }
    }

public:
    TaskPlainRAM()
        : Task<Bit, uint32_t, PlainWorkerInfo>(ADDRESS_BIT /* addr */ +
                                               1 /* wren */ + 8 /* wdata */),
          data_(1 << ADDRESS_BIT)
    {
    }

    size_t size() const
    {
        return (1 << ADDRESS_BIT);
    }

    void set(size_t addr, uint8_t val)
    {
        data_.at(addr) = val;
    }

    uint8_t get(size_t addr)
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
        ar(cereal::base_class<Task<Bit, uint32_t, PlainWorkerInfo>>(this),
           data_);
    }
};
CEREAL_REGISTER_TYPE(TaskPlainRAM);

inline TaskNetwork<PlainWorkerInfo> makePlainRAMNetwork(
    const std::string &ramPortName)
{
    NetworkBuilderBase<PlainWorkerInfo> builder;

    // Create RAM
    auto taskRAM = std::make_shared<TaskPlainRAM>();
    builder.addTask(NodeLabel{"RAM", "body"}, taskRAM);
    builder.registerTask("ram", ramPortName, 0, taskRAM);

    // Create inputs and outputs, and connect to RAM
    for (size_t i = 0; i < TaskPlainRAM::ADDRESS_BIT; i++) {
        auto taskINPUT = builder.addINPUT<TaskPlainGateWIRE>("addr", i, false);
        connectTasks(taskINPUT, taskRAM);
    }
    auto taskWriteEnabled =
        builder.addINPUT<TaskPlainGateWIRE>("wren", 0, false);
    connectTasks(taskWriteEnabled, taskRAM);
    for (size_t i = 0; i < 8; i++) {
        auto taskINPUT = builder.addINPUT<TaskPlainGateWIRE>("wdata", i, false);
        connectTasks(taskINPUT, taskRAM);
    }
    for (size_t i = 0; i < 8; i++) {
        auto taskSplitter = std::make_shared<TaskPlainSplitter>(i);
        builder.addTask(NodeLabel{"SPLITTER", utility::fok("RAM[", i, "]")},
                        taskSplitter);
        connectTasks(taskRAM, taskSplitter);

        auto taskOUTPUT =
            builder.addOUTPUT<TaskPlainGateWIRE>("rdata", i, true);
        connectTasks(taskSplitter, taskOUTPUT);
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
