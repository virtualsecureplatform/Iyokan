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
#include <picojson.h>
#include <toml.hpp>

#include <cereal/cereal.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/bitset.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/tuple.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/utility.hpp>
#include <cereal/types/vector.hpp>

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

// Thanks to: https://qiita.com/IgnorantCoder/items/a6cebba4de6cb5901335
// Thanks to: https://qiita.com/tyanmahou/items/c2a7c389e666d1b1bdea
// Thanks to: https://cpprefjp.github.io/reference/type_traits/void_t.html
template <class, template <class...> class, class...>
struct is_detected_impl : std::false_type {
};
template <template <class...> class Op, class... Args>
struct is_detected_impl<std::void_t<Op<Args...>>, Op, Args...>
    : std::true_type {
};
template <template <class...> class Op, class... Args>
using is_detected = is_detected_impl<void, Op, Args...>;

// Check if T has RAM(int, int)
template <class T, class... Args>
using hasMethodFuncRAMImpl =
    decltype(std::declval<T>().RAM(std::declval<Args>()...));
template <class T>
constexpr bool hasMethodFuncRAM =
    is_detected<hasMethodFuncRAMImpl, T, int, int>::value;

}  // namespace detail

// NodeLabel is used to identify a DepNode in debugging/profiling.
struct NodeLabel {
    int id;
    std::string kind, desc;

    NodeLabel()
    {
    }

    NodeLabel(std::string kind, std::string desc)
        : id(genid()), kind(kind), desc(desc)
    {
    }

    std::string str() const
    {
        return utility::fok("#", id, " ", kind, " ", desc);
    }

    std::ostream &operator<<(std::ostream &os)
    {
        os << str();
        return os;
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(id, kind, desc);
    }

private:
    inline int genid()
    {
        // FIXME: Assume that ids over (1 << 16) will not be passed by the user.
        static int id = (1 << 16);
        return id++;
    }
};

class ProgressGraphMaker {
private:
    struct Node {
        NodeLabel label;
        int index;
        std::optional<std::chrono::system_clock::time_point> start =
                                                                 std::nullopt,
                                                             end = std::nullopt;
    };

    struct Edge {
        int from, to;
        int index;
    };

    std::unordered_map<int, Node> nodes_;
    std::vector<Edge> edges_;

    int numStartedNodes_, numNotifiedEdges_;

    std::mutex mtxWrite_;

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
        std::lock_guard<std::mutex> lock(mtxWrite_);

        auto &n = node(label);
        n.index = numStartedNodes_++;
        assert(!n.start && !n.end);
        n.start = std::chrono::high_resolution_clock::now();
    }

    void finishNode(const NodeLabel &label)
    {
        std::lock_guard<std::mutex> lock(mtxWrite_);

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

    void dumpTimeCSV(std::ostream &os) const
    {
        using namespace std::chrono;

        for (auto &&[id, node] : nodes_) {
            using namespace utility;
            os << "\"" << node.start.value() << "\"";
            os << ",\"" << node.end.value() << "\"";
            os << ",\"" << node.index << "\"";
            os << ",\"" << node.label.id << "\"";
            os << ",\"" << node.label.kind << "\"";
            os << ",\"" << node.label.desc << "\"";
            os << std::endl;
        }
    }

    void dumpJSON(std::ostream &os) const
    {
        picojson::object nodes;
        for (auto &&[id, node] : nodes_) {
            picojson::object json;
            if (node.start)
                json.emplace("start", utility::fok(*node.start));
            if (node.end)
                json.emplace("end", utility::fok(*node.end));
            json.emplace("index", static_cast<double>(node.index));
            json.emplace("id", static_cast<double>(node.label.id));
            json.emplace("kind", node.label.kind);
            json.emplace("desc", node.label.desc);

            nodes.emplace(utility::fok(id), json);
        }

        picojson::array edges;
        for (auto &&edge : edges_) {
            picojson::object json;
            json.emplace("index", static_cast<double>(edge.index));
            json.emplace("from", static_cast<double>(edge.from));
            json.emplace("to", static_cast<double>(edge.to));

            edges.emplace_back(json);
        }

        picojson::object root;
        root.emplace("nodes", nodes);
        root.emplace("edges", edges);
        os << picojson::value(root);
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

class DepNodeBase {
private:
    int priority_;
    NodeLabel label_;

public:
    DepNodeBase()
    {
    }

    DepNodeBase(NodeLabel label) : priority_(-1), label_(std::move(label))
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

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(priority_, label_);
    }
};
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
    virtual void checkValid(error::Stack &err) = 0;
    virtual void notifyOneInputReady() = 0;
    virtual bool areInputsReady() const = 0;
    virtual void startAsync(WorkerInfo,
                            ProgressGraphMaker *graph = nullptr) = 0;
    virtual bool hasFinished() const = 0;
    virtual void tick() = 0;  // Reset for next process.

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(depnode_);
    }
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

    virtual void startAsyncImpl(WorkerInfo /* wi */)
    {
        assert(0 && "Unreachable");
    }

    virtual void startAsyncImpl(WorkerInfo wi, ProgressGraphMaker *graph)
    {
        startAsyncImpl(std::move(wi));
        if (graph) {
            graph->startNode(this->depnode()->label());
        }
    }

