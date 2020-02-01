#ifndef VIRTUALSECUREPLATFORM_IYOKAN_MAIN_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_MAIN_HPP

/*
                  DepNode --references--> dependent DepNode s
                     |
                    has
                     |
                     v
     Task --is a--> TaskBase
      |
     references
      |
      v
    input Task s
*/

#include <algorithm>
#include <cassert>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <ThreadPool.h>

// Forward declarations
template <class WorkerInfo>
class DepNode;
template <class WorkerInfo>
class NetworkBuilderBase;
struct NodeLabel;
template <class WorkerInfo>
class ReadyQueue;
template <class WorkerInfo>
class TaskNetwork;
class ProgressGraphMaker;

namespace detail {
template <class... Args>
std::string fok(Args... args)
{
    std::stringstream ss;
    (ss << ... << args);
    return ss.str();
}
}  // namespace detail

template <class WorkerInfo>
class TaskBase {
    friend void NetworkBuilderBase<WorkerInfo>::addTask(
        NodeLabel, int, std::shared_ptr<TaskBase<WorkerInfo>>);

private:
    std::weak_ptr<DepNode<WorkerInfo>> depnode_;

public:
    std::shared_ptr<DepNode<WorkerInfo>> depnode() const
    {
        return depnode_.lock();
    }

    virtual size_t getInputSize() const = 0;
    virtual bool isValid() = 0;
    virtual void notifyOneInputReady() = 0;
    virtual bool areInputsReady() const = 0;
    virtual void startAsync(WorkerInfo) = 0;
    virtual bool hasFinished() const = 0;
    virtual void tick() = 0;  // Reset for next process.
};

template <class InType, class OutType, class WorkerInfo>
class Task : public TaskBase<WorkerInfo> {
private:
    size_t numReadyInputs_;
    std::shared_ptr<OutType> output_;

    // Use weak_ptr here in order to avoid circular references.
    std::vector<std::weak_ptr<const InType>> inputs_;

protected:
    const InType &input(size_t index) const
    {
        std::shared_ptr<const InType> in = inputs_.at(index).lock();
        assert(in);
        return *in;
    }

    OutType &output()
    {
        return *output_;
    }

    const OutType &output() const
    {
        return *output_;
    }

    virtual void startAsyncImpl(WorkerInfo wi) = 0;

public:
    Task(size_t expectedNumInputs)
        : numReadyInputs_(0),
          output_(std::make_shared<OutType>()),
          inputs_(expectedNumInputs)
    {
    }

    virtual ~Task()
    {
    }

    size_t getInputSize() const override
    {
        return inputs_.size();
    }

    void acceptOneMoreInput()
    {
        inputs_.resize(inputs_.size() + 1);
    }

    void addInputPtr(const std::shared_ptr<const InType> &input)
    {
        auto it = std::find_if(inputs_.begin(), inputs_.end(),
                               [](auto &&in) { return in.use_count() == 0; });
        assert(it != inputs_.end());
        *it = input;
    }

    std::shared_ptr<const OutType> getOutputPtr() const
    {
        return output_;
    }

    virtual bool isValid() override
    {
        return this->depnode() &&
               std::all_of(inputs_.begin(), inputs_.end(),
                           [](auto &&in) { return in.use_count() != 0; });
    }

    virtual void tick() override
    {
        numReadyInputs_ = 0;
    }

    virtual void notifyOneInputReady() override
    {
        numReadyInputs_++;
        assert(numReadyInputs_ <= inputs_.size());
    }

    virtual bool areInputsReady() const override
    {
        return numReadyInputs_ == inputs_.size();
    }

    void startAsync(WorkerInfo wi) override
    {
        startAsyncImpl(std::move(wi));
    }
};

struct NodeLabel {
    int id;
    std::string kind, desc;
};

template <class WorkerInfo>
class DepNode : public std::enable_shared_from_this<DepNode<WorkerInfo>> {
    friend TaskNetwork<WorkerInfo>;
    friend void ReadyQueue<WorkerInfo>::push(
        const std::shared_ptr<DepNode<WorkerInfo>> &);

private:
    bool hasQueued_;
    int priority_;
    std::shared_ptr<TaskBase<WorkerInfo>> task_;

    // Use weak_ptr here in order to avoid circular references.
    std::vector<std::weak_ptr<DepNode>> dependents_;

