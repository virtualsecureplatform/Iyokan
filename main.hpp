#ifndef VIRTUALSECUREPLATFORM_IYOKAN_L2_2_MAIN_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_L2_2_MAIN_HPP

/*
     Node --has---> DepNode --has-> dependent DepNode s
      |              |
     has            has
      |              |
      v              v
     Task --is a--> TaskBase
      |
     references
      |
      v
    input Task s
*/

#include <cassert>
#include <iostream>
#include <map>
#include <memory>
#include <queue>
#include <unordered_map>
#include <vector>

template <class WorkerInfo>
class TaskBase {
public:
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

public:
    Task() : numReadyInputs_(0), output_(std::make_shared<OutType>())
    {
    }

    virtual ~Task()
    {
    }

    void addInputPtr(const std::shared_ptr<const InType> &input)
    {
        inputs_.emplace_back(input);
    }

    std::shared_ptr<const OutType> getOutputPtr() const
    {
        return output_;
    }

    virtual void tick() override
    {
        numReadyInputs_ = 0;
    }

    virtual void notifyOneInputReady() override
    {
        numReadyInputs_++;
        if (numReadyInputs_ > inputs_.size())
            throw 0;
    }

    virtual bool areInputsReady() const override
    {
        return numReadyInputs_ == inputs_.size();
    }
};

template <class WorkerInfo>
class DepNode {
private:
    bool alreadyStarted_;
    std::shared_ptr<TaskBase<WorkerInfo>> task_;
    std::vector<std::shared_ptr<DepNode>> dependents_;

public:
    DepNode(std::shared_ptr<TaskBase<WorkerInfo>> task)
        : alreadyStarted_(false), task_(task)
    {
    }

    void addDependent(const std::shared_ptr<DepNode> &dep)
    {
        dependents_.push_back(dep);
    }

    enum class STATUS {
        CONT,
        FINISHED,
    };
    STATUS update(WorkerInfo wi)
    {
        if (!alreadyStarted_)
            task_->startAsync(std::move(wi));

        if (!task_->hasFinished())
            return STATUS::CONT;

        return STATUS::FINISHED;
    }

    void propagate(std::queue<std::shared_ptr<DepNode>> &readyQueue)
    {
        assert(task_->hasFinished());

        for (auto &&dep : dependents_) {
            dep->task_->notifyOneInputReady();
            if (!dep->task_->areInputsReady())
                continue;
            readyQueue.push(dep);
        }
    }
};

template <class WorkerInfo>
class Worker {
protected:
    using QueueType = std::queue<std::shared_ptr<DepNode<WorkerInfo>>>;

private:
    QueueType &readyQueue_;
    size_t &numFinishedTargets_;
    std::shared_ptr<DepNode<WorkerInfo>> target_;

public:
    Worker(QueueType &readyQueue, size_t &numFinishedTargets)
        : readyQueue_(readyQueue),
          numFinishedTargets_(numFinishedTargets),
          target_(nullptr)
    {
    }

    virtual ~Worker()
    {
    }

    void update()
    {
        if (target_ == nullptr && !readyQueue_.empty()) {
            // Found a task to tackle.
            target_ = readyQueue_.front();
            readyQueue_.pop();
            assert(target_ != nullptr);
        }

        if (target_ != nullptr) {
            auto status = target_->update(getWorkerInfo());
            if (status == DepNode<WorkerInfo>::STATUS::FINISHED) {
                // The task has finished.
                target_->propagate(readyQueue_);
                target_ = nullptr;
                numFinishedTargets_++;
            }
        }
    }

protected:
    virtual WorkerInfo getWorkerInfo() = 0;
};

template <class TaskType, class TaskTypeMem, class WorkerInfo>
class NetworkBuilder;

template <class TaskType, class TaskTypeMem, class WorkerInfo>
class TaskNetwork {
public:
    using QueueType = std::queue<std::shared_ptr<DepNode<WorkerInfo>>>;

