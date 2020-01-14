#include "main.hpp"

//
#include "plain.hpp"

//
#include <fstream>

template <class TaskNetwork>
void processAllGates(TaskNetwork& net, int numWorkers)
{
    auto readyQueue = net.getReadyQueue();

    // Create workers.
    size_t numFinishedTargets = 0;
    std::vector<PlainWorker> workers;
    for (int i = 0; i < numWorkers; i++)
        workers.emplace_back(readyQueue, numFinishedTargets);

    // Process all targets.
    while (numFinishedTargets < net.numNodes()) {
        // Detect infinite loops.
        assert(std::any_of(workers.begin(), workers.end(),
                           [](auto&& w) { return w.isWorking(); }) ||
               !readyQueue.empty());

        for (auto&& w : workers)
            w.update();
    }

    assert(readyQueue.empty());
}

void testPlainNOT()
{
    PlainNetworkBuilder builder;
    builder.INPUT(0, "A", 0);
    builder.NOT(1);
    builder.OUTPUT(2, "out", 0);
    builder.connect(0, 1);
    builder.connect(1, 2);

    PlainNetwork net = std::move(builder);

    std::shared_ptr<TaskPlainGateMem> out = net.output("out", 0).task;

    std::array<std::tuple<int, int>, 8> invals{{{0, 1}, {0, 1}}};
    for (int i = 0; i < 2; i++) {
        // Set inputs.
        net.input("A", 0).task->set(std::get<0>(invals[i]));

        processAllGates(net, 2);

        // Check if results are okay.
        assert(out->get() == std::get<1>(invals[i]));

        net.tick();
    }
}

void testPlainMUX()
{
    PlainNetworkBuilder builder;
    builder.INPUT(0, "A", 0);
    builder.INPUT(1, "B", 0);
    builder.INPUT(2, "S", 0);
    builder.MUX(3);
    builder.OUTPUT(4, "out", 0);
    builder.connect(0, 3);
    builder.connect(1, 3);
    builder.connect(2, 3);
    builder.connect(3, 4);

    PlainNetwork net = std::move(builder);

    std::shared_ptr<TaskPlainGateMem> out = net.output("out", 0).task;

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
        net.input("A", 0).task->set(std::get<0>(invals[i]));
        net.input("B", 0).task->set(std::get<1>(invals[i]));
        net.input("S", 0).task->set(std::get<2>(invals[i]));

        processAllGates(net, 2);

        // Check if results are okay.
        assert(out->get() == std::get<3>(invals[i]));

        net.tick();
    }
}