    const NodeLabel label_;

public:
    DepNode(int priority, std::shared_ptr<TaskBase<WorkerInfo>> task,
            NodeLabel label)
        : hasQueued_(false),
          priority_(priority),
          task_(std::move(task)),
          label_(std::move(label))
    {
    }

    bool hasQueued() const
    {
        return hasQueued_;
    }

    int priority() const
    {
        return priority_;
    }

    std::shared_ptr<TaskBase<WorkerInfo>> task()
    {
        return task_;
    }

    void addDependent(const std::shared_ptr<DepNode> &dep)
    {
        dependents_.push_back(dep);
    }

    void start(WorkerInfo wi)
    {
        task_->startAsync(std::move(wi));
    }

    void start(WorkerInfo wi, ProgressGraphMaker &graph);

    bool hasFinished() const
    {
        return task_->hasFinished();
    }

    void propagate(ReadyQueue<WorkerInfo> &readyQueue);
    void propagate(ReadyQueue<WorkerInfo> &readyQueue,
                   ProgressGraphMaker &graph);

    void tick()
    {
        hasQueued_ = false;
        task_->tick();
    }
};

template <class WorkerInfo>
class ReadyQueue {
private:
    std::priority_queue<std::pair<int, std::shared_ptr<DepNode<WorkerInfo>>>>
        queue_;

public:
    bool empty() const
    {
        return queue_.empty();
    }

    std::shared_ptr<DepNode<WorkerInfo>> pop()
    {
        auto [priority, depnode] = std::move(queue_.top());
        queue_.pop();
        return depnode;
    }

    void push(const std::shared_ptr<DepNode<WorkerInfo>> &depnode)
    {
        queue_.emplace(depnode->priority(), depnode);
        depnode->hasQueued_ = true;
    }
};

class ProgressGraphMaker {
private:
    struct Node {
        NodeLabel label;
        int index;
    };

    struct Edge {
        int from, to;
        int index;
    };

    std::unordered_map<int, Node> nodes_;
    std::vector<Edge> edges_;

    int numStartedNodes_, numNotifiedEdges_;

private:
    Node &node(const NodeLabel &label)
    {
        auto [it, emplaced] = nodes_.try_emplace(label.id, Node{label, -1});
        return it->second;
    }

public:
    ProgressGraphMaker() : numStartedNodes_(0), numNotifiedEdges_(0)
    {
    }

    void startNode(const NodeLabel &label)
    {
        node(label).index = numStartedNodes_++;
    }

    void notify(const NodeLabel &from, const NodeLabel &to)
    {
        edges_.push_back(Edge{from.id, to.id, numNotifiedEdges_++});
    }

    void dumpDOT(std::ostream &os) const
    {
        os << "digraph progress_graph_maker {" << std::endl;
        os << "node [ shape = record ];" << std::endl;
        for (auto &&[id, node] : nodes_) {
            os << "n" << id << " [label = \"{" << node.label.kind;
            if (!node.label.desc.empty())
                os << "|" << node.label.desc;
            os << "}\"];" << std::endl;
        }
        os << std::endl;
        for (auto &&edge : edges_) {
            os << "n" << edge.from << " -> "
               << "n" << edge.to << " [label = \"" << edge.index << "\"];"
               << std::endl;
        }
        os << "}" << std::endl;
    }
};

template <class WorkerInfo>
void DepNode<WorkerInfo>::start(WorkerInfo wi, ProgressGraphMaker &graph)
{
    task_->startAsync(std::move(wi));
    graph.startNode(label_);
}

template <class WorkerInfo>
void DepNode<WorkerInfo>::propagate(ReadyQueue<WorkerInfo> &readyQueue)
{
    assert(task_->hasFinished());

    for (auto &&dep_weak : dependents_) {
        auto dep = dep_weak.lock();
        auto &&task = dep->task_;

        task->notifyOneInputReady();
        if (!dep->hasQueued() && task->areInputsReady())
            readyQueue.push(dep);
    }
}

template <class WorkerInfo>
void DepNode<WorkerInfo>::propagate(ReadyQueue<WorkerInfo> &readyQueue,
                                    ProgressGraphMaker &graph)
{
    propagate(readyQueue);

    for (auto &&dep_weak : dependents_) {
        auto dep = dep_weak.lock();
        graph.notify(label_, dep->label_);
    }
}

