#include "iyokan_nt.hpp"
#include "blueprint.hpp"
#include "dataholder_nt.hpp"
#include "error_nt.hpp"

#include <cassert>

#include <algorithm>

namespace {
void prioritizeTaskByRanku(const nt::TaskFinder& finder)
{
    // c.f. https://en.wikipedia.org/wiki/Heterogeneous_Earliest_Finish_Time
    // FIXME: Take communication costs into account
    // FIXME: Tune computation costs by dynamic measurements

    using namespace nt;

    std::unordered_map<std::string, int> compCost = {
        {"DFF", 0},          {"WIRE", 0},     {"INPUT", 0},
        {"OUTPUT", 0},       {"AND", 10},     {"NAND", 10},
        {"ANDNOT", 10},      {"OR", 10},      {"NOR", 10},
        {"ORNOT", 10},       {"XOR", 10},     {"XNOR", 10},
        {"MUX", 20},         {"NOT", 0},      {"CONSTONE", 0},
        {"CONSTZERO", 0},    {"CB", 100},     {"CBInv", 100},
        {"CBWithInv", 100},  {"MUXWoSE", 20}, {"CMUXs", 10},
        {"SEI", 0},          {"GB", 10},      {"ROMUX", 10},
        {"RAMUX", 10},       {"SEI&KS", 5},   {"cufhe2tfhepp", 0},
        {"tfhepp2cufhe", 0}, {"bridge", 0},   {"RAMWriter", 0},
        {"RAMReader", 0},    {"ROM", 0},      {"SDFF", 0},
        {"RAM", 0},
    };

    std::unordered_map<Task*, int>
        numReadyChildren;   // task |-> # of ready children
    std::queue<Task*> que;  // Initial tasks to be visited
    finder.eachTask([&](UID, Task* task) {
        const std::vector<Task*>& children = task->children();

        // Count the children that have no inputs to wait for
        size_t n = std::count_if(
            children.begin(), children.end(),
            [&](Task* child) { return child->areAllInputsReady(); });
        numReadyChildren.emplace(task, n);

        // Initial nodes should be "terminals", that is,
        // they have no children OR all of their children has no inputs to wait
        // for.
        if (children.size() == n)
            que.push(task);
    });
    assert(!que.empty());

    size_t numPrioritizedTasks = 0;
    while (!que.empty()) {
        Task* task = que.front();
        que.pop();

        // Calculate and set the priority for the task
        int pri = 0;
        for (Task* child : task->children())
            // Only take valid children (i.e., ones that have no some inputs to
            // wait for) into account
            if (!child->areAllInputsReady())
                pri = std::max(pri, child->priority());
        auto it = compCost.find(task->label().kind);
        if (it == compCost.end())
            ERR_DIE("Internal error: compCost does not have key: "
                    << task->label().kind);
        task->setPriority(pri + it->second);
        numPrioritizedTasks++;

        if (task->areAllInputsReady())  // The end of the travel
            continue;

        // Push parents into the queue if all of their children are ready
        for (Task* parent : task->parents()) {
            numReadyChildren.at(parent)++;
            assert(parent->children().size() >= numReadyChildren.at(parent));
            if (parent->children().size() == numReadyChildren.at(parent))
                que.push(parent);
        }
    }
    if (finder.size() > numPrioritizedTasks) {
        LOG_DBG << "finder.size() " << finder.size()
                << " != numPrioritizedTasks " << numPrioritizedTasks;
        finder.eachTask([&](UID, Task* task) {
            const Label& l = task->label();
            if (task->priority() == -1)
                LOG_DBG << "\t" << l.uid << " " << l.kind << " ";
        });
        ERR_DIE("Invalid network; some nodes will not be executed.");
    }
    assert(finder.size() == numPrioritizedTasks);
}
}  // namespace

