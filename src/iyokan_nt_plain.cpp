#include "iyokan_nt_plain.hpp"
#include "dataholder_nt.hpp"
#include "iyokan_nt.hpp"
#include "packet_nt.hpp"

#include <fstream>

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

// struct InputSource is used by class TaskInput to set correct input value
// every cycle.
struct InputSource {
    int atPortWidth, atPortBit;
    std::vector<Bit>* bits;
};

class TaskInput : public TaskCommon<Bit> {
private:
    std::optional<InputSource> source_;

public:
    TaskInput(Label label, Allocator& alc) : TaskCommon<Bit>(label, alc, 0, 1)
    {
    }
    TaskInput(InputSource source, Label label, Allocator& alc)
        : TaskCommon<Bit>(label, alc, 0, 1), source_(source)
    {
    }

    void onAfterTick(size_t currentCycle) override
    {
        if (source_) {
            // Set the output value from the source
            assert(getInputSize() == 0);
            InputSource& s = source_.value();
            size_t index =
                (s.atPortWidth * currentCycle + s.atPortBit) % s.bits->size();
            output() = s.bits->at(index);
        }
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
private:
    std::optional<Bit> initialValue_;

public:
    TaskDFF(Label label, Allocator& alc)
        : nt::TaskDFF<Bit>(std::move(label), alc), initialValue_(std::nullopt)
    {
    }

    TaskDFF(Bit initialValue, Label label, Allocator& alc)
        : nt::TaskDFF<Bit>(std::move(label), alc), initialValue_(initialValue)
    {
    }

    void onAfterTick(size_t currentCycle) override
    {
        if (currentCycle == 0)
            output() = initialValue_.value_or(output());
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

class TaskROM : public TaskCommon<Bit> {
public:
    TaskROM(Bit value, Label label, Allocator& alc)
        : TaskCommon<Bit>(label, alc, 0)
    {
        output() = value;
    }

    void startAsynchronously(WorkerInfo&) override
    {
    }

    bool hasFinished() const override
    {
        return true;
    }

    bool canRunPlain() const override
    {
        return true;
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
DEF_COMMON_TASK_CLASS(Andnot, 2, (input(0) & !input(1)));
DEF_COMMON_TASK_CLASS(ConstOne, 0, 1_b);
DEF_COMMON_TASK_CLASS(ConstZero, 0, 0_b);
DEF_COMMON_TASK_CLASS(Mux, 3, input(2) == 0_b ? input(0) : input(1));
DEF_COMMON_TASK_CLASS(Nand, 2, !(input(0) & input(1)));
DEF_COMMON_TASK_CLASS(Nmux, 3, input(2) == 0_b ? !input(0) : !input(1));
DEF_COMMON_TASK_CLASS(Nor, 2, !(input(0) | input(1)));
DEF_COMMON_TASK_CLASS(Not, 1, !input(0));
DEF_COMMON_TASK_CLASS(Or, 2, (input(0) | input(1)));
DEF_COMMON_TASK_CLASS(Ornot, 2, (input(0) | !input(1)));
DEF_COMMON_TASK_CLASS(Xnor, 2, !(input(0) ^ input(1)));
DEF_COMMON_TASK_CLASS(Xor, 2, (input(0) ^ input(1)));
#undef DEF_COMMON_TASK_CLASS

class NetworkBuilder : public nt::NetworkBuilder {
private:
    std::unordered_map<UID, TaskCommon<Bit>*> uid2common_;
    UID nextUID_;
    const PlainPacket* const reqPacket_;
    const std::map<ConfigName, InputSource>* const cname2source_;

private:
    UID genUID()
    {
        return nextUID_++;
    }

public:
    NetworkBuilder(const std::map<ConfigName, InputSource>& cname2source,
                   const PlainPacket& reqPacket, Allocator& alc)
        : nt::NetworkBuilder(alc),
          uid2common_(),
          nextUID_(0),
          reqPacket_(&reqPacket),
          cname2source_(&cname2source)
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

#define DEF_COMMON_TASK(CAPName, CamelName)                            \
    UID CAPName() override                                             \
    {                                                                  \
        UID uid = genUID();                                            \
        Task##CamelName* task = nullptr;                               \
        task = emplaceTask<Task##CamelName>(                           \
            Label{uid, #CamelName, std::nullopt}, currentAllocator()); \
        uid2common_.emplace(uid, task);                                \
        return uid;                                                    \
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

    UID SDFF0() override
    {
        UID uid = genUID();
        TaskDFF* task = emplaceTask<TaskDFF>(
            0_b, Label{uid, "SDFF0", std::nullopt}, currentAllocator());
        uid2common_.emplace(uid, task);
        return uid;
    }

    UID SDFF1() override
    {
        UID uid = genUID();
        TaskDFF* task = emplaceTask<TaskDFF>(
            1_b, Label{uid, "SDFF1", std::nullopt}, currentAllocator());
        uid2common_.emplace(uid, task);
        return uid;
    }

    UID INPUT(const std::string& nodeName, const std::string& portName,
              int portBit) override
    {
        Allocator& alc = currentAllocator();
        UID uid = genUID();
        ConfigName cname = ConfigName{nodeName, portName, portBit};
        Label label{uid, Label::INPUT, cname};
        TaskInput* task = nullptr;
        if (auto it = cname2source_->find(cname); it != cname2source_->end())
            task = emplaceTask<TaskInput>(it->second, label, alc);
        else
            task = emplaceTask<TaskInput>(label, alc);
        uid2common_.emplace(uid, task);
        return uid;
    }

    UID OUTPUT(const std::string& nodeName, const std::string& portName,
               int portBit) override
    {
        UID uid = genUID();
        TaskOutput* task = nullptr;
        task = emplaceTask<TaskOutput>(
            Label{uid, Label::OUTPUT, ConfigName{nodeName, portName, portBit}},
            currentAllocator());
        uid2common_.emplace(uid, task);
        return uid;
    }

    UID ROM(const std::string& nodeName, const std::string& portName,
            int portBit) override
    {
        assert(reqPacket_ != nullptr);
        assert(portName == "romdata");

        UID uid = genUID();
        TaskROM* task = emplaceTask<TaskROM>(
            reqPacket_->rom.at(nodeName).at(portBit),
            Label{uid, "ROM", ConfigName{nodeName, portName, portBit}},
            currentAllocator());
        uid2common_.emplace(uid, task);
        return uid;
    }

    UID RAM(const std::string& nodeName, const std::string& portName,
            int portBit) override
    {
        assert(reqPacket_ != nullptr);
        assert(portName == "ramdata");

        UID uid = genUID();
        TaskDFF* task = emplaceTask<TaskDFF>(
            reqPacket_->ram.at(nodeName).at(portBit),
            Label{uid, "RAM", ConfigName{nodeName, portName, portBit}},
            currentAllocator());
        uid2common_.emplace(uid, task);

        // FIXME: We need to memorize this task to make a response packet.

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
    std::optional<Network> network_;
    PlainPacket reqPacket_;
    int currentCycle_;
    nt::Blueprint bp_;

public:
    Frontend(const RunParameter& pr, Allocator& alc);
    void run();
};

Frontend::Frontend(const RunParameter& pr, Allocator& alc)
    : pr_(pr),
      network_(std::nullopt),
      reqPacket_(readPlainPacket(pr_.inputFile)),
      currentCycle_(0),
      bp_(pr_.blueprintFile)
{
    // Create map from ConfigName to InputSource
    std::map<ConfigName, InputSource> cname2source;
    for (auto&& [key, port] : bp_.atPorts()) {
        if (port.kind != Label::INPUT)
            continue;
        auto& [atPortName, atPortBit] = key;
        auto it = reqPacket_.bits.find(atPortName);
        if (it == reqPacket_.bits.end())
            continue;
        if (atPortName == "reset")
            ERR_DIE("@reset cannot be set by user's input");
        cname2source.emplace(port.cname,
                             InputSource{bp_.atPortWidths().at(atPortName),
                                         atPortBit, &it->second});
    }

    NetworkBuilder nb{cname2source, reqPacket_, alc};

    // [[file]]
    for (auto&& file : bp_.files())
        readNetworkFromFile(file, nb);

    // [[builtin]] type = ram | type = mux-ram
    for (auto&& ram : bp_.builtinRAMs()) {
        // We ignore ram.type and always use mux-ram in plaintext mode.
        makeMUXRAM(ram, nb);
    }

    // [[builtin]] type = rom | type = mux-rom
    for (auto&& rom : bp_.builtinROMs()) {
        // We ignore rom.type and always use mux-rom in plaintext mode.
        makeMUXROM(rom, nb);
    }

    auto get = [&](const blueprint::Port& port) -> Task* {
        Task* task = nb.finder().findByConfigName(port.cname);
        if (task->label().kind != port.kind)
            ERR_DIE("Invalid port: " << port.cname << " is "
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
        assert(src.kind == Label::OUTPUT);
        assert(dst.kind == Label::INPUT);
        nb.connect(get(src)->label().uid, get(dst)->label().uid);
    }

    // Set priority to each DepNode
    // FIXME

    network_.emplace(nb.createNetwork());
    // FIXME check if network is valid
}

void Frontend::run()
{
    const Bit bit0 = 0_b, bit1 = 1_b;

    // Create workers
    std::vector<std::unique_ptr<nt::Worker>> workers;
    for (size_t i = 0; i < pr_.numCPUWorkers; i++)
        workers.emplace_back(std::make_unique<Worker>());

    // Create runner and finder for the network
    NetworkRunner runner{std::move(network_.value()), std::move(workers)};
    network_ = std::nullopt;
    const TaskFinder& finder = runner.network().finder();

    // Process reset cycle if @reset is used
    // FIXME: Add support for --skip-reset flag
    if (auto reset = bp_.at("reset"); reset && reset->kind == Label::INPUT) {
        Task* t = finder.findByConfigName(reset->cname);
        t->setInput(&bit1);  // Set reset on
        runner.run();
        t->setInput(&bit0);  // Set reset off
    }

    // Process normal cycles
    for (size_t i = 0; i < pr_.numCycles; i++) {
        runner.tick();
        runner.onAfterTick(i);
        runner.run();
    }

    // FIXME output result packet
}

/**************************************************/
/***** TEST ***************************************/
/**************************************************/

void test0()
{
    WorkerInfo wi;
    DataHolder dh;
    Bit bit0 = 0_b, bit1 = 1_b;
    PlainPacket pkt;
    std::map<ConfigName, InputSource> c2s;

    {
        Allocator alc;
        TaskConstOne t0{Label{1, "", std::nullopt}, alc};
        TaskOutput t1{Label{2, "", std::nullopt}, alc};
        t1.addInput(&t0);
        t0.startAsynchronously(wi);
        t1.startAsynchronously(wi);
        assert(t0.hasFinished());
        t1.getOutput(dh);
        assert(dh.getBit() == 1_b);
    }

    {
        Allocator alc;
        TaskConstZero t0{Label{0, "", std::nullopt}, alc};
        TaskConstOne t1{Label{1, "", std::nullopt}, alc};
        TaskNand t2{Label{2, "", std::nullopt}, alc};
        TaskOutput t3{Label{3, "", std::nullopt}, alc};
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
        Allocator alc;
        NetworkBuilder nb{c2s, pkt, alc};
        UID id0 = nb.INPUT("", "A", 0), id1 = nb.INPUT("", "B", 0),
            id2 = nb.NAND(), id3 = nb.OUTPUT("", "C", 0);
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
        Allocator alc;
        NetworkBuilder nb{c2s, pkt, alc};
        UID id0 = nb.INPUT("", "reset", 0), id1 = nb.OUTPUT("", "out", 0),
            id2 = nb.DFF(), id3 = nb.NOT(), id4 = nb.ANDNOT();
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

    {
        Allocator alc;
        NetworkBuilder nb{c2s, pkt, alc};

        readNetworkFromFile(
            blueprint::File{blueprint::File::TYPE::YOSYS_JSON,
                            "test/yosys-json/addr-4bit-yosys.json", "addr"},
            nb);

        std::vector<std::unique_ptr<nt::Worker>> workers;
        workers.emplace_back(std::make_unique<Worker>());

        NetworkRunner runner{nb.createNetwork(), std::move(workers)};
        auto&& finder = runner.network().finder();
        Task *tA0 = finder.findByConfigName({"addr", "io_inA", 0}),
             *tA1 = finder.findByConfigName({"addr", "io_inA", 1}),
             *tA2 = finder.findByConfigName({"addr", "io_inA", 2}),
             *tA3 = finder.findByConfigName({"addr", "io_inA", 3});
        Task *tB0 = finder.findByConfigName({"addr", "io_inB", 0}),
             *tB1 = finder.findByConfigName({"addr", "io_inB", 1}),
             *tB2 = finder.findByConfigName({"addr", "io_inB", 2}),
             *tB3 = finder.findByConfigName({"addr", "io_inB", 3});
        Task *tO0 = finder.findByConfigName({"addr", "io_out", 0}),
             *tO1 = finder.findByConfigName({"addr", "io_out", 1}),
             *tO2 = finder.findByConfigName({"addr", "io_out", 2}),
             *tO3 = finder.findByConfigName({"addr", "io_out", 3});

        tA0->setInput(&bit1);
        tA1->setInput(&bit0);
        tA2->setInput(&bit1);
        tA3->setInput(&bit0);
        tB0->setInput(&bit0);
        tB1->setInput(&bit1);
        tB2->setInput(&bit0);
        tB3->setInput(&bit1);

        runner.run();

        tO0->getOutput(dh);
        assert(dh.getBit() == 1_b);
        tO1->getOutput(dh);
        assert(dh.getBit() == 1_b);
        tO2->getOutput(dh);
        assert(dh.getBit() == 1_b);
        tO3->getOutput(dh);
        assert(dh.getBit() == 1_b);
    }

    {
        Allocator alc;
        NetworkBuilder nb{c2s, pkt, alc};

        readNetworkFromFile(
            blueprint::File{blueprint::File::TYPE::IYOKANL1_JSON,
                            "test/iyokanl1-json/addr-4bit-iyokanl1.json",
                            "addr"},
            nb);

        std::vector<std::unique_ptr<nt::Worker>> workers;
        workers.emplace_back(std::make_unique<Worker>());

        NetworkRunner runner{nb.createNetwork(), std::move(workers)};
        auto&& finder = runner.network().finder();
        Task *tA0 = finder.findByConfigName({"addr", "io_inA", 0}),
             *tA1 = finder.findByConfigName({"addr", "io_inA", 1}),
             *tA2 = finder.findByConfigName({"addr", "io_inA", 2}),
             *tA3 = finder.findByConfigName({"addr", "io_inA", 3});
        Task *tB0 = finder.findByConfigName({"addr", "io_inB", 0}),
             *tB1 = finder.findByConfigName({"addr", "io_inB", 1}),
             *tB2 = finder.findByConfigName({"addr", "io_inB", 2}),
             *tB3 = finder.findByConfigName({"addr", "io_inB", 3});
        Task *tO0 = finder.findByConfigName({"addr", "io_out", 0}),
             *tO1 = finder.findByConfigName({"addr", "io_out", 1}),
             *tO2 = finder.findByConfigName({"addr", "io_out", 2}),
             *tO3 = finder.findByConfigName({"addr", "io_out", 3});

        tA0->setInput(&bit1);
        tA1->setInput(&bit0);
        tA2->setInput(&bit0);
        tA3->setInput(&bit0);
        tB0->setInput(&bit0);
        tB1->setInput(&bit1);
        tB2->setInput(&bit0);
        tB3->setInput(&bit1);

        runner.run();

        tO0->getOutput(dh);
        assert(dh.getBit() == 1_b);
        tO1->getOutput(dh);
        assert(dh.getBit() == 1_b);
        tO2->getOutput(dh);
        assert(dh.getBit() == 0_b);
        tO3->getOutput(dh);
        assert(dh.getBit() == 1_b);
    }

    {
        Allocator alc;
        NetworkBuilder nb{c2s, pkt, alc};

        readNetworkFromFile(
            blueprint::File{blueprint::File::TYPE::YOSYS_JSON,
                            "test/yosys-json/counter-4bit-yosys.json",
                            "counter"},
            nb);

        std::vector<std::unique_ptr<nt::Worker>> workers;
        workers.emplace_back(std::make_unique<Worker>());

        NetworkRunner runner{nb.createNetwork(), std::move(workers)};
        auto&& finder = runner.network().finder();
        Task *tRst = finder.findByConfigName({"counter", "reset", 0}),
             *tOut0 = finder.findByConfigName({"counter", "io_out", 0}),
             *tOut1 = finder.findByConfigName({"counter", "io_out", 1}),
             *tOut2 = finder.findByConfigName({"counter", "io_out", 2}),
             *tOut3 = finder.findByConfigName({"counter", "io_out", 3});

        tRst->setInput(&bit1);
        runner.run();
        tRst->setInput(&bit0);

        // Cycle #1
        runner.tick();
        runner.run();
        // Cycle #2
        runner.tick();
        runner.run();
        // Cycle #3
        runner.tick();
        runner.run();

        // The output is 2, that is, '0b0010'
        tOut0->getOutput(dh);
        assert(dh.getBit() == 0_b);
        tOut1->getOutput(dh);
        assert(dh.getBit() == 1_b);
        tOut2->getOutput(dh);
        assert(dh.getBit() == 0_b);
        tOut3->getOutput(dh);
        assert(dh.getBit() == 0_b);
    }

    {
        Allocator alc;
        NetworkBuilder nb{c2s, pkt, alc};

        readNetworkFromFile(
            blueprint::File{blueprint::File::TYPE::YOSYS_JSON,
                            "test/yosys-json/register-init-4bit-yosys.json",
                            "register_init"},
            nb);

        std::vector<std::unique_ptr<nt::Worker>> workers;
        workers.emplace_back(std::make_unique<Worker>());

        NetworkRunner runner{nb.createNetwork(), std::move(workers)};
        auto&& finder = runner.network().finder();
        Task *tIn0 = finder.findByConfigName({"register_init", "io_in", 0}),
             *tIn1 = finder.findByConfigName({"register_init", "io_in", 1}),
             *tIn2 = finder.findByConfigName({"register_init", "io_in", 2}),
             *tIn3 = finder.findByConfigName({"register_init", "io_in", 3}),
             *tOut0 = finder.findByConfigName({"register_init", "io_out", 0}),
             *tOut1 = finder.findByConfigName({"register_init", "io_out", 1}),
             *tOut2 = finder.findByConfigName({"register_init", "io_out", 2}),
             *tOut3 = finder.findByConfigName({"register_init", "io_out", 3});

        // Set 0xc to input
        tIn0->setInput(&bit0);
        tIn1->setInput(&bit0);
        tIn2->setInput(&bit1);
        tIn3->setInput(&bit1);

        // Skip the reset cycle (assume --skip-reset flag).

        // Cycle #1
        runner.tick();
        runner.onAfterTick(0);
        runner.run();

        // The output is 9, that is, '0b1001'
        tOut0->getOutput(dh);
        assert(dh.getBit() == 1_b);
        tOut1->getOutput(dh);
        assert(dh.getBit() == 0_b);
        tOut2->getOutput(dh);
        assert(dh.getBit() == 0_b);
        tOut3->getOutput(dh);
        assert(dh.getBit() == 1_b);

        // Cycle #2
        runner.tick();
        runner.run();

        // The output is 12, that is, '0b1100'
        tOut0->getOutput(dh);
        assert(dh.getBit() == 0_b);
        tOut1->getOutput(dh);
        assert(dh.getBit() == 0_b);
        tOut2->getOutput(dh);
        assert(dh.getBit() == 1_b);
        tOut3->getOutput(dh);
        assert(dh.getBit() == 1_b);
    }

    {
        PlainPacket pkt;
        pkt.rom["rom"] = {0_b, 1_b, 0_b, 0_b, 1_b, 0_b, 1_b, 1_b};

        Allocator alc;
        NetworkBuilder nb{c2s, pkt, alc};
        makeMUXROM(blueprint::BuiltinROM{blueprint::BuiltinROM::TYPE::MUX,
                                         "rom", 2, 2},
                   nb);

        std::vector<std::unique_ptr<nt::Worker>> workers;
        workers.emplace_back(std::make_unique<Worker>());

        NetworkRunner runner{nb.createNetwork(), std::move(workers)};
        auto&& finder = runner.network().finder();
        Task *tAddr0 = finder.findByConfigName({"rom", "addr", 0}),
             *tAddr1 = finder.findByConfigName({"rom", "addr", 1}),
             *tRdata0 = finder.findByConfigName({"rom", "rdata", 0}),
             *tRdata1 = finder.findByConfigName({"rom", "rdata", 1});

        tAddr0->setInput(&bit0);
        tAddr1->setInput(&bit1);
        runner.run();

        tRdata0->getOutput(dh);
        assert(dh.getBit() == 1_b);
        tRdata1->getOutput(dh);
        assert(dh.getBit() == 0_b);
    }

    {
        PlainPacket pkt;
        pkt.ram["ram"] = {0_b, 1_b, 0_b, 0_b, 1_b, 0_b, 1_b, 1_b};

        Allocator alc;
        NetworkBuilder nb{c2s, pkt, alc};
        makeMUXRAM(blueprint::BuiltinRAM{blueprint::BuiltinRAM::TYPE::MUX,
                                         "ram", 2, 2, 2},
                   nb);

        std::vector<std::unique_ptr<nt::Worker>> workers;
        workers.emplace_back(std::make_unique<Worker>());

        NetworkRunner runner{nb.createNetwork(), std::move(workers)};
        auto&& finder = runner.network().finder();
        Task *tAddr0 = finder.findByConfigName({"ram", "addr", 0}),
             *tAddr1 = finder.findByConfigName({"ram", "addr", 1}),
             *tWren = finder.findByConfigName({"ram", "wren", 0}),
             *tRdata0 = finder.findByConfigName({"ram", "rdata", 0}),
             *tRdata1 = finder.findByConfigName({"ram", "rdata", 1}),
             *tWdata0 = finder.findByConfigName({"ram", "wdata", 0}),
             *tWdata1 = finder.findByConfigName({"ram", "wdata", 1});

        // Reset cycle
        runner.run();

        // Cycle #1
        runner.tick();
        runner.onAfterTick(0);
        tAddr0->setInput(&bit1);
        tAddr1->setInput(&bit0);
        tWren->setInput(&bit0);
        tWdata0->setInput(&bit1);
        tWdata1->setInput(&bit1);
        runner.run();

        tRdata0->getOutput(dh);
        assert(dh.getBit() == 0_b);
        tRdata1->getOutput(dh);
        assert(dh.getBit() == 0_b);

        // Cycle #2
        runner.tick();
        tWren->setInput(&bit1);
        runner.run();

        // Cycle #3
        runner.tick();
        runner.run();

        tRdata0->getOutput(dh);
        assert(dh.getBit() == 1_b);
        tRdata1->getOutput(dh);
        assert(dh.getBit() == 1_b);
    }

    //{
    //    // Prepare the input packet
    //    PlainPacket inPkt{
    //        {},  // ram
    //        {},  // rom
    //        {    // bits
    //         {"A", /* 0xc */ {0_b, 0_b, 1_b, 1_b}},
    //         {"B", /* 0xa */ {0_b, 1_b, 0_b, 1_b}}},
    //        std::nullopt,  // numCycles
    //    };
    //    writePlainPacket("_test_in", inPkt);

    //    // Prepare the expected output packet
    //    PlainPacket expectedOutPkt{
    //        {},                                       // ram
    //        {},                                       // rom
    //        {{"out", /* 6 */ {0_b, 1_b, 1_b, 0_b}}},  // bits
    //        1,                                        // numCycles
    //    };

    //    Allocator alc;
    //    Frontend frontend{
    //        RunParameter{
    //            "test/config-toml/addr-4bit.toml",  // blueprintFile
    //            "_test_in",                         // inputFile
    //            "_test_out",                        // outputFile
    //            2,                                  // numCPUWorkers
    //            1,                                  // numCycles
    //            SCHED::RANKU,                       // sched
    //        },
    //        alc};
    //    frontend.run();
    //    PlainPacket got = readPlainPacket("_test_out");
    //    assert(got == expectedOutPkt);
    //}
}

}  // namespace plain
}  // namespace nt
