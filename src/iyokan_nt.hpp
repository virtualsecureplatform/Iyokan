#ifndef VIRTUALSECUREPLATFORM_IYOKAN_NT_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_NT_HPP

#include <any>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace nt {

class Allocator {
private:
    std::unordered_map<std::string, std::unique_ptr<Allocator>> subs_;
    std::vector<std::any> data_;

public:
    Allocator(/* optional snapshot file */)
    {
    }

    Allocator& subAllocator(const std::string& key)
    {
        assert(data_.size() == 0);
        auto it = subs_.find(key);
        if (it == subs_.end()) {
            auto sub = std::make_unique<Allocator>();
            std::tie(it, std::ignore) = subs_.emplace(key, std::move(sub));
        }
        return *it->second;
    }

    template <class T>
    T* make(size_t index)
    {
        assert(subs_.size() == 0);
        if (data_.size() <= index)
            data_.resize(index + 1);
        std::any& v = data_.at(index);
        assert(!v.has_value());
        return &v.emplace<T>();
    }

    template <class T>
    T* get(size_t index)
    {
        assert(subs_.size() == 0);
        assert(index < data_.size());
        T* ret = std::any_cast<T>(&data_.at(index));
        assert(ret != nullptr);
        return ret;
    }
};

enum class TAG_KEY {
    KIND,
    PORT_NAME,
    PORT_BIT,
};
using Tag = std::pair<TAG_KEY, std::string>;

using UID = uint64_t;
struct Label {
    UID uid;
    std::unordered_map<TAG_KEY, std::string> tags;
};

class Task {
private:
    Label label_;
    std::vector<Task*> dependents_;

public:
    Task(Label label);
    virtual ~Task();

    const Label& label() const
    {
        return label_;
    }

    const std::vector<Task*>& dependents() const
    {
        return dependents_;
    }

    void addDependent(Task* task)
    {
        dependents_.push_back(task);
    }

    virtual void notifyOneInputReady() = 0;
    virtual bool areAllInputsReady() const = 0;
    virtual void startAsynchronously() = 0;
    virtual bool hasFinished() const = 0;
    virtual void tick() = 0;  // Reset for next cycle
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
    std::vector<T*> inputs_;
    T* output_;

protected:
    T input(size_t i) const
    {
        return *inputs_.at(i);
    }

    T& output()
    {
        return *output_;
    }

public:
    TaskCommon(Label label, Allocator& alc, size_t numExpectedInputs)
        : Task(std::move(label)),
          numReadyInputs_(0),
          inputs_(numExpectedInputs, nullptr),
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

    void addInput(T* newIn)
    {
        for (size_t i = 0; i < inputs_.size(); i++) {
            if (inputs_.at(i) != nullptr)
                continue;
            inputs_.at(i) = newIn;
            return;
        }
        assert(false && "Too many calls of addInput");
    }

    const T* const getOutput() const
    {
        return output_;
    }
};

class NetworkBuilder {
private:
    Allocator& alc_;
    std::unordered_map<UID, Task*> uid2task_;

public:
    NetworkBuilder(Allocator& alc);
};

class Network {
public:
    Network(NetworkBuilder&& builder);

    Task* findByUID(UID uid) const;
    Task* findByTags(const std::vector<Tag>& tags) const;

    void eachTask(std::function<void(Task*)> handler) const;
};

}  // namespace nt

#include "packet.hpp"

namespace nt {
namespace plain {

class TaskNand : public TaskCommon<Bit> {
public:
    TaskNand(Label label, Allocator& alc) : TaskCommon<Bit>(label, alc, 2)
    {
    }

    ~TaskNand()
    {
    }

    void startAsynchronously() override
    {
        output() = ~(input(0) & input(1));
    }

    bool hasFinished() const override
    {
        return true;
    }
};
}  // namespace plain
}  // namespace nt

#endif
