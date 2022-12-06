#include "iyokan_nt_tfhepp.hpp"
#include "blueprint.hpp"
#include "dataholder_nt.hpp"
#include "iyokan_nt.hpp"
#include "packet_nt.hpp"

#include <ThreadPool.h>

namespace {

/* class Thread */

class Thread {
private:
    // The C++17 keyword 'inline' is necessary here to avoid duplicate
    // definition of 'pool_'. Thanks to:
    // https://stackoverflow.com/questions/11709859/how-to-have-static-data-members-in-a-header-only-library
    static inline std::shared_ptr<ThreadPool> pool_;
    std::atomic_bool finished_;

public:
    Thread();

    bool hasFinished() const;
    static void setNumThreads(int newNumThreads);

    template <class Func>
    Thread& operator=(Func func)
    {
        finished_ = false;
        pool_->enqueue([this, func]() {
            func();
            finished_ = true;
        });
        return *this;
    }
};

Thread::Thread() : finished_(false)
{
    if (!pool_) {
        const int DEFAULT_NUM_THREADS = 10;
        pool_ = std::make_shared<ThreadPool>(DEFAULT_NUM_THREADS);
    }
}

bool Thread::hasFinished() const
{
    return finished_;
}

void Thread::setNumThreads(int newNumThreads)
{
    pool_ = std::make_shared<ThreadPool>(newNumThreads);
}

}  // namespace

namespace nt::tfhepp {

struct WorkerInfo {
    TFHEppBKey bkey;

    WorkerInfo(TFHEppBKey bkey) : bkey(std::move(bkey))
    {
    }
};

class Worker : public nt::Worker {
private:
    WorkerInfo wi_;

protected:
    bool canExecute(Task* task) override
    {
        return task->canRunTFHEpp();
    }

    void startTask(Task* task) override
    {
        task->startAsynchronously(wi_);
    }

public:
    Worker(WorkerInfo wi) : wi_(std::move(wi))
    {
    }
};

// struct InputSource is used by class TaskInput to set correct input value
// every cycle.
struct InputSource {
    int atPortWidth, atPortBit;
    std::vector<TLWELvl0>* bits;
};

class TaskInput : public TaskCommon<TLWELvl0> {
private:
    std::optional<InputSource> source_;

public:
    TaskInput(Label label, Allocator& alc)
        : TaskCommon<TLWELvl0>(label, alc, 0, 1), source_(std::nullopt)
    {
    }
    TaskInput(InputSource source, Label label, Allocator& alc)
        : TaskCommon<TLWELvl0>(label, alc, 0, 1), source_(source)
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

    bool canRunTFHEpp() const override
    {
        return true;
    }

    void setInput(const DataHolder& h) override
    {
        // Set the input i.e., set the output value of this gate
        h.getTLWELvl0(output());
    }
};

class TaskOutput : public TaskCommon<TLWELvl0> {
public:
    TaskOutput(Label label, Allocator& alc)
        : TaskCommon<TLWELvl0>(label, alc, 1)
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

    bool canRunTFHEpp() const override
    {
        return true;
    }

    void getOutput(DataHolder& h) override
    {
        h.setTLWELvl0(&output());
    }
};

class TaskDFF : public nt::TaskDFF<TLWELvl0> {
private:
    std::optional<TLWELvl0> initialValue_;

public:
    TaskDFF(Label label, Allocator& alc)
        : nt::TaskDFF<TLWELvl0>(std::move(label), alc),
          initialValue_(std::nullopt)
    {
    }

    TaskDFF(const TLWELvl0& initialValue, Label label, Allocator& alc)
        : nt::TaskDFF<TLWELvl0>(std::move(label), alc),
          initialValue_(initialValue)
    {
    }

    void onAfterTick(size_t currentCycle) override
    {
        if (currentCycle == 0 && initialValue_)
            output() = initialValue_.value();
    }

    bool canRunTFHEpp() const override
    {
        return true;
    }

