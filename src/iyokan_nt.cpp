#include "iyokan_nt.hpp"
#include "error_nt.hpp"

#include <fmt/format.h>
#include <picojson.h>
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

///* class YosysJSONReader */
//
// namespace {
//
// class YosysJSONReader {
// private:
//    enum class PORT {
//        IN,
//        OUT,
//    };
//
//    struct Port {
//        PORT type;
//        int id, bit;
//
//        Port(PORT type, int id, int bit) : type(type), id(id), bit(bit)
//        {
//        }
//    };
//
//    enum class CELL {
//        NOT,
//        AND,
//        ANDNOT,
//        NAND,
//        OR,
//        XOR,
//        XNOR,
//        NOR,
//        ORNOT,
//        DFFP,
//        SDFFPP0,
//        SDFFPP1,
//        MUX,
//    };
//
//    struct Cell {
//        CELL type;
//        int id, bit0, bit1, bit2;
//
//        Cell(CELL type, int id, int bit0)
//            : type(type), id(id), bit0(bit0), bit1(-1), bit2(-1)
//        {
//        }
//        Cell(CELL type, int id, int bit0, int bit1)
//            : type(type), id(id), bit0(bit0), bit1(bit1), bit2(-1)
//        {
//        }
//        Cell(CELL type, int id, int bit0, int bit1, int bit2)
//            : type(type), id(id), bit0(bit0), bit1(bit1), bit2(bit2)
//        {
//        }
//    };
//
// private:
//    static int getConnBit(const picojson::object& conn, const std::string&
//    key)
//    {
//        using namespace picojson;
//        const auto& bits = conn.at(key).get<array>();
//        if (bits.size() != 1)
//            ERR_DIE("Invalid JSON: wrong conn size: expected 1, got "
//                    << bits.size());
//        if (!bits.at(0).is<double>())
//            ERR_DIE(
//                "Connection of cells to a constant driver is not
//                implemented.");
//        return bits.at(0).get<double>();
//    }
//
// public:
//    template <class NetworkBuilder>
//    static void read(NetworkBuilder& builder, std::istream& is)
//    {
//        // Convert Yosys JSON to gates. Thanks to:
//        //
//        https://github.com/virtualsecureplatform/Iyokan-L1/blob/ef7c9a993ddbfd54ef58e66b116b681e59d90a3c/Converter/YosysConverter.cs
//        using namespace picojson;
//
//        value v;
//        const std::string err = parse(v, is);
//        if (!err.empty())
//            ERR_DIE("Invalid JSON of network: " << err);
//
//        object& root = v.get<object>();
//        object& modules = root.at("modules").get<object>();
//        if (modules.size() != 1)
//            ERR_DIE(".modules should be an object of size 1");
//        object& modul = modules.begin()->second.get<object>();
//        object& ports = modul.at("ports").get<object>();
//        object& cells = modul.at("cells").get<object>();
//
//        std::unordered_map<int, int> bit2id;
//
//        // Create INPUT/OUTPUT and extract port connection info
//        std::vector<Port> portvec;
//        for (auto&& [key, valAny] : ports) {
//            object& val = valAny.template get<object>();
//            std::string& direction = val["direction"].get<std::string>();
//            array& bits = val["bits"].get<array>();
//
//            if (key == "clock")
//                continue;
//            if (key == "reset" && bits.size() == 0)
//                continue;
//            if (direction != "input" && direction != "output")
//                ERR_DIE("Invalid direction token: " << direction);
//
//            const bool isDirInput = direction == "input";
//            const std::string& portName = key;
//            for (size_t i = 0; i < bits.size(); i++) {
//                const int portBit = i;
//
//                if (bits.at(i).is<std::string>()) {
//                    // Yosys document
//                    // (https://yosyshq.net/yosys/cmd_write_json.html) says:
//                    //
//                    //     Signal bits that are connected to a constant driver
//                    //     are denoted as string "0" or "1" instead of a
//                    number.
//                    //
//                    // We handle this case here.
//
//                    if (isDirInput)
//                        ERR_DIE(
//                            "Invalid bits: INPUT that is connected to a "
//                            "constant driver is not implemented");
//
//                    std::string cnstStr = bits.at(i).get<std::string>();
//                    bool cnst = cnstStr == "1";
//                    if (!cnst && cnstStr != "0")
//                        LOG_S(WARNING)
//                            << "Constant bit of '{}' is regarded as '0'."
//                            << cnstStr;
//
//                    int id1 = builder.OUTPUT(portName, portBit),
//                        id0 = cnst ? builder.CONSTONE() : builder.CONSTZERO();
//                    builder.connect(id0, id1);
//                }
//                else {
//                    const int bit = bits.at(i).get<double>();
//
//                    int id = isDirInput ? builder.INPUT(portName, portBit)
//                                        : builder.OUTPUT(portName, portBit);
//                    portvec.emplace_back(isDirInput ? PORT::IN : PORT::OUT,
//                    id,
//                                         bit);
//                    if (isDirInput)
//                        bit2id.emplace(bit, id);
//                }
//            }
//        }
//
//        // Create gates and extract gate connection info
//        const std::unordered_map<std::string, CELL> mapCell = {
//            {"$_NOT_", CELL::NOT},
//            {"$_AND_", CELL::AND},
//            {"$_ANDNOT_", CELL::ANDNOT},
//            {"$_NAND_", CELL::NAND},
//            {"$_OR_", CELL::OR},
//            {"$_XOR_", CELL::XOR},
//            {"$_XNOR_", CELL::XNOR},
//            {"$_NOR_", CELL::NOR},
//            {"$_ORNOT_", CELL::ORNOT},
//            {"$_DFF_P_", CELL::DFFP},
//            {"$_SDFF_PP0_", CELL::SDFFPP0},
//            {"$_SDFF_PP1_", CELL::SDFFPP1},
//            {"$_MUX_", CELL::MUX},
//        };
//        std::vector<Cell> cellvec;
//        for (auto&& [_key, valAny] : cells) {
//            object& val = valAny.template get<object>();
//            const std::string& type = val.at("type").get<std::string>();
//            object& conn = val.at("connections").get<object>();
//            auto get = [&](const char* key) -> int {
//                return getConnBit(conn, key);
//            };
//
//            int bit = -1, id = -1;
//            switch (mapCell.at(type)) {
//            case CELL::AND:
//                id = builder.AND();
//                cellvec.emplace_back(CELL::AND, id, get("A"), get("B"));
//                bit = get("Y");
//                break;
//            case CELL::NAND:
//                id = builder.NAND();
//                cellvec.emplace_back(CELL::NAND, id, get("A"), get("B"));
//                bit = get("Y");
//                break;
//            case CELL::XOR:
//                id = builder.XOR();
//                cellvec.emplace_back(CELL::XOR, id, get("A"), get("B"));
//                bit = get("Y");
//                break;
//            case CELL::XNOR:
//                id = builder.XNOR();
//                cellvec.emplace_back(CELL::XNOR, id, get("A"), get("B"));
//                bit = get("Y");
//                break;
//            case CELL::NOR:
//                id = builder.NOR();
//                cellvec.emplace_back(CELL::NOR, id, get("A"), get("B"));
//                bit = get("Y");
//                break;
//            case CELL::ANDNOT:
//                id = builder.ANDNOT();
//                cellvec.emplace_back(CELL::ANDNOT, id, get("A"), get("B"));
//                bit = get("Y");
//                break;
//            case CELL::OR:
//                id = builder.OR();
//                cellvec.emplace_back(CELL::OR, id, get("A"), get("B"));
//                bit = get("Y");
//                break;
//            case CELL::ORNOT:
//                id = builder.ORNOT();
//                cellvec.emplace_back(CELL::ORNOT, id, get("A"), get("B"));
//                bit = get("Y");
//                break;
//            case CELL::DFFP:
//                id = builder.DFF();
//                cellvec.emplace_back(CELL::DFFP, id, get("D"));
//                bit = get("Q");
//                break;
//            case CELL::SDFFPP0:
//                id = builder.SDFF(Bit(false));
//                cellvec.emplace_back(CELL::DFFP, id, get("D"));
//                bit = get("Q");
//                break;
//            case CELL::SDFFPP1:
//                id = builder.SDFF(Bit(true));
//                cellvec.emplace_back(CELL::DFFP, id, get("D"));
//                bit = get("Q");
//                break;
//            case CELL::NOT:
//                id = builder.NOT();
//                cellvec.emplace_back(CELL::NOT, id, get("A"));
//                bit = get("Y");
//                break;
//            case CELL::MUX:
//                id = builder.MUX();
//                cellvec.emplace_back(CELL::MUX, id, get("A"), get("B"),
//                                     get("S"));
//                bit = get("Y");
//                break;
//            }
//            bit2id.emplace(bit, id);
//        }
//
//        for (auto&& port : portvec) {
//            if (port.type == PORT::IN)
//                // Actually nothing to do!
//                continue;
//            builder.connect(bit2id.at(port.bit), port.id);
//        }
//
//        for (auto&& cell : cellvec) {
//            switch (cell.type) {
//            case CELL::AND:
//            case CELL::NAND:
//            case CELL::XOR:
//            case CELL::XNOR:
//            case CELL::NOR:
//            case CELL::ANDNOT:
//            case CELL::OR:
//            case CELL::ORNOT:
//                builder.connect(bit2id.at(cell.bit0), cell.id);
//                builder.connect(bit2id.at(cell.bit1), cell.id);
//                break;
//            case CELL::DFFP:
//            case CELL::SDFFPP0:
//            case CELL::SDFFPP1:
//            case CELL::NOT:
//                builder.connect(bit2id.at(cell.bit0), cell.id);
//                break;
//            case CELL::MUX:
//                builder.connect(bit2id.at(cell.bit0), cell.id);
//                builder.connect(bit2id.at(cell.bit1), cell.id);
//                builder.connect(bit2id.at(cell.bit2), cell.id);
//                break;
//            }
//        }
//    }
//};
//
//}  // namespace