public:
    Task()
    {
    }

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

    void checkValid(error::Stack &err) override
    {
        assert(this->depnode());

        const NodeLabel &label = this->depnode()->label();
        if (!std::all_of(inputs_.begin(), inputs_.end(),
                         [](auto &&in) { return in.use_count() != 0; }))
            err.add("Not enough inputs: ", label.str());
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

    void startAsync(WorkerInfo wi, ProgressGraphMaker *graph) override
    {
        startAsyncImpl(std::move(wi), graph);
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<TaskBase<WorkerInfo>>(this), numReadyInputs_,
           output_, inputs_);
    }
};

template <class T0, class T1>
void connectTasks(const std::shared_ptr<T0> &from,
                  const std::shared_ptr<T1> &to)
{
    to->addInputPtr(from->getOutputPtr());
    from->depnode()->addDependent(to->depnode());
}

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

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(kind, portName, portBit);
    }
};

namespace graph {
struct Node {
    NodeLabel label;
    std::set<int> parents, children;
    bool hasNoInputsToWaitFor;

    Node(const NodeLabel &label, bool hasNoInputsToWaitFor)
        : label(label), hasNoInputsToWaitFor(hasNoInputsToWaitFor)
    {
    }
};
using NodePtr = std::shared_ptr<graph::Node>;

inline std::unordered_map<int, int> doRankuSort(
    const std::unordered_map<int, graph::NodePtr> &id2node)
{
    // c.f. https://en.wikipedia.org/wiki/Heterogeneous_Earliest_Finish_Time
    // FIXME: Take communication costs into account
    // FIXME: Tune computation costs by dynamic measurements

    std::unordered_map<std::string, int> compCost = {
        {"DFF", 0},          {"WIRE", 0},         {"INPUT", 0},
        {"OUTPUT", 0},       {"AND", 10},         {"NAND", 10},
        {"ANDNOT", 10},      {"OR", 10},          {"NOR", 10},
        {"ORNOT", 10},       {"XOR", 10},         {"XNOR", 10},
        {"MUX", 20},         {"NOT", 0},          {"CB", 100},
        {"CBInv", 100},      {"CBWithInv", 100},  {"MUXWoSE", 20},
        {"CMUXs", 10},       {"SEI", 0},          {"GB", 10},
        {"ROMUX", 10},       {"RAMUX", 10},       {"SEI&KS", 5},
        {"cufhe2tfhepp", 0}, {"tfhepp2cufhe", 0}, {"bridge", 0},
        {"RAMWriter", 0},    {"RAMReader", 0},    {"ROM", 0},
    };

    auto isPseudoInit = [&](int id) {
        return id2node.at(id)->hasNoInputsToWaitFor;
    };

    // Make a map from id to the number of ready children of the node
    std::unordered_map<NodePtr, int> numReadyChildren;
    for (auto &&[id, node] : id2node) {
        size_t n = std::count_if(node->children.begin(), node->children.end(),
                                 isPseudoInit);
        numReadyChildren.emplace(node, n);
    }

    std::queue<NodePtr> que;
    for (auto &&[id, node] : id2node) {
        // Initial nodes should be "terminals", that is,
        // they have no children OR all of their children has no inputs to wait
        // for.
        if (std::all_of(node->children.begin(), node->children.end(),
                        isPseudoInit))
            que.push(node);
    }
    assert(!que.empty());

    std::unordered_map<NodePtr, int> node2pri;
    while (!que.empty()) {
        auto node = que.front();
        que.pop();

        // Calculate the priority for the node
        int pri = 0;
        for (auto &&childId : node->children) {
            NodePtr child = id2node.at(childId);
            if (!child->hasNoInputsToWaitFor)
                pri = std::max(pri, node2pri.at(child));
        }
        if (compCost.find(node->label.kind) == compCost.end())
            spdlog::info("{}", node->label.kind);
        int w = compCost.at(node->label.kind);
        auto [it, inserted] = node2pri.emplace(node, pri + w);
        assert(inserted);

        if (node->hasNoInputsToWaitFor)
            continue;

        for (auto parentId : node->parents) {
            NodePtr parent = id2node.at(parentId);
            numReadyChildren.at(parent)++;
            assert(parent->children.size() >= numReadyChildren.at(parent));
            if (parent->children.size() == numReadyChildren.at(parent))
                que.push(parent);
        }
    }
    if (id2node.size() > node2pri.size()) {
        spdlog::debug("id2node {} != node2pri {}", id2node.size(),
                      node2pri.size());
        for (auto &&[id, node] : id2node) {
            auto it = node2pri.find(node);
            if (it == node2pri.end()) {
                spdlog::debug("\t{} {} {}", node->label.id, node->label.kind,
                              node->label.desc);
            }
        }
        error::die("Invalid network; some nodes will not be executed.");
    }
    assert(id2node.size() == node2pri.size());

    std::unordered_map<int, int> id2pri;
    for (auto &&[node, pri] : node2pri)
        id2pri[node->label.id] = pri;

    return id2pri;
}

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
            if (child->hasNoInputsToWaitFor)  // false parent-child
                                              // relationship
                continue;
            numReadyParents.at(child)++;
            assert(child->parents.size() >= numReadyParents.at(child));
            if (child->parents.size() == numReadyParents.at(child))
                que.push(child);
        }
    }

    if (id2node.size() > node2index.size()) {
        spdlog::debug("id2node {} != node2index {}", id2node.size(),
                      node2index.size());
        for (auto &&[id, node] : id2node) {
            auto it = node2index.find(node);
            if (it == node2index.end()) {
                spdlog::debug("\t{} {} {}", node->label.id, node->label.kind,
                              node->label.desc);
            }
        }
        error::die("Invalid network; some nodes will not be executed.");
    }
    assert(id2node.size() == node2index.size());

    std::unordered_map<int, int> id2index;
    for (auto &&[node, index] : node2index)
        id2index[node->label.id] = index;

    return id2index;
}

}  // namespace graph

