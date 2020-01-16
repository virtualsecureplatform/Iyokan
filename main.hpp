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
#include <iostream>
#include <map>
#include <memory>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <vector>

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
public:
    virtual bool isValid() = 0;
    virtual void notifyOneInputReady() = 0;
    virtual bool areInputsReady() const = 0;
    virtual void startAsync(WorkerInfo) = 0;
    virtual bool hasStarted() const = 0;
    virtual bool hasFinished() const = 0;
    virtual void tick() = 0;  // Reset for next process.
};

template <class InType, class OutType, class WorkerInfo>
class Task : public TaskBase<WorkerInfo> {
private:
    bool hasStarted_;
    size_t numReadyInputs_;
    std::shared_ptr<OutType> output_;

    // Use weak_ptr here in order to avoid circular references.
    std::vector<std::weak_ptr<const InType>> inputs_;

protected:
    size_t inputSize() const
    {
        return inputs_.size();
    }

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
        : hasStarted_(false),
          numReadyInputs_(0),
          output_(std::make_shared<OutType>()),
          inputs_(expectedNumInputs)
    {
    }

    virtual ~Task()
    {
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
        return std::all_of(inputs_.begin(), inputs_.end(),
                           [](auto &&in) { return in.use_count() != 0; });
    }

    virtual void tick() override
    {
        hasStarted_ = false;
        numReadyInputs_ = 0;
        // FIXME: Should inputs_ be reset?
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

    bool hasStarted() const override
    {
        return hasStarted_;
    }

    void startAsync(WorkerInfo wi) override
    {
        startAsyncImpl(std::move(wi));
        hasStarted_ = true;
    }
};

template <class WorkerInfo>
class ReadyQueue;

class ProgressGraphMaker;

struct NodeLabel {
    int id;
    std::string kind, desc;
};

template <class WorkerInfo>
class DepNode {
public:
private:
    int priority_;
    std::shared_ptr<TaskBase<WorkerInfo>> task_;

    // Use weak_ptr here in order to avoid circular references.
    std::vector<std::weak_ptr<DepNode>> dependents_;