void testPlainBinopGates()
{
    PlainNetworkBuilder builder;
    builder.INPUT(0, "in0", 0);
    builder.INPUT(1, "in1", 0);

    int nextId = 10;

    std::unordered_map<std::string, std::array<uint8_t, 4 /* 00, 01, 10, 11 */>>
        id2res;

#define DEFINE_BINOP_GATE_TEST(name, e00, e01, e10, e11) \
    do {                                                 \
        int gateId = nextId++;                           \
        int outputId = nextId++;                         \
        builder.name(gateId);                            \
        builder.OUTPUT(outputId, "out_" #name, 0);       \
        builder.connect(0, gateId);                      \
        builder.connect(1, gateId);                      \
        builder.connect(gateId, outputId);               \
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

    PlainNetwork net = std::move(builder);

    std::array<std::pair<int, int>, 4> invals{{{0, 0}, {0, 1}, {1, 0}, {1, 1}}};
    for (int i = 0; i < 4; i++) {
        // Set inputs.
        net.input("in0", 0).task->set(invals[i].first);
        net.input("in1", 0).task->set(invals[i].second);

        processAllGates(net, 7);

        // Check if results are okay.
        for (auto&& [portName, res] : id2res)
            assert(res[i] == net.output(portName, 0).task->get());

        net.tick();
    }
}

void testPlainFromJSONtest_pass_4bit()
{
    const std::string fileName = "test/test-pass-4bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<PlainNetworkBuilder>(ifs);
    assert(net.isValid());

    net.input("io_in", 0).task->set(0);
    net.input("io_in", 1).task->set(1);
    net.input("io_in", 2).task->set(1);
    net.input("io_in", 3).task->set(0);

    processAllGates(net, 2);

    assert(net.output("io_out", 0).task->get() == 0);
    assert(net.output("io_out", 1).task->get() == 1);
    assert(net.output("io_out", 2).task->get() == 1);
    assert(net.output("io_out", 3).task->get() == 0);
}

void testPlainFromJSONtest_and_4bit()
{
    const std::string fileName = "test/test-and-4bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<PlainNetworkBuilder>(ifs);
    assert(net.isValid());

    net.input("io_inA", 0).task->set(0);
    net.input("io_inA", 1).task->set(0);
    net.input("io_inA", 2).task->set(1);
    net.input("io_inA", 3).task->set(1);
    net.input("io_inB", 0).task->set(0);
    net.input("io_inB", 1).task->set(1);
    net.input("io_inB", 2).task->set(0);
    net.input("io_inB", 3).task->set(1);

    processAllGates(net, 3);

    assert(net.output("io_out", 0).task->get() == 0);
    assert(net.output("io_out", 1).task->get() == 0);
    assert(net.output("io_out", 2).task->get() == 0);
    assert(net.output("io_out", 3).task->get() == 1);
}

void testPlainFromJSONtest_and_4_2bit()
{
    const std::string fileName = "test/test-and-4_2bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<PlainNetworkBuilder>(ifs);
    assert(net.isValid());

    net.input("io_inA", 0).task->set(1);
    net.input("io_inA", 1).task->set(0);
    net.input("io_inA", 2).task->set(1);
    net.input("io_inA", 3).task->set(1);
    net.input("io_inB", 0).task->set(1);
    net.input("io_inB", 1).task->set(1);
    net.input("io_inB", 2).task->set(1);
    net.input("io_inB", 3).task->set(1);

    processAllGates(net, 3);

    assert(net.output("io_out", 0).task->get() == 1);
    assert(net.output("io_out", 1).task->get() == 0);
}

void testPlainFromJSONtest_mux_4bit()
{
    const std::string fileName = "test/test-mux-4bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<PlainNetworkBuilder>(ifs);
    assert(net.isValid());

    net.input("io_inA", 0).task->set(0);
    net.input("io_inA", 1).task->set(0);
    net.input("io_inA", 2).task->set(1);
    net.input("io_inA", 3).task->set(1);
    net.input("io_inB", 0).task->set(0);
    net.input("io_inB", 1).task->set(1);
    net.input("io_inB", 2).task->set(0);
    net.input("io_inB", 3).task->set(1);

    net.input("io_sel", 0).task->set(0);
    processAllGates(net, 3);
    assert(net.output("io_out", 0).task->get() == 0);
    assert(net.output("io_out", 1).task->get() == 0);
    assert(net.output("io_out", 2).task->get() == 1);
    assert(net.output("io_out", 3).task->get() == 1);
    net.tick();

    net.input("io_sel", 0).task->set(1);
    processAllGates(net, 3);
    assert(net.output("io_out", 0).task->get() == 0);
    assert(net.output("io_out", 1).task->get() == 1);
    assert(net.output("io_out", 2).task->get() == 0);
    assert(net.output("io_out", 3).task->get() == 1);
}

void testPlainFromJSONtest_addr_4bit()
{
    const std::string fileName = "test/test-addr-4bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<PlainNetworkBuilder>(ifs);
    assert(net.isValid());

    net.input("io_inA", 0).task->set(0);
    net.input("io_inA", 1).task->set(0);
    net.input("io_inA", 2).task->set(1);
    net.input("io_inA", 3).task->set(1);
    net.input("io_inB", 0).task->set(0);
    net.input("io_inB", 1).task->set(1);
    net.input("io_inB", 2).task->set(0);
    net.input("io_inB", 3).task->set(1);

    processAllGates(net, 3);

    assert(net.output("io_out", 0).task->get() == 0);
    assert(net.output("io_out", 1).task->get() == 1);
    assert(net.output("io_out", 2).task->get() == 1);
    assert(net.output("io_out", 3).task->get() == 0);
}

void testPlainFromJSONtest_register_4bit()
{
    const std::string fileName = "test/test-register-4bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<PlainNetworkBuilder>(ifs);
    assert(net.isValid());

    net.input("io_in", 0).task->set(0);
    net.input("io_in", 1).task->set(0);
    net.input("io_in", 2).task->set(1);
    net.input("io_in", 3).task->set(1);

    // 1: Reset all DFFs.
    net.input("reset", 0).task->set(1);
    processAllGates(net, 3);
    net.tick();

    assert(
        std::dynamic_pointer_cast<TaskPlainGateMem>(net.node(13).task)->get() ==
        0);
    assert(
        std::dynamic_pointer_cast<TaskPlainGateMem>(net.node(14).task)->get() ==
        0);
    assert(
        std::dynamic_pointer_cast<TaskPlainGateMem>(net.node(15).task)->get() ==
        0);
    assert(
        std::dynamic_pointer_cast<TaskPlainGateMem>(net.node(16).task)->get() ==
        0);

    // 2: Store values into DFFs.
    net.input("reset", 0).task->set(0);
    processAllGates(net, 3);
    net.tick();

    assert(
        std::dynamic_pointer_cast<TaskPlainGateMem>(net.node(13).task)->get() ==
        0);
    assert(
        std::dynamic_pointer_cast<TaskPlainGateMem>(net.node(14).task)->get() ==
        0);
    assert(
        std::dynamic_pointer_cast<TaskPlainGateMem>(net.node(15).task)->get() ==
        1);
    assert(
        std::dynamic_pointer_cast<TaskPlainGateMem>(net.node(16).task)->get() ==
        1);

    assert(net.output("io_out", 0).task->get() == 0);
    assert(net.output("io_out", 1).task->get() == 0);
    assert(net.output("io_out", 2).task->get() == 0);
    assert(net.output("io_out", 3).task->get() == 0);

    // 3: Get outputs.
    net.input("reset", 0).task->set(0);
    processAllGates(net, 3);
    net.tick();

    assert(net.output("io_out", 0).task->get() == 0);
    assert(net.output("io_out", 1).task->get() == 0);
    assert(net.output("io_out", 2).task->get() == 1);
    assert(net.output("io_out", 3).task->get() == 1);
}

void testPlainSequentialCircuit()
{
    /*
                    B               D
       reset(0) >---> ANDNOT(4) >---> DFF(2)
                        ^ A            v Q
                        |              |
                        *--< NOT(3) <--*-----> OUTPUT(1)
                                    A
    */

    PlainNetworkBuilder builder;
    builder.INPUT(0, "reset", 0);
    builder.OUTPUT(1, "out", 0);
    builder.DFF(2);
    builder.NOT(3);
    builder.ANDNOT(4);
    builder.connect(2, 1);
    builder.connect(4, 2);
    builder.connect(2, 3);
    builder.connect(3, 4);
    builder.connect(0, 4);

    PlainNetwork net = std::move(builder);
    assert(net.isValid());

    std::shared_ptr<TaskPlainGateMem> dff =
        std::dynamic_pointer_cast<TaskPlainGateMem>(net.node(2).task);
    std::shared_ptr<TaskPlainGateMem> out = net.output("out", 0).task;

    // 1:
    net.input("reset", 0).task->set(1);
    processAllGates(net, 3);

    // 2:
    net.tick();
    assert(dff->get() == 0);
    net.input("reset", 0).task->set(0);
    processAllGates(net, 3);
    assert(out->get() == 0);

    // 3:
    net.tick();
    assert(dff->get() == 1);
    processAllGates(net, 3);
    assert(out->get() == 1);

    // 4:
    net.tick();
    assert(dff->get() == 0);
    processAllGates(net, 3);
    assert(out->get() == 0);
}

void testPlainFromJSONtest_counter_4bit()
{
    const std::string fileName = "test/test-counter-4bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<PlainNetworkBuilder>(ifs);
    assert(net.isValid());

    std::vector<std::array<int, 4>> outvals{{{0, 0, 0, 0},
                                             {1, 0, 0, 0},
                                             {0, 1, 0, 0},
                                             {1, 1, 0, 0},
                                             {0, 0, 1, 0},
                                             {1, 0, 1, 0},
                                             {0, 1, 1, 0},
                                             {1, 1, 1, 0},
                                             {0, 0, 0, 1},
                                             {1, 0, 0, 1},
                                             {0, 1, 0, 1},
                                             {1, 1, 0, 1},
                                             {0, 0, 1, 1},
                                             {1, 0, 1, 1},
                                             {0, 1, 1, 1},
                                             {1, 1, 1, 1}}};

    net.input("reset", 0).task->set(1);
    processAllGates(net, 3);

    net.input("reset", 0).task->set(0);
    for (size_t i = 0; i < outvals.size(); i++) {
        net.tick();
        processAllGates(net, 3);
        assert(net.output("io_out", 0).task->get() == outvals[i][0]);
        assert(net.output("io_out", 1).task->get() == outvals[i][1]);
        assert(net.output("io_out", 2).task->get() == outvals[i][2]);
        assert(net.output("io_out", 3).task->get() == outvals[i][3]);
    }
}

void testPlainFromJSONdiamond_core()
{
    const std::string fileName = "test/diamond-core.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<PlainNetworkBuilder>(ifs);
    assert(net.isValid());

    // 0: 74 80     lsi ra, 24
    // 2: 00 00     nop
    net.mem("rom", "0", 0x00).task->set(0);
    net.mem("rom", "0", 0x01).task->set(0);
    net.mem("rom", "0", 0x02).task->set(1);
    net.mem("rom", "0", 0x03).task->set(0);
    net.mem("rom", "0", 0x04).task->set(1);
    net.mem("rom", "0", 0x05).task->set(1);
    net.mem("rom", "0", 0x06).task->set(1);
    net.mem("rom", "0", 0x07).task->set(0);

    net.mem("rom", "0", 0x08).task->set(0);
    net.mem("rom", "0", 0x09).task->set(0);
    net.mem("rom", "0", 0x0a).task->set(0);
    net.mem("rom", "0", 0x0b).task->set(0);
    net.mem("rom", "0", 0x0c).task->set(0);
    net.mem("rom", "0", 0x0d).task->set(0);
    net.mem("rom", "0", 0x0e).task->set(0);
    net.mem("rom", "0", 0x0f).task->set(1);

    net.mem("rom", "0", 0x10).task->set(0);
    net.mem("rom", "0", 0x11).task->set(0);
    net.mem("rom", "0", 0x12).task->set(0);
    net.mem("rom", "0", 0x13).task->set(0);
    net.mem("rom", "0", 0x14).task->set(0);
    net.mem("rom", "0", 0x15).task->set(0);
    net.mem("rom", "0", 0x16).task->set(0);
    net.mem("rom", "0", 0x17).task->set(0);

    net.mem("rom", "0", 0x18).task->set(0);
    net.mem("rom", "0", 0x19).task->set(0);
    net.mem("rom", "0", 0x1a).task->set(0);
    net.mem("rom", "0", 0x1b).task->set(0);
    net.mem("rom", "0", 0x1c).task->set(0);
    net.mem("rom", "0", 0x1d).task->set(0);
    net.mem("rom", "0", 0x1e).task->set(0);
    net.mem("rom", "0", 0x1f).task->set(0);

    net.input("reset", 0).task->set(1);
    processAllGates(net, 7);

    net.input("reset", 0).task->set(0);
    for (int i = 0; i < 5; i++) {
        net.tick();
        processAllGates(net, 7);
    }

    assert(net.output("io_regOut_x0", 0x00).task->get() == 0);
    assert(net.output("io_regOut_x0", 0x01).task->get() == 0);
    assert(net.output("io_regOut_x0", 0x02).task->get() == 0);
    assert(net.output("io_regOut_x0", 0x03).task->get() == 1);
    assert(net.output("io_regOut_x0", 0x04).task->get() == 1);
    assert(net.output("io_regOut_x0", 0x05).task->get() == 0);
    assert(net.output("io_regOut_x0", 0x06).task->get() == 0);
    assert(net.output("io_regOut_x0", 0x07).task->get() == 0);
    assert(net.output("io_regOut_x0", 0x08).task->get() == 0);
    assert(net.output("io_regOut_x0", 0x09).task->get() == 0);
    assert(net.output("io_regOut_x0", 0x0a).task->get() == 0);
    assert(net.output("io_regOut_x0", 0x0b).task->get() == 0);
    assert(net.output("io_regOut_x0", 0x0c).task->get() == 0);
    assert(net.output("io_regOut_x0", 0x0d).task->get() == 0);
    assert(net.output("io_regOut_x0", 0x0e).task->get() == 0);
    assert(net.output("io_regOut_x0", 0x0f).task->get() == 0);
}

int main()
{
    testPlainBinopGates();
    testPlainMUX();
    testPlainNOT();
    testPlainFromJSONtest_pass_4bit();
    testPlainFromJSONtest_and_4bit();
    testPlainFromJSONtest_and_4_2bit();
    testPlainFromJSONtest_mux_4bit();
    testPlainFromJSONtest_addr_4bit();
    testPlainFromJSONtest_register_4bit();
    testPlainSequentialCircuit();
    testPlainFromJSONtest_counter_4bit();
    testPlainFromJSONdiamond_core();
}
