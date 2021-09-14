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

/* class Network */

Network::Network(std::unordered_map<UID, std::unique_ptr<Task>> uid2task)
    : uid2task_(std::move(uid2task))
{
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