template <class WorkerInfo>
class Worker {
private:
    ReadyQueue<WorkerInfo> &readyQueue_;
    size_t &numFinishedTargets_;
    std::shared_ptr<DepNode<WorkerInfo>> target_;
    std::shared_ptr<ProgressGraphMaker> graph_;

public:
    Worker(ReadyQueue<WorkerInfo> &readyQueue, size_t &numFinishedTargets,
           std::shared_ptr<ProgressGraphMaker> graph)
        : readyQueue_(readyQueue),
          numFinishedTargets_(numFinishedTargets),
          target_(nullptr),
          graph_(std::move(graph))
    {
    }

    virtual ~Worker()
    {
    }

    void update()
    {
        if (target_ == nullptr && !readyQueue_.empty()) {
            // Found a task to tackle.
            target_ = readyQueue_.pop();
            assert(target_ != nullptr);
            if (graph_)
                target_->start(getWorkerInfo(), *graph_);
            else
                target_->start(getWorkerInfo());
        }

        if (target_ != nullptr) {
            if (target_->hasFinished()) {
                if (graph_)
                    target_->propagate(readyQueue_, *graph_);
                else
                    target_->propagate(readyQueue_);
                target_ = nullptr;
                numFinishedTargets_++;
            }
        }
    }

    bool isWorking() const
    {
        return target_ != nullptr;
    }

protected:
    virtual WorkerInfo getWorkerInfo() = 0;
};

template <class WorkerInfo>
class TaskNetwork {
private:
    std::unordered_map<int, std::shared_ptr<DepNode<WorkerInfo>>> id2node_;
    std::map<std::tuple<std::string, std::string, int>,
             std::shared_ptr<TaskBase<WorkerInfo>>>
        namedMems_;

private:
    bool verifyInputSizeExpected() const
    {
        std::unordered_map<std::shared_ptr<DepNode<WorkerInfo>>,
                           std::set<std::shared_ptr<DepNode<WorkerInfo>>>>
            sources;
        for (auto &&[id, depnode] : id2node_)
            for (auto &&dependent : depnode->dependents_)
                sources[dependent.lock()].insert(depnode);

        for (auto &&[dst, srcs] : sources)
            if (dst->task()->getInputSize() != srcs.size())
                return false;

        return true;
    }

public:
    TaskNetwork(
        std::unordered_map<int, std::shared_ptr<DepNode<WorkerInfo>>> id2node,
        std::map<std::tuple<std::string, std::string, int>,
                 std::shared_ptr<TaskBase<WorkerInfo>>>
            namedMems)
        : id2node_(std::move(id2node)), namedMems_(namedMems)
    {
    }

    TaskNetwork(NetworkBuilderBase<WorkerInfo> &&builder);

    size_t numNodes() const
    {
        return id2node_.size();
    }

    ReadyQueue<WorkerInfo> getReadyQueue() const
    {
        ReadyQueue<WorkerInfo> queue;
        for (auto &&[id, node] : id2node_)
            if (node->task()->areInputsReady())
                queue.push(node);
        return queue;
    }

    std::shared_ptr<DepNode<WorkerInfo>> &node(int id)
    {
        return id2node_[id];
    }

    template <class T>
    std::shared_ptr<T> get(const std::string &kind, const std::string &portName,
                           int portBit)
    {
        auto it = namedMems_.find(std::make_tuple(kind, portName, portBit));
        assert(it != namedMems_.end());
        return std::dynamic_pointer_cast<T>(it->second);
    }

    void tick()
    {
        for (auto &&[key, node] : id2node_)
            node->tick();
    }

    bool isValid()
    {
        for (auto &&[key, node] : id2node_)
            if (!node->task()->isValid())
                return false;

        if (!verifyInputSizeExpected())
            return false;

        return true;
    }

