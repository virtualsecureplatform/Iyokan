#include "iyokan_nt.hpp"
#include "error_nt.hpp"

#include <fmt/format.h>
#include <toml.hpp>  // FIXME: this makes compilation slow

#include <regex>
#include <stdexcept>

namespace nt {

std::vector<std::string> regexMatch(const std::string& text,
                                    const std::regex& re)
{
    std::vector<std::string> ret;
    std::smatch m;
    if (!std::regex_match(text, m, re))
        return ret;
    for (auto&& elm : m)
        ret.push_back(elm.str());
    return ret;
}

/* class Allocator */

Allocator::Allocator()
{
}

Allocator& Allocator::subAllocator(const std::string& key)
{
    assert(data_.size() == 0);
    auto it = subs_.find(key);
    if (it == subs_.end()) {
        auto sub = std::make_unique<Allocator>();
        std::tie(it, std::ignore) = subs_.emplace(key, std::move(sub));
    }
    return *it->second;
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
        const ConfigName& cname = task->label().cname.value();
        byConfigName_.emplace(
            std::make_tuple(cname.nodeName, cname.portName, cname.portBit),
            task);
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

void Network::pushReadyTasks(ReadyQueue& readyQueue)
{
    for (auto&& task : tasks_)
        if (task->areAllInputsReady())
            readyQueue.push(task.get());
}

void Network::tick()
{
    for (auto&& task : tasks_)
        task->tick();
}

/* class NetworkBuilder */

NetworkBuilder::NetworkBuilder(Allocator& alcRoot)
    : finder_(), tasks_(), consumed_(false), currentAlc_(&alcRoot)
{
}

NetworkBuilder::~NetworkBuilder()
{
}

Allocator& NetworkBuilder::currentAllocator()
{
    return *currentAlc_;
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
    network_.pushReadyTasks(readyQueue_);
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
    network_.tick();
}

/* class Blueprint */

Blueprint::Blueprint(const std::string& fileName)
{
    namespace fs = std::filesystem;

    // Read the file
    std::stringstream inputStream;
    {
        std::ifstream ifs{fileName};
        if (!ifs)
            ERR_DIE("File not found: " << fileName);
        inputStream << ifs.rdbuf();
        source_ = inputStream.str();
        inputStream.seekg(std::ios::beg);
    }

    // Parse config file
    const auto src = toml::parse(inputStream, fileName);

    // Find working directory of config
    fs::path wd = fs::absolute(fileName);
    wd.remove_filename();

    // [[file]]
    {
        const auto srcFiles =
            toml::find_or<std::vector<toml::value>>(src, "file", {});
        for (const auto& srcFile : srcFiles) {
            std::string typeStr = toml::find<std::string>(srcFile, "type");
            fs::path path = toml::find<std::string>(srcFile, "path");
            std::string name = toml::find<std::string>(srcFile, "name");

            blueprint::File::TYPE type;
            if (typeStr == "iyokanl1-json")
                type = blueprint::File::TYPE::IYOKANL1_JSON;
            else if (typeStr == "yosys-json")
                type = blueprint::File::TYPE::YOSYS_JSON;
            else
                ERR_DIE("Invalid file type: " << typeStr);

            if (path.is_relative())
                path = wd / path;  // Make path absolute

            files_.push_back(blueprint::File{type, path.string(), name});
        }
    }

    // [[builtin]]
    {
        const auto srcBuiltins =
            toml::find_or<std::vector<toml::value>>(src, "builtin", {});
        for (const auto& srcBuiltin : srcBuiltins) {
            const auto type = toml::find<std::string>(srcBuiltin, "type");
            const auto name = toml::find<std::string>(srcBuiltin, "name");

            if (type == "rom" || type == "mux-rom") {
                auto romType = type == "rom"
                                   ? blueprint::BuiltinROM::TYPE::CMUX_MEMORY
                                   : blueprint::BuiltinROM::TYPE::MUX;
                const auto inAddrWidth =
                    toml::find<size_t>(srcBuiltin, "in_addr_width");
                const auto outRdataWidth =
                    toml::find<size_t>(srcBuiltin, "out_rdata_width");

                builtinROMs_.push_back(blueprint::BuiltinROM{
                    romType, name, inAddrWidth, outRdataWidth});
            }
            else if (type == "ram" || type == "mux-ram") {
                auto ramType = type == "ram"
                                   ? blueprint::BuiltinRAM::TYPE::CMUX_MEMORY
                                   : blueprint::BuiltinRAM::TYPE::MUX;
                const auto inAddrWidth =
                    toml::find<size_t>(srcBuiltin, "in_addr_width");
                const auto inWdataWidth =
                    toml::find<size_t>(srcBuiltin, "in_wdata_width");
                const auto outRdataWidth =
                    toml::find<size_t>(srcBuiltin, "out_rdata_width");

                builtinRAMs_.push_back(blueprint::BuiltinRAM{
                    ramType, name, inAddrWidth, inWdataWidth, outRdataWidth});
            }
        }
    }

    // [connect]
    {
        const auto srcConnect = toml::find_or<toml::table>(src, "connect", {});
        for (const auto& [srcKey, srcValue] : srcConnect) {
            if (srcKey == "TOGND") {  // TOGND = [@...[n:m], @...[n:m], ...]
                auto ary = toml::get<std::vector<std::string>>(srcValue);
                for (const auto& portStr : ary) {  // @...[n:m]
                    if (portStr.empty() || portStr.at(0) != '@')
                        ERR_DIE("Invalid port name for TOGND: " << portStr);
                    auto ports = parsePortString(portStr, "output");
                    for (auto&& port : ports) {  // @...[n]
                        const std::string& name = port.portName;
                        int bit = port.portBit;
                        auto [it, inserted] = atPortWidths_.emplace(name, 0);
                        it->second = std::max(it->second, bit + 1);
                    }
                }
                continue;
            }

            std::string srcTo = srcKey,
                        srcFrom = toml::get<std::string>(srcValue),
                        errMsg = fmt::format("Invalid connect: {} = {}", srcTo,
                                             srcFrom);

            // Check if input is correct.
            if (srcTo.empty() || srcFrom.empty() ||
                (srcTo[0] == '@' && srcFrom[0] == '@'))
                ERR_DIE(errMsg);

            // Others.
            std::vector<blueprint::Port> portsTo =
                                             parsePortString(srcTo, "input"),
                                         portsFrom =
                                             parsePortString(srcFrom, "output");
            if (portsTo.size() != portsFrom.size())
                ERR_DIE(errMsg);

            for (size_t i = 0; i < portsTo.size(); i++) {
                const blueprint::Port& to = portsTo[i];
                const blueprint::Port& from = portsFrom[i];

                if (srcTo[0] == '@') {  // @... = ...
                    if (!to.nodeName.empty() || from.nodeName.empty())
                        ERR_DIE(errMsg);

                    const std::string& name = to.portName;
                    int bit = to.portBit;

                    {
                        auto [it, inserted] =
                            atPorts_.emplace(std::make_tuple(name, bit), from);
                        if (!inserted)
                            LOG_S(WARNING)
                                << srcTo
                                << " is used multiple times. Only the first "
                                   "one is effective.";
                    }

                    auto [it, inserted] = atPortWidths_.emplace(name, 0);
                    it->second = std::max(it->second, bit + 1);
                }
                else if (srcFrom[0] == '@') {  // ... = @...
                    if (!from.nodeName.empty() || to.nodeName.empty())
                        ERR_DIE(errMsg);

                    const std::string& name = from.portName;
                    int bit = from.portBit;

                    {
                        auto [it, inserted] =
                            atPorts_.emplace(std::make_tuple(name, bit), to);
                        if (!inserted)
                            LOG_S(WARNING)
                                << srcFrom
                                << " is used multiple times. Only the first "
                                   "one is effective. (FIXME)";
                    }

                    auto [it, inserted] = atPortWidths_.emplace(name, 0);
                    it->second = std::max(it->second, bit + 1);
                }
                else {  // ... = ...
                    edges_.emplace_back(from, to);
                }
            }
        }
    }
}

std::vector<blueprint::Port> Blueprint::parsePortString(const std::string& src,
                                                        const std::string& kind)
{
    std::string nodeName, portName;
    int portBitFrom, portBitTo;

    auto match = regexMatch(
        src,
        std::regex(R"(^@?(?:([^/]+)/)?([^[]+)(?:\[([0-9]+):([0-9]+)\])?$)"));
    if (match.empty())
        ERR_DIE("Invalid port string: " << src);

    assert(match.size() == 1 + 4);

    nodeName = match[1];
    portName = match[2];

    if (match[3].empty()) {  // hoge/piyo
        assert(match[4].empty());
        portBitFrom = 0;
        portBitTo = 0;
    }
    else {  // hoge/piyo[foo:bar]
        assert(!match[4].empty());
        portBitFrom = std::stoi(match[3]);
        portBitTo = std::stoi(match[4]);
    }

    std::vector<blueprint::Port> ret;
    for (int i = portBitFrom; i < portBitTo + 1; i++)
        ret.push_back(blueprint::Port{nodeName, kind, portName, i});
    return ret;
}

bool Blueprint::needsCircuitKey() const
{
    for (const auto& bprom : builtinROMs_)
        if (bprom.type == blueprint::BuiltinROM::TYPE::CMUX_MEMORY)
            return true;
    for (const auto& bpram : builtinRAMs_)
        if (bpram.type == blueprint::BuiltinRAM::TYPE::CMUX_MEMORY)
            return true;
    return false;
}

const std::string& Blueprint::sourceFile() const
{
    return sourceFile_;
}

const std::string& Blueprint::source() const
{
    return source_;
}

const std::vector<blueprint::File>& Blueprint::files() const
{
    return files_;
}

const std::vector<blueprint::BuiltinROM>& Blueprint::builtinROMs() const
{
    return builtinROMs_;
}

const std::vector<blueprint::BuiltinRAM>& Blueprint::builtinRAMs() const
{
    return builtinRAMs_;
}

const std::vector<std::pair<blueprint::Port, blueprint::Port>>&
Blueprint::edges() const
{
    return edges_;
}

const std::map<std::tuple<std::string, int>, blueprint::Port>&
Blueprint::atPorts() const
{
    return atPorts_;
}

std::optional<blueprint::Port> Blueprint::at(const std::string& portName,
                                             int portBit) const
{
    auto it = atPorts_.find(std::make_tuple(portName, portBit));
    if (it == atPorts_.end())
        return std::nullopt;
    return it->second;
}

const std::unordered_map<std::string, int>& Blueprint::atPortWidths() const
{
    return atPortWidths_;
}

/**************************************************/
/***** TEST ***************************************/
/**************************************************/

/*
void testBuildAdder(NetworkBuilder& nb)
{
    UID a = nb.INPUT("0", "A", 0), b = nb.INPUT("1", "B", 0),
        ci = nb.INPUT("2", "Ci", 0), s = nb.OUTPUT("3", "S", 0),
        c0 = nb.OUTPUT("4", "C0", 0), and0 = nb.AND("5"), and1 = nb.AND("6"),
        or0 = nb.OR("7"), xor0 = nb.XOR("8"), xor1 = nb.XOR("9");
    nb.connect(a, xor0);
    nb.connect(b, xor0);
    nb.connect(ci, xor1);
    nb.connect(xor0, xor1);
    nb.connect(a, and0);
    nb.connect(b, and0);
    nb.connect(ci, and1);
    nb.connect(xor0, and1);
    nb.connect(and1, or0);
    nb.connect(and0, or0);
    nb.connect(xor1, s);
    nb.connect(or0, c0);
}
*/

}  // namespace nt