    void startAsynchronously(WorkerInfo&) override
    {
        // Nothing to do, because the main process is done in
        // nt::TaskDFF<TLWELvl0>::tick().
    }

    void getOutput(DataHolder& h) override
    {
        h.setTLWELvl0(&output());
    }
};

class TaskROM : public TaskCommon<TLWELvl0> {
public:
    TaskROM(const TLWELvl0& value, Label label, Allocator& alc)
        : TaskCommon<TLWELvl0>(label, alc, 0)
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

    bool canRunTFHEpp() const override
    {
        return true;
    }
};

#define DEF_TASK(CamelName, inputSize, compCost, expr)               \
    class Task##CamelName : public TaskCommon<TLWELvl0> {            \
    private:                                                         \
        Thread thr_;                                                 \
                                                                     \
    public:                                                          \
        Task##CamelName(Label label, Allocator& alc)                 \
            : TaskCommon<TLWELvl0>(std::move(label), alc, inputSize) \
        {                                                            \
        }                                                            \
        void startAsynchronously(WorkerInfo& wi) override            \
        {                                                            \
            thr_ = [&]() {                                           \
                [[maybe_unused]] const GateKeyFFT& gk = *wi.bkey.gk; \
                (expr);                                              \
            };                                                       \
        }                                                            \
        bool hasFinished() const override                            \
        {                                                            \
            return thr_.hasFinished();                               \
        }                                                            \
        bool canRunTFHEpp() const override                           \
        {                                                            \
            return true;                                             \
        }                                                            \
        int getComputationCost() const override                      \
        {                                                            \
            return compCost;                                         \
        }                                                            \
    };
DEF_TASK(And, 2, 10, TFHEpp::HomAND(output(), input(0), input(1), gk));
DEF_TASK(Andnot, 2, 10, TFHEpp::HomANDYN(output(), input(0), input(1), gk));
DEF_TASK(ConstOne, 0, 0, TFHEpp::HomCONSTANTONE(output()));
DEF_TASK(ConstZero, 0, 0, TFHEpp::HomCONSTANTZERO(output()));
DEF_TASK(Mux, 3, 20,
         TFHEpp::HomMUX(output(), input(2), input(1), input(0), gk));
DEF_TASK(Nand, 2, 10, TFHEpp::HomNAND(output(), input(0), input(1), gk));
DEF_TASK(Nmux, 3, 20,
         TFHEpp::HomNMUX(output(), input(2), input(1), input(0), gk));
DEF_TASK(Nor, 2, 10, TFHEpp::HomNOR(output(), input(0), input(1), gk));
DEF_TASK(Not, 1, 0, TFHEpp::HomNOT(output(), input(0)));
DEF_TASK(Or, 2, 10, TFHEpp::HomOR(output(), input(0), input(1), gk));
DEF_TASK(Ornot, 2, 10, TFHEpp::HomORYN(output(), input(0), input(1), gk));
DEF_TASK(Xnor, 2, 10, TFHEpp::HomXNOR(output(), input(0), input(1), gk));
DEF_TASK(Xor, 2, 10, TFHEpp::HomXOR(output(), input(0), input(1), gk));
#undef DEF_TASK