    template <class T>
    TaskNetwork<WorkerInfo> merge(
        const TaskNetwork<WorkerInfo> &rhs,
        const std::vector<std::tuple<std::string, int, std::string, int>>
            &lhs2rhs,
        const std::vector<std::tuple<std::string, int, std::string, int>>
            &rhs2lhs)
    {
        TaskNetwork<WorkerInfo> ret{*this};

        for (auto &&item : rhs.id2node_)
            ret.id2node_.insert(item);
        for (auto &&item : rhs.namedMems_)
            ret.namedMems_.insert(item);

        for (auto &&[lPortName, lPortBit, rPortName, rPortBit] : lhs2rhs) {
            /*
               ------------+  in           out  +-----------
               LHS OUTPUT  |--> connects to >---|  RHS INPUT
               ------------+                    +-----------
            */
            auto inkey = std::make_tuple("output", lPortName, lPortBit);
            auto outkey = std::make_tuple("input", rPortName, rPortBit);
            auto in = std::dynamic_pointer_cast<Task<T, T, WorkerInfo>>(
                namedMems_.at(inkey));
            auto out = std::dynamic_pointer_cast<Task<T, T, WorkerInfo>>(
                rhs.namedMems_.at(outkey));
            assert(in);
            assert(out);

            ret.namedMems_.erase(inkey);
            ret.namedMems_.erase(outkey);
            out->acceptOneMoreInput();
            NetworkBuilderBase<WorkerInfo>::connectTasks(in, out);
        }

        for (auto &&[rPortName, rPortBit, lPortName, lPortBit] : rhs2lhs) {
            /*
               ------------+  in           out  +-----------
               RHS OUTPUT  |--> connects to >---|  LHS INPUT
               ------------+                    +-----------
            */
            auto inkey = std::make_tuple("output", rPortName, rPortBit);
            auto outkey = std::make_tuple("input", lPortName, lPortBit);
            auto in = std::dynamic_pointer_cast<Task<T, T, WorkerInfo>>(
                rhs.namedMems_.at(inkey));
            auto out = std::dynamic_pointer_cast<Task<T, T, WorkerInfo>>(
                namedMems_.at(outkey));

            ret.namedMems_.erase(inkey);
            ret.namedMems_.erase(outkey);
            out->acceptOneMoreInput();
            NetworkBuilderBase<WorkerInfo>::connectTasks(in, out);
        }

        return ret;
    }
};

template <class WorkerInfo>
class NetworkBuilderBase {
    friend TaskNetwork<WorkerInfo>;

public:
    using NetworkType = TaskNetwork<WorkerInfo>;

private:
    std::unordered_map<int, std::shared_ptr<DepNode<WorkerInfo>>> id2node_;

    std::map<std::tuple<std::string, std::string, int>,
             std::shared_ptr<TaskBase<WorkerInfo>>>
        namedMems_;

protected:
    std::shared_ptr<DepNode<WorkerInfo>> node(int index)
    {
        return id2node_.at(index);
    }

public:
    void addTask(NodeLabel label, int priority,
                 std::shared_ptr<TaskBase<WorkerInfo>> task)
    {
        auto depnode =
            std::make_shared<DepNode<WorkerInfo>>(priority, task, label);
        task->depnode_ = depnode;
        auto [it, inserted] = id2node_.emplace(label.id, depnode);
        assert(inserted);
    }

    void registerTask(const std::string &kind, const std::string &portName,
                      int portBit,
                      const std::shared_ptr<TaskBase<WorkerInfo>> &task)
    {
        namedMems_.emplace(std::make_tuple(kind, portName, portBit), task);
    }

    template <class T, class... Args>
    std::shared_ptr<T> addINPUT(int id, int priority,
                                const std::string &portName, int portBit,
                                Args &&... args)
    {
        NodeLabel label{id, "INPUT", detail::fok(portName, "[", portBit, "]")};
        auto task = std::make_shared<T>(std::forward<Args>(args)...);
        addTask(label, priority, task);
        registerTask("input", portName, portBit, task);
        return task;
    }

    template <class T, class... Args>
    std::shared_ptr<T> addOUTPUT(int id, int priority,
                                 const std::string &portName, int portBit,
                                 Args &&... args)
    {
        NodeLabel label{id, "OUTPUT", detail::fok(portName, "[", portBit, "]")};
        auto task = std::make_shared<T>(std::forward<Args>(args)...);
        addTask(label, priority, task);
        registerTask("output", portName, portBit, task);
        return task;
    }

    template <class T0, class T1>
    static void connectTasks(const std::shared_ptr<T0> &from,
                             const std::shared_ptr<T1> &to)
    {
        to->addInputPtr(from->getOutputPtr());
        from->depnode()->addDependent(to->depnode());
    }

    static int genid()
    {
        // FIXME: Assume that ids over (1 << 16) will not be passed by the user.
        static int id = (1 << 16);
        return id++;
    }
};

template <class TaskType, class TaskTypeMem, class TaskTypeDFF,
          class TaskTypeWIRE, class WorkerInfo>
