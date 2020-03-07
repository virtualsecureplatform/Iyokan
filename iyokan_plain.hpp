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
};

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
    TaskPlainGateWIRE(bool inputNeeded)
        : TaskMem<Bit, Bit, PlainWorkerInfo>(inputNeeded ? 1 : 0)
    {
    }

    bool hasFinished() const override
    {
        return true;
    }
};

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
    };
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

class TaskPlainROM : public Task<Bit, uint32_t, PlainWorkerInfo> {
private:
    std::vector<uint32_t> data_;

private:
    void startAsyncImpl(PlainWorkerInfo) override
    {
        size_t addr = 0;
        for (int i = 6; i >= 0; i--)
            addr = (addr << 1) | static_cast<size_t>(input(i));
        output() = data_.at(addr);
    }

public:
    TaskPlainROM() : Task<Bit, uint32_t, PlainWorkerInfo>(7), data_(1 << 7)
    {
    }

    size_t size() const
    {
        return data_.size() * 4;
    }

    void set4le(size_t addr, uint32_t val)
    {
        assert((addr & 0b11) == 0);
        data_.at(addr >> 2) = val;
    }

    uint32_t get4le(size_t addr)
    {
        assert((addr & 0b11) == 0);
        return data_.at(addr >> 2);
    }

    bool hasFinished() const override
    {
        return true;
    }
};

class TaskPlainSplitter : public Task<uint32_t, Bit, PlainWorkerInfo> {
private:
    size_t index_;

private:
    void startAsyncImpl(PlainWorkerInfo) override
    {
        output() = Bit((input(0) >> index_) & 1u);
    }

public:
    TaskPlainSplitter(size_t index)
        : Task<uint32_t, Bit, PlainWorkerInfo>(1), index_(index)
    {
    }

    bool hasFinished() const override
    {
        return true;
    }
};

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
};

inline TaskNetwork<PlainWorkerInfo> makePlainRAMNetwork(
    const std::string &ramPortName)
{
    NetworkBuilderBase<PlainWorkerInfo> builder;

    // Create RAM
    auto taskRAM = std::make_shared<TaskPlainRAM>();
    builder.addTask(NodeLabel{detail::genid(), "RAM", "body"}, taskRAM);
    builder.registerTask("ram", ramPortName, 0, taskRAM);

    // Create inputs and outputs, and connect to RAM
    for (size_t i = 0; i < TaskPlainRAM::ADDRESS_BIT; i++) {
        auto taskINPUT = builder.addINPUT<TaskPlainGateWIRE>(detail::genid(),
                                                             "addr", i, false);
        builder.connectTasks(taskINPUT, taskRAM);
    }
    auto taskWriteEnabled =
        builder.addINPUT<TaskPlainGateWIRE>(detail::genid(), "wren", 0, false);
    builder.connectTasks(taskWriteEnabled, taskRAM);
    for (size_t i = 0; i < 8; i++) {
        auto taskINPUT = builder.addINPUT<TaskPlainGateWIRE>(detail::genid(),
                                                             "wdata", i, false);
        builder.connectTasks(taskINPUT, taskRAM);
    }
    for (size_t i = 0; i < 8; i++) {
        auto taskSplitter = std::make_shared<TaskPlainSplitter>(i);
        builder.addTask(NodeLabel{detail::genid(), "SPLITTER",
                                  utility::fok("RAM[", i, "]")},
                        taskSplitter);
        builder.connectTasks(taskRAM, taskSplitter);

        auto taskOUTPUT = builder.addOUTPUT<TaskPlainGateWIRE>(
            detail::genid(), "rdata", i, true);
        builder.connectTasks(taskSplitter, taskOUTPUT);
    }

    return TaskNetwork<PlainWorkerInfo>(std::move(builder));
}

inline TaskNetwork<PlainWorkerInfo> makePlainROMNetwork()
{
    /*
       INPUT (addr[0]) ---+-----+  +-- SPLITTER --- OUTPUT (rdata[0])
                          |     |  |
       INPUT (addr[1]) ---+ ROM +--+-- SPLITTER --- OUTPUT (rdata[1])
                          |     |  |
       INPUT (addr[2]) ---+     +  +-- SPLITTER --- OUTPUT (rdata[2])
                          |     |  |
                            ...       ...
                          |     |  |
       INPUT (addr[6]) ---+-----+  +-- SPLITTER --- OUTPUT (rdata[6])
                                   |
                                   +-- SPLITTER --- OUTPUT (rdata[7])
                                   |
                                      ...
                                   |
                                   +-- SPLITTER --- OUTPUT (rdata[31])
    */

    NetworkBuilderBase<PlainWorkerInfo> builder;

    // Create inputs.
    std::vector<std::shared_ptr<TaskPlainGateWIRE>> inputs;
    for (int i = 0; i < 7; i++) {
        auto taskINPUT = builder.addINPUT<TaskPlainGateWIRE>(detail::genid(),
                                                             "addr", i, false);
        inputs.push_back(taskINPUT);
    }

    // Create ROM.
    auto taskROM = std::make_shared<TaskPlainROM>();
    builder.addTask(NodeLabel{detail::genid(), "ROM", "body"}, taskROM);
    builder.registerTask("rom", "all", 0, taskROM);

    // Connect inputs and ROM.
    for (auto &&in : inputs)
        builder.connectTasks(in, taskROM);

    // Create splitters and connect ROM to each of them.
    std::vector<std::shared_ptr<TaskPlainSplitter>> taskSplitters;
    for (int i = 0; i < 32; i++) {
        auto taskSplitter = std::make_shared<TaskPlainSplitter>(i);
        builder.addTask(NodeLabel{detail::genid(), "SPLITTER",
                                  utility::fok("ROM[", i, "]")},
                        taskSplitter);
        taskSplitters.push_back(taskSplitter);

        builder.connectTasks(taskROM, taskSplitter);
    }

    // Create outputs and connect corresponding splitter to it.
    for (int i = 0; i < 32; i++) {
        auto taskOUTPUT = builder.addOUTPUT<TaskPlainGateWIRE>(
            detail::genid(), "rdata", i, true);
        auto &&splitter = taskSplitters[i];
        builder.connectTasks(splitter, taskOUTPUT);
    }

    return TaskNetwork<PlainWorkerInfo>(std::move(builder));
}

void doPlain(const Options &opt);
void processAllGates(PlainNetwork &net, int numWorkers,
                     std::shared_ptr<ProgressGraphMaker> graph = nullptr);

#endif