class GraphVisitor {
public:
    enum class STRATEGY {
        DFS,     // Follow as many edges as possible from ready nodes.
        SINGLE,  // Visit each node in arbitrary order.
    };

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

        auto node = std::make_shared<graph::Node>(
            depnode.label(), depnode.task()->areInputsReady());

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

    virtual STRATEGY getStrategy() const
    {
        return STRATEGY::DFS;
    }

    const std::unordered_map<int, graph::NodePtr> &getMap() const
    {
        return id2node_;
    }

    template <class WorkerInfo>
    bool start(DepNode<WorkerInfo> &curNode)
    {
        auto [node, isNew] = depnode2node(curNode);

        if (getStrategy() == STRATEGY::DFS && !nodeStack_.empty()) {
            graph::NodePtr parent = nodeStack_.top();
            parent->children.insert(node->label.id);
            node->parents.insert(parent->label.id);
        }

        if (!isNew)
            return false;

        onStart(curNode);

        if (getStrategy() == STRATEGY::DFS)
            nodeStack_.push(node);
        return true;
    }

    void end()
    {
        if (getStrategy() == STRATEGY::DFS) {
            assert(!nodeStack_.empty());
            nodeStack_.pop();
        }
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

class GateCountVisitor : public GraphVisitor {
private:
    std::unordered_map<std::string, int> kind2count_;

public:
    const std::unordered_map<std::string, int> &kind2count() const
    {
        return kind2count_;
    }

    STRATEGY getStrategy() const override
    {
        return STRATEGY::SINGLE;
    }

private:
    void onStart(DepNodeBase &depnode) override
    {
        std::string kind = depnode.label().kind;
        auto [it, inserted] = kind2count_.emplace(kind, 0);
        it->second++;
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
    DepNode()
    {
    }

    DepNode(std::shared_ptr<TaskBase<WorkerInfo>> task, NodeLabel label)
        : DepNodeBase(std::move(label)),
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

    void start(WorkerInfo wi, ProgressGraphMaker &graph)
    {
        task_->startAsync(std::move(wi), &graph);
    }

    bool hasFinished() const
    {
        return task_->hasFinished();
    }

    virtual void propagate(ReadyQueue<WorkerInfo> &readyQueue);
    virtual void propagate(ReadyQueue<WorkerInfo> &readyQueue,
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

        if (visitor.getStrategy() == GraphVisitor::STRATEGY::DFS) {
            for (auto &&dep : dependents_) {
                auto next = dep.lock();
                assert(next);
                next->visit(visitor);
            }
        }

        visitor.end();
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<DepNodeBase>(this), hasQueued_, task_,
           dependents_);
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

    void checkInputSizeIsExpected(error::Stack &err)
    {
        GraphVisitor visitor;
        visit(visitor);
        const auto &id2node = visitor.getMap();
        for (auto &&[id, node] : id2node)
            if (node->parents.size() != id2node_.at(id)->task()->getInputSize())
                err.add("Incorrect connection: ", node->label.str());
    }

public:
    TaskNetwork()
    {
    }

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

    void checkValid(error::Stack &err)
    {
        for (auto &&[key, node] : id2node_)
            node->task()->checkValid(err);

        checkInputSizeIsExpected(err);

        {
            GraphVisitor visitor;
            visit(visitor);
            if (visitor.getMap().size() != id2node_.size())
                err.add("Network is not weekly connected");
        }
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
            connectTasks(in, out);
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
            connectTasks(in, out);
        }

        return ret;
    }

    void visit(GraphVisitor &visitor)
    {
        using ST = GraphVisitor::STRATEGY;
        ST st = visitor.getStrategy();
        for (auto &&[id, node] : id2node_) {
            if (st == ST::SINGLE ||
                (st == ST::DFS && node->task()->areInputsReady()))
                node->visit(visitor);
        }
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(id2node_, namedMems_);
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

    std::shared_ptr<TaskBase<WorkerInfo>> addTask(
        NodeLabel label, const std::shared_ptr<TaskBase<WorkerInfo>> &task)
    {
        auto depnode = std::make_shared<DepNode<WorkerInfo>>(task, label);
        depnode->prepareTaskBase();
        auto [it, inserted] = id2node_.emplace(label.id, depnode);
        assert(inserted);
        return task;
    }

    template <class T, class... Args>
    std::shared_ptr<T> emplaceTask(NodeLabel label, Args &&... args)
    {
        auto task = std::make_shared<T>(std::forward<Args>(args)...);
        addTask(label, task);
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
    std::shared_ptr<T> addINPUT(const std::string &portName, int portBit,
                                Args &&... args)
    {
        NodeLabel label{"INPUT", utility::fok(portName, "[", portBit, "]")};
        auto task = std::make_shared<T>(std::forward<Args>(args)...);
        addTask(label, task);
        registerTask("input", portName, portBit, task);
        return task;
    }

    template <class T, class... Args>
    std::shared_ptr<T> addOUTPUT(const std::string &portName, int portBit,
                                 Args &&... args)
    {
        NodeLabel label{"OUTPUT", utility::fok(portName, "[", portBit, "]")};
        auto task = std::make_shared<T>(std::forward<Args>(args)...);
        addTask(label, task);
        registerTask("output", portName, portBit, task);
        return task;
    }
};

template <class TaskType, class TaskTypeMem, class TaskTypeDFF,
          class TaskTypeWIRE, class WorkerInfo>
class NetworkBuilder : public NetworkBuilderBase<WorkerInfo> {
public:
    using ParamTaskTypeWIRE = TaskTypeWIRE;
    using ParamTaskTypeMem = TaskTypeMem;

protected:
    std::shared_ptr<TaskTypeDFF> addDFF()
    {
        auto task = std::make_shared<TaskTypeDFF>();
        this->addTask(NodeLabel{"DFF", ""}, task);
        return task;
    }

    std::shared_ptr<TaskTypeDFF> addNamedDFF(const std::string &kind,
                                             const std::string &portName,
                                             int portBit)
    {
        auto task = addDFF();
        this->registerTask(kind, portName, portBit, task);
        return task;
    }

    std::shared_ptr<TaskTypeWIRE> addNamedWIRE(bool inputNeeded,
                                               const std::string &kind,
                                               const std::string &portName,
                                               int portBit)
    {
        auto task = std::make_shared<TaskTypeWIRE>(inputNeeded);
        this->addTask(
            NodeLabel{"WIRE", utility::fok(portName, "[", portBit, "]")}, task);
        this->registerTask(kind, portName, portBit, task);
        return task;
    }

public:
    int DFF()
    {
        auto task = addDFF();
        return task->depnode()->label().id;
    }

    int ROM(const std::string &portName, int portBit)
    {
        auto task = addNamedWIRE(false, "rom", portName, portBit);
        return task->depnode()->label().id;
    }

    int INPUT(const std::string &portName, int portBit)
    {
        auto task =
            this->template addINPUT<TaskTypeWIRE>(portName, portBit, false);
        return task->depnode()->label().id;
    }

    int OUTPUT(const std::string &portName, int portBit)
    {
        auto task =
            this->template addOUTPUT<TaskTypeWIRE>(portName, portBit, true);
        return task->depnode()->label().id;
    }

    void connect(int from, int to)
    {
        connectTasks(
            std::dynamic_pointer_cast<TaskType>(this->node(from)->task()),
            std::dynamic_pointer_cast<TaskType>(this->node(to)->task()));
    }

#define DEFINE_GATE(name)                               \
protected:                                              \
    virtual std::shared_ptr<TaskType> name##Impl() = 0; \
                                                        \
public:                                                 \
    int name()                                          \
    {                                                   \
        auto task = name##Impl();                       \
        this->addTask(NodeLabel{#name, ""}, task);      \
        return task->depnode()->label().id;             \
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

template <class NetworkBuilder>
class RAMNetworkBuilder : public NetworkBuilder {
private:
    int width_;

public:
    RAMNetworkBuilder(int width) : width_(width)
    {
    }

    int RAM(int addr, int bit)
    {
        auto task = this->addNamedDFF("ram", "ramdata", addr * width_ + bit);
        return task->depnode()->label().id;
    }
};

template <class WorkerInfo>
TaskNetwork<WorkerInfo>::TaskNetwork(NetworkBuilderBase<WorkerInfo> &&builder)
    : id2node_(std::move(builder.id2node_)),
      namedMems_(std::move(builder.namedMems_))
{
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
    TaskMem()
    {
    }

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

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<Task<InType, OutType, WorkerInfo>>(this));
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

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<TaskMem<InType, OutType, WorkerInfo>>(this));
    }
};

template <class InType, class OutType, class WorkerInfo>
class TaskAsync : public Task<InType, OutType, WorkerInfo> {
private:
    AsyncThread thr_;

private:
    virtual void startSync(WorkerInfo wi) = 0;

    void startAsyncImpl(WorkerInfo wi, ProgressGraphMaker *graph) override
    {
        thr_ = [this, wi, graph] {
            if (graph)
                graph->startNode(this->depnode()->label());
            startSync(wi);
        };
    }

public:
    TaskAsync()
    {
    }

    TaskAsync(size_t expectedNumInputs)
        : Task<InType, OutType, WorkerInfo>(expectedNumInputs)
    {
    }

    bool hasFinished() const override
    {
        return thr_.hasFinished();
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<Task<InType, OutType, WorkerInfo>>(this));
    }
};

template <class WorkerInfo>
class TaskBlackHole : public TaskBase<WorkerInfo> {
private:
    int numInputs_, numReadyInputs_;

public:
    TaskBlackHole()
    {
    }

    TaskBlackHole(int numInputs) : numInputs_(numInputs), numReadyInputs_(0)
    {
    }

    size_t getInputSize() const override
    {
        return numInputs_;
    }

    void checkValid(error::Stack &) override
    {
        assert(this->depnode());
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

    void startAsync(WorkerInfo, ProgressGraphMaker *graph) override
    {
        if (graph)
            graph->startNode(this->depnode()->label());

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

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<TaskBase<WorkerInfo>>(this), numInputs_,
           numReadyInputs_);
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
    BridgeDepNode()
    {
    }

    BridgeDepNode(std::shared_ptr<DepNode<OutWorkerInfo>> src)
        : DepNode<InWorkerInfo>(
              std::make_shared<TaskBlackHole<InWorkerInfo>>(1),
              NodeLabel{"bridge", ""}),
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

    void propagate(ReadyQueue<InWorkerInfo> &readyQueue,
                   ProgressGraphMaker &graph) override
    {
        graph.finishNode(this->label());

        propagate(readyQueue);

        graph.notify(this->label(), src_->label());
    }

    void visit(GraphVisitor &visitor) override
    {
        if (!visitor.start(*this))
            return;
        if (visitor.getStrategy() == GraphVisitor::STRATEGY::DFS)
            src_->visit(visitor);
        visitor.end();
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<DepNode<InWorkerInfo>>(this), src_);
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

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(type, path, name);
    }
};
struct BuiltinROM {
    enum class TYPE {
        CMUX_MEMORY,
        MUX,
    } type;
    std::string name;
    size_t inAddrWidth, outRdataWidth;

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(type, name, inAddrWidth, outRdataWidth);
    }
};
struct BuiltinRAM {
    enum class TYPE {
        CMUX_MEMORY,
        MUX,
    } type;
    std::string name;
    size_t inAddrWidth, inWdataWidth, outRdataWidth;

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(type, name, inAddrWidth, inWdataWidth, outRdataWidth);
    }
};
struct Port {
    std::string nodeName;
    TaskLabel portLabel;

    bool operator==(const Port &rhs) const
    {
        return nodeName == rhs.nodeName && portLabel == rhs.portLabel;
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(nodeName, portLabel);
    }
};
}  // namespace blueprint

class NetworkBlueprint {
private:
    std::string sourceFile_;
    std::vector<blueprint::File> files_;
    std::vector<blueprint::BuiltinROM> builtinROMs_;
    std::vector<blueprint::BuiltinRAM> builtinRAMs_;
    std::vector<std::pair<blueprint::Port, blueprint::Port>> edges_;

