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
#include <filesystem>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <queue>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <variant>
#include <vector>

#include <ThreadPool.h>
#include <toml.hpp>

#include "error.hpp"
#include "utility.hpp"

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
inline int genid()
{
    // FIXME: Assume that ids over (1 << 16) will not be passed by the user.
    static int id = (1 << 16);
    return id++;
}

}  // namespace detail

template <class WorkerInfo>
class TaskBase {
    friend void DepNode<WorkerInfo>::prepareTaskBase();

public:
    using ParamWorkerInfo = WorkerInfo;

private:
    std::weak_ptr<DepNode<WorkerInfo>> depnode_;

public:
    virtual ~TaskBase()
    {
    }

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

// TaskLabel is used to find a certain task. Not all tasks need to have
// TaskLabel, because many tasks don't have to be modified (e.g. set initial
// value) once networks are organized.
struct TaskLabel {
    std::string kind, portName;
    int portBit;

    bool operator==(const TaskLabel &rhs) const
    {
        return kind == rhs.kind && portName == rhs.portName &&
               portBit == rhs.portBit;
    }

    bool operator<(const TaskLabel &rhs) const
    {
        return std::make_tuple(kind, portName, portBit) <
               std::make_tuple(rhs.kind, rhs.portName, rhs.portBit);
    }
};

// NodeLabel is used to identify a DepNode in debugging/profiling.
struct NodeLabel {
    int id;
    std::string kind, desc;
};

class DepNodeBase {
private:
    int priority_;
    NodeLabel label_;

public:
    DepNodeBase(int priority, NodeLabel label)
        : priority_(priority), label_(std::move(label))
    {
    }

    int priority() const noexcept
    {
        return priority_;
    }

    void priority(int newPri) noexcept
    {
        priority_ = newPri;
    }

    const NodeLabel &label() const noexcept
    {
        return label_;
    }
};

namespace graph {
struct Node {
    NodeLabel label;
    std::set<int> parents, children;
    bool hasNoInputsToWaitFor;
};
using NodePtr = std::shared_ptr<graph::Node>;

inline std::unordered_map<int, int> doTopologicalSort(
    const std::unordered_map<int, graph::NodePtr> &id2node)
{
    // Make a map from id to the number of ready parents of the node
    std::unordered_map<NodePtr, int> numReadyParents;
    for (auto &&[id, node] : id2node)
        numReadyParents[node] = 0;

    // Make the initial queue for sorting
    std::queue<NodePtr> que;
    for (auto &&[id, node] : id2node)
        if (node->hasNoInputsToWaitFor)
            que.push(node);

    // Do topological sort
    std::unordered_map<NodePtr, int> node2index;
    while (!que.empty()) {
        auto node = que.front();
        que.pop();

        // Get the index for node
        int index = -1;
        if (!node->hasNoInputsToWaitFor) {
            for (auto &&parentId : node->parents) {
                NodePtr parent = id2node.at(parentId);
                index = std::max(index, node2index.at(parent));
            }
        }
        node2index[node] = index + 1;

        for (auto &&childId : node->children) {
            NodePtr child = id2node.at(childId);
            if (child->hasNoInputsToWaitFor)  // false parent-child relationship
                continue;
            numReadyParents.at(child)++;
            assert(child->parents.size() >= numReadyParents.at(child));
            if (child->parents.size() == numReadyParents.at(child))
                que.push(child);
        }
    }
    assert(id2node.size() == node2index.size());

    std::unordered_map<int, int> id2index;
    for (auto &&[node, index] : node2index)
        id2index[node->label.id] = index;

    return id2index;
}

}  // namespace graph

class GraphVisitor {
private:
    std::stack<graph::NodePtr> nodeStack_;
    std::unordered_map<int, graph::NodePtr> id2node_;

private:
    template <class WorkerInfo>
    std::pair<graph::NodePtr, bool> depnode2node(
        const DepNode<WorkerInfo> &depnode)
    {
        auto it = id2node_.find(depnode.label().id);
        if (it != id2node_.end())
            return std::make_pair(it->second, false);

        auto node = std::make_shared<graph::Node>();
        assert(node);
        node->label = depnode.label();
        node->hasNoInputsToWaitFor = depnode.task()->areInputsReady();

        id2node_.emplace(depnode.label().id, node);

        return std::make_pair(node, true);
    }

protected:
    virtual void onStart(DepNodeBase &)
    {
    }

