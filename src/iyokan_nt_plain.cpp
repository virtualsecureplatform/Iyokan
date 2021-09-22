#include "iyokan_nt_plain.hpp"
#include "dataholder_nt.hpp"
#include "iyokan_nt.hpp"
#include "packet_nt.hpp"

template <class T1, class T2>
std::string fok2(T1 t1, T2 t2)
{
    std::stringstream ss;
    ss << t1 << t2;
    return ss.str();
}

namespace nt {
namespace plain {

class WorkerInfo {
};

class Worker : public nt::Worker {
private:
    WorkerInfo wi_;

protected:
    bool canExecute(Task* task) override
    {
        return task->canRunPlain();
    }

    void startTask(Task* task) override
    {
        task->startAsynchronously(wi_);
    }
};

class TaskInput : public TaskCommon<Bit> {
public:
    TaskInput(Label label, Allocator& alc) : TaskCommon<Bit>(label, alc, 0, 1)
    {
    }

    void startAsynchronously(WorkerInfo&) override
    {
        if (getInputSize() == 1)
            output() = input(0);
    }

    bool hasFinished() const override
    {
        return true;
    }

    bool canRunPlain() const override
    {
        return true;
    }

    void setInput(const DataHolder& h) override
    {
        // Set the input i.e., set the output value of this gate
        output() = h.getBit();
    }
};

class TaskOutput : public TaskCommon<Bit> {
public:
    TaskOutput(Label label, Allocator& alc) : TaskCommon<Bit>(label, alc, 1)
    {
    }

    void startAsynchronously(WorkerInfo&) override
    {
        output() = input(0);
    }

    bool hasFinished() const override
    {
        return true;
    }

    bool canRunPlain() const override
    {
        return true;
    }

    void getOutput(DataHolder& h) override
    {
        h.setBit(&output());
    }
};

class TaskDFF : public nt::TaskDFF<Bit> {
public:
    TaskDFF(Label label, Allocator& alc)
        : nt::TaskDFF<Bit>(std::move(label), alc)
    {
    }

    bool canRunPlain() const override
    {
        return true;
    }

    void startAsynchronously(WorkerInfo&) override
    {
        // Nothing to do, because the main process is done in
        // nt::TaskDFF<Bit>::tick().
    }
};

#define DEF_COMMON_TASK_CLASS(CamelName, inputSize, expr)       \
    class Task##CamelName : public TaskCommon<Bit> {            \
    public:                                                     \
        Task##CamelName(Label label, Allocator& alc)            \
            : TaskCommon<Bit>(std::move(label), alc, inputSize) \
        {                                                       \
        }                                                       \
        void startAsynchronously(WorkerInfo&) override          \
        {                                                       \
            output() = (expr);                                  \
        }                                                       \
        bool hasFinished() const override                       \
        {                                                       \
            return true;                                        \
        }                                                       \
        bool canRunPlain() const override                       \
        {                                                       \
            return true;                                        \
        }                                                       \
    };
DEF_COMMON_TASK_CLASS(And, 2, (input(0) & input(1)));
DEF_COMMON_TASK_CLASS(Andnot, 2, (input(0) & ~input(1)));
DEF_COMMON_TASK_CLASS(ConstOne, 0, 1_b);
DEF_COMMON_TASK_CLASS(ConstZero, 0, 0_b);
DEF_COMMON_TASK_CLASS(Mux, 3, input(2) == 0_b ? input(0) : input(1));
DEF_COMMON_TASK_CLASS(Nand, 2, ~(input(0) & input(1)));
DEF_COMMON_TASK_CLASS(Nmux, 3, input(2) == 0_b ? ~input(0) : ~input(1));
DEF_COMMON_TASK_CLASS(Nor, 2, ~(input(0) | input(1)));
DEF_COMMON_TASK_CLASS(Not, 1, ~input(0));
DEF_COMMON_TASK_CLASS(Or, 2, (input(0) | input(1)));
DEF_COMMON_TASK_CLASS(Ornot, 2, (input(0) | ~input(1)));
DEF_COMMON_TASK_CLASS(Xnor, 2, ~(input(0) ^ input(1)));
DEF_COMMON_TASK_CLASS(Xor, 2, (input(0) ^ input(1)));
#undef DEF_COMMON_TASK_CLASS

class NetworkBuilder : public nt::NetworkBuilder {
private:
    std::unordered_map<UID, TaskCommon<Bit>*> uid2common_;
    UID nextUID_;

private:
    UID genUID()
    {
        return nextUID_++;
    }

public:
    NetworkBuilder(Allocator& alc)
        : nt::NetworkBuilder(alc), uid2common_(), nextUID_(0)
    {
    }

