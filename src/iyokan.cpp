#include "iyokan.hpp"

namespace graph {
std::unordered_map<int, int> doRankuSort(
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

std::unordered_map<int, int> doTopologicalSort(
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