class NetworkBuilder : public nt::NetworkBuilder {
private:
    std::unordered_map<UID, TaskCommon<TLWELvl0>*> uid2common_;
    UID nextUID_;
    const TFHEPacket* const reqPacket_;
    const std::map<ConfigName, InputSource>* const cname2isource_;

private:
    UID genUID()
    {
        return nextUID_++;
    }

public:
    NetworkBuilder(const std::map<ConfigName, InputSource>& cname2isource,
                   const TFHEPacket& reqPacket, Allocator& alc)
        : nt::NetworkBuilder(alc),
          uid2common_(),
          nextUID_(0),
          reqPacket_(&reqPacket),
          cname2isource_(&cname2isource)
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

#define DEF_COMMON_TASK(CAPName, CamelName)                          \
    UID CAPName() override                                           \
    {                                                                \
        UID uid = genUID();                                          \
        Task##CamelName* task = nullptr;                             \
        task = emplaceTask<Task##CamelName>(                         \
            Label{uid, #CAPName, std::nullopt}, currentAllocator()); \
        uid2common_.emplace(uid, task);                              \
        return uid;                                                  \
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
        TLWELvl0 tlwe;
        TFHEpp::HomCONSTANTZERO(tlwe);
        UID uid = genUID();
        TaskDFF* task = emplaceTask<TaskDFF>(
            tlwe, Label{uid, "SDFF0", std::nullopt}, currentAllocator());
        uid2common_.emplace(uid, task);
        return uid;
    }

    UID SDFF1() override
    {
        TLWELvl0 tlwe;
        TFHEpp::HomCONSTANTONE(tlwe);
        UID uid = genUID();
        TaskDFF* task = emplaceTask<TaskDFF>(
            tlwe, Label{uid, "SDFF1", std::nullopt}, currentAllocator());
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
        if (auto it = cname2isource_->find(cname); it != cname2isource_->end())
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
            reqPacket_->romInTLWE.at(nodeName).at(portBit),
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
            reqPacket_->ramInTLWE.at(nodeName).at(portBit),
            Label{uid, "RAM", ConfigName{nodeName, portName, portBit}},
            currentAllocator());
        uid2common_.emplace(uid, task);

        return uid;
    }
};

class Frontend : public nt::Frontend {
private:
    TFHEPacket reqPacket_;
    TFHEppBKey bkey_;

private:
    void setBit0(DataHolder& dh) override;
    void setBit1(DataHolder& dh) override;
    void dumpResPacket(const std::string& outpath, const TaskFinder& finder,
                       int numCycles) override;
    std::vector<std::unique_ptr<nt::Worker>> makeWorkers() override;

    // Actual constructor.
    void doConstruct();

public:
    Frontend(const RunParameter& pr);
    Frontend(const Snapshot& ss);
};

Frontend::Frontend(const RunParameter& pr)
    : nt::Frontend(pr), reqPacket_(readTFHEPacket(runParam().inputFile))
{
    doConstruct();
}

Frontend::Frontend(const Snapshot& ss)
    : nt::Frontend(ss), reqPacket_(readTFHEPacket(runParam().inputFile))
{
    doConstruct();
}

void Frontend::doConstruct()
{
    const Blueprint& bp = blueprint();

    // Create map from ConfigName to InputSource
    std::map<ConfigName, InputSource> cname2isource;
    for (auto&& [key, port] : bp.atPorts()) {
        // Find only inputs, that is, "[connect] ... = @..."
        if (port.kind != Label::INPUT)
            continue;

        // Get "@atPortName[atPortBit]"
        auto& [atPortName, atPortBit] = key;

        // Check if reqPacket_ contains input data for @atPortName
        auto it = reqPacket_.bits.find(atPortName);
        if (it == reqPacket_.bits.end())
            continue;

        // Die if users try to set the value of @reset[0] since it is set only
        // by system
        if (atPortName == "reset")
            ERR_DIE("@reset cannot be set by user's input");

        // Add a new entry to cname2isource
        InputSource s{bp.atPortWidths().at(atPortName), atPortBit, &it->second};
        cname2isource.emplace(port.cname, s);
    }

    // Build the network. The instance is in nt::Frontend
    NetworkBuilder nb{cname2isource, reqPacket_, allocator()};
    buildNetwork(nb);

    // Read bkey
    readTFHEppBKey(bkey_, runParam().bkeyFile);
}

void Frontend::setBit0(DataHolder& dh)
{
    static TLWELvl0 tlwe;
    TFHEpp::HomCONSTANTZERO(tlwe);
    dh.setTLWELvl0(&tlwe);
}