    struct MemNode {
        std::shared_ptr<TaskTypeMem> task;
        std::shared_ptr<DepNode<WorkerInfo>> depnode;
    };

    struct Node {
        std::shared_ptr<TaskType> task;
        std::shared_ptr<DepNode<WorkerInfo>> depnode;
    };

private:
    std::unordered_map<int, Node> id2node_;
    std::map<std::tuple<std::string, std::string, int>, MemNode> namedMems_;

private:
    MemNode &mem(const std::string &kind, const std::string &portName,
                 int portBit)
    {
        auto it = namedMems_.find(std::make_tuple(kind, portName, portBit));
        assert(it != namedMems_.end());
        return it->second;
    }

public:
    TaskNetwork(NetworkBuilder<TaskType, TaskTypeMem, WorkerInfo> &&builder);

    size_t numNodes() const
    {
        return id2node_.size();
    }

    QueueType getReadyQueue() const
    {
        QueueType queue;
        for (auto &&[id, node] : id2node_)
            if (node.task->areInputsReady())
                queue.push(node.depnode);
        return queue;
    }

    Node &node(int id)
    {
        return id2node_[id];
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
            node.task->tick();
    }
};

template <class TaskType, class TaskTypeMem, class WorkerInfo>
class NetworkBuilder {
    friend TaskNetwork<TaskType, TaskTypeMem, WorkerInfo>;

public:
    using NetworkType = TaskNetwork<TaskType, TaskTypeMem, WorkerInfo>;

private:
    std::unordered_map<int, typename NetworkType::Node> id2node_;

    std::map<std::tuple<std::string, std::string, int>,
             typename NetworkType::MemNode>
        namedMems_;

private:
    typename NetworkType::MemNode addMem(bool clockNeeded, int id)
    {
        auto task = std::make_shared<TaskTypeMem>(clockNeeded);
        auto depnode = std::make_shared<DepNode<WorkerInfo>>(task);
        id2node_.emplace(id, typename NetworkType::Node{task, depnode});
        return typename NetworkType::MemNode{task, depnode};
    }

    void addNamedMem(bool clockNeeded, int id, const std::string &kind,
                     const std::string &portName, int portBit)
    {
        namedMems_.emplace(std::make_tuple(kind, portName, portBit),
                           addMem(clockNeeded, id));
    }

public:
    void DFF(int id)
    {
        addMem(true, id);
    }

    void ROM(int id, const std::string &portName, int portBit)
    {
        addNamedMem(false, id, "rom", portName, portBit);
    }

    void RAM(int id, const std::string &portName, int portBit)
    {
        addNamedMem(true, id, "ram", portName, portBit);
    }

    void INPUT(int id, const std::string &portName, int portBit)
    {
        addNamedMem(false, id, "input", portName, portBit);
    }

    void OUTPUT(int id, const std::string &portName, int portBit)
    {
        addNamedMem(false, id, "output", portName, portBit);
    }

