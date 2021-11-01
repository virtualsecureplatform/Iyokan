#include "iyokan_nt.hpp"
#include "error_nt.hpp"

#include <algorithm>

namespace nt {

/* struct ConfigName */
std::ostream& operator<<(std::ostream& os, const ConfigName& c)
{
    os << c.nodeName << "/" << c.portName << "[" << c.portBit << "]";
    return os;
}

/* struct Label */
// Initialization of static variables.
const char* const Label::INPUT = "Input";
const char* const Label::OUTPUT = "Output";

/* class Allocator */

Allocator::Allocator()
{
}

/* class Task */

Task::Task(Label label)
    : label_(std::move(label)),
      parents_(),
      children_(),
      priority_(0),
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

void Task::onAfterFirstTick()
{
    // Do nothing by default.
}

void Task::startAsynchronously(plain::WorkerInfo&)
{
    ERR_UNREACHABLE;
}

/* class TaskFinder */

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

void NetworkRunner::onAfterFirstTick()
{
    network_.eachTask([&](Task* task) { task->onAfterFirstTick(); });
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
                        size_t indexWRdata, NetworkBuilder& nb)
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
            ram = nb.RAM(nodeName, "ramdata", addr * inAddrWidth + indexWRdata);
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
        make1bitRAMWithMUX(ram.name, addrInputs, wrenInput, i, nb);
    }
}

}  // namespace nt