void Frontend::setBit1(DataHolder& dh)
{
    static TLWELvl0 tlwe;
    TFHEpp::HomCONSTANTONE(tlwe);
    dh.setTLWELvl0(&tlwe);
}

void Frontend::dumpResPacket(const std::string& outpath,
                             const TaskFinder& finder, int numCycles)
{
    DataHolder dh;
    TFHEPacket out;
    const Blueprint& bp = blueprint();

    // Set the current number of cycles
    out.numCycles = numCycles;

    // Get values of output @port
    out.bits.clear();
    for (auto&& [key, port] : bp.atPorts()) {
        // Find "[connect] @atPortName[atPortBit] = ..."
        if (port.kind != Label::OUTPUT)
            continue;
        auto& [atPortName, atPortBit] = key;

        // Get the value
        Task* t = finder.findByConfigName(port.cname);
        t->getOutput(dh);

        // Assign the value to the corresponding bit of the response packet
        auto& bits = out.bits[atPortName];
        if (bits.size() < atPortBit + 1)
            bits.resize(atPortBit + 1);
        dh.getTLWELvl0(bits.at(atPortBit));
    }

    // Get values of RAM
    for (auto&& ram : bp.builtinRAMs()) {
        std::vector<TLWELvl0>& dst = out.ramInTLWE[ram.name];
        size_t size = (1 << ram.inAddrWidth) * ram.outRdataWidth;
        dst.resize(size);
        for (size_t i = 0; i < size; i++) {
            ConfigName cname{ram.name, "ramdata", static_cast<int>(i)};
            Task* t = finder.findByConfigName(cname);
            t->getOutput(dh);
            dh.getTLWELvl0(dst.at(i));
        }
    }

    // Dump the result packet
    writeTFHEPacket(outpath, out);
}

std::vector<std::unique_ptr<nt::Worker>> Frontend::makeWorkers()
{
    const RunParameter& pr = runParam();
    std::vector<std::unique_ptr<nt::Worker>> workers;
    for (size_t i = 0; i < pr.numCPUWorkers; i++)
        workers.emplace_back(std::make_unique<Worker>(bkey_));
    return workers;
}

/**************************************************/
/***** TEST ***************************************/
/**************************************************/