namespace nt {

/* class Task */

Task::Task(Label label)
    : label_(std::move(label)),
      parents_(),
      children_(),
      priority_(-1),
      hasQueued_(false)
{
}

Task::~Task()
{
}

const Label& Task::label() const
{
    return label_;
}

const std::vector<Task*>& Task::parents() const
{
    return parents_;
}

const std::vector<Task*>& Task::children() const
{
    return children_;
}

int Task::priority() const
{
    return priority_;
}

bool Task::hasQueued() const
{
    return hasQueued_;
}

void Task::addChild(Task* task)
{
    assert(task != nullptr);
    children_.push_back(task);
}

void Task::addParent(Task* task)
{
    assert(task != nullptr);
    parents_.push_back(task);
}

void Task::setPriority(int newPri)
{
    priority_ = newPri;
}

void Task::setQueued()
{
    assert(!hasQueued_);
    hasQueued_ = true;
}

void Task::tick()
{
    hasQueued_ = false;
}

void Task::getOutput(DataHolder&)
{
    ERR_UNREACHABLE;
}

void Task::setInput(const DataHolder&)
{
    ERR_UNREACHABLE;
}

bool Task::canRunPlain() const
{
    return false;
}

void Task::onAfterTick(size_t)
{
    // Do nothing by default.
}

void Task::startAsynchronously(plain::WorkerInfo&)
{
    ERR_UNREACHABLE;
}

/* class TaskFinder */

size_t TaskFinder::size() const
{
    return byUID_.size();
}

void TaskFinder::add(Task* task)
{
    const Label& label = task->label();
    byUID_.emplace(label.uid, task);

    if (label.cname) {
        const ConfigName& cname = label.cname.value();
        auto [it, inserted] = byConfigName_.emplace(
            std::make_tuple(cname.nodeName, cname.portName, cname.portBit),
            task);
        if (!inserted)
            ERR_DIE("Same config name already exists: "
                    << cname.nodeName << "/" << cname.portName << "["
                    << cname.portBit << "]");
    }
}

Task* TaskFinder::findByUID(UID uid) const
{
    return byUID_.at(uid);
}

Task* TaskFinder::findByConfigName(const ConfigName& cname) const
{
    return byConfigName_.at(
        std::make_tuple(cname.nodeName, cname.portName, cname.portBit));
}

/* class ReadyQueue */

bool ReadyQueue::empty() const
{
    return queue_.empty();
}

void ReadyQueue::pop()
{
    queue_.pop();
}

Task* ReadyQueue::peek() const
{
    auto [pri, task] = queue_.top();
    return task;
}

void ReadyQueue::push(Task* task)
{
    queue_.emplace(task->priority(), task);
    task->setQueued();
}

/* class Network */

Network::Network(TaskFinder finder, std::vector<std::unique_ptr<Task>> tasks)
    : finder_(std::move(finder)), tasks_(std::move(tasks))
{
}

size_t Network::size() const
{
    return tasks_.size();
}

const TaskFinder& Network::finder() const
{
    return finder_;
}

/* class NetworkBuilder */

NetworkBuilder::NetworkBuilder(Allocator& alc)
    : finder_(), tasks_(), consumed_(false), alc_(&alc)
{
}

NetworkBuilder::~NetworkBuilder()
{
}

Allocator& NetworkBuilder::currentAllocator()
{
    return *alc_;
}

const TaskFinder& NetworkBuilder::finder() const
{
    assert(!consumed_);
    return finder_;
}

Network NetworkBuilder::createNetwork()
{
    assert(!consumed_);
    consumed_ = true;
    return Network{std::move(finder_), std::move(tasks_)};
}

/* class Worker */

Worker::Worker() : target_(nullptr)
{
}

Worker::~Worker()
{
}

void Worker::update(ReadyQueue& readyQueue, size_t& numFinishedTargets)
{
    if (target_ == nullptr && !readyQueue.empty()) {
        // Try to find the task to tackle next
        Task* cand = readyQueue.peek();
        assert(cand != nullptr);
        if (canExecute(cand)) {
            target_ = cand;
            readyQueue.pop();
            startTask(target_);
        }
    }

    if (target_ != nullptr && target_->hasFinished()) {
        for (Task* child : target_->children()) {
            if (child->hasQueued())
                continue;
            child->notifyOneInputReady();
            if (child->areAllInputsReady())
                readyQueue.push(child);
        }
        target_ = nullptr;
        numFinishedTargets++;
    }
}

bool Worker::isWorking() const
{
    return target_ != nullptr;
}

/* class NetworkRunner */

NetworkRunner::NetworkRunner(Network network,
                             std::vector<std::unique_ptr<Worker>> workers)
    : network_(std::move(network)),
      workers_(std::move(workers)),
      readyQueue_(),
      numFinishedTargets_(0)
{
    assert(workers_.size() != 0);
    for (auto&& w : workers)
        assert(w != nullptr);
}

void NetworkRunner::prepareToRun()
{
    assert(readyQueue_.empty());

    numFinishedTargets_ = 0;

    // Push ready tasks to the ready queue.
    network_.eachTask([&](Task* task) {
        if (task->areAllInputsReady())
            readyQueue_.push(task);
    });
}

void NetworkRunner::update()
{
    for (auto&& w : workers_)
        w->update(readyQueue_, numFinishedTargets_);
}

const Network& NetworkRunner::network() const
{
    return network_;
}

size_t NetworkRunner::numFinishedTargets() const
{
    return numFinishedTargets_;
}

bool NetworkRunner::isRunning() const
{
    return std::any_of(workers_.begin(), workers_.end(),
                       [](auto&& w) { return w->isWorking(); }) ||
           !readyQueue_.empty();
}

void NetworkRunner::run()
{
    prepareToRun();
    while (numFinishedTargets() < network().size()) {
        assert(isRunning() && "Invalid network: maybe some unreachable tasks?");
        update();
    }
}

void NetworkRunner::tick()
{
    network_.eachTask([&](Task* task) { task->tick(); });
}

void NetworkRunner::onAfterTick(size_t currentCycle)
{
    network_.eachTask([&](Task* task) { task->onAfterTick(currentCycle); });
}

/* struct RunParameter */

void RunParameter::print() const
{
    LOG_S(INFO) << "Run parameters";
    LOG_S(INFO) << "\tMode: plain";
    LOG_S(INFO) << "\tBlueprint: " << blueprintFile;
    LOG_S(INFO) << "\t# of CPU Workers: " << numCPUWorkers;
    LOG_S(INFO) << "\t# of cycles: " << numCycles;
    LOG_S(INFO) << "\tCurrent cycle #: " << currentCycle;
    LOG_S(INFO) << "\tInput file (request packet): " << inputFile;
    LOG_S(INFO) << "\tOutput file (result packet): " << outputFile;
    LOG_S(INFO) << "\tSchedule: " << (sched == SCHED::TOPO ? "topo" : "ranku");
}

/* class Frontend */

Frontend::Frontend(const RunParameter& pr)
    : pr_(pr),
      network_(std::nullopt),
      currentCycle_(pr.currentCycle),
      bp_(std::make_unique<Blueprint>(pr_.blueprintFile)),
      alc_(std::make_shared<Allocator>())
{
    assert(alc_);
}

Frontend::Frontend(const Snapshot& ss)
    : pr_(ss.getRunParam()),
      network_(std::nullopt),
      currentCycle_(pr_.currentCycle),
      bp_(std::make_unique<Blueprint>(pr_.blueprintFile)),
      alc_(ss.getAllocator())
{
    assert(alc_);
}

void Frontend::buildNetwork(NetworkBuilder& nb)
{
    const Blueprint& bp = blueprint();
    const TaskFinder& finder = nb.finder();

    // [[file]]
    for (auto&& file : bp.files())
        readNetworkFromFile(file, nb);

    // [[builtin]] type = ram | type = mux-ram
    for (auto&& ram : bp.builtinRAMs()) {
        // We ignore ram.type and always use mux-ram in plaintext mode.
        makeMUXRAM(ram, nb);
    }

    // [[builtin]] type = rom | type = mux-rom
    for (auto&& rom : bp.builtinROMs()) {
        // We ignore rom.type and always use mux-rom in plaintext mode.
        makeMUXROM(rom, nb);
    }

    auto get = [&](const blueprint::Port& port) -> Task* {
        Task* task = finder.findByConfigName(port.cname);
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
    for (auto&& [key, port] : bp.atPorts()) {
        get(port);  // Only checks if port exists
    }
    // Then, connect other ports. `get` checks if they also exist.
    for (auto&& [src, dst] : bp.edges()) {
        assert(src.kind == Label::OUTPUT);
        assert(dst.kind == Label::INPUT);
        nb.connect(get(src)->label().uid, get(dst)->label().uid);
    }

    // Create the network from the builder
    network_.emplace(nb.createNetwork());
    // FIXME check if network is valid

    // Set priority to each task
    switch (pr_.sched) {
    case SCHED::TOPO:
        ERR_DIE("Scheduling topo is not supported anymore");  // FIXME
        break;

    case SCHED::RANKU:
        prioritizeTaskByRanku(network_->finder());
        break;
    }
}

Frontend::~Frontend()
{
}

void Frontend::run()
{
    const Blueprint& bp = blueprint();

    DataHolder bit0, bit1;
    setBit0(bit0);
    setBit1(bit1);

    // Create workers
    LOG_DBG << "CREATE WORKERS";
    std::vector<std::unique_ptr<nt::Worker>> workers = makeWorkers();

    // Create runner and finder for the network
    LOG_DBG << "CREATE RUNNER";
    NetworkRunner runner{std::move(network_.value()), std::move(workers)};
    network_ = std::nullopt;
    const TaskFinder& finder = runner.network().finder();

    // Process reset cycle if @reset is used
    // FIXME: Add support for --skip-reset flag
    if (currentCycle_ == 0) {
        auto reset = bp.at("reset");
        if (reset && reset->kind == Label::INPUT) {
            LOG_DBG << "RESET";
            Task* t = finder.findByConfigName(reset->cname);
            t->setInput(bit1);  // Set reset on
            runner.run();
            t->setInput(bit0);  // Set reset off
        }
    }

    // Process normal cycles
    for (int i = 0; i < pr_.numCycles; i++, currentCycle_++) {
        LOG_DBG_SCOPE("Cycle #%d (i = %d)", currentCycle_, i);

        // Mount new values to DFFs
        LOG_DBG << "TICK";
        runner.tick();

        // Set new input data. If i is equal to 0, it also mounts initial data
        // to RAMs.
        LOG_DBG << "ON AFTER TICK";
        runner.onAfterTick(currentCycle_);

        // Go computing of each gate
        LOG_DBG << "RUN";
        runner.run();

        /*
        // Debug printing of all the gates
        runner.network().eachTask([&](Task* t) {
            TaskCommon<Bit>* p = dynamic_cast<TaskCommon<Bit>*>(t);
            if (p == nullptr)
                return;
            const Label& l = t->label();
            if (t->label().cname)
                LOG_DBG << l.kind << "\t" << *l.cname << "\t"
                        << p->DEBUG_output();
            else
                LOG_DBG << l.kind << "\t" << p->DEBUG_output();
        });
        */
    }

    // Dump result packet
    LOG_DBG << "DUMP RES PACKET";
    dumpResPacket(pr_.outputFile, finder, currentCycle_);

    // Dump snapshot
    if (pr_.snapshotFile) {
        LOG_DBG << "DUMP SNAPSHOT";
        Snapshot ss{pr_, allocatorPtr()};
        ss.updateCurrentCycle(currentCycle_);
        ss.dump(pr_.snapshotFile.value());
    }
}

/* makeMUXROM */

namespace {
void make1bitROMWithMUX(const std::string& nodeName,
                        const std::vector<UID>& addrInputs,
                        size_t outRdataWidth, size_t indexOutRdata,
                        NetworkBuilder& nb)
{
    /*
       INPUT
        addr[1] ------------------------------+
       INPUT                                  |
        addr[0] --+-----------------+         |
                  |                 |         |
                  | ROM             |         |
                  |  romdata[0] -- |\         |
                  | ROM            | | --+    |
                  |  romdata[1] -- |/    +-- |\           OUTPUT
                  |                          | | -- ... -- rdata[indexOutRdata]
                  +-----------------+    +-- |/
                                    |    |
                    ROM             |    |
                     romdata[2] -- +\    |
                    ROM            | | --+
                     romdata[3] -- |/

        ...

                    ROM
                     addr[2^inAddrWidth-1] -- ...
    */

    const size_t inAddrWidth = addrInputs.size();

    // Create ROMs
    std::vector<UID> workingIds;
    for (size_t i = 0; i < (1 << inAddrWidth); i++) {
        UID id = nb.ROM(nodeName, "romdata", indexOutRdata + i * outRdataWidth);
        workingIds.push_back(id);
    }

    // Create MUXs
    for (size_t i = 0; i < inAddrWidth; i++) {
        assert(workingIds.size() > 0 && workingIds.size() % 2 == 0);
        std::vector<UID> newWorkingIds;
        for (size_t j = 0; j < workingIds.size(); j += 2) {
            UID id = nb.MUX();
            nb.connect(workingIds.at(j), id);
            nb.connect(workingIds.at(j + 1), id);
            nb.connect(addrInputs.at(i), id);
            newWorkingIds.push_back(id);
        }
        workingIds.swap(newWorkingIds);
    }
    assert(workingIds.size() == 1);

    // Create output
    UID id = nb.OUTPUT(nodeName, "rdata", indexOutRdata);
    nb.connect(workingIds.at(0), id);
}
}  // namespace

void makeMUXROM(const blueprint::BuiltinROM& rom, NetworkBuilder& nb)
{
    // Create inputs
    std::vector<UID> addrInputs;
    for (size_t i = 0; i < rom.inAddrWidth; i++) {
        UID id = nb.INPUT(rom.name, "addr", i);
        addrInputs.push_back(id);
    }

    // Create 1bit ROMs
    for (size_t i = 0; i < rom.outRdataWidth; i++) {
        make1bitROMWithMUX(rom.name, addrInputs, rom.outRdataWidth, i, nb);
    }
}

/* makeMUXRAM */

namespace {

void make1bitRAMWithMUX(const std::string& nodeName,
                        const std::vector<UID>& addrInputs, UID wrenInput,
                        size_t dataWidth, size_t indexWRdata,
                        NetworkBuilder& nb)
{
    /*
        wdata[indexWRdata]
          |
          |   +---------------------+
          |   |                     |
          |   +--|\                 |
          |      | |-- ramdata[.] --+------------+-|\
          +------|/                                | |-- rdata[indexWRdata]
          |       |    +---------------------+   +-|/
          |       a    +--|\                 |---+
          |               | |-- ramdata[.] --+
          +---------------|/
          |                |
          |                b

          ...

          |
          +---- ... -- ramdata[2^inAddrWidth-1] --


                a   b
                |   |
                -----
   addr[0] --- /0   1\ DMUX
               -------           ...
                  |               |
                  +-------+-------+
                          |
                         ... ...
                          |   |
                          |   |
                          -----
 addr[inAddrWidth-1] --- /0   1\ DMUX
                         -------
                            |
                          wren


    DMUX: (in, sel) -> (out0, out1)
        out0 = andnot(in, sel)
        out1 = and(in, sel)
    */

    const size_t inAddrWidth = addrInputs.size();

    // Create input "wdata[indexWRdata]"
    UID wdataInput = nb.INPUT(nodeName, "wdata", indexWRdata);

    // Create DMUXs
    std::vector<UID> workingIds = {wrenInput}, newWorkingIds;
    for (auto it = addrInputs.rbegin(); it != addrInputs.rend(); ++it) {
        UID addr = *it;
        for (UID src : workingIds) {
            // Create DMUX
            //   dst0 = andnot(src, addr)
            //   dst1 = and(src, addr)
            UID dst0 = nb.ANDNOT();
            UID dst1 = nb.AND();
            nb.connect(src, dst0);
            nb.connect(addr, dst0);
            nb.connect(src, dst1);
            nb.connect(addr, dst1);

            newWorkingIds.push_back(dst0);
            newWorkingIds.push_back(dst1);
        }
        workingIds.swap(newWorkingIds);
        newWorkingIds.clear();
    }
    assert(workingIds.size() == (1 << inAddrWidth));

    // Create RAMs
    for (size_t addr = 0; addr < (1 << inAddrWidth); addr++) {
        /*
                                +-------------------------+
                                |                         |
                                +--|\   RAM               |--
           INPUT                   | |-- ramdata[ ... ] --+
            wdata[indexRWdata] ----|/
                                    |
                                   sel
         */
        UID sel = workingIds.at(addr), mux = nb.MUX(),
            ram = nb.RAM(nodeName, "ramdata", addr * dataWidth + indexWRdata);
        nb.connect(ram, mux);
        nb.connect(wdataInput, mux);
        nb.connect(sel, mux);
        nb.connect(mux, ram);
        newWorkingIds.push_back(ram);
    }
    workingIds.swap(newWorkingIds);
    newWorkingIds.clear();

    // Create MUXs
    for (size_t i = 0; i < inAddrWidth; i++) {
        assert(workingIds.size() > 0 && workingIds.size() % 2 == 0);
        for (size_t j = 0; j < workingIds.size(); j += 2) {
            UID id = nb.MUX();
            nb.connect(workingIds.at(j), id);
            nb.connect(workingIds.at(j + 1), id);
            nb.connect(addrInputs.at(i), id);
            newWorkingIds.push_back(id);
        }
        workingIds.swap(newWorkingIds);
        newWorkingIds.clear();
    }
    assert(workingIds.size() == 1);

    // Create output "rdata[indexWRdata]"
    UID rdataOutput = nb.OUTPUT(nodeName, "rdata", indexWRdata);
    nb.connect(workingIds.at(0), rdataOutput);
}

}  // namespace

/*
   // Iyokan-L1 JSON of MUX RAM pre-compiled (and optimized) by Yosys
extern char _binary_mux_ram_8_8_8_min_json_start[];
extern char _binary_mux_ram_8_8_8_min_json_end[];
extern char _binary_mux_ram_8_8_8_min_json_size[];
extern char _binary_mux_ram_8_16_16_min_json_start[];
extern char _binary_mux_ram_8_16_16_min_json_end[];
extern char _binary_mux_ram_8_16_16_min_json_size[];
extern char _binary_mux_ram_9_16_16_min_json_start[];
extern char _binary_mux_ram_9_16_16_min_json_end[];
extern char _binary_mux_ram_9_16_16_min_json_size[];
*/

void makeMUXRAM(const blueprint::BuiltinRAM& ram, NetworkBuilder& nb)
{
    assert(ram.inWdataWidth == ram.outRdataWidth);

    /*
#define USE_PRECOMPILED_BINARY(addrW, dataW)                               \
    if (inAddrWidth == addrW && dataWidth == dataW) {                      \
        std::stringstream ss{std::string{                                  \
            _binary_mux_ram_##addrW##_##dataW##_##dataW##_min_json_start,  \
            _binary_mux_ram_##addrW##_##dataW##_##dataW##_min_json_end}};  \
        IyokanL1JSONReader::read(b, ss);                                   \
        auto net = std::make_shared<typename NetworkBuilder::NetworkType>( \
            std::move(b));                                                 \
                                                                           \
        error::Stack err;                                                  \
        net->checkValid(err);                                              \
        assert(err.empty());                                               \
                                                                           \
        return net;                                                        \
    }
    USE_PRECOMPILED_BINARY(8, 8);
    USE_PRECOMPILED_BINARY(8, 16);
    USE_PRECOMPILED_BINARY(9, 16);
#undef USE_PRECOMPILED_BINARY
*/

    // Create inputs
    std::vector<UID> addrInputs;
    for (size_t i = 0; i < ram.inAddrWidth; i++) {
        UID id = nb.INPUT(ram.name, "addr", i);
        addrInputs.push_back(id);
    }
    UID wrenInput = nb.INPUT(ram.name, "wren", 0);

    // Create 1bitRAMs
    for (size_t i = 0; i < ram.outRdataWidth; i++) {
        make1bitRAMWithMUX(ram.name, addrInputs, wrenInput, ram.outRdataWidth,
                           i, nb);
    }
}

void test0()
{
    // operator< for ConfigName
    {
        bool res = false;
        res = ConfigName{"abc", "def", 0} < ConfigName{"abc", "dfe", 0};
        assert(res);
        res = ConfigName{"acc", "def", 0} < ConfigName{"abc", "dfe", 0};
        assert(!res);
        res = ConfigName{"abc", "def", 0} < ConfigName{"abc", "def", 0};
        assert(!res);
        res = ConfigName{"abc", "def", 0} < ConfigName{"abc", "def", 1};
        assert(res);
    }
}

}  // namespace nt