    const NodeLabel label_;

public:
    DepNode(int priority, std::shared_ptr<TaskBase<WorkerInfo>> task,
            NodeLabel label)
        : priority_(priority), task_(task), label_(std::move(label))
    {
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
            os << "n" << id << " [label = \"{" << node.label.kind << "|"
               << node.label.desc << "}\"];" << std::endl;
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
        if (!task->hasStarted() && task->areInputsReady())
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

template <class TaskType, class TaskTypeMem, class TaskTypeDFF,
          class TaskTypeWIRE, class WorkerInfo>
class NetworkBuilder;

template <class TaskTypeMem, class WorkerInfo>
class TaskNetwork {
public:
    struct MemNode {
        std::shared_ptr<TaskTypeMem> task;
        std::shared_ptr<DepNode<WorkerInfo>> depnode;
    };

private:
    std::unordered_map<int, std::shared_ptr<DepNode<WorkerInfo>>> id2node_;
    std::map<std::tuple<std::string, std::string, int>, MemNode> namedMems_;

public:
    template <class TaskType, class TaskTypeDFF, class TaskTypeWIRE>
    TaskNetwork(NetworkBuilder<TaskType, TaskTypeMem, TaskTypeDFF, TaskTypeWIRE,
                               WorkerInfo> &&builder);

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

    MemNode &mem(const std::string &kind, const std::string &portName,
                 int portBit)
    {
        auto it = namedMems_.find(std::make_tuple(kind, portName, portBit));
        assert(it != namedMems_.end());
        return it->second;
    }

    MemNode &input(const std::string &portName, int portBit)
    {
        return mem("input", portName, portBit);
    }

    MemNode &output(const std::string &portName, int portBit)
    {
        return mem("output", portName, portBit);
    }

    void tick()
    {
        for (auto &&[key, node] : id2node_)
            node->task()->tick();
    }

    bool isValid()
    {
        for (auto &&[key, node] : id2node_)
            if (!node->task()->isValid())
                return false;
        return true;
    }
};

template <class TaskType, class TaskTypeMem, class TaskTypeDFF,
          class TaskTypeWIRE, class WorkerInfo>
class NetworkBuilder {
    friend TaskNetwork<TaskTypeMem, WorkerInfo>;

public:
    using ParamTaskTypeMem = TaskTypeMem;
    using NetworkType = TaskNetwork<TaskTypeMem, WorkerInfo>;

private:
    std::unordered_map<int, std::shared_ptr<DepNode<WorkerInfo>>> id2node_;

    std::map<std::tuple<std::string, std::string, int>,
             typename NetworkType::MemNode>
        namedMems_;

private:
    typename NetworkType::MemNode addDFF(int id, int priority = 0)
    {
        auto task = std::make_shared<TaskTypeDFF>();
        auto depnode = std::make_shared<DepNode<WorkerInfo>>(
            priority, task, NodeLabel{id, "DFF", ""});
        id2node_.emplace(id, depnode);
        return typename NetworkType::MemNode{task, depnode};
    }

    void addNamedDFF(int id, const std::string &kind,
                     const std::string &portName, int portBit, int priority)
    {
        namedMems_.emplace(std::make_tuple(kind, portName, portBit),
                           addDFF(id, priority));
    }

    void addNamedWIRE(bool inputNeeded, int id, const std::string &kind,
                      const std::string &portName, int portBit, int priority)
    {
        auto task = std::make_shared<TaskTypeWIRE>(inputNeeded);
        auto depnode = std::make_shared<DepNode<WorkerInfo>>(
            priority, task,
            NodeLabel{id, "WIRE", detail::fok(portName, " ", portBit)});
        id2node_.emplace(id, depnode);
        namedMems_.emplace(std::make_tuple(kind, portName, portBit),
                           typename NetworkType::MemNode{task, depnode});
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
        addNamedWIRE(false, id, "input", portName, portBit, priority);
    }

    void OUTPUT(int id, const std::string &portName, int portBit,
                int priority = 0)
    {
        addNamedWIRE(true, id, "output", portName, portBit, priority);
    }

    void connect(int from, int to)
    {
        auto fromIt = id2node_.find(from);
        assert(fromIt != id2node_.end());
        auto toIt = id2node_.find(to);
        assert(toIt != id2node_.end());

        auto toTask = std::dynamic_pointer_cast<TaskType>(toIt->second->task());
        assert(toTask);
        auto fromTask =
            std::dynamic_pointer_cast<TaskType>(fromIt->second->task());
        assert(fromTask);

        toTask->addInputPtr(fromTask->getOutputPtr());
        fromIt->second->addDependent(toIt->second);
    }

#define DEFINE_GATE(name)                                     \
protected:                                                    \
    virtual std::shared_ptr<TaskType> name##Impl() = 0;       \
                                                              \
public:                                                       \
    void name(int id, int priority = 0)                       \
    {                                                         \
        auto task = name##Impl();                             \
        auto depnode = std::make_shared<DepNode<WorkerInfo>>( \
            priority, task, NodeLabel{id, #name, ""});        \
        id2node_.emplace(id, depnode);                        \
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

template <class TaskTypeMem, class WorkerInfo>
template <class TaskType, class TaskTypeDFF, class TaskTypeWIRE>
TaskNetwork<TaskTypeMem, WorkerInfo>::TaskNetwork(
    NetworkBuilder<TaskType, TaskTypeMem, TaskTypeDFF, TaskTypeWIRE, WorkerInfo>
        &&builder)
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
    // FIXME: Make use of attribute `priority`
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

#include <ThreadPool.h>

#include <future>

class AsyncThread {
private:
    static ThreadPool pool_;
    std::atomic_bool finished_;

public:
    AsyncThread() : finished_(false)
    {
    }

    template <class Func>
    AsyncThread &operator=(Func func)
    {
        finished_ = false;
        pool_.enqueue([this, func]() {
            func();
            finished_ = true;
        });
        return *this;
    }

    bool hasFinished() const
    {
        return finished_;
    }
};
ThreadPool AsyncThread::pool_{10};

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

#endif
