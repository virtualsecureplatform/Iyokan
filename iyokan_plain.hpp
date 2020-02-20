#ifndef VIRTUALSECUREPLATFORM_IYOKAN_PLAIN_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_PLAIN_HPP

#include "iyokan.hpp"

using TaskPlainGate = Task<uint8_t, uint8_t, uint8_t /*dummy*/>;
using TaskPlainGateMem = TaskMem<uint8_t, uint8_t, uint8_t /* dummy */>;

class TaskPlainGateDFF : public TaskDFF<uint8_t, uint8_t, uint8_t /* dummy */> {
public:
    TaskPlainGateDFF()
    {
        output() = 0;
    }
};

class TaskPlainGateWIRE
    : public TaskMem<uint8_t, uint8_t, uint8_t /* dummy */> {
private:
    void startAsyncImpl(uint8_t) override
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
        : TaskMem<uint8_t, uint8_t, uint8_t /* dummy */>(inputNeeded ? 1 : 0)
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
        void startAsyncImpl(uint8_t) override            \
        {                                                \
            output() = (expr)&1;                         \
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
DEFINE_TASK_PLAIN_GATE(MUX, 3, input(2) == 0 ? input(0) : input(1));
DEFINE_TASK_PLAIN_GATE(NOT, 1, ~input(0));
#undef DEFINE_TASK_PLAIN_GATE

class PlainNetworkBuilder
    : public NetworkBuilder<TaskPlainGate, TaskPlainGateMem, TaskPlainGateDFF,
                            TaskPlainGateWIRE, uint8_t /* dummy */> {
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

class PlainWorker : public Worker<uint8_t /* dummy */> {
private:
    uint8_t getWorkerInfo()
    {
        return 0;  // dummy
    }

public:
    PlainWorker(ReadyQueue<uint8_t> &readyQueue, size_t &numFinishedTargets,
                std::shared_ptr<ProgressGraphMaker> graph)
        : Worker(readyQueue, numFinishedTargets, graph)
    {
    }
};

class TaskPlainROM : public Task<uint8_t, uint32_t, uint8_t /* dummy */> {
private:
    std::vector<uint32_t> data_;

private:
    void startAsyncImpl(uint8_t) override
    {
        size_t addr = 0;
        for (int i = 6; i >= 0; i--)
            addr = (addr << 1) | (input(i) & 1u);
        output() = data_.at(addr);
    }

public:
    TaskPlainROM() : Task<uint8_t, uint32_t, uint8_t>(7), data_(1 << 7)
    {
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

class TaskPlainSplitter : public Task<uint32_t, uint8_t, uint8_t /* dummy */> {
private:
    size_t index_;

private:
    void startAsyncImpl(uint8_t) override
    {
        output() = (input(0) >> index_) & 1u;
    }

public:
    TaskPlainSplitter(size_t index)
        : Task<uint32_t, uint8_t, uint8_t>(1), index_(index)
    {
    }

    bool hasFinished() const override
    {
        return true;
    }
};

class TaskPlainRAM
    : public Task<uint8_t /* only 1 bit used */, uint32_t /* only 8 bit used */,
                  uint8_t /* dummy */> {
public:
    const static size_t ADDRESS_BIT = 8;

private:
    std::vector<uint8_t> data_;

private:
    void startAsyncImpl(uint8_t) override
    {
        size_t addr = 0;
        for (size_t i = 0; i < ADDRESS_BIT; i++)
            addr |= (input(i) & 1u) << i;
        output() = data_.at(addr);

        if (input(ADDRESS_BIT)) {
            uint8_t val = 0;
            for (int i = 0; i < 8; i++)
                val |= (input(ADDRESS_BIT + 1 + i) & 1u) << i;
            data_.at(addr) = val;
        }
    }

public:
    TaskPlainRAM()
        : Task<uint8_t, uint32_t, uint8_t>(ADDRESS_BIT /* addr */ +
                                           1 /* wren */ + 8 /* wdata */),
          data_(1 << ADDRESS_BIT)
    {
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

inline TaskNetwork<uint8_t> makePlainRAMNetwork(const std::string &ramPortName)
{
    NetworkBuilderBase<uint8_t> builder;

    // Create RAM
    auto taskRAM = std::make_shared<TaskPlainRAM>();
    builder.addTask(NodeLabel{detail::genid(), "RAM", "body"}, 0, taskRAM);
    builder.registerTask("ram", ramPortName, 0, taskRAM);

    // Create inputs and outputs, and connect to RAM
    for (size_t i = 0; i < TaskPlainRAM::ADDRESS_BIT; i++) {
        auto taskINPUT = builder.addINPUT<TaskPlainGateWIRE>(detail::genid(), 0,
                                                             "addr", i, false);
        builder.connectTasks(taskINPUT, taskRAM);
    }
    auto taskWriteEnabled = builder.addINPUT<TaskPlainGateWIRE>(
        detail::genid(), 0, "wren", 0, false);
    builder.connectTasks(taskWriteEnabled, taskRAM);
    for (size_t i = 0; i < 8; i++) {
        auto taskINPUT = builder.addINPUT<TaskPlainGateWIRE>(detail::genid(), 0,
                                                             "wdata", i, false);
        builder.connectTasks(taskINPUT, taskRAM);
    }
    for (size_t i = 0; i < 8; i++) {
        auto taskSplitter = std::make_shared<TaskPlainSplitter>(i);
        builder.addTask(
            NodeLabel{detail::genid(), "SPLITTER", detail::fok("RAM[", i, "]")},
            0, taskSplitter);
        builder.connectTasks(taskRAM, taskSplitter);

        auto taskOUTPUT = builder.addOUTPUT<TaskPlainGateWIRE>(
            detail::genid(), 0, "rdata", i, true);
        builder.connectTasks(taskSplitter, taskOUTPUT);
    }

    return TaskNetwork<uint8_t>(std::move(builder));
}

inline TaskNetwork<uint8_t> makePlainROMNetwork()
{
    /*
       INPUT (ROM[0]) ---+-----+  +-- SPLITTER --- OUTPUT (ROM[0])
                         |     |  |
       INPUT (ROM[1]) ---+ ROM +--+-- SPLITTER --- OUTPUT (ROM[1])
                         |     |  |
       INPUT (ROM[2]) ---+     +  +-- SPLITTER --- OUTPUT (ROM[2])
                         |     |  |
                           ...       ...
                         |     |  |
       INPUT (ROM[6]) ---+-----+  +-- SPLITTER --- OUTPUT (ROM[6])
                                  |
                                  +-- SPLITTER --- OUTPUT (ROM[7])
                                  |
                                     ...
                                  |
                                  +-- SPLITTER --- OUTPUT (ROM[31])
    */

    NetworkBuilderBase<uint8_t> builder;

    // Create inputs.
    std::vector<std::shared_ptr<TaskPlainGateWIRE>> inputs;
    for (int i = 0; i < 7; i++) {
        auto taskINPUT = builder.addINPUT<TaskPlainGateWIRE>(detail::genid(), 0,
                                                             "ROM", i, false);
        inputs.push_back(taskINPUT);
    }

    // Create ROM.
    auto taskROM = std::make_shared<TaskPlainROM>();
    builder.addTask(NodeLabel{detail::genid(), "ROM", "body"}, 0, taskROM);
    builder.registerTask("rom", "all", 0, taskROM);

    // Connect inputs and ROM.
    for (auto &&in : inputs)
        builder.connectTasks(in, taskROM);

    // Create splitters and connect ROM to each of them.
    std::vector<std::shared_ptr<TaskPlainSplitter>> taskSplitters;
    for (int i = 0; i < 32; i++) {
        auto taskSplitter = std::make_shared<TaskPlainSplitter>(i);
        builder.addTask(
            NodeLabel{detail::genid(), "SPLITTER", detail::fok("ROM[", i, "]")},
            0, taskSplitter);
        taskSplitters.push_back(taskSplitter);

        builder.connectTasks(taskROM, taskSplitter);
    }

    // Create outputs and connect corresponding splitter to it.
    for (int i = 0; i < 32; i++) {
        auto taskOUTPUT = builder.addOUTPUT<TaskPlainGateWIRE>(
            detail::genid(), 0, "ROM", i, true);
        auto &&splitter = taskSplitters[i];
        builder.connectTasks(splitter, taskOUTPUT);
    }

    return TaskNetwork<uint8_t>(std::move(builder));
}

void doPlain(const Options &opt);
void processAllGates(PlainNetwork &net, int numWorkers,
                     std::shared_ptr<ProgressGraphMaker> graph = nullptr);

#endif