    ~NetworkBuilder()
    {
    }

    void connect(UID fromUID, UID toUID) override
    {
        auto &from = uid2common_.at(fromUID), &to = uid2common_.at(toUID);
        to->addInput(from);
    }

#define DEF_COMMON_TASK(CAPName, CamelName)                                \
    UID CAPName(const std::string& alcKey) override                        \
    {                                                                      \
        UID uid = genUID();                                                \
        Task##CamelName* task = nullptr;                                   \
        this->withSubAllocator(alcKey, [&](auto&&) {                       \
            task = emplaceTask<Task##CamelName>(                           \
                Label{uid, #CamelName, std::nullopt}, currentAllocator()); \
        });                                                                \
        uid2common_.emplace(uid, task);                                    \
        return uid;                                                        \
    }
    DEF_COMMON_TASK(AND, And);
    DEF_COMMON_TASK(ANDNOT, Andnot);
    DEF_COMMON_TASK(CONSTONE, ConstOne);
    DEF_COMMON_TASK(CONSTZERO, ConstZero);
    DEF_COMMON_TASK(DFF, DFF);
    DEF_COMMON_TASK(MUX, Mux);
    DEF_COMMON_TASK(NAND, Nand);
    DEF_COMMON_TASK(NMUX, Nmux);
    DEF_COMMON_TASK(NOR, Nor);
    DEF_COMMON_TASK(NOT, Not);
    DEF_COMMON_TASK(OR, Or);
    DEF_COMMON_TASK(ORNOT, Ornot);
    DEF_COMMON_TASK(XNOR, Xnor);
    DEF_COMMON_TASK(XOR, Xor);
#undef DEF_COMMON_TASK

    UID INPUT(const std::string& alcKey, const std::string& nodeName,
              const std::string& portName, int portBit) override
    {
        UID uid = genUID();
        TaskInput* task = nullptr;
        withSubAllocator(alcKey, [&](auto&&) {
            task = emplaceTask<TaskInput>(
                Label{uid, "Input", ConfigName{nodeName, portName, portBit}},
                currentAllocator());
        });
        uid2common_.emplace(uid, task);
        return uid;
    }

    UID OUTPUT(const std::string& alcKey, const std::string& nodeName,
               const std::string& portName, int portBit) override
    {
        UID uid = genUID();
        TaskOutput* task = nullptr;
        withSubAllocator(alcKey, [&](auto&&) {
            task = emplaceTask<TaskOutput>(
                Label{uid, "Output", ConfigName{nodeName, portName, portBit}},
                currentAllocator());
        });
        uid2common_.emplace(uid, task);
        return uid;
    }
};

enum class SCHED {
    TOPO,
    RANKU,
};

struct RunParameter {
    std::string blueprintFile, inputFile, outputFile;
    int numCPUWorkers, numCycles;
    SCHED sched;

