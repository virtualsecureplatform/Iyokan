#ifndef VIRTUALSECUREPLATFORM_IYOKAN_NT_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_NT_HPP

#include <cassert>
#include <cstdint>

#include <any>
#include <deque>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "error_nt.hpp"
#include "label.hpp"

// Forward declarations
namespace nt {
namespace plain {
class WorkerInfo;
}
class DataHolder;
class Blueprint;
namespace blueprint {
class File;
class BuiltinROM;
class BuiltinRAM;
}  // namespace blueprint
}  // namespace nt
namespace cereal {
class PortableBinaryOutputArchive;
class PortableBinaryInputArchive;
}  // namespace cereal

namespace nt {

class Allocator {
private:
    // Prohibit copying Allcator. Probably moving it is not a problem.
    Allocator(const Allocator&) = delete;
    Allocator& operator=(const Allocator&) = delete;

public:
    using Index = size_t;

private:
    // Allocator can be constructed in 2 ways:
    // - With no arguments. In this case, Allocator has empty data at first, and
    //   make() really makes a new object. The member variable
    //   hasLoadedFromIStream_ is false, and indexToBeMade_ is not used.
    // - With snapshot file path. In this case, Allocator reads data from the
    //   snapshot file, and make() returns data_[indexToBeMade_++]. The member
    //   variable hasLoadedFromIStream_ is true, and indexToBeMade_ indicates
    //   the next index of the data returned by make().
    bool hasLoadedFromIStream_;
    size_t indexToBeMade_;

    // data_ has all the allocated objects in std::any.
    // We use std::deque here since push_back/emplace_back of std::deque do
    // not invalidate the address of its element, while ones of std::vector
    // do.
    std::deque<std::any> data_;

public:
    Allocator();
    Allocator(cereal::PortableBinaryInputArchive& ar);

    void dumpAllocatedData(cereal::PortableBinaryOutputArchive& ar) const;

    template <class T>
    T* get(Index index)
    {
        assert(index < data_.size());
        assert(!hasLoadedFromIStream_ || index < indexToBeMade_);
        T* ret = std::any_cast<T>(&data_.at(index));
        assert(ret != nullptr);
        return ret;
    }

    template <class T>
    T* make()
    {
        if (hasLoadedFromIStream_) {
            return std::any_cast<T>(&data_.at(indexToBeMade_++));
        }
        else {
            std::any& v = data_.emplace_back();
            return &v.emplace<T>();
        }
    }
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

    // Get computation cost of this task. Used for scheduling of tasks.
    virtual int getComputationCost() const;

    virtual void notifyOneInputReady() = 0;
    virtual bool areAllInputsReady() const = 0;
    virtual bool hasFinished() const = 0;

    // tick() resets the internal state of the task for the next cycle
    virtual void tick();

    // Get output value. Only available for output and DFF gates.
    virtual void getOutput(DataHolder&);

    // Set input value. Only available for input gates.
    virtual void setInput(const DataHolder&);

    // onAfterTick() will be called after each tick.
    virtual void onAfterTick(size_t currentCycle);

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
    size_t size() const;
    void add(Task* task);
    Task* findByUID(UID uid) const;
    Task* findByConfigName(const ConfigName& cname) const;

    template <class F>
    void eachTask(F f) const
    {
        for (auto&& [uid, task] : byUID_)
            f(uid, task);
    }
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

    // public output(). Debug purpose only.
    T& DEBUG_output()
    {
        return output();
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
    virtual UID ROM(const std::string& nodeName, const std::string& portName,
                    int portBit) = 0;
    virtual UID RAM(const std::string& nodeName, const std::string& portName,
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
    void onAfterTick(size_t currentCycle);
};

enum class SCHED {
    TOPO,
    RANKU,
};

struct RunParameter {
    std::string blueprintFile, inputFile, outputFile;
    int numCPUWorkers, numCycles, currentCycle;
    SCHED sched;

    std::optional<std::string> snapshotFile;

    void print() const;
};

class Snapshot {
private:
    RunParameter pr_;
    std::shared_ptr<Allocator> alc_;

public:
    Snapshot(const RunParameter& pr, const std::shared_ptr<Allocator>& alc);
    Snapshot(const std::string& snapshotFile);

    const RunParameter& getRunParam() const;
    const std::shared_ptr<Allocator>& getAllocator() const;
    void updateCurrentCycle(int currentCycle);
    void updateNumCycles(int numCycles);
    void dump(const std::string& snapshotFile) const;
};

class Frontend {
private:
    // Prohibit copying Frontend.
    Frontend(const Frontend&) = delete;
    Frontend& operator=(const Frontend&) = delete;

private:
    const RunParameter pr_;
    std::optional<Network> network_;
    int currentCycle_;
    std::unique_ptr<Blueprint> bp_;
    std::shared_ptr<Allocator> alc_;

protected:
    virtual void setBit0(DataHolder& dh) = 0;
    virtual void setBit1(DataHolder& dh) = 0;
    virtual void dumpResPacket(const std::string& outpath,
                               const TaskFinder& finder, int numCycles) = 0;
    virtual std::vector<std::unique_ptr<nt::Worker>> makeWorkers() = 0;

    void buildNetwork(NetworkBuilder& nb);
    const RunParameter& runParam() const
    {
        return pr_;
    }
    const Blueprint& blueprint() const
    {
        return *bp_;
    }
    Allocator& allocator()
    {
        return *alc_;
    }
    const std::shared_ptr<Allocator>& allocatorPtr() const
    {
        return alc_;
    }

public:
    Frontend(const RunParameter& pr);
    Frontend(const Snapshot& snapshot);
    virtual ~Frontend();

    void run();
};

void readNetworkFromFile(const blueprint::File& file, NetworkBuilder& nb);
void makeMUXROM(const blueprint::BuiltinROM& rom, NetworkBuilder& nb);
void makeMUXRAM(const blueprint::BuiltinRAM& ram, NetworkBuilder& nb);

void test0();

}  // namespace nt

#endif