class NetworkBuilder : public NetworkBuilderBase<WorkerInfo> {
public:
    using ParamTaskTypeWIRE = TaskTypeWIRE;
    using ParamTaskTypeMem = TaskTypeMem;

private:
    std::shared_ptr<TaskTypeDFF> addDFF(int id, int priority = 0)
    {
        auto task = std::make_shared<TaskTypeDFF>();
        this->addTask(NodeLabel{id, "DFF", ""}, priority, task);
        return task;
    }

    void addNamedDFF(int id, const std::string &kind,
                     const std::string &portName, int portBit, int priority)
    {
        this->registerTask(kind, portName, portBit, addDFF(id, priority));
    }

    void addNamedWIRE(bool inputNeeded, int id, const std::string &kind,
                      const std::string &portName, int portBit, int priority)
    {
        auto task = std::make_shared<TaskTypeWIRE>(inputNeeded);
        this->addTask(
            NodeLabel{id, "WIRE", detail::fok(portName, "[", portBit, "]")},
            priority, task);
        this->registerTask(kind, portName, portBit, task);
    }

public:
    void DFF(int id, int priority = 0)
    {
        addDFF(id, priority);
    }

    void ROM(int id, const std::string &portName, int portBit, int priority = 0)
    {
        addNamedWIRE(false, id, "rom", portName, portBit, priority);
    }

    void RAM(int id, const std::string &portName, int portBit, int priority = 0)
    {
        addNamedDFF(id, "ram", portName, portBit, priority);
    }

    void INPUT(int id, const std::string &portName, int portBit,
               int priority = 0)
    {
        this->template addINPUT<TaskTypeWIRE>(id, priority, portName, portBit,
                                              false);
    }

    void OUTPUT(int id, const std::string &portName, int portBit,
                int priority = 0)
    {
        this->template addOUTPUT<TaskTypeWIRE>(id, priority, portName, portBit,
                                               true);
    }