    void connect(int from, int to)
    {
        auto fromIt = id2node_.find(from);
        assert(fromIt != id2node_.end());
        auto toIt = id2node_.find(to);
        assert(toIt != id2node_.end());

        toIt->second.task->addInputPtr(fromIt->second.task->getOutputPtr());
        fromIt->second.depnode->addDependent(toIt->second.depnode);
    }

#define DEFINE_GATE(name)                                                \
protected:                                                               \
    virtual std::shared_ptr<TaskType> name##Impl() = 0;                  \
                                                                         \
public:                                                                  \
    void name(int id)                                                    \
    {                                                                    \
        auto task = name##Impl();                                        \
        auto depnode = std::make_shared<DepNode<WorkerInfo>>(task);      \
        id2node_.emplace(id, typename NetworkType::Node{task, depnode}); \
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

template <class TaskType, class TaskTypeMem, class WorkerInfo>
TaskNetwork<TaskType, TaskTypeMem, WorkerInfo>::TaskNetwork(
    NetworkBuilder<TaskType, TaskTypeMem, WorkerInfo> &&builder)
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
            builder.INPUT(id, portName, portBit);
        }
        else if (type == "output") {
            builder.OUTPUT(id, portName, portBit);
        }
    }
    for (const auto &e : cells) {  // vectorをrange-based-forでまわしている。
        picojson::object cell = e.get<picojson::object>();
        std::string type = cell.at("type").get<std::string>();
        int id = static_cast<int>(cell.at("id").get<double>());
        int priority = static_cast<int>(cell.at("priority").get<double>());
        if (type == "AND") {
            builder.AND(id);
        }
        else if (type == "NAND") {
            builder.NAND(id);
        }
        else if (type == "ANDNOT") {
            builder.ANDNOT(id);
        }
        else if (type == "XOR") {
            builder.XOR(id);
        }
        else if (type == "XNOR") {
            builder.XNOR(id);
        }
        else if (type == "DFFP") {
            builder.DFF(id);
        }
        else if (type == "NOT") {
            builder.NOT(id);
        }
        else if (type == "NOR") {
            builder.NOR(id);
        }
        else if (type == "OR") {
            builder.OR(id);
        }
        else if (type == "ORNOT") {
            builder.ORNOT(id);
        }
        else if (type == "MUX") {
            builder.MUX(id);
        }
        else if (type == "ROM") {
            int romAddress =
                static_cast<int>(cell.at("romAddress").get<double>());
            int romBit = static_cast<int>(cell.at("romBit").get<double>());
            builder.ROM(id, std::to_string(romAddress), romBit);
        }
        else if (type == "RAM") {
            int ramAddress =
                static_cast<int>(cell.at("ramAddress").get<double>());
            int ramBit = static_cast<int>(cell.at("ramBit").get<double>());
            builder.RAM(id, std::to_string(ramAddress), ramBit);
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
            for (const auto &b : bits) {
                int logic = static_cast<int>(b.get<double>());
                builder.connect(id, logic);
            }
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
            picojson::array &Y = output.at("Y").get<picojson::array>();
            builder.connect(A, id);
            builder.connect(B, id);
            for (const auto &y : Y) {
                int bitY = static_cast<int>(y.get<double>());
                builder.connect(id, bitY);
            }
        }
        else if (type == "DFFP") {
            int D = static_cast<int>(input.at("D").get<double>());
            picojson::array &Q = output.at("Q").get<picojson::array>();
            builder.connect(D, id);
            for (const auto &q : Q) {
                int bitQ = static_cast<int>(q.get<double>());
                builder.connect(id, bitQ);
            }
        }
        else if (type == "NOT") {
            int A = static_cast<int>(input.at("A").get<double>());
            picojson::array &Y = output.at("Y").get<picojson::array>();
            builder.connect(A, id);
            for (const auto &y : Y) {
                int bitY = static_cast<int>(y.get<double>());
                builder.connect(id, bitY);
            }
        }
        else if (type == "MUX") {
            int A = static_cast<int>(input.at("A").get<double>());
            int B = static_cast<int>(input.at("B").get<double>());
            int S = static_cast<int>(input.at("S").get<double>());
            picojson::array &Y = output.at("Y").get<picojson::array>();
            builder.connect(A, id);
            builder.connect(B, id);
            builder.connect(S, id);
            for (const auto &y : Y) {
                int bitY = static_cast<int>(y.get<double>());
                builder.connect(id, bitY);
            }
        }
        else if (type == "ROM") {
            picojson::array &Q = output.at("Q").get<picojson::array>();
            for (const auto &q : Q) {
                int bitQ = static_cast<int>(q.get<double>());
                builder.connect(id, bitQ);
            }
        }
        else if (type == "RAM") {
            int D = static_cast<int>(input.at("D").get<double>());
            picojson::array &Q = output.at("Q").get<picojson::array>();
            builder.connect(D, id);
            for (const auto &q : Q) {
                int bitQ = static_cast<int>(q.get<double>());
                builder.connect(id, bitQ);
            }
        }
        else {
            throw std::runtime_error("Not executed");
        }
    }
}

#endif
