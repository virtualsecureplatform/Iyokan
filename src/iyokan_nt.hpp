#ifndef VIRTUALSECUREPLATFORM_IYOKAN_NT_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_NT_HPP

#include <any>
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace nt {

class Allocator {
private:
    std::unordered_map<std::string, std::unique_ptr<Allocator>> subs_;
    std::vector<std::any> data_;

public:
    Allocator(/* optional snapshot file */);

    Allocator& subAllocator(const std::string& key);

    template <class T>
    T* make(size_t index)
    {
        if (data_.size() <= index)
            data_.resize(index + 1);
        std::any& v = data_.at(index);
        assert(!v.has_value());
        return &v.emplace<T>();
    }

    template <class T>
    T* get(size_t index)
    {
        assert(index < data_.size());
        T* ret = std::any_cast<T>(&data_.at(index));
        assert(ret != nullptr);
        return ret;
    }
};

using UID = uint64_t;
struct ConfigName {
    std::string nodeName, portName;
    int portBit;
};
struct Label {
    UID uid;
    std::string kind;
    std::optional<ConfigName> cname;
};

namespace plain {
class WorkerInfo;
}

class Task {
private:
    Label label_;
    std::vector<Task*> parents_, children_;
    int priority_;
    bool hasQueued_;

public:
    Task(Label label) : label_(label)
    {
    }

    virtual ~Task()
    {
    }

    const Label& label() const
    {
        return label_;
    }

    const std::vector<Task*>& parents() const
    {
        return parents_;
    }

    const std::vector<Task*>& children() const
    {
        return children_;
    }

    int priority() const
    {
        return priority_;
    }

    bool hasQueued() const
    {
        return hasQueued_;
    }

    void addChild(Task* task)
    {
        assert(task != nullptr);
        children_.push_back(task);
    }

    void addParent(Task* task)
    {
        assert(task != nullptr);
        parents_.push_back(task);
    }

    void setPriority(int newPri)
    {
        priority_ = newPri;
    }

    void setQueued()
    {
        assert(!hasQueued_);
        hasQueued_ = true;
    }

    virtual void notifyOneInputReady() = 0;
    virtual bool areAllInputsReady() const = 0;
    virtual bool hasFinished() const = 0;
    virtual void tick() = 0;  // Reset for next cycle

    virtual bool canRunPlain() const
    {
        return false;
    }

    virtual void startAsynchronously(plain::WorkerInfo&)
    {
        assert(0 && "Internal error: not implemented task for plain mode");
    }
};

class TaskFinder {
private:
    std::unordered_map<UID, Task*> byUID_;
    std::map<std::tuple<std::string, std::string, int>, Task*> byConfigName_;

public:
    void add(Task* task);
    Task* findByUID(UID uid) const;
    Task* findByConfigName(const ConfigName& cname) const;
};

// TaskCommon can be used as base class of many "common" tasks.
// "Common" here means:
//   1. # of outputs is 1. (#inputs can be >1.)
//   2. All the inputs and output have the same type.
//   3. (and so on)
// TaskCommon guarantees:
//   1. Its output exists in alc.get<T>(0).
template <class T>
class TaskCommon : public Task {
private:
    size_t numReadyInputs_;
    const size_t numMinExpectedInputs_, numMaxExpectedInputs_;
    std::vector<T*> inputs_;
    T* output_;

protected:
    size_t getInputSize() const
    {
        return inputs_.size();
    }

    const T& input(size_t i) const
    {
        assert(i < inputs_.size());
        return *inputs_.at(i);
    }

    T& output()
    {
        assert(output_ != nullptr);
        return *output_;
    }

public:
    TaskCommon(Label label, Allocator& alc, size_t numMinExpectedInputs,
               std::optional<size_t> numMaxExpectedInputs = std::nullopt)
        : Task(std::move(label)),
          numReadyInputs_(0),
          numMinExpectedInputs_(numMinExpectedInputs),
          numMaxExpectedInputs_(
              numMaxExpectedInputs.value_or(numMinExpectedInputs)),
          inputs_(),
          output_(alc.make<T>(0))
    {
    }

    virtual ~TaskCommon()
    {
    }

    void notifyOneInputReady() override
    {
        numReadyInputs_++;
        assert(numReadyInputs_ <= inputs_.size());
    }

    bool areAllInputsReady() const override
    {
        return numReadyInputs_ == inputs_.size();
    }

    void tick() override
    {
        numReadyInputs_ = 0;
    }

    void addInput(TaskCommon<T>* newIn)
    {
        assert(newIn != nullptr);
        addInput(newIn, newIn->output_);
    }

    void addInput(Task* newParent, T* newIn)
    {
        assert(newParent != nullptr && newIn != nullptr);
        assert(inputs_.size() < numMaxExpectedInputs_);

        addParent(newParent);
        newParent->addChild(this);

        inputs_.push_back(newIn);
    }
};

class ReadyQueue {
private:
    std::priority_queue<std::pair<int, Task*>> queue_;

public:
    bool empty() const;
    void pop();
    Task* peek() const;
    void push(Task* task);
};

class Network {
private:
    TaskFinder finder_;
    std::vector<std::unique_ptr<Task>> tasks_;

public:
    Network(TaskFinder finder, std::vector<std::unique_ptr<Task>> tasks);

    size_t size() const;
    const TaskFinder& finder() const;

    void pushReadyTasks(ReadyQueue& readyQueue);
    void tick();
    void eachTask(std::function<void(Task*)> handler) const;
};

class NetworkBuilder {
private:
    TaskFinder finder_;
    std::vector<std::unique_ptr<Task>> tasks_;
    bool consumed_;
    Allocator* currentAlc_;

protected:
    // Create a new task. T must be derived from class Task.
    template <class T, class... Args>
    T* emplaceTask(Args&&... args)
    {
        assert(!consumed_);
        T* task = new T(std::forward<Args>(args)...);
        tasks_.emplace_back(task);
        finder_.add(task);
        return task;
    }

    Allocator& currentAllocator();

public:
    NetworkBuilder(Allocator& alcRoot);
    virtual ~NetworkBuilder();

    const TaskFinder& finder() const;

    Network createNetwork();

    void withSubAllocator(const std::string& alcKey,
                          std::function<void(NetworkBuilder&)> f);

    virtual void connect(UID from, UID to) = 0;

    // not/and/or are C++ keywords, so member functions here are in capitals.
    virtual UID INPUT(const std::string& alcKey, const std::string& nodeName,
                      const std::string& portName, int portBit) = 0;
    virtual UID OUTPUT(const std::string& alcKey, const std::string& nodeName,
                       const std::string& portName, int portBit) = 0;
    virtual UID CONSTONE(const std::string& alcKey) = 0;
    virtual UID CONSTZERO(const std::string& alcKey) = 0;
    // virtual UID AND(const std::string& alcKey) = 0;
    // virtual UID ANDNOT(const std::string& alcKey) = 0;
    // virtual UID MUX(const std::string& alcKey) = 0;
    virtual UID NAND(const std::string& alcKey) = 0;
    // virtual UID NMUX(const std::string& alcKey) = 0;
    // virtual UID NOR(const std::string& alcKey) = 0;
    // virtual UID NOT(const std::string& alcKey) = 0;
    // virtual UID OR(const std::string& alcKey) = 0;
    // virtual UID ORNOT(const std::string& alcKey) = 0;
    // virtual UID XNOR(const std::string& alcKey) = 0;
    // virtual UID XOR(const std::string& alcKey) = 0;
};

class Worker {
private:
    Task* target_;

public:
    Worker();
    virtual ~Worker();

    void update(ReadyQueue& readyQueue, size_t& numFinishedTargets);
    bool isWorking() const;

protected:
    virtual void startTask(Task* task) = 0;
    virtual bool canExecute(Task* task) = 0;
};

class NetworkRunner {
private:
    Network network_;
    std::vector<std::unique_ptr<Worker>> workers_;
    ReadyQueue readyQueue_;
    size_t numFinishedTargets_;

public:
    NetworkRunner(Network network,
                  std::vector<std::unique_ptr<Worker>> workers);

    const Network& network() const;
    size_t numFinishedTargets() const;
    void prepareToRun();
    bool isRunning() const;
    void update();
    void tick();
};

namespace blueprint {  // blueprint components
struct File {
    enum class TYPE {
        IYOKANL1_JSON,
        YOSYS_JSON,
    } type;
    std::string path, name;
};

struct BuiltinROM {
    enum class TYPE {
        CMUX_MEMORY,
        MUX,
    } type;
    std::string name;
    size_t inAddrWidth, outRdataWidth;
};

struct BuiltinRAM {
    enum class TYPE {
        CMUX_MEMORY,
        MUX,
    } type;
    std::string name;
    size_t inAddrWidth, inWdataWidth, outRdataWidth;
};

struct Port {
    std::string nodeName, kind, portName;
    int portBit;
};
}  // namespace blueprint

class Blueprint {
private:
    std::string sourceFile_, source_;

    std::vector<blueprint::File> files_;
    std::vector<blueprint::BuiltinROM> builtinROMs_;
    std::vector<blueprint::BuiltinRAM> builtinRAMs_;
    std::vector<std::pair<blueprint::Port, blueprint::Port>> edges_;

    std::map<std::tuple<std::string, int>, blueprint::Port> atPorts_;
    std::unordered_map<std::string, int> atPortWidths_;

private:
    std::vector<blueprint::Port> parsePortString(const std::string& src,
                                                 const std::string& kind);

public:
    Blueprint(const std::string& fileName);

    bool needsCircuitKey() const;
    const std::string& sourceFile() const;
    const std::string& source() const;
    const std::vector<blueprint::File>& files() const;
    const std::vector<blueprint::BuiltinROM>& builtinROMs() const;
    const std::vector<blueprint::BuiltinRAM>& builtinRAMs() const;
    const std::vector<std::pair<blueprint::Port, blueprint::Port>>& edges()
        const;
    const std::map<std::tuple<std::string, int>, blueprint::Port>& atPorts()
        const;
    std::optional<blueprint::Port> at(const std::string& portName,
                                      int portBit = 0) const;
    const std::unordered_map<std::string, int>& atPortWidths() const;
};

}  // namespace nt

#endif