    void connect(int from, int to)
    {
        this->connectTasks(
            std::dynamic_pointer_cast<TaskType>(this->node(from)->task()),
            std::dynamic_pointer_cast<TaskType>(this->node(to)->task()));
    }

#define DEFINE_GATE(name)                                        \
protected:                                                       \
    virtual std::shared_ptr<TaskType> name##Impl() = 0;          \
                                                                 \
public:                                                          \
    void name(int id, int priority = 0)                          \
    {                                                            \
        auto task = name##Impl();                                \
        this->addTask(NodeLabel{id, #name, ""}, priority, task); \
    }
    DEFINE_GATE(AND);
    DEFINE_GATE(NAND);
    DEFINE_GATE(ANDNOT);
    DEFINE_GATE(OR);
    DEFINE_GATE(NOR);
    DEFINE_GATE(ORNOT);
    DEFINE_GATE(XOR);
    DEFINE_GATE(XNOR);
    DEFINE_GATE(MUX);
    DEFINE_GATE(NOT);
#undef DEFINE_GATE
};

template <class WorkerInfo>
TaskNetwork<WorkerInfo>::TaskNetwork(NetworkBuilderBase<WorkerInfo> &&builder)
    : id2node_(std::move(builder.id2node_)),
      namedMems_(std::move(builder.namedMems_))
{
}

#include <picojson.h>

// Read JSON from the stream, build a network, and return a tuple of inputs,
// outputs, and nodes.
template <class NetworkBuilder>
typename NetworkBuilder::NetworkType readNetworkFromJSON(std::istream &is)
{
    // Read the stream as picojson::value.
    picojson::value v;
    const std::string err = picojson::parse(v, is);
    if (!err.empty()) {
        throw std::runtime_error(err);
    }

    NetworkBuilder builder;
    readNetworkFromJSONImpl(builder, v);

    return typename NetworkBuilder::NetworkType{std::move(builder)};
}

template <class NetworkBuilder>
void readNetworkFromJSONImpl(NetworkBuilder &builder, picojson::value &v)
{
    picojson::object &obj = v.get<picojson::object>();
    picojson::array &cells = obj["cells"].get<picojson::array>();
    picojson::array &ports = obj["ports"].get<picojson::array>();
    for (const auto &e : ports) {  // vectorをrange-based-forでまわしている。
        picojson::object port = e.get<picojson::object>();
        std::string type = port.at("type").get<std::string>();
        int id = static_cast<int>(port.at("id").get<double>());
        std::string portName = port.at("portName").get<std::string>();
        int portBit = static_cast<int>(port.at("portBit").get<double>());
        int priority = static_cast<int>(port.at("priority").get<double>());
        if (type == "input") {
            builder.INPUT(id, portName, portBit, priority);
        }
        else if (type == "output") {
            builder.OUTPUT(id, portName, portBit, priority);
        }
    }
    for (const auto &e : cells) {  // vectorをrange-based-forでまわしている。
        picojson::object cell = e.get<picojson::object>();
        std::string type = cell.at("type").get<std::string>();
        int id = static_cast<int>(cell.at("id").get<double>());
        int priority = static_cast<int>(cell.at("priority").get<double>());
        if (type == "AND") {
            builder.AND(id, priority);
        }
        else if (type == "NAND") {
            builder.NAND(id, priority);
        }
        else if (type == "ANDNOT") {
            builder.ANDNOT(id, priority);
        }
        else if (type == "XOR") {
            builder.XOR(id, priority);
        }
        else if (type == "XNOR") {
            builder.XNOR(id, priority);
        }
        else if (type == "DFFP") {
            builder.DFF(id, priority);
        }
        else if (type == "NOT") {
            builder.NOT(id, priority);
        }
        else if (type == "NOR") {
            builder.NOR(id, priority);
        }
        else if (type == "OR") {
            builder.OR(id, priority);
        }
        else if (type == "ORNOT") {
            builder.ORNOT(id, priority);
        }
        else if (type == "MUX") {
            builder.MUX(id, priority);
        }
        else if (type == "ROM") {
            int romAddress =
                static_cast<int>(cell.at("romAddress").get<double>());
            int romBit = static_cast<int>(cell.at("romBit").get<double>());
            builder.ROM(id, std::to_string(romAddress), romBit, priority);
        }
        else if (type == "RAM") {
            int ramAddress =
                static_cast<int>(cell.at("ramAddress").get<double>());
            int ramBit = static_cast<int>(cell.at("ramBit").get<double>());
            builder.RAM(id, std::to_string(ramAddress), ramBit, priority);
        }
        else {
            throw std::runtime_error("Not implemented:" + type);
        }
    }
    for (const auto &e : ports) {  // vectorをrange-based-forでまわしている。
        picojson::object port = e.get<picojson::object>();
        std::string type = port.at("type").get<std::string>();
        std::string name = port.at("name").get<std::string>();
        int id = static_cast<int>(port.at("id").get<double>());
        picojson::array &bits = port.at("bits").get<picojson::array>();
        if (type == "input") {
            // nothing to do!
        }
        else if (type == "output") {
            for (const auto &b : bits) {
                int logic = static_cast<int>(b.get<double>());
                builder.connect(logic, id);
            }
        }
    }
    for (const auto &e : cells) {  // vectorをrange-based-forでまわしている。
        picojson::object cell = e.get<picojson::object>();
        std::string type = cell.at("type").get<std::string>();
        int id = static_cast<int>(cell.at("id").get<double>());
        picojson::object input = cell.at("input").get<picojson::object>();
        picojson::object output = cell.at("output").get<picojson::object>();
        if (type == "AND" || type == "NAND" || type == "XOR" ||
            type == "XNOR" || type == "NOR" || type == "ANDNOT" ||
            type == "OR" || type == "ORNOT") {
            int A = static_cast<int>(input.at("A").get<double>());
            int B = static_cast<int>(input.at("B").get<double>());
            // picojson::array &Y = output.at("Y").get<picojson::array>();
            builder.connect(A, id);
            builder.connect(B, id);
        }
        else if (type == "DFFP") {
            int D = static_cast<int>(input.at("D").get<double>());
            // picojson::array &Q = output.at("Q").get<picojson::array>();
            builder.connect(D, id);
        }
        else if (type == "NOT") {
            int A = static_cast<int>(input.at("A").get<double>());
            // picojson::array &Y = output.at("Y").get<picojson::array>();
            builder.connect(A, id);
        }
        else if (type == "MUX") {
            int A = static_cast<int>(input.at("A").get<double>());
            int B = static_cast<int>(input.at("B").get<double>());
            int S = static_cast<int>(input.at("S").get<double>());
            // picojson::array &Y = output.at("Y").get<picojson::array>();
            builder.connect(A, id);
            builder.connect(B, id);
            builder.connect(S, id);
        }
        else if (type == "ROM") {
            // picojson::array &Q = output.at("Q").get<picojson::array>();
            // Nothing to do!
        }
        else if (type == "RAM") {
            int D = static_cast<int>(input.at("D").get<double>());
            // picojson::array &Q = output.at("Q").get<picojson::array>();
            builder.connect(D, id);
        }
        else {
            throw std::runtime_error("Not executed");
        }
    }
}

class AsyncThread {
private:
    // The C++17 keyword 'inline' is necessary here to avoid duplicate
    // definition of 'pool_'. Thanks to:
    // https://stackoverflow.com/questions/11709859/how-to-have-static-data-members-in-a-header-only-library
    static inline std::shared_ptr<ThreadPool> pool_;
    std::atomic_bool finished_;

public:
    AsyncThread() : finished_(false)
    {
        if (!pool_) {
            const int DEFAULT_NUM_THREADS = 10;
            pool_ = std::make_shared<ThreadPool>(DEFAULT_NUM_THREADS);
        }
    }

    template <class Func>
    AsyncThread &operator=(Func func)
    {
        finished_ = false;
        pool_->enqueue([this, func]() {
            func();
            finished_ = true;
        });
        return *this;
    }

    bool hasFinished() const
    {
        return finished_;
    }

    static void setNumThreads(int newNumThreads)
    {
        pool_ = std::make_shared<ThreadPool>(newNumThreads);
    }
};

template <class InType, class OutType, class WorkerInfo>
class TaskMem : public Task<InType, OutType, WorkerInfo> {
public:
    TaskMem(int numInputs) : Task<InType, OutType, WorkerInfo>(numInputs)
    {
    }

    void set(const OutType &newval)
    {
        this->output() = newval;
    }

    const OutType &get() const
    {
        return this->output();
    }
};

template <class InType, class OutType, class WorkerInfo>
class TaskDFF : public TaskMem<InType, OutType, WorkerInfo> {
protected:
    void startAsyncImpl(WorkerInfo) override
    {
    }

public:
    TaskDFF() : TaskMem<InType, OutType, WorkerInfo>(1)
    {
    }

    bool areInputsReady() const override
    {
        // Since areInputsReady() is called after calling of tick(), the
        // input should already be in val_.
        return true;
    }

    void tick() override
    {
        TaskMem<InType, OutType, WorkerInfo>::tick();

        // We HAVE TO prefix 'this->' here. Thanks to:
        // https://stackoverflow.com/questions/4643074/why-do-i-have-to-access-template-base-class-members-through-the-this-pointer
        this->output() = this->input(0);
    }

    bool hasFinished() const override
    {
        // Since hasFinished() is called after calling of tick(), the
        // input should already be in val_.
        return true;
    }
};

template <class InType, class OutType, class WorkerInfo>
class TaskAsync : public Task<InType, OutType, WorkerInfo> {
private:
    AsyncThread thr_;

private:
    virtual void startSync(WorkerInfo wi) = 0;

    void startAsyncImpl(WorkerInfo wi) override
    {
        thr_ = [this, wi] { startSync(wi); };
    }

public:
    TaskAsync(size_t expectedNumInputs)
        : Task<InType, OutType, WorkerInfo>(expectedNumInputs)
    {
    }

    bool hasFinished() const override
    {
        return thr_.hasFinished();
    }
};

struct Options {
    // Processor's logic file.
    std::string logicFile;
    // Port corresponding between core and ROM if enabled.
    std::vector<std::string> romPorts;
    // Misc.
    std::string inputFile, outputFile;
    int numWorkers = std::thread::hardware_concurrency(), numCycles = 0;
};

template <class Func>
int processCycles(int numCycles, Func func)
{
    for (int i = 0; i < numCycles; i++) {
        std::cout << "#" << (i + 1) << std::flush;

        auto begin = std::chrono::high_resolution_clock::now();
        bool shouldBreak = func();
        auto end = std::chrono::high_resolution_clock::now();

        std::cout << "\tdone. ("
                  << std::chrono::duration_cast<std::chrono::microseconds>(
                         end - begin)
                         .count()
                  << " us)" << std::endl;

        if (shouldBreak) {
            std::cout << "break." << std::endl;
            return i + 1;
        }
    }

    return numCycles;
}

#endif
