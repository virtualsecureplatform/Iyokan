#include "main.hpp"

//
#include "plain.hpp"

//
#include <fstream>

template <class Inputs, class AllNodes>
void processAllGates(Inputs& inputs, AllNodes& allNodes, int numWorkers)
{
    // Create workers.
    PlainNetworkBuilder::QueueType readyQueue;
    size_t numFinishedTargets = 0;
    std::vector<PlainWorker> workers;
    for (int i = 0; i < numWorkers; i++)
        workers.emplace_back(readyQueue, numFinishedTargets);

    // Push input nodes into ready queue.
    for (auto&& [portName, node] : inputs)
        readyQueue.push(node.depnode);

    // Process all targets.
    while (numFinishedTargets < allNodes.size())
        for (auto&& w : workers)
            w.update();
}

template <class AllNodes>
void tickGates(AllNodes& allNodes)
{
    // Go next.
    for (auto&& [id, node] : allNodes)
        node.task->tick();
}

void testPlainNOT()
{
    PlainNetworkBuilder net;
    net.INPUT(0, "A", 0);
    net.NOT(1);
    net.OUTPUT(2, "out", 0);
    net.connect(0, 1);
    net.connect(1, 2);

    auto inputs = net.inputs();
    auto outputs = net.outputs();
    auto allNodes = net.nodes();

    auto outIt = outputs.find(std::make_pair("out", 0));
    assert(outIt != outputs.end());
    std::shared_ptr<TaskPlainGateMem> out = outIt->second.task;

    std::array<std::tuple<int, int>, 8> invals{{{0, 1}, {0, 1}}};
    for (int i = 0; i < 2; i++) {
        // Set inputs.
        inputs[std::make_pair("A", 0)].task->set(std::get<0>(invals[i]));

        processAllGates(inputs, allNodes, 2);

        // Check if results are okay.
        assert(out->get() == std::get<1>(invals[i]));

        tickGates(allNodes);
    }
}

void testPlainMUX()
{
    PlainNetworkBuilder net;
    net.INPUT(0, "A", 0);
    net.INPUT(1, "B", 0);
    net.INPUT(2, "S", 0);
    net.MUX(3);
    net.OUTPUT(4, "out", 0);
    net.connect(0, 3);
    net.connect(1, 3);
    net.connect(2, 3);
    net.connect(3, 4);

    auto inputs = net.inputs();
    auto outputs = net.outputs();
    auto allNodes = net.nodes();

    auto outIt = outputs.find(std::make_pair("out", 0));
    assert(outIt != outputs.end());
    std::shared_ptr<TaskPlainGateMem> out = outIt->second.task;

    std::array<std::tuple<int, int, int, int>, 8> invals{{/*A,B, S, O*/
                                                          {0, 0, 0, 0},
                                                          {0, 0, 1, 0},
                                                          {0, 1, 0, 0},
                                                          {0, 1, 1, 1},
                                                          {1, 0, 0, 1},
                                                          {1, 0, 1, 0},
                                                          {1, 1, 0, 1},
                                                          {1, 1, 1, 1}}};
    for (int i = 0; i < 8; i++) {
        // Set inputs.
        inputs[std::make_pair("A", 0)].task->set(std::get<0>(invals[i]));
        inputs[std::make_pair("B", 0)].task->set(std::get<1>(invals[i]));
        inputs[std::make_pair("S", 0)].task->set(std::get<2>(invals[i]));

        processAllGates(inputs, allNodes, 2);

        // Check if results are okay.
        assert(out->get() == std::get<3>(invals[i]));

        tickGates(allNodes);
    }
}

void testPlainBinopGates()
{
    PlainNetworkBuilder net;
    net.INPUT(0, "in0", 0);
    net.INPUT(1, "in1", 0);

    int nextId = 10;

    std::unordered_map<std::string, std::array<uint8_t, 4 /* 00, 01, 10, 11 */>>
        id2res;

#define DEFINE_BINOP_GATE_TEST(name, e00, e01, e10, e11) \
    do {                                                 \
        int gateId = nextId++;                           \
        int outputId = nextId++;                         \
        net.name(gateId);                                \
        net.OUTPUT(outputId, "out_" #name, 0);           \
        net.connect(0, gateId);                          \
        net.connect(1, gateId);                          \
        net.connect(gateId, outputId);                   \
        id2res["out_" #name] = {e00, e01, e10, e11};     \
    } while (false);
    DEFINE_BINOP_GATE_TEST(AND, 0, 0, 0, 1);
    DEFINE_BINOP_GATE_TEST(NAND, 1, 1, 1, 0);
    DEFINE_BINOP_GATE_TEST(ANDNOT, 0, 0, 1, 0);
    DEFINE_BINOP_GATE_TEST(OR, 0, 1, 1, 1);
    DEFINE_BINOP_GATE_TEST(ORNOT, 1, 0, 1, 1);
    DEFINE_BINOP_GATE_TEST(XOR, 0, 1, 1, 0);
    DEFINE_BINOP_GATE_TEST(XNOR, 1, 0, 0, 1);
#undef DEFINE_BINOP_GATE_TEST

    auto inputs = net.inputs();
    auto outputs = net.outputs();
    auto allNodes = net.nodes();

    std::array<std::pair<int, int>, 4> invals{{{0, 0}, {0, 1}, {1, 0}, {1, 1}}};
    for (int i = 0; i < 4; i++) {
        // Set inputs.
        inputs[std::make_pair("in0", 0)].task->set(invals[i].first);
        inputs[std::make_pair("in1", 0)].task->set(invals[i].second);

        processAllGates(inputs, allNodes, 7);

        // Check if results are okay.
        for (auto&& [key, node] : outputs)
            assert(id2res[key.first][i] == node.task->get());

        tickGates(allNodes);
    }
}

void testPlainFromJSONtest_pass_4bit()
{
    const std::string fileName = "test/test-pass-4bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto [inputs, outputs, allNodes] =
        readNetworkFromJSON<PlainNetworkBuilder>(ifs);
    inputs[std::make_pair("io_in", 0)].task->set(0);
    inputs[std::make_pair("io_in", 1)].task->set(1);
    inputs[std::make_pair("io_in", 2)].task->set(1);
    inputs[std::make_pair("io_in", 3)].task->set(0);

    processAllGates(inputs, allNodes, 2);

    assert(outputs[std::make_pair("io_out", 0)].task->get() == 0);
    assert(outputs[std::make_pair("io_out", 1)].task->get() == 1);
    assert(outputs[std::make_pair("io_out", 2)].task->get() == 1);
    assert(outputs[std::make_pair("io_out", 3)].task->get() == 0);
}

int main()
{
    testPlainBinopGates();
    testPlainMUX();
    testPlainNOT();
    testPlainFromJSONtest_pass_4bit();
}
