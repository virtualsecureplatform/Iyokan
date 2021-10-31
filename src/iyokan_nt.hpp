#ifndef VIRTUALSECUREPLATFORM_IYOKAN_NT_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_NT_HPP

#include <cassert>
#include <cstdint>

#include <any>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "error_nt.hpp"

namespace nt {

// Forward declarations
namespace plain {
class WorkerInfo;
}
class DataHolder;

class Allocator {
public:
    using Index = size_t;

private:
    std::vector<std::unique_ptr<std::any>> data_;

public:
    Allocator(/* optional snapshot file */);

    template <class T>
    T* make()
    {
        data_.emplace_back(std::make_unique<std::any>());
        std::any& v = *data_.back();
        return &v.emplace<T>();
    }

    template <class T>
    T* get(Index index)
    {
        assert(index < data_.size());
        T* ret = std::any_cast<T>(data_.at(index).get());
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

class Task {
private:
    Label label_;
    std::vector<Task*> parents_, children_;
    int priority_;
    bool hasQueued_;

public:
    Task(Label label);
    virtual ~Task();

    const Label& label() const;
    const std::vector<Task*>& parents() const;
    const std::vector<Task*>& children() const;
    int priority() const;
    bool hasQueued() const;
    void addChild(Task* task);
    void addParent(Task* task);
    void setPriority(int newPri);
    void setQueued();

    virtual void notifyOneInputReady() = 0;
    virtual bool areAllInputsReady() const = 0;
    virtual bool hasFinished() const = 0;

    // tick() resets the internal state of the task for the next cycle
    virtual void tick();

    // Get output value. Only available for output gates.
    virtual void getOutput(DataHolder&);

    // Set input value. Only available for input gates.
    virtual void setInput(const DataHolder&);

    // onAfterFirstTick() will be called after the first tick.
    virtual void onAfterFirstTick();

    // Return true iff this task can be run in plaintext mode.
    virtual bool canRunPlain() const;

    // Start this task asynchronously in plaintext mode.
    // Only available when canRunPlain() returns true.
    virtual void startAsynchronously(plain::WorkerInfo&);
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
          output_(alc.make<T>())
    {
    }

    virtual ~TaskCommon()
    {
    }

    virtual void notifyOneInputReady() override
    {
        numReadyInputs_++;
        assert(numReadyInputs_ <= inputs_.size());
    }

    virtual bool areAllInputsReady() const override
    {
        return numReadyInputs_ == inputs_.size();
    }

    virtual void tick() override
    {
        Task::tick();
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

// class TaskDFF can be used as base class of DFF tasks.
// TaskDFF inherits TaskCommon, so it has addInput member functions.
// NetworkBuilder can use it to connect common gates with DFFs.
template <class T>
class TaskDFF : public TaskCommon<T> {
public:
    TaskDFF(Label label, Allocator& alc)
        : TaskCommon<T>(std::move(label), alc, 1)
    {
    }

    virtual ~TaskDFF()
    {
    }

    void notifyOneInputReady() override
    {
        ERR_UNREACHABLE;
    }

    bool areAllInputsReady() const override
    {
        // Since areAllInputsReady() is called after calling of tick(), the
        // input should already be in output().
        return true;
    }

    bool hasFinished() const override
    {
        // Since hasFinished() is called after calling of tick(), the
        // input should already be in output().
        return true;
    }

    void tick() override
    {
        TaskCommon<T>::tick();
        this->output() = this->input(0);
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

    template <class F>
    void eachTask(F f) const
    {
        for (auto&& task : tasks_)
            f(task.get());
    }

    template <class F>
    void eachTask(F f)
    {
        for (auto&& task : tasks_)
            f(task.get());
    }
};

class NetworkBuilder {
private:
    TaskFinder finder_;
    std::vector<std::unique_ptr<Task>> tasks_;
    bool consumed_;
    Allocator* alc_;

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
    NetworkBuilder(Allocator& alc);
    virtual ~NetworkBuilder();

    const TaskFinder& finder() const;

    Network createNetwork();

    virtual void connect(UID from, UID to) = 0;

    // not/and/or are C++ keywords, so the member functions here are in
    // capitals.
    virtual UID INPUT(const std::string& nodeName, const std::string& portName,
                      int portBit) = 0;
    virtual UID OUTPUT(const std::string& nodeName, const std::string& portName,
                       int portBit) = 0;

    virtual UID AND() = 0;
    virtual UID ANDNOT() = 0;
    virtual UID CONSTONE() = 0;
    virtual UID CONSTZERO() = 0;
    virtual UID DFF() = 0;
    virtual UID MUX() = 0;
    virtual UID NAND() = 0;
    virtual UID NMUX() = 0;
    virtual UID NOR() = 0;
    virtual UID NOT() = 0;
    virtual UID OR() = 0;
    virtual UID ORNOT() = 0;
    virtual UID SDFF0() = 0;
    virtual UID SDFF1() = 0;
    virtual UID XNOR() = 0;
    virtual UID XOR() = 0;
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

private:
    void prepareToRun();
    void update();

public:
    NetworkRunner(Network network,
                  std::vector<std::unique_ptr<Worker>> workers);

    const Network& network() const;
    size_t numFinishedTargets() const;
    bool isRunning() const;
    void run();
    void tick();
    void onAfterFirstTick();
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

void readYosysJSONNetwork(const std::string& nodeName, std::istream& is,
                          NetworkBuilder& nb);
void readIyokanL1JSONNetwork(const std::string& nodeName, std::istream& is,
                             NetworkBuilder& nb);

}  // namespace nt

#endif