void test0()
{
    // Set # of CPU cores as # of threads
    int numCPUCores = std::thread::hardware_concurrency();
    Thread::setNumThreads(numCPUCores);

    // Prepare secret and bootstrapping keys
    const char* const bkeyPath = "_test_bkey";
    std::optional<SecretKey> sk;
    {
        LOG_DBG_SCOPE("GENERATE KEYS");
        LOG_DBG << "GENERATE SECRET KEY";
        sk.emplace();
        LOG_DBG << "GENERATE BOOTSTRAPPING KEY";
        writeTFHEppBKey(bkeyPath, TFHEppBKey{*sk});
    }

    auto go = [&](const std::string& blueprintPath,
                  const std::string& inPktPath,
                  const std::string& expectedOutPktPath, int numCycles) {
        const char* const reqPktPath = "_test_in";
        const char* const resPktPath = "_test_out";

        auto inPlainPkt = PlainPacket::fromTOML(inPktPath),
             expectedOutPlainPkt = PlainPacket::fromTOML(expectedOutPktPath);
        auto inPkt = inPlainPkt.encrypt(*sk);
        writeTFHEPacket(reqPktPath, inPkt);

        LOG_DBG_SCOPE("go");
        Frontend frontend{RunParameter{
            blueprintPath,  // blueprintFile
            reqPktPath,     // inputFile
            resPktPath,     // outputFile
            bkeyPath,       // bkeyFile
            numCPUCores,    // numCPUWorkers
            numCycles,      // numCycles
            0,              // currentCycle
            SCHED::RANKU,   // sched
            std::nullopt,   // snapshotFile
        }};
        frontend.run();

        TFHEPacket got = readTFHEPacket(resPktPath);
        PlainPacket gotPlain = got.decrypt(*sk);
        assert(gotPlain == expectedOutPlainPkt);
    };

    go("test/config-toml/const-4bit.toml", "test/in/test22.in",
       "test/out/test22.out", 1);
    go("test/config-toml/addr-4bit.toml", "test/in/test04.in",
       "test/out/test04.out", 1);
    go("test/config-toml/pass-addr-pass-4bit.toml", "test/in/test04.in",
       "test/out/test04.out", 1);
    go("test/config-toml/addr-register-4bit.toml", "test/in/test16.in",
       "test/out/test16.out", 3);
    go("test/config-toml/div-8bit.toml", "test/in/test05.in",
       "test/out/test05.out", 1);
    go("test/config-toml/ram-addr8bit.toml", "test/in/test06.in",
       "test/out/test06.out", 16);
    go("test/config-toml/ram-addr9bit.toml", "test/in/test07.in",
       "test/out/test07.out", 16);
    go("test/config-toml/ram-8-16-16.toml", "test/in/test08.in",
       "test/out/test08.out", 8);
    go("test/config-toml/rom-4-8.toml", "test/in/test15.in",
       "test/out/test15.out", 1);
    go("test/config-toml/counter-4bit.toml", "test/in/test13.in",
       "test/out/test13.out", 3);
    go("test/config-toml/cahp-ruby.toml", "test/in/test09.in",
       "test/out/test09-ruby.out", 7);

    auto go_ss = [&](const std::string& blueprintPath,
                     const std::string& inPktPath,
                     const std::string& expectedOutPktPath, int numCycles) {
        const char* const reqPktPath = "_test_in";
        const char* const resPktPath = "_test_out";
        const char* const snapshotPath = "_test_snapshot";

        auto inPlainPkt = PlainPacket::fromTOML(inPktPath),
             expectedOutPlainPkt = PlainPacket::fromTOML(expectedOutPktPath);
        auto inPkt = inPlainPkt.encrypt(*sk);
        writeTFHEPacket(reqPktPath, inPkt);

        int secondNumCycles = numCycles / 2,
            firstNumCycles = numCycles - secondNumCycles;

        {
            LOG_DBG_SCOPE("go_ss 1st");
            Frontend frontend{RunParameter{
                blueprintPath,   // blueprintFile
                reqPktPath,      // inputFile
                resPktPath,      // outputFile
                bkeyPath,        // bkeyFile
                numCPUCores,     // numCPUWorkers
                firstNumCycles,  // numCycles
                0,               // currentCycle
                SCHED::RANKU,    // sched
                snapshotPath,    // snapshotFile
            }};
            frontend.run();
        }
        {
            LOG_DBG_SCOPE("go_ss 2nd");
            Snapshot ss{snapshotPath};
            ss.updateNumCycles(secondNumCycles);
            Frontend frontend{ss};
            frontend.run();

            TFHEPacket got = readTFHEPacket(resPktPath);
            PlainPacket gotPlain = got.decrypt(*sk);
            assert(gotPlain == expectedOutPlainPkt);
        }
    };
    go_ss("test/config-toml/addr-register-4bit.toml", "test/in/test16.in",
          "test/out/test16.out", 3);
    go_ss("test/config-toml/ram-addr8bit.toml", "test/in/test06.in",
          "test/out/test06.out", 16);
    go_ss("test/config-toml/ram-addr9bit.toml", "test/in/test07.in",
          "test/out/test07.out", 16);
    go_ss("test/config-toml/ram-8-16-16.toml", "test/in/test08.in",
          "test/out/test08.out", 8);
    go_ss("test/config-toml/counter-4bit.toml", "test/in/test13.in",
          "test/out/test13.out", 3);
    go_ss("test/config-toml/cahp-ruby.toml", "test/in/test09.in",
          "test/out/test09-ruby.out", 7);
}

}  // namespace nt::tfhepp