    void print() const
    {
        LOG_S(INFO) << "Run parameters";
        LOG_S(INFO) << "\tMode: plain";
        LOG_S(INFO) << "\tBlueprint: " << blueprintFile;
        LOG_S(INFO) << "\t# of CPU Workers: " << numCPUWorkers;
        LOG_S(INFO) << "\t# of cycles: " << numCycles;
        LOG_S(INFO) << "\tInput file (request packet): " << inputFile;
        LOG_S(INFO) << "\tOutput file (result packet): " << outputFile;
        LOG_S(INFO) << "\tSchedule: "
                    << (sched == SCHED::TOPO ? "topo" : "ranku");
    }
};

class Frontend {
private:
    RunParameter pr_;
    std::unique_ptr<NetworkRunner> runner_;
    PlainPacket reqPacket_;
    int currentCycle_;
    nt::Blueprint bp_;

private:
    static void readNetworkFromFile(const blueprint::File& file,
                                    nt::NetworkBuilder& nb);
    static void makeMUXRAM(const blueprint::BuiltinRAM& ram,
                           nt::NetworkBuilder& nb);
    static void makeMUXROM(const blueprint::BuiltinROM& rom,
                           nt::NetworkBuilder& nb);

protected:
    void makeRAM(const blueprint::BuiltinRAM& ram, nt::NetworkBuilder& nb);
    void makeROM(const blueprint::BuiltinROM& rom, nt::NetworkBuilder& nb);
    void make1bitROMWithMUX(const blueprint::BuiltinROM& rom,
                            const std::vector<UID>& addrInputs,
                            nt::NetworkBuilder& nb);

public:
    Frontend(const RunParameter& pr, Allocator& alc);
};

Frontend::Frontend(const RunParameter& pr, Allocator& alc)
    : pr_(pr),
      runner_(nullptr),
      reqPacket_(readPlainPacket(pr_.inputFile)),
      currentCycle_(0),
      bp_(pr_.blueprintFile)
{
    NetworkBuilder nb{alc};

    // [[file]]
    for (auto&& file : bp_.files())
        nb.withSubAllocator(file.name,
                            [&](auto&& nb) { readNetworkFromFile(file, nb); });

    // [[builtin]] type = ram | type = mux-ram
    for (auto&& ram : bp_.builtinRAMs()) {
        nb.withSubAllocator(ram.name, [&](auto&& nb) {
            switch (ram.type) {
            case blueprint::BuiltinRAM::TYPE::CMUX_MEMORY:
                makeRAM(ram, nb);
                break;
            case blueprint::BuiltinRAM::TYPE::MUX:
                makeMUXRAM(ram, nb);
                break;
            }
        });
    }

    // [[builtin]] type = rom | type = mux-rom
    for (auto&& rom : bp_.builtinROMs()) {
        nb.withSubAllocator(rom.name, [&](auto&& nb) {
            switch (rom.type) {
            case blueprint::BuiltinROM::TYPE::CMUX_MEMORY:
                makeROM(rom, nb);
                break;
            case blueprint::BuiltinROM::TYPE::MUX:
                makeMUXROM(rom, nb);
                break;
            }
        });

        auto it = reqPacket_.rom.find(rom.name);
        if (it != reqPacket_.rom.end()) {
            // FIXME: rom init
            assert(false);
        }
    }

    auto get = [&](const blueprint::Port& port) -> Task* {
        Task* task = nb.finder().findByConfigName(
            {port.nodeName, port.portName, port.portBit});
        if (task->label().kind != port.kind)
            ERR_DIE("Invalid port: " << port.nodeName << "/" << port.portName
                                     << "[" << port.portBit << "] is "
                                     << task->label().kind << ", not "
                                     << port.kind);
        return task;
    };

    // [connect]
    // We need to treat "... = @..." and "@... = ..." differently from
    // "..." = ...".
    // First, check if ports that are connected to or from "@..." exist.
    for (auto&& [key, port] : bp_.atPorts()) {
        get(port);  // Only checks if port exists
    }
    // Then, connect other ports. `get` checks if they also exist.
    for (auto&& [src, dst] : bp_.edges()) {
        assert(src.kind == "output");
        assert(dst.kind == "input");
        nb.connect(get(src)->label().uid, get(dst)->label().uid);
    }

    // Set priority to each DepNode
    // FIXME
}

void Frontend::readNetworkFromFile(const blueprint::File& file,
                                   nt::NetworkBuilder& nb)
{
    // FIXME
}

void Frontend::makeMUXRAM(const blueprint::BuiltinRAM& ram,
                          nt::NetworkBuilder& nb)
{
    // FIXME
}

void Frontend::makeMUXROM(const blueprint::BuiltinROM& rom,
                          nt::NetworkBuilder& nb)
{
    // FIXME
}

void Frontend::makeRAM(const blueprint::BuiltinRAM& ram, nt::NetworkBuilder& nb)
{
    // FIXME: relax this constraint
    if (ram.inWdataWidth != ram.outRdataWidth)
        ERR_DIE(
            "Invalid RAM size; RAM that has different sizes of "
            "wdata and rdata is not implemented.");

    // FIXME
}

void Frontend::makeROM(const blueprint::BuiltinROM& rom, nt::NetworkBuilder& nb)
{
    // Create inputs
    std::vector<UID> addrInputs;
    for (size_t i = 0; i < rom.inAddrWidth; i++) {
        UID id = nb.INPUT(fok2("addr", i), rom.name, "addr", i);
        addrInputs.push_back(id);
    }

    // Create 1bit ROMs
    for (size_t i = 0; i < rom.outRdataWidth; i++) {
        nb.withSubAllocator(fok2("rom1bit{}", i), [&](nt::NetworkBuilder& nb) {
            make1bitROMWithMUX(rom, addrInputs, nb);
        });
    }
}

void Frontend::make1bitROMWithMUX(const blueprint::BuiltinROM& rom,
                                  const std::vector<UID>& addrInputs,
                                  nt::NetworkBuilder& nb)
{
    // FIXME
}

/**************************************************/
/***** TEST ***************************************/
/**************************************************/

void test0()
{
    WorkerInfo wi;
    DataHolder dh;
    Bit bit0 = 0_b, bit1 = 1_b;

    {
        Allocator root;
        TaskConstOne t0{Label{1, "", std::nullopt},
                        root.subAllocator("constone")};
        TaskOutput t1{Label{2, "", std::nullopt}, root.subAllocator("out")};
        t1.addInput(&t0);
        t0.startAsynchronously(wi);
        t1.startAsynchronously(wi);
        assert(t0.hasFinished());
        t1.getOutput(dh);
        assert(dh.getBit() == 1_b);
    }

    {
        Allocator root;
        Allocator &sub0 = root.subAllocator("0"),
                  &sub1 = root.subAllocator("1"),
                  &sub2 = root.subAllocator("2"),
                  &sub3 = root.subAllocator("3");
        TaskConstZero t0{Label{0, "", std::nullopt}, sub0};
        TaskConstOne t1{Label{1, "", std::nullopt}, sub1};
        TaskNand t2{Label{2, "", std::nullopt}, sub2};
        TaskOutput t3{Label{3, "", std::nullopt}, sub3};
        t2.addInput(&t0);
        t2.addInput(&t1);
        t3.addInput(&t2);
        t0.startAsynchronously(wi);
        t1.startAsynchronously(wi);
        t2.startAsynchronously(wi);
        t3.startAsynchronously(wi);
        assert(t0.hasFinished() && t1.hasFinished() && t2.hasFinished() &&
               t3.hasFinished());
        t3.getOutput(dh);
        assert(dh.getBit() == 1_b);
    }

    {
        Allocator root;
        NetworkBuilder nb{root};
        UID id0 = nb.INPUT("0", "", "A", 0), id1 = nb.INPUT("1", "", "B", 0),
            id2 = nb.NAND("2"), id3 = nb.OUTPUT("3", "", "C", 0);
        nb.connect(id0, id2);
        nb.connect(id1, id2);
        nb.connect(id2, id3);

        std::vector<std::unique_ptr<nt::Worker>> workers;
        workers.emplace_back(std::make_unique<Worker>());

        NetworkRunner runner{nb.createNetwork(), std::move(workers)};
        Task* t0 = runner.network().finder().findByUID(id0);
        Task* t1 = runner.network().finder().findByUID(id1);
        Task* t3 = runner.network().finder().findByUID(id3);

        t0->setInput(&bit1);
        t1->setInput(&bit1);
        runner.run();
        t3->getOutput(dh);
        assert(dh.getBit() == 0_b);
    }

    {
        /*
                        B               D
           reset(0) >---> ANDNOT(4) >---> DFF(2)
                            ^ A            v Q
                            |              |
                            *--< NOT(3) <--*-----> OUTPUT(1)
                                        A
        */
        Allocator root;
        NetworkBuilder nb{root};
        UID id0 = nb.INPUT("0", "", "reset", 0),
            id1 = nb.OUTPUT("1", "", "out", 0), id2 = nb.DFF("2"),
            id3 = nb.NOT("3"), id4 = nb.ANDNOT("4");
        nb.connect(id2, id1);
        nb.connect(id4, id2);
        nb.connect(id2, id3);
        nb.connect(id3, id4);
        nb.connect(id0, id4);

        std::vector<std::unique_ptr<nt::Worker>> workers;
        workers.emplace_back(std::make_unique<Worker>());

        NetworkRunner runner{nb.createNetwork(), std::move(workers)};
        Task* t0 = runner.network().finder().findByUID(id0);
        Task* t1 = runner.network().finder().findByUID(id1);

        t0->setInput(&bit1);
        runner.run();
        t0->setInput(&bit0);

        runner.tick();
        runner.run();
        t1->getOutput(dh);
        assert(dh.getBit() == 0_b);

        runner.tick();
        runner.run();
        t1->getOutput(dh);
        assert(dh.getBit() == 1_b);
    }
}

}  // namespace plain
}  // namespace nt
