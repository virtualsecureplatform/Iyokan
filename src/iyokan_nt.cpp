#include "iyokan_nt.hpp"
#include <stdexcept>

namespace nt {

/* class Allocator */

Allocator::Allocator()
{
}

Allocator& Allocator::subAllocator(const std::string& key)
{
    assert(data_.size() == 0);
    auto it = subs_.find(key);
    if (it == subs_.end()) {
        auto sub = std::make_unique<Allocator>();
        std::tie(it, std::ignore) = subs_.emplace(key, std::move(sub));
    }
    return *it->second;
}

/* class ReadyQueue */

bool ReadyQueue::empty() const
{
    return queue_.empty();
}

void ReadyQueue::pop()
{
    queue_.pop();
}

Task* ReadyQueue::peek() const
{
    auto [pri, task] = queue_.top();
    return task;
}

void ReadyQueue::push(Task* task)
{
    queue_.emplace(task->priority(), task);
    task->setQueued();
}

/* class Network */

Network::Network(std::unordered_map<UID, std::unique_ptr<Task>> uid2task)
    : uid2task_(std::move(uid2task))
{
}

size_t Network::size() const
{
    return uid2task_.size();
}

Task* Network::findByUID(UID uid) const
{
    Task* ret = uid2task_.at(uid).get();
    assert(ret != nullptr);
    return ret;
}

Task* Network::findByTags(const std::vector<Tag>& tags) const
{
    // FIXME: More efficient way?
    for (auto&& [uid, task] : uid2task_) {
        const auto& taskTags = task->label().tags;
        bool found = std::all_of(tags.begin(), tags.end(), [&](const Tag& t) {
            auto it = taskTags.find(t.first);
            return it != taskTags.end() && it->second == t.second;
        });
        if (found)
            return task.get();
    }
    throw std::runtime_error("Network::findByTags: not found");
}

void Network::pushReadyTasks(ReadyQueue& readyQueue)
{
    for (auto&& [uid, task] : uid2task_)
        if (task->areAllInputsReady())
            readyQueue.push(task.get());
}

void Network::tick()
{
    for (auto&& [uid, task] : uid2task_)
        task->tick();
}

/* class Worker */

Worker::Worker() : target_(nullptr)
{
}

Worker::~Worker()
{
}

void Worker::update(ReadyQueue& readyQueue, size_t& numFinishedTargets)
{
    if (target_ == nullptr && !readyQueue.empty()) {
        // Try to find the task to tackle next
        Task* cand = readyQueue.peek();
        assert(cand != nullptr);
        if (canExecute(cand)) {
            target_ = cand;
            readyQueue.pop();
            startTask(target_);
        }
    }

    if (target_ != nullptr && target_->hasFinished()) {
        for (Task* child : target_->children()) {
            child->notifyOneInputReady();
            if (!child->hasQueued() && child->areAllInputsReady())
                readyQueue.push(child);
        }
        target_ = nullptr;
        numFinishedTargets++;
    }
}

bool Worker::isWorking() const
{
    return target_ != nullptr;
}

/* class NetworkRunner */

NetworkRunner::NetworkRunner(Network network,
                             std::vector<std::unique_ptr<Worker>> workers)
    : network_(std::move(network)),
      workers_(std::move(workers)),
      readyQueue_(),
      numFinishedTargets_(0)
{
    assert(workers_.size() != 0);
    for (auto&& w : workers)
        assert(w != nullptr);
}

const Network& NetworkRunner::network() const
{
    return network_;
}

size_t NetworkRunner::numFinishedTargets() const
{
    return numFinishedTargets_;
}

void NetworkRunner::prepareToRun()
{
    assert(readyQueue_.empty());

    numFinishedTargets_ = 0;
    network_.pushReadyTasks(readyQueue_);
}

bool NetworkRunner::isRunning() const
{
    return std::any_of(workers_.begin(), workers_.end(),
                       [](auto&& w) { return w->isWorking(); }) ||
           !readyQueue_.empty();
}

void NetworkRunner::update()
{
    for (auto&& w : workers_)
        w->update(readyQueue_, numFinishedTargets_);
}

void NetworkRunner::tick()
{
    network_.tick();
}

/**************************************************/
/***** TEST ***************************************/
/**************************************************/

/*
void testBuildAdder(NetworkBuilder& nb)
{
    UID a = nb.INPUT("0", "A", 0), b = nb.INPUT("1", "B", 0),
        ci = nb.INPUT("2", "Ci", 0), s = nb.OUTPUT("3", "S", 0),
        c0 = nb.OUTPUT("4", "C0", 0), and0 = nb.AND("5"), and1 = nb.AND("6"),
        or0 = nb.OR("7"), xor0 = nb.XOR("8"), xor1 = nb.XOR("9");
    nb.connect(a, xor0);
    nb.connect(b, xor0);
    nb.connect(ci, xor1);
    nb.connect(xor0, xor1);
    nb.connect(a, and0);
    nb.connect(b, and0);
    nb.connect(ci, and1);
    nb.connect(xor0, and1);
    nb.connect(and1, or0);
    nb.connect(and0, or0);
    nb.connect(xor1, s);
    nb.connect(or0, c0);
}
*/

}  // namespace nt