    virtual void onEnd()
    {
    }

public:
    GraphVisitor()
    {
    }

    virtual ~GraphVisitor()
    {
    }

    const std::unordered_map<int, graph::NodePtr> &getMap() const
    {
        return id2node_;
    }

    template <class WorkerInfo>
    bool start(DepNode<WorkerInfo> &curNode)
    {
        auto [node, isNew] = depnode2node(curNode);

        if (!nodeStack_.empty()) {
            graph::NodePtr parent = nodeStack_.top();
            parent->children.insert(node->label.id);
            node->parents.insert(parent->label.id);
        }

        if (!isNew)
            return false;

        onStart(curNode);

        nodeStack_.push(node);
        return true;
    }

    void end()
    {
        assert(!nodeStack_.empty());
        nodeStack_.pop();
        onEnd();
    }
};

class PrioritySetVisitor : public GraphVisitor {
private:
    std::unordered_map<int, int> id2index_;

public:
    PrioritySetVisitor(std::unordered_map<int, int> id2index)
        : id2index_(std::move(id2index))
    {
    }

private:
    void onStart(DepNodeBase &depnode) override
    {
        depnode.priority(id2index_.at(depnode.label().id));
    }

    void onEnd() override
    {
    }
};

template <class WorkerInfo>
class DepNode : public DepNodeBase,
                public std::enable_shared_from_this<DepNode<WorkerInfo>> {
    friend TaskNetwork<WorkerInfo>;
    friend void ReadyQueue<WorkerInfo>::push(
        const std::shared_ptr<DepNode<WorkerInfo>> &);

private:
    bool hasQueued_;
    std::shared_ptr<TaskBase<WorkerInfo>> task_;

    // Use weak_ptr here in order to avoid circular references.
    std::vector<std::weak_ptr<DepNode>> dependents_;

public:
    DepNode(int priority, std::shared_ptr<TaskBase<WorkerInfo>> task,
            NodeLabel label)
        : DepNodeBase(priority, std::move(label)),
          hasQueued_(false),
          task_(std::move(task))
    {
    }

    virtual ~DepNode()
    {
    }

    void prepareTaskBase()
    {
        assert(task_);
        // weak_from_this() must not be called in the constructor.
        task_->depnode_ = this->weak_from_this();
    }

    bool hasQueued() const
    {
        return hasQueued_;
    }

    std::shared_ptr<const TaskBase<WorkerInfo>> task() const
    {
        return task_;
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

    virtual void propagate(ReadyQueue<WorkerInfo> &readyQueue);
    void propagate(ReadyQueue<WorkerInfo> &readyQueue,
                   ProgressGraphMaker &graph);

    void tick()
    {
        hasQueued_ = false;
        task_->tick();
    }

    virtual void visit(GraphVisitor &visitor)
    {
        if (!visitor.start(*this))
            return;

        for (auto &&dep : dependents_) {
            auto next = dep.lock();
            assert(next);
            next->visit(visitor);
        }

        visitor.end();
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
        std::optional<std::chrono::high_resolution_clock::time_point>
            start = std::nullopt,
            end = std::nullopt;
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

    void reset()
    {
        nodes_.clear();
        edges_.clear();
        numStartedNodes_ = 0;
        numNotifiedEdges_ = 0;
    }

    void startNode(const NodeLabel &label)
    {
        auto &n = node(label);
        n.index = numStartedNodes_++;
        assert(!n.start && !n.end);
        n.start = std::chrono::high_resolution_clock::now();
    }

    void finishNode(const NodeLabel &label)
    {
        auto &n = node(label);
        assert(n.start && !n.end);
        n.end = std::chrono::high_resolution_clock::now();
    }

    void notify(const NodeLabel &from, const NodeLabel &to)
    {
        edges_.push_back(Edge{from.id, to.id, numNotifiedEdges_++});
    }

    void dumpTime(std::ostream &os) const
    {
        std::unordered_map<std::string, std::vector<Node>> kind2nodes;
        for (auto &&[id, node] : nodes_)
            kind2nodes[node.label.kind].push_back(node);
        for (auto &&[kind, nodes] : kind2nodes) {
            int64_t total = 0;
            for (auto &&node : nodes) {
                assert(node.start && node.end);
                total += std::chrono::duration_cast<std::chrono::milliseconds>(
                             *node.end - *node.start)
                             .count();
            }
            os << kind << "\t" << total << "\t" << nodes.size() << std::endl;
        }
    }

    void dumpCSV(std::ostream &os) const
    {
        using namespace std::chrono;

        for (auto &&[id, node] : nodes_) {
            os << "\""
               << duration_cast<nanoseconds>(
                      node.start.value().time_since_epoch())
                      .count()
               << "\"";
            os << ",\""
               << duration_cast<nanoseconds>(
                      node.end.value().time_since_epoch())
                      .count()
               << "\"";
            os << ",\"" << node.index << "\"";
            os << ",\"" << node.label.id << "\"";
            os << ",\"" << node.label.kind << "\"";
            os << ",\"" << node.label.desc << "\"";
            os << std::endl;
        }
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
    graph.startNode(label());
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
    graph.finishNode(label());

    propagate(readyQueue);

    for (auto &&dep_weak : dependents_) {
        auto dep = dep_weak.lock();
        graph.notify(label(), dep->label());
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
public:
    using Label2TaskMap =
        std::map<TaskLabel, std::shared_ptr<TaskBase<WorkerInfo>>>;

private:
    std::unordered_map<int, std::shared_ptr<DepNode<WorkerInfo>>> id2node_;
    Label2TaskMap namedMems_;

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
        Label2TaskMap namedMems)
        : id2node_(std::move(id2node)), namedMems_(namedMems)
    {
    }

    TaskNetwork(NetworkBuilderBase<WorkerInfo> &&builder);

    const Label2TaskMap &getNamedMems() const
    {
        return namedMems_;
    }

    size_t numNodes() const
    {
        return id2node_.size();
    }

    void pushReadyTasks(ReadyQueue<WorkerInfo> &queue) const
    {
        for (auto &&[id, node] : id2node_)
            if (node->task()->areInputsReady())
                queue.push(node);
    }

    std::shared_ptr<DepNode<WorkerInfo>> &node(int id)
    {
        return id2node_.at(id);
    }

    template <class T>
    std::shared_ptr<T> get_if(TaskLabel label)
    {
        auto it = namedMems_.find(label);
        if (it == namedMems_.end())
            return nullptr;
        return std::dynamic_pointer_cast<T>(it->second);
    }

    template <class T>
    std::shared_ptr<T> get(TaskLabel label)
    {
        auto ret = std::dynamic_pointer_cast<T>(namedMems_.at(label));
        assert(ret);
        return ret;
    }

    template <class T>
    std::shared_ptr<T> get(const std::string &kind, const std::string &portName,
                           int portBit)
    {
        return get<T>(TaskLabel{kind, portName, portBit});
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

    TaskNetwork<WorkerInfo> merge(const TaskNetwork<WorkerInfo> &rhs)
    {
        TaskNetwork<WorkerInfo> ret{*this};

        for (auto &&item : rhs.id2node_)
            ret.id2node_.insert(item);
        for (auto &&item : rhs.namedMems_)
            ret.namedMems_.insert(item);

        return ret;
    }

    template <class T>
    TaskNetwork<WorkerInfo> merge(
        const TaskNetwork<WorkerInfo> &rhs,
        const std::vector<std::tuple<std::string, int, std::string, int>>
            &lhs2rhs,
        const std::vector<std::tuple<std::string, int, std::string, int>>
            &rhs2lhs)
    {
        TaskNetwork<WorkerInfo> ret = merge(rhs);

        for (auto &&[lPortName, lPortBit, rPortName, rPortBit] : lhs2rhs) {
            /*
               ------------+  in           out  +-----------
               LHS OUTPUT  |--> connects to >---|  RHS INPUT
               ------------+                    +-----------
            */
            auto inkey = TaskLabel{"output", lPortName, lPortBit};
            auto outkey = TaskLabel{"input", rPortName, rPortBit};
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
            auto inkey = TaskLabel{"output", rPortName, rPortBit};
            auto outkey = TaskLabel{"input", lPortName, lPortBit};
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

    void visit(GraphVisitor &visitor)
    {
        for (auto &&[id, node] : id2node_)
            if (node->task()->areInputsReady())
                node->visit(visitor);
    }
};

template <class WorkerInfo>
class NetworkBuilderBase {
    friend TaskNetwork<WorkerInfo>;

public:
    using NetworkType = TaskNetwork<WorkerInfo>;

private:
    std::unordered_map<int, std::shared_ptr<DepNode<WorkerInfo>>> id2node_;

    typename TaskNetwork<WorkerInfo>::Label2TaskMap namedMems_;

protected:
    std::shared_ptr<DepNode<WorkerInfo>> node(int index)
    {
        return id2node_.at(index);
    }

public:
    template <class T>
    std::shared_ptr<T> getTask(const std::string &kind,
                               const std::string &portName, int portBit)
    {
        return std::dynamic_pointer_cast<T>(
            namedMems_.at(TaskLabel{kind, portName, portBit}));
    }

    void addTask(NodeLabel label, int priority,
                 std::shared_ptr<TaskBase<WorkerInfo>> task)
    {
        auto depnode =
            std::make_shared<DepNode<WorkerInfo>>(priority, task, label);
        depnode->prepareTaskBase();
        auto [it, inserted] = id2node_.emplace(label.id, depnode);
        assert(inserted);
    }

    template <class T, class... Args>
    std::shared_ptr<T> emplaceTask(NodeLabel label, int priority,
                                   Args &&... args)
    {
        auto task = std::make_shared<T>(std::forward<Args>(args)...);
        addTask(label, priority, task);
        return task;
    }

    void registerTask(const std::string &kind, const std::string &portName,
                      int portBit,
                      const std::shared_ptr<TaskBase<WorkerInfo>> &task)
    {
        auto [item, inserted] =
            namedMems_.emplace(TaskLabel{kind, portName, portBit}, task);
        assert(inserted);
    }

    template <class T, class... Args>
    std::shared_ptr<T> addINPUT(int id, int priority,
                                const std::string &portName, int portBit,
                                Args &&... args)
    {
        NodeLabel label{id, "INPUT", utility::fok(portName, "[", portBit, "]")};
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
        NodeLabel label{id, "OUTPUT",
                        utility::fok(portName, "[", portBit, "]")};
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
            NodeLabel{id, "WIRE", utility::fok(portName, "[", portBit, "]")},
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

template <class NetworkBuilder>
typename NetworkBuilder::NetworkType readNetworkFromJSON(std::istream &is)
{
    picojson::value v;
    const std::string err = picojson::parse(v, is);
    if (!err.empty())
        error::die("Invalid JSON of network: ", err);

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
    for (const auto &e : ports) {
        picojson::object port = e.get<picojson::object>();
        std::string type = port.at("type").get<std::string>();
        int id = static_cast<int>(port.at("id").get<double>());
        std::string portName = port.at("portName").get<std::string>();
        int portBit = static_cast<int>(port.at("portBit").get<double>());
        int priority = static_cast<int>(port.at("priority").get<double>());
        if (type == "input")
            builder.INPUT(id, portName, portBit, priority);
        else if (type == "output")
            builder.OUTPUT(id, portName, portBit, priority);
    }
    for (const auto &e : cells) {
        picojson::object cell = e.get<picojson::object>();
        std::string type = cell.at("type").get<std::string>();
        int id = static_cast<int>(cell.at("id").get<double>());
        int priority = static_cast<int>(cell.at("priority").get<double>());
        if (type == "AND")
            builder.AND(id, priority);
        else if (type == "NAND")
            builder.NAND(id, priority);
        else if (type == "ANDNOT")
            builder.ANDNOT(id, priority);
        else if (type == "XOR")
            builder.XOR(id, priority);
        else if (type == "XNOR")
            builder.XNOR(id, priority);
        else if (type == "DFFP")
            builder.DFF(id, priority);
        else if (type == "NOT")
            builder.NOT(id, priority);
        else if (type == "NOR")
            builder.NOR(id, priority);
        else if (type == "OR")
            builder.OR(id, priority);
        else if (type == "ORNOT")
            builder.ORNOT(id, priority);
        else if (type == "MUX")
            builder.MUX(id, priority);
        else
            error::die("Invalid JSON of network. Invalid type: ", type);
    }
    for (const auto &e : ports) {
        picojson::object port = e.get<picojson::object>();
        std::string type = port.at("type").get<std::string>();
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
    for (const auto &e : cells) {
        picojson::object cell = e.get<picojson::object>();
        std::string type = cell.at("type").get<std::string>();
        int id = static_cast<int>(cell.at("id").get<double>());
        picojson::object input = cell.at("input").get<picojson::object>();
        if (type == "AND" || type == "NAND" || type == "XOR" ||
            type == "XNOR" || type == "NOR" || type == "ANDNOT" ||
            type == "OR" || type == "ORNOT") {
            int A = static_cast<int>(input.at("A").get<double>());
            int B = static_cast<int>(input.at("B").get<double>());
            builder.connect(A, id);
            builder.connect(B, id);
        }
        else if (type == "DFFP") {
            int D = static_cast<int>(input.at("D").get<double>());
            builder.connect(D, id);
        }
        else if (type == "NOT") {
            int A = static_cast<int>(input.at("A").get<double>());
            builder.connect(A, id);
        }
        else if (type == "MUX") {
            int A = static_cast<int>(input.at("A").get<double>());
            int B = static_cast<int>(input.at("B").get<double>());
            int S = static_cast<int>(input.at("S").get<double>());
            builder.connect(A, id);
            builder.connect(B, id);
            builder.connect(S, id);
        }
        else {
            error::die("Invalid JSON of network. Invalid type: ", type);
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

template <class WorkerInfo>
class TaskBlackHole : public TaskBase<WorkerInfo> {
private:
    int numInputs_, numReadyInputs_;

public:
    TaskBlackHole(int numInputs) : numInputs_(numInputs), numReadyInputs_(0)
    {
    }

    size_t getInputSize() const override
    {
        return numInputs_;
    }

    bool isValid() override
    {
        return true;
    }

    void notifyOneInputReady() override
    {
        numReadyInputs_++;
        assert(numReadyInputs_ <= numInputs_);
    }

    bool areInputsReady() const override
    {
        return numReadyInputs_ == numInputs_;
    }

    void startAsync(WorkerInfo) override
    {
        // Nothing to do!
    }

    bool hasFinished() const override
    {
        return true;
    }

    void tick() override
    {
        numReadyInputs_ = 0;
    }
};

/*
                                    BridgeDepNode::propagate(ReadyQueue&):
                                             notify and push
                                         ++====================++
                                         ||                    ||
                    dependents_    (BridgeDepNode)             vv
    DepNode<WI0> ------------------> DepNode<WI0>         DepNode<WI1>
       |                                |                     |
       | task_                          | task_               | task_
       v                                v                     v
    Task<T0, T1, WI0>               TaskBlackHole      Task<T1, T2, WI1>
           ^                        <T1, T1, WI0>              |
           |                                                   |
           +---------------------------------------------------+
                                  inputs_
*/

template <class InWorkerInfo, class OutWorkerInfo>
class BridgeDepNode : public DepNode<InWorkerInfo> {
private:
    std::shared_ptr<DepNode<OutWorkerInfo>> src_;
    std::weak_ptr<ReadyQueue<OutWorkerInfo>> outReadyQueue_;

public:
    BridgeDepNode(std::shared_ptr<DepNode<OutWorkerInfo>> src)
        : DepNode<InWorkerInfo>(
              0, std::make_shared<TaskBlackHole<InWorkerInfo>>(1),
              NodeLabel{detail::genid(), "bridge", ""}),
          src_(src)
    {
    }

    void setReadyQueue(std::shared_ptr<ReadyQueue<OutWorkerInfo>> readyQueue)
    {
        outReadyQueue_ = readyQueue;
    }

    void propagate(ReadyQueue<InWorkerInfo> &) override
    {
        assert(src_);
        assert(!outReadyQueue_.expired());

        src_->task()->notifyOneInputReady();
        if (!src_->hasQueued() && src_->task()->areInputsReady()) {
            auto que = outReadyQueue_.lock();
            assert(que);
            que->push(src_);
        }
    }

    void visit(GraphVisitor &visitor) override
    {
        if (!visitor.start(*this))
            return;
        src_->visit(visitor);
        visitor.end();
    }
};

template <class T0, class T1>
std::shared_ptr<
    BridgeDepNode<typename T0::ParamWorkerInfo, typename T1::ParamWorkerInfo>>
connectWithBridge(std::shared_ptr<T0> lhs, std::shared_ptr<T1> rhs)
{
    rhs->addInputPtr(lhs->getOutputPtr());
    auto newrhs = std::make_shared<BridgeDepNode<typename T0::ParamWorkerInfo,
                                                 typename T1::ParamWorkerInfo>>(
        rhs->depnode());
    newrhs->prepareTaskBase();
    lhs->depnode()->addDependent(newrhs);
    return newrhs;
}

namespace blueprint {
struct File {
    enum class TYPE {
        IYOKANL1_JSON,
    } type;
    std::string path, name;
};
struct BuiltinROM {
    std::string name;
    size_t inAddrWidth, outRdataWidth;
};
struct BuiltinRAM {
    std::string name;
    size_t inAddrWidth, inWdataWidth, outRdataWidth;
};
struct Port {
    std::string nodeName;
    TaskLabel portLabel;

    bool operator==(const Port &rhs) const
    {
        return nodeName == rhs.nodeName && portLabel == rhs.portLabel;
    }
};
}  // namespace blueprint

class NetworkBlueprint {
private:
    std::vector<blueprint::File> files_;
    std::vector<blueprint::BuiltinROM> builtinROMs_;
    std::vector<blueprint::BuiltinRAM> builtinRAMs_;
    std::vector<std::pair<blueprint::Port, blueprint::Port>> edges_;

    std::map<std::tuple<std::string, int>, blueprint::Port> atPorts_;

private:
    std::vector<blueprint::Port> parsePortString(const std::string &src,
                                                 const std::string &kind)
    {
        std::string nodeName, portName;
        int portBitFrom, portBitTo;

        auto match = utility::regexMatch(
            src, std::regex(
                     R"(^@?(?:([^/]+)/)?([^[]+)(?:\[([0-9]+):([0-9]+)\])?$)"));
        if (match.empty())
            error::die("Invalid port string: ", src);

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
            ret.push_back(
                blueprint::Port{nodeName, TaskLabel{kind, portName, i}});
        return ret;
    }

public:
    NetworkBlueprint(const std::string &fileName)
    {
        namespace fs = std::filesystem;

        // Parse config file
        const auto src = toml::parse(fileName);

        // Find working directory of config
        fs::path wd = fs::absolute(fileName);
        wd.remove_filename();

        // [[file]]
        {
            const auto srcFiles =
                toml::find<std::vector<toml::value>>(src, "file");
            for (const auto &srcFile : srcFiles) {
                std::string type = toml::find<std::string>(srcFile, "type");
                fs::path path = toml::find<std::string>(srcFile, "path");
                std::string name = toml::find<std::string>(srcFile, "name");

                assert(type == "iyokanl1-json");

                if (path.is_relative())
                    path = wd / path;  // Make path absolute

                files_.push_back(blueprint::File{
                    blueprint::File::TYPE::IYOKANL1_JSON, path.string(), name});
            }
        }

        // [[builtin]]
        {
            const auto srcBuiltins =
                toml::find<std::vector<toml::value>>(src, "builtin");
            for (const auto &srcBuiltin : srcBuiltins) {
                const auto type = toml::find<std::string>(srcBuiltin, "type");
                const auto name = toml::find<std::string>(srcBuiltin, "name");

                if (type == "rom") {
                    const auto inAddrWidth =
                        toml::find<size_t>(srcBuiltin, "in_addr_width");
                    const auto outRdataWidth =
                        toml::find<size_t>(srcBuiltin, "out_rdata_width");

                    builtinROMs_.push_back(blueprint::BuiltinROM{
                        name, inAddrWidth, outRdataWidth});
                }
                else if (type == "ram") {
                    const auto inAddrWidth =
                        toml::find<size_t>(srcBuiltin, "in_addr_width");
                    const auto inWdataWidth =
                        toml::find<size_t>(srcBuiltin, "in_wdata_width");
                    const auto outRdataWidth =
                        toml::find<size_t>(srcBuiltin, "out_rdata_width");

                    builtinRAMs_.push_back(blueprint::BuiltinRAM{
                        name, inAddrWidth, inWdataWidth, outRdataWidth});
                }
            }
        }

        // [connect]
        {
            const auto srcConnect = toml::find<toml::table>(src, "connect");
            for (const auto &[srcKey, srcValue] : srcConnect) {
                std::string srcTo = srcKey,
                            srcFrom = toml::get<std::string>(srcValue),
                            errMsg = utility::fok("Invalid connect: ", srcTo,
                                                  " = ", srcFrom);

                // Check if input is correct.
                if (srcTo.empty() || srcFrom.empty() ||
                    (srcTo[0] == '@' && srcFrom[0] == '@'))
                    error::die(errMsg);

                // Others.
                std::vector<blueprint::Port> portsTo = parsePortString(srcTo,
                                                                       "input"),
                                             portsFrom = parsePortString(
                                                 srcFrom, "output");
                if (portsTo.size() != portsFrom.size())
                    error::die(errMsg);

                for (size_t i = 0; i < portsTo.size(); i++) {
                    const blueprint::Port &to = portsTo[i];
                    const blueprint::Port &from = portsFrom[i];

                    if (srcTo[0] == '@') {  // @... = ...
                        if (!to.nodeName.empty() || from.nodeName.empty())
                            error::die(errMsg);

                        auto key = std::make_tuple(to.portLabel.portName,
                                                   to.portLabel.portBit);
                        atPorts_.emplace(key, from);
                    }
                    else if (srcFrom[0] == '@') {  // ... = @...
                        if (!from.nodeName.empty() || to.nodeName.empty())
                            error::die(errMsg);

                        auto key = std::make_tuple(from.portLabel.portName,
                                                   from.portLabel.portBit);
                        atPorts_.emplace(key, to);
                    }
                    else {  // ... = ...
                        edges_.emplace_back(from, to);
                    }
                }
            }
        }
    }

    const std::vector<blueprint::File> &files() const
    {
        return files_;
    }

    const std::vector<blueprint::BuiltinROM> &builtinROMs() const
    {
        return builtinROMs_;
    }

    const std::vector<blueprint::BuiltinRAM> &builtinRAMs() const
    {
        return builtinRAMs_;
    }

    const std::vector<std::pair<blueprint::Port, blueprint::Port>> &edges()
        const
    {
        return edges_;
    }

    const std::map<std::tuple<std::string, int>, blueprint::Port> &atPorts()
        const
    {
        return atPorts_;
    }

    std::optional<blueprint::Port> at(const std::string &portName,
                                      int portBit = 0) const
    {
        auto it = atPorts_.find(std::make_tuple(portName, portBit));
        if (it == atPorts_.end())
            return std::nullopt;
        return it->second;
    }
};

struct Options {
    std::optional<NetworkBlueprint> blueprint;
    std::string inputFile, outputFile;
    int numCPUWorkers = std::thread::hardware_concurrency(),
        numGPUWorkers = 80 * 10, numCycles = -1;
    std::optional<std::string> secretKey;
    bool quiet = false;
    std::optional<std::string> dumpPrefix;
};

template <class Func>
int processCycles(int numCycles, std::ostream &os, Func func)
{
    for (int i = 0; i < numCycles; i++) {
        os << "#" << (i + 1) << std::flush;

        auto begin = std::chrono::high_resolution_clock::now();
        bool shouldBreak = func(i);
        auto end = std::chrono::high_resolution_clock::now();

        os << "\tdone. ("
           << std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                  .count()
           << " us)" << std::endl;

        if (shouldBreak) {
            os << "break." << std::endl;
            return i + 1;
        }
    }

    return numCycles;
}

template <class WorkerInfo, class WorkerType>
class NetworkRunner {
private:
    std::shared_ptr<ReadyQueue<WorkerInfo>> readyQueue_;
    size_t numFinishedTargets_;
    std::vector<WorkerType> workers_;
    std::vector<std::shared_ptr<TaskNetwork<WorkerInfo>>> nets_;

public:
    NetworkRunner()
        : readyQueue_(std::make_shared<ReadyQueue<WorkerInfo>>()),
          numFinishedTargets_(0)
    {
    }

    bool isValid()
    {
        for (auto &&net : nets_)
            if (!net->isValid())
                return false;
        return true;
    }

    void addNetwork(const std::shared_ptr<TaskNetwork<WorkerInfo>> &net)
    {
        nets_.push_back(net);
    }

    template <class... Args>
    void addWorker(Args &&... args)
    {
        workers_.emplace_back(*readyQueue_, numFinishedTargets_,
                              std::forward<Args>(args)...);
    }

    void prepareToRun()
    {
        assert(readyQueue_->empty());
        assert(nets_.empty() || workers_.size() > 0);

        numFinishedTargets_ = 0;
        for (auto &&net : nets_)
            net->pushReadyTasks(*readyQueue_);
    }

    size_t numNodes() const
    {
        size_t ret = 0;
        for (auto &&net : nets_)
            ret += net->numNodes();
        return ret;
    }

    size_t getNumFinishedTargets() const
    {
        return numFinishedTargets_;
    }

    std::shared_ptr<ReadyQueue<WorkerInfo>> getReadyQueue() const
    {
        return readyQueue_;
    }

    bool isRunning() const
    {
        return std::any_of(workers_.begin(), workers_.end(),
                           [](auto &&w) { return w.isWorking(); }) ||
               !readyQueue_->empty();
    }

    void update()
    {
        for (auto &&w : workers_)
            w.update();
    }

    void tick()
    {
        for (auto &&net : nets_)
            net->tick();
    }
};

#endif
