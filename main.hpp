#ifndef VIRTUALSECUREPLATFORM_IYOKAN_L2_2_MAIN_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_L2_2_MAIN_HPP

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
    std::vector<std::shared_ptr<const InType>> inputs_;

protected:
    size_t inputSize() const
    {
        return inputs_.size();
    }

    const InType &input(size_t index) const
    {
        return *inputs_.at(index);
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

    void addInputPtr(std::shared_ptr<const InType> input)
    {
        inputs_.push_back(std::move(input));
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

template <class TaskType, class TaskTypeINPUT, class TaskTypeOUTPUT,
          class WorkerInfo>
class NetworkBuilder {
public:
    using QueueType = std::queue<std::shared_ptr<DepNode<WorkerInfo>>>;

public:
    struct InputNode {
        std::shared_ptr<TaskTypeINPUT> task;
        std::shared_ptr<DepNode<WorkerInfo>> depnode;
    };
    struct OutputNode {
        std::shared_ptr<TaskTypeOUTPUT> task;
        std::shared_ptr<DepNode<WorkerInfo>> depnode;
    };
    struct Node {
        std::shared_ptr<TaskType> task;
        std::shared_ptr<DepNode<WorkerInfo>> depnode;
    };

private:
    std::unordered_map<int, Node> id2node_;

    std::map<std::pair<std::string, int>, InputNode> inputs_;
    std::map<std::pair<std::string, int>, OutputNode> outputs_;

public:
    NetworkBuilder()
    {
    }

    std::unordered_map<int, Node> nodes() const
    {
        return id2node_;
    }

    std::map<std::pair<std::string, int>, InputNode> inputs() const
    {
        return inputs_;
    }

    std::map<std::pair<std::string, int>, OutputNode> outputs() const
    {
        return outputs_;
    }

    /*
    std::unordered_map<std::string, std::vector<InputNode>> inputs() const
    {
        // Convert map[(string, int)]InputNode to map[string]([]InputNode)
        std::unordered_map<std::string, std::vector<InputNode>> ret;
        for (auto &&[key, node] : inputs_) {
            std::vector<InputNode> &v = ret[key.first];
            if (v.size() < key.second + 1)
                v.resize(key.second + 1, InputNode{nullptr, nullptr});
            v.at(key.second) = node;
        }
        return ret;
    }

    std::unordered_map<std::string, std::vector<OutputNode>> outputs() const
    {
        // Convert map[(string, int)]OutputNode to map[string]([]OutputNode)
        std::unordered_map<std::string, std::vector<OutputNode>> ret;
        for (auto [key, node] : outputs_) {
            std::vector<OutputNode> &v = ret[key.first];
            if (v.size() < key.second + 1)
                v.resize(key.second + 1, OutputNode{nullptr, nullptr});
            v.at(key.second) = node;
        }
        return ret;
    }
    */

    void INPUT(int id, const std::string &portName, int portBit)
    {
        auto task = std::make_shared<TaskTypeINPUT>();
        auto depnode = std::make_shared<DepNode<WorkerInfo>>(task);
        id2node_.emplace(id, Node{task, depnode});

        inputs_[std::make_pair(portName, portBit)] = InputNode{task, depnode};
    }

    void OUTPUT(int id, const std::string &portName, int portBit)
    {
        auto task = std::make_shared<TaskTypeOUTPUT>();
        auto depnode = std::make_shared<DepNode<WorkerInfo>>(task);
        id2node_.emplace(id, Node{task, depnode});

        outputs_[std::make_pair(portName, portBit)] = OutputNode{task, depnode};
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

#define DEFINE_GATE(name)                                           \
protected:                                                          \
    virtual std::shared_ptr<TaskType> name##Impl() = 0;             \
                                                                    \
public:                                                             \
    void name(int id)                                               \
    {                                                               \
        auto task = name##Impl();                                   \
        auto depnode = std::make_shared<DepNode<WorkerInfo>>(task); \
        id2node_.emplace(id, Node{task, depnode});                  \
    }
    DEFINE_GATE(AND);
    DEFINE_GATE(NAND);
    DEFINE_GATE(ANDNOT);
    DEFINE_GATE(OR);
    DEFINE_GATE(ORNOT);
    DEFINE_GATE(XOR);
    DEFINE_GATE(XNOR);
    DEFINE_GATE(MUX);
    DEFINE_GATE(NOT);
#undef DEFINE_GATE
};

using TaskPlainGate = Task<uint8_t, uint8_t, uint8_t /*dummy*/>;

// TaskPlainGateMem can be used as INPUT/OUTPUT/ROM/RAM/DFF depending on how to
// connect it to other gates.
class TaskPlainGateMem : public TaskPlainGate {
private:
    uint8_t val_;

public:
    TaskPlainGateMem()
    {
    }

    void startAsync(uint8_t) override
    {
        if (inputSize() == 0) {  // INPUT / ROM
            output() = val_;
        }
        else {  // OUTPUT / RAM / DFF
            val_ = input(0);
            output() = val_;
        }
    }

    bool hasFinished() const override
    {
        return true;
    }

    void set(uint8_t val)
    {
        val_ = val & 1;
    }

    uint8_t get() const
    {
        return val_;
    }
};

#define DEFINE_TASK_PLAIN_GATE(name, expr)             \
    class TaskPlainGate##name : public TaskPlainGate { \
    public:                                            \
        void startAsync(uint8_t) override              \
        {                                              \
            output() = (expr)&1;                       \
        }                                              \
        bool hasFinished() const override              \
        {                                              \
            return true;                               \
        }                                              \
    };
DEFINE_TASK_PLAIN_GATE(AND, (input(0) & input(1)));
DEFINE_TASK_PLAIN_GATE(NAND, ~(input(0) & input(1)));
DEFINE_TASK_PLAIN_GATE(ANDNOT, (input(0) & ~input(1)));
DEFINE_TASK_PLAIN_GATE(OR, (input(0) | input(1)));
DEFINE_TASK_PLAIN_GATE(ORNOT, (input(0) | ~input(1)));
DEFINE_TASK_PLAIN_GATE(XOR, (input(0) ^ input(1)));
DEFINE_TASK_PLAIN_GATE(XNOR, ~(input(0) ^ input(1)));
DEFINE_TASK_PLAIN_GATE(MUX, input(2) == 0 ? input(0) : input(1));
DEFINE_TASK_PLAIN_GATE(NOT, ~input(0));
#undef DEFINE_TASK_PLAIN_GATE

class PlainNetworkBuilder
    : public NetworkBuilder<TaskPlainGate, TaskPlainGateMem, TaskPlainGateMem,
                            uint8_t /* dummy */> {
private:
#define DEFINE_GATE_IMPL(name)                           \
    std::shared_ptr<TaskPlainGate> name##Impl() override \
    {                                                    \
        return std::make_shared<TaskPlainGate##name>();  \
    }
    DEFINE_GATE_IMPL(AND);
    DEFINE_GATE_IMPL(NAND);
    DEFINE_GATE_IMPL(ANDNOT);
    DEFINE_GATE_IMPL(OR);
    DEFINE_GATE_IMPL(ORNOT);
    DEFINE_GATE_IMPL(XOR);
    DEFINE_GATE_IMPL(XNOR);
    DEFINE_GATE_IMPL(MUX);
    DEFINE_GATE_IMPL(NOT);
#undef DEFINE_GATE_IMPL
};

class PlainWorker : public Worker<uint8_t /* dummy */> {
private:
    uint8_t getWorkerInfo()
    {
        return 0;  // dummy
    }

public:
    PlainWorker(Worker<uint8_t>::QueueType &readyQueue,
                size_t &numFinishedTargets)
        : Worker(readyQueue, numFinishedTargets)
    {
    }
};

#endif