void readYosysJSONNetwork(std::istream& is, NetworkBuilder& nb)
{
    //    YosysJSONReader::read(nb, is);
}

///* class IyokanL1JSONReader */
//
// namespace {
//
// class IyokanL1JSONReader {
// public:
//    template <class NetworkBuilder>
//    static void read(NetworkBuilder& builder, std::istream& is)
//    {
//        std::unordered_map<int, int> id2taskId;
//        auto addId = [&](int id, int taskId) { id2taskId.emplace(id, taskId);
//        }; auto findTaskId = [&](int id) {
//            auto it = id2taskId.find(id);
//            if (it == id2taskId.end())
//                error::die("Invalid JSON");
//            return it->second;
//        };
//        auto connectIds = [&](int from, int to) {
//            builder.connect(findTaskId(from), findTaskId(to));
//        };
//
//        picojson::value v;
//        const std::string err = picojson::parse(v, is);
//        if (!err.empty())
//            error::die("Invalid JSON of network: ", err);
//
//        picojson::object& obj = v.get<picojson::object>();
//        picojson::array& cells = obj["cells"].get<picojson::array>();
//        picojson::array& ports = obj["ports"].get<picojson::array>();
//        for (const auto& e : ports) {
//            picojson::object port = e.get<picojson::object>();
//            std::string type = port.at("type").get<std::string>();
//            int id = static_cast<int>(port.at("id").get<double>());
//            std::string portName = port.at("portName").get<std::string>();
//            int portBit = static_cast<int>(port.at("portBit").get<double>());
//            if (type == "input")
//                addId(id, builder.INPUT(portName, portBit));
//            else if (type == "output")
//                addId(id, builder.OUTPUT(portName, portBit));
//        }
//        for (const auto& e : cells) {
//            picojson::object cell = e.get<picojson::object>();
//            std::string type = cell.at("type").get<std::string>();
//            int id = static_cast<int>(cell.at("id").get<double>());
//            if (type == "AND")
//                addId(id, builder.AND());
//            else if (type == "NAND")
//                addId(id, builder.NAND());
//            else if (type == "ANDNOT")
//                addId(id, builder.ANDNOT());
//            else if (type == "XOR")
//                addId(id, builder.XOR());
//            else if (type == "XNOR")
//                addId(id, builder.XNOR());
//            else if (type == "DFFP")
//                addId(id, builder.DFF());
//            else if (type == "NOT")
//                addId(id, builder.NOT());
//            else if (type == "NOR")
//                addId(id, builder.NOR());
//            else if (type == "OR")
//                addId(id, builder.OR());
//            else if (type == "ORNOT")
//                addId(id, builder.ORNOT());
//            else if (type == "MUX")
//                addId(id, builder.MUX());
//            else {
//                bool valid = false;
//                // If builder.RAM() exists
//                if constexpr (detail::hasMethodFuncRAM<NetworkBuilder>) {
//                    if (type == "RAM") {
//                        int addr = cell.at("ramAddress").get<double>(),
//                            bit = cell.at("ramBit").get<double>();
//                        addId(id, builder.RAM(addr, bit));
//                        valid = true;
//                    }
//                }
//
//                if (!valid) {
//                    error::die("Invalid JSON of network. Invalid type: ",
//                    type);
//                }
//            }
//        }
//        for (const auto& e : ports) {
//            picojson::object port = e.get<picojson::object>();
//            std::string type = port.at("type").get<std::string>();
//            int id = static_cast<int>(port.at("id").get<double>());
//            picojson::array& bits = port.at("bits").get<picojson::array>();
//            if (type == "input") {
//                // nothing to do!
//            }
//            else if (type == "output") {
//                for (const auto& b : bits) {
//                    int logic = static_cast<int>(b.get<double>());
//                    connectIds(logic, id);
//                }
//            }
//        }
//        for (const auto& e : cells) {
//            picojson::object cell = e.get<picojson::object>();
//            std::string type = cell.at("type").get<std::string>();
//            int id = static_cast<int>(cell.at("id").get<double>());
//            picojson::object input = cell.at("input").get<picojson::object>();
//            if (type == "AND" || type == "NAND" || type == "XOR" ||
//                type == "XNOR" || type == "NOR" || type == "ANDNOT" ||
//                type == "OR" || type == "ORNOT") {
//                int A = static_cast<int>(input.at("A").get<double>());
//                int B = static_cast<int>(input.at("B").get<double>());
//                connectIds(A, id);
//                connectIds(B, id);
//            }
//            else if (type == "DFFP" || type == "RAM") {
//                int D = static_cast<int>(input.at("D").get<double>());
//                connectIds(D, id);
//            }
//            else if (type == "NOT") {
//                int A = static_cast<int>(input.at("A").get<double>());
//                connectIds(A, id);
//            }
//            else if (type == "MUX") {
//                int A = static_cast<int>(input.at("A").get<double>());
//                int B = static_cast<int>(input.at("B").get<double>());
//                int S = static_cast<int>(input.at("S").get<double>());
//                connectIds(A, id);
//                connectIds(B, id);
//                connectIds(S, id);
//            }
//            else {
//                error::die("Invalid JSON of network. Invalid type: ", type);
//            }
//        }
//    }
//};
//
//}  // namespace

void readIyokanL1JSONNetwork(std::istream& is, NetworkBuilder& nb)
{
    //    IyokanL1JSONReader::read(nb, is);
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