    std::map<std::tuple<std::string, int>, blueprint::Port> atPorts_;
    std::unordered_map<std::string, int> atPortWidths_;

public:
    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(sourceFile_, files_, builtinROMs_, builtinRAMs_, edges_, atPorts_,
           atPortWidths_);
    }

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
    NetworkBlueprint()
    {
    }

    NetworkBlueprint(const std::string &fileName) : sourceFile_(fileName)
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
                toml::find_or<std::vector<toml::value>>(src, "file", {});
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
                toml::find_or<std::vector<toml::value>>(src, "builtin", {});
            for (const auto &srcBuiltin : srcBuiltins) {
                const auto type = toml::find<std::string>(srcBuiltin, "type");
                const auto name = toml::find<std::string>(srcBuiltin, "name");

                if (type == "rom" || type == "mux-rom") {
                    auto romType =
                        type == "rom" ? blueprint::BuiltinROM::TYPE::CMUX_MEMORY
                                      : blueprint::BuiltinROM::TYPE::MUX;
                    const auto inAddrWidth =
                        toml::find<size_t>(srcBuiltin, "in_addr_width");
                    const auto outRdataWidth =
                        toml::find<size_t>(srcBuiltin, "out_rdata_width");

                    builtinROMs_.push_back(blueprint::BuiltinROM{
                        romType, name, inAddrWidth, outRdataWidth});
                }
                else if (type == "ram" || type == "mux-ram") {
                    auto ramType =
                        type == "ram" ? blueprint::BuiltinRAM::TYPE::CMUX_MEMORY
                                      : blueprint::BuiltinRAM::TYPE::MUX;
                    const auto inAddrWidth =
                        toml::find<size_t>(srcBuiltin, "in_addr_width");
                    const auto inWdataWidth =
                        toml::find<size_t>(srcBuiltin, "in_wdata_width");
                    const auto outRdataWidth =
                        toml::find<size_t>(srcBuiltin, "out_rdata_width");

                    builtinRAMs_.push_back(
                        blueprint::BuiltinRAM{ramType, name, inAddrWidth,
                                              inWdataWidth, outRdataWidth});
                }
            }
        }

        // [connect]
        {
            const auto srcConnect =
                toml::find_or<toml::table>(src, "connect", {});
            for (const auto &[srcKey, srcValue] : srcConnect) {
                if (srcKey == "TOGND") {  // TOGND = [@...[n:m], @...[n:m], ...]
                    auto ary = toml::get<std::vector<std::string>>(srcValue);
                    for (const auto &portStr : ary) {  // @...[n:m]
                        if (portStr.empty() || portStr.at(0) != '@')
                            error::die("Invalid port name for TOGND: ",
                                       portStr);
                        auto ports = parsePortString(portStr, "output");
                        for (auto &&port : ports) {  // @...[n]
                            const std::string &name = port.portLabel.portName;
                            int bit = port.portLabel.portBit;
                            auto [it, inserted] =
                                atPortWidths_.emplace(name, 0);
                            it->second = std::max(it->second, bit + 1);
                        }
                    }
                    continue;
                }

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

                        const std::string &name = to.portLabel.portName;
                        int bit = to.portLabel.portBit;

                        atPorts_.emplace(std::make_tuple(name, bit), from);

                        auto [it, inserted] = atPortWidths_.emplace(name, 0);
                        it->second = std::max(it->second, bit + 1);
                    }
                    else if (srcFrom[0] == '@') {  // ... = @...
                        if (!from.nodeName.empty() || to.nodeName.empty())
                            error::die(errMsg);

                        const std::string &name = from.portLabel.portName;
                        int bit = from.portLabel.portBit;

                        atPorts_.emplace(std::make_tuple(name, bit), to);

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

    bool needsCircuitKey() const
    {
        for (const auto &bprom : builtinROMs_)
            if (bprom.type == blueprint::BuiltinROM::TYPE::CMUX_MEMORY)
                return true;
        for (const auto &bpram : builtinRAMs_)
            if (bpram.type == blueprint::BuiltinRAM::TYPE::CMUX_MEMORY)
                return true;
        return false;
    }

    const std::string &sourceFile() const
    {
        return sourceFile_;
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

    const std::unordered_map<std::string, int> &atPortWidths() const
    {
        return atPortWidths_;
    }
};

enum class SCHED {
    UND,
    TOPO,
    RANKU,
};

struct Options {
    std::optional<NetworkBlueprint> blueprint;
    std::optional<int> numCPUWorkers, numGPUWorkers, numGPU, numCycles;
    std::optional<std::string> bkeyFile, inputFile, outputFile, secretKey,
        dumpPrefix, snapshotFile, resumeFile;
    std::optional<std::string> dumpTimeCSVPrefix, dumpGraphJSONPrefix;
    SCHED sched = SCHED::UND;
    bool stdoutCSV = false;
};

template <class Func>
std::chrono::microseconds timeit(Func f)
{
    auto begin = std::chrono::high_resolution_clock::now();
    f();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(end - begin);
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

template <class NetworkBuilder>
typename NetworkBuilder::NetworkType readNetworkFromJSON(std::istream &is)
{
    NetworkBuilder builder;
    readNetworkFromJSONImpl(builder, is);

    return typename NetworkBuilder::NetworkType{std::move(builder)};
}

template <class NetworkBuilder>
void readNetworkFromJSONImpl(NetworkBuilder &builder, std::istream &is)
{
    std::unordered_map<int, int> id2taskId;
    auto addId = [&](int id, int taskId) { id2taskId.emplace(id, taskId); };
    auto findTaskId = [&](int id) {
        auto it = id2taskId.find(id);
        if (it == id2taskId.end())
            error::die("Invalid JSON");
        return it->second;
    };
    auto connectIds = [&](int from, int to) {
        builder.connect(findTaskId(from), findTaskId(to));
    };

    picojson::value v;
    const std::string err = picojson::parse(v, is);
    if (!err.empty())
        error::die("Invalid JSON of network: ", err);

    picojson::object &obj = v.get<picojson::object>();
    picojson::array &cells = obj["cells"].get<picojson::array>();
    picojson::array &ports = obj["ports"].get<picojson::array>();
    for (const auto &e : ports) {
        picojson::object port = e.get<picojson::object>();
        std::string type = port.at("type").get<std::string>();
        int id = static_cast<int>(port.at("id").get<double>());
        std::string portName = port.at("portName").get<std::string>();
        int portBit = static_cast<int>(port.at("portBit").get<double>());
        if (type == "input")
            addId(id, builder.INPUT(portName, portBit));
        else if (type == "output")
            addId(id, builder.OUTPUT(portName, portBit));
    }
    for (const auto &e : cells) {
        picojson::object cell = e.get<picojson::object>();
        std::string type = cell.at("type").get<std::string>();
        int id = static_cast<int>(cell.at("id").get<double>());
        if (type == "AND")
            addId(id, builder.AND());
        else if (type == "NAND")
            addId(id, builder.NAND());
        else if (type == "ANDNOT")
            addId(id, builder.ANDNOT());
        else if (type == "XOR")
            addId(id, builder.XOR());
        else if (type == "XNOR")
            addId(id, builder.XNOR());
        else if (type == "DFFP")
            addId(id, builder.DFF());
        else if (type == "NOT")
            addId(id, builder.NOT());
        else if (type == "NOR")
            addId(id, builder.NOR());
        else if (type == "OR")
            addId(id, builder.OR());
        else if (type == "ORNOT")
            addId(id, builder.ORNOT());
        else if (type == "MUX")
            addId(id, builder.MUX());
        else {
            bool valid = false;
            // If builder.RAM() exists
            if constexpr (detail::hasMethodFuncRAM<NetworkBuilder>) {
                if (type == "RAM") {
                    int addr = cell.at("ramAddress").get<double>(),
                        bit = cell.at("ramBit").get<double>();
                    addId(id, builder.RAM(addr, bit));
                    valid = true;
                }
            }

            if (!valid) {
                error::die("Invalid JSON of network. Invalid type: ", type);
            }
        }
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
                connectIds(logic, id);
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
            connectIds(A, id);
            connectIds(B, id);
        }
        else if (type == "DFFP" || type == "RAM") {
            int D = static_cast<int>(input.at("D").get<double>());
            connectIds(D, id);
        }
        else if (type == "NOT") {
            int A = static_cast<int>(input.at("A").get<double>());
            connectIds(A, id);
        }
        else if (type == "MUX") {
            int A = static_cast<int>(input.at("A").get<double>());
            int B = static_cast<int>(input.at("B").get<double>());
            int S = static_cast<int>(input.at("S").get<double>());
            connectIds(A, id);
            connectIds(B, id);
            connectIds(S, id);
        }
        else {
            error::die("Invalid JSON of network. Invalid type: ", type);
        }
    }
}

template <class NetworkBuilder>
std::shared_ptr<typename NetworkBuilder::NetworkType> readNetwork(
    const blueprint::File &file)
{
    std::ifstream ifs{file.path, std::ios::binary};
    if (!ifs)
        error::die("Invalid [[file]] path: ", file.path);
    auto net = std::make_shared<typename NetworkBuilder::NetworkType>(
        readNetworkFromJSON<NetworkBuilder>(ifs));

    error::Stack err;
    net->checkValid(err);
    if (!err.empty())
        error::die("Invalid network named \"", file.name, "\":\n", err.str());

    return net;
}

template <class NetworkBuilder>
std::shared_ptr<typename NetworkBuilder::NetworkType> makeROMWithMUX(
    int inAddrWidth, int outRdataWidth)
{
    NetworkBuilder b;

    // Create inputs
    std::vector<int> addrInputs;
    for (int i = 0; i < inAddrWidth; i++) {
        int id = b.INPUT("addr", i);
        addrInputs.push_back(id);
    }

    // Create 1bitROMs
    for (int i = 0; i < outRdataWidth; i++) {
        make1bitROMWithMUX(b, addrInputs, outRdataWidth, i);
    }

    return std::make_shared<typename NetworkBuilder::NetworkType>(std::move(b));
}

template <class NetworkBuilder>
void make1bitROMWithMUX(NetworkBuilder &b, const std::vector<int> &addrInputs,
                        int outRdataWidth, int indexOutRdata)
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

    const int inAddrWidth = addrInputs.size();

    // Create ROMs
    std::vector<int> workingIds;
    for (int i = 0; i < (1 << inAddrWidth); i++) {
        int id = b.ROM("romdata", indexOutRdata + i * outRdataWidth);
        workingIds.push_back(id);
    }

    // Create MUXs
    for (int i = 0; i < inAddrWidth; i++) {
        assert(workingIds.size() > 0 && workingIds.size() % 2 == 0);
        std::vector<int> newWorkingIds;
        for (int j = 0; j < workingIds.size(); j += 2) {
            int id = b.MUX();
            b.connect(workingIds.at(j), id);
            b.connect(workingIds.at(j + 1), id);
            b.connect(addrInputs.at(i), id);
            newWorkingIds.push_back(id);
        }
        workingIds.swap(newWorkingIds);
    }
    assert(workingIds.size() == 1);

    // Create output
    int id = b.OUTPUT("rdata", indexOutRdata);
    b.connect(workingIds.at(0), id);
}

extern char _binary_mux_ram_8_8_8_min_json_start[];
extern char _binary_mux_ram_8_8_8_min_json_end[];
extern char _binary_mux_ram_8_8_8_min_json_size[];
extern char _binary_mux_ram_8_16_16_min_json_start[];
extern char _binary_mux_ram_8_16_16_min_json_end[];
extern char _binary_mux_ram_8_16_16_min_json_size[];
extern char _binary_mux_ram_9_16_16_min_json_start[];
extern char _binary_mux_ram_9_16_16_min_json_end[];
extern char _binary_mux_ram_9_16_16_min_json_size[];
template <class NetworkBuilder>
std::shared_ptr<typename NetworkBuilder::NetworkType> makeRAMWithMUX(
    int inAddrWidth, int dataWidth)
{
    RAMNetworkBuilder<NetworkBuilder> b{dataWidth};

#define USE_PRECOMPILED_BINARY(addrW, dataW)                               \
    if (inAddrWidth == addrW && dataWidth == dataW) {                      \
        std::stringstream ss{std::string{                                  \
            _binary_mux_ram_##addrW##_##dataW##_##dataW##_min_json_start,  \
            _binary_mux_ram_##addrW##_##dataW##_##dataW##_min_json_end}};  \
        readNetworkFromJSONImpl(b, ss);                                    \
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

    // Create inputs
    std::vector<int> addrInputs;
    for (int i = 0; i < inAddrWidth; i++) {
        int id = b.INPUT("addr", i);
        addrInputs.push_back(id);
    }
    int wrenInput = b.INPUT("wren", 0);

    // Create 1bitRAMs
    for (int i = 0; i < dataWidth; i++) {
        make1bitRAMWithMUX(b, addrInputs, wrenInput, i);
    }

    return std::make_shared<typename NetworkBuilder::NetworkType>(std::move(b));
}

template <class NetworkBuilder>
void make1bitRAMWithMUX(NetworkBuilder &b, const std::vector<int> &addrInputs,
                        int wrenInput, int indexWRdata)
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

    const int inAddrWidth = addrInputs.size();

    // Create input "wdata[indexWRdata]"
    int wdataInput = b.INPUT("wdata", indexWRdata);

    // Create DMUXs
    std::vector<int> workingIds = {wrenInput}, newWorkingIds;
    for (auto it = addrInputs.rbegin(); it != addrInputs.rend(); ++it) {
        int addr = *it;
        for (int src : workingIds) {
            // Create DMUX
            //   dst0 = andnot(src, addr)
            //   dst1 = and(src, addr)
            int dst0 = b.ANDNOT();
            int dst1 = b.AND();
            b.connect(src, dst0);
            b.connect(addr, dst0);
            b.connect(src, dst1);
            b.connect(addr, dst1);

            newWorkingIds.push_back(dst0);
            newWorkingIds.push_back(dst1);
        }
        workingIds.swap(newWorkingIds);
        newWorkingIds.clear();
    }
    assert(workingIds.size() == (1 << inAddrWidth));

    // Create RAMs
    for (int addr = 0; addr < (1 << inAddrWidth); addr++) {
        /*
                                +-------------------------+
                                |                         |
                                +--|\   RAM               |--
           INPUT                   | |-- ramdata[ ... ] --+
            wdata[indexRWdata] ----|/
                                    |
                                   sel
         */
        int sel = workingIds.at(addr), mux = b.MUX(),
            ram = b.RAM(addr, indexWRdata);
        b.connect(ram, mux);
        b.connect(wdataInput, mux);
        b.connect(sel, mux);
        b.connect(mux, ram);
        newWorkingIds.push_back(ram);
    }
    workingIds.swap(newWorkingIds);
    newWorkingIds.clear();

    // Create MUXs
    for (int i = 0; i < inAddrWidth; i++) {
        assert(workingIds.size() > 0 && workingIds.size() % 2 == 0);
        for (int j = 0; j < workingIds.size(); j += 2) {
            int id = b.MUX();
            b.connect(workingIds.at(j), id);
            b.connect(workingIds.at(j + 1), id);
            b.connect(addrInputs.at(i), id);
            newWorkingIds.push_back(id);
        }
        workingIds.swap(newWorkingIds);
        newWorkingIds.clear();
    }
    assert(workingIds.size() == 1);

    // Create output "rdata[indexWRdata]"
    int rdataOutput = b.OUTPUT("rdata", indexWRdata);
    b.connect(workingIds.at(0), rdataOutput);
}

#endif
