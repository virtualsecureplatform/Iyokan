#include "iyokan.hpp"

#include "packet.hpp"

//
#include <fstream>

template <class NetworkBuilder, class TaskNetwork>
auto get(TaskNetwork& net, const std::string& kind, const std::string& portName,
         int portBit)
{
    return net.template get<typename NetworkBuilder::ParamTaskTypeWIRE>(
        kind, portName, portBit);
}

// Assume variable names 'NetworkBuilder' and 'net'
#define ASSERT_OUTPUT_EQ(portName, portBit, expected)                          \
    assert(getOutput(get<NetworkBuilder>(net, "output", portName, portBit)) == \
           (expected))
#define SET_INPUT(portName, portBit, val) \
    setInput(get<NetworkBuilder>(net, "input", portName, portBit), val)

template <class NetworkBuilder>
void testNOT()
{
    NetworkBuilder builder;
    builder.INPUT(0, "A", 0);
    builder.NOT(1);
    builder.OUTPUT(2, "out", 0);
    builder.connect(0, 1);
    builder.connect(1, 2);

    TaskNetwork net = std::move(builder);
    auto out = get<NetworkBuilder>(net, "output", "out", 0);

    std::array<std::tuple<int, int>, 8> invals{{{0, 1}, {0, 1}}};
    for (int i = 0; i < 2; i++) {
        // Set inputs.
        SET_INPUT("A", 0, std::get<0>(invals[i]));

        processAllGates(net);

        // Check if results are okay.
        assert(getOutput(out) == std::get<1>(invals[i]));

        net.tick();
    }
}

template <class NetworkBuilder>
void testMUX()
{
    NetworkBuilder builder;
    builder.INPUT(0, "A", 0);
    builder.INPUT(1, "B", 0);
    builder.INPUT(2, "S", 0);
    builder.MUX(3);
    builder.OUTPUT(4, "out", 0);
    builder.connect(0, 3);
    builder.connect(1, 3);
    builder.connect(2, 3);
    builder.connect(3, 4);

    TaskNetwork net = std::move(builder);

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
        SET_INPUT("A", 0, std::get<0>(invals[i]));
        SET_INPUT("B", 0, std::get<1>(invals[i]));
        SET_INPUT("S", 0, std::get<2>(invals[i]));

        processAllGates(net);

        // Check if results are okay.
        ASSERT_OUTPUT_EQ("out", 0, std::get<3>(invals[i]));

        net.tick();
    }
}

template <class NetworkBuilder>
void testBinopGates()
{
    NetworkBuilder builder;
    builder.INPUT(0, "in0", 0);
    builder.INPUT(1, "in1", 0);

    int nextId = 10;

    std::unordered_map<std::string, std::array<uint8_t, 4 /* 00, 01, 10, 11 */
                                               >>
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

    TaskNetwork net = std::move(builder);

    std::array<std::pair<int, int>, 4> invals{{{0, 0}, {0, 1}, {1, 0}, {1, 1}}};
    for (int i = 0; i < 4; i++) {
        // Set inputs.
        SET_INPUT("in0", 0, invals[i].first ? 1 : 0);
        SET_INPUT("in1", 0, invals[i].second ? 1 : 0);

        processAllGates(net);

        // Check if results are okay.
        for (auto&& [portName, res] : id2res)
            ASSERT_OUTPUT_EQ(portName, 0, res[i]);

        net.tick();
    }
}

template <class NetworkBuilder>
void testFromJSONtest_pass_4bit()
{
    const std::string fileName = "test/test-pass-4bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
    assert(net.isValid());

    SET_INPUT("io_in", 0, 0);
    SET_INPUT("io_in", 1, 1);
    SET_INPUT("io_in", 2, 1);
    SET_INPUT("io_in", 3, 0);

    processAllGates(net);

    ASSERT_OUTPUT_EQ("io_out", 0, 0);
    ASSERT_OUTPUT_EQ("io_out", 1, 1);
    ASSERT_OUTPUT_EQ("io_out", 2, 1);
    ASSERT_OUTPUT_EQ("io_out", 3, 0);
}

template <class NetworkBuilder>
void testFromJSONtest_and_4bit()
{
    const std::string fileName = "test/test-and-4bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
    assert(net.isValid());

    SET_INPUT("io_inA", 0, 0);
    SET_INPUT("io_inA", 1, 0);
    SET_INPUT("io_inA", 2, 1);
    SET_INPUT("io_inA", 3, 1);
    SET_INPUT("io_inB", 0, 0);
    SET_INPUT("io_inB", 1, 1);
    SET_INPUT("io_inB", 2, 0);
    SET_INPUT("io_inB", 3, 1);

    processAllGates(net);

    ASSERT_OUTPUT_EQ("io_out", 0, 0);
    ASSERT_OUTPUT_EQ("io_out", 1, 0);
    ASSERT_OUTPUT_EQ("io_out", 2, 0);
    ASSERT_OUTPUT_EQ("io_out", 3, 1);
}

template <class NetworkBuilder>
void testFromJSONtest_and_4_2bit()
{
    const std::string fileName = "test/test-and-4_2bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
    assert(net.isValid());

    SET_INPUT("io_inA", 0, 1);
    SET_INPUT("io_inA", 1, 0);
    SET_INPUT("io_inA", 2, 1);
    SET_INPUT("io_inA", 3, 1);
    SET_INPUT("io_inB", 0, 1);
    SET_INPUT("io_inB", 1, 1);
    SET_INPUT("io_inB", 2, 1);
    SET_INPUT("io_inB", 3, 1);

    processAllGates(net);

    ASSERT_OUTPUT_EQ("io_out", 0, 1);
    ASSERT_OUTPUT_EQ("io_out", 1, 0);
}

template <class NetworkBuilder>
void testFromJSONtest_mux_4bit()
{
    const std::string fileName = "test/test-mux-4bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
    assert(net.isValid());

    SET_INPUT("io_inA", 0, 0);
    SET_INPUT("io_inA", 1, 0);
    SET_INPUT("io_inA", 2, 1);
    SET_INPUT("io_inA", 3, 1);
    SET_INPUT("io_inB", 0, 0);
    SET_INPUT("io_inB", 1, 1);
    SET_INPUT("io_inB", 2, 0);
    SET_INPUT("io_inB", 3, 1);

    SET_INPUT("io_sel", 0, 0);
    processAllGates(net);
    ASSERT_OUTPUT_EQ("io_out", 0, 0);
    ASSERT_OUTPUT_EQ("io_out", 1, 0);
    ASSERT_OUTPUT_EQ("io_out", 2, 1);
    ASSERT_OUTPUT_EQ("io_out", 3, 1);
    net.tick();

    SET_INPUT("io_sel", 0, 1);
    processAllGates(net);
    ASSERT_OUTPUT_EQ("io_out", 0, 0);
    ASSERT_OUTPUT_EQ("io_out", 1, 1);
    ASSERT_OUTPUT_EQ("io_out", 2, 0);
    ASSERT_OUTPUT_EQ("io_out", 3, 1);
}

template <class NetworkBuilder>
void testFromJSONtest_addr_4bit()
{
    const std::string fileName = "test/test-addr-4bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
    assert(net.isValid());

    SET_INPUT("io_inA", 0, 0);
    SET_INPUT("io_inA", 1, 0);
    SET_INPUT("io_inA", 2, 1);
    SET_INPUT("io_inA", 3, 1);
    SET_INPUT("io_inB", 0, 0);
    SET_INPUT("io_inB", 1, 1);
    SET_INPUT("io_inB", 2, 0);
    SET_INPUT("io_inB", 3, 1);

    processAllGates(net);

    ASSERT_OUTPUT_EQ("io_out", 0, 0);
    ASSERT_OUTPUT_EQ("io_out", 1, 1);
    ASSERT_OUTPUT_EQ("io_out", 2, 1);
    ASSERT_OUTPUT_EQ("io_out", 3, 0);
}

template <class NetworkBuilder>
void testFromJSONtest_register_4bit()
{
    const std::string fileName = "test/test-register-4bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
    assert(net.isValid());

    SET_INPUT("io_in", 0, 0);
    SET_INPUT("io_in", 1, 0);
    SET_INPUT("io_in", 2, 1);
    SET_INPUT("io_in", 3, 1);

    // 1: Reset all DFFs.
    SET_INPUT("reset", 0, 1);
    processAllGates(net);
    net.tick();

    assert(getOutput(std::dynamic_pointer_cast<
                     typename NetworkBuilder::ParamTaskTypeMem>(
               net.node(13)->task())) == 0);
    assert(getOutput(std::dynamic_pointer_cast<
                     typename NetworkBuilder::ParamTaskTypeMem>(
               net.node(14)->task())) == 0);
    assert(getOutput(std::dynamic_pointer_cast<
                     typename NetworkBuilder::ParamTaskTypeMem>(
               net.node(15)->task())) == 0);
    assert(getOutput(std::dynamic_pointer_cast<
                     typename NetworkBuilder::ParamTaskTypeMem>(
               net.node(16)->task())) == 0);

    // 2: Store values into DFFs.
    SET_INPUT("reset", 0, 0);
    processAllGates(net);
    net.tick();

    assert(getOutput(std::dynamic_pointer_cast<
                     typename NetworkBuilder::ParamTaskTypeMem>(
               net.node(13)->task())) == 0);
    assert(getOutput(std::dynamic_pointer_cast<
                     typename NetworkBuilder::ParamTaskTypeMem>(
               net.node(14)->task())) == 0);
    assert(getOutput(std::dynamic_pointer_cast<
                     typename NetworkBuilder::ParamTaskTypeMem>(
               net.node(15)->task())) == 1);
    assert(getOutput(std::dynamic_pointer_cast<
                     typename NetworkBuilder::ParamTaskTypeMem>(
               net.node(16)->task())) == 1);

    ASSERT_OUTPUT_EQ("io_out", 0, 0);
    ASSERT_OUTPUT_EQ("io_out", 1, 0);
    ASSERT_OUTPUT_EQ("io_out", 2, 0);
    ASSERT_OUTPUT_EQ("io_out", 3, 0);

    // 3: Get outputs.
    SET_INPUT("reset", 0, 0);
    processAllGates(net);
    net.tick();

    ASSERT_OUTPUT_EQ("io_out", 0, 0);
    ASSERT_OUTPUT_EQ("io_out", 1, 0);
    ASSERT_OUTPUT_EQ("io_out", 2, 1);
    ASSERT_OUTPUT_EQ("io_out", 3, 1);
}

template <class NetworkBuilder>
void testSequentialCircuit()
{
    /*
                    B               D
       reset(0) >---> ANDNOT(4) >---> DFF(2)
                        ^ A            v Q
                        |              |
                        *--< NOT(3) <--*-----> OUTPUT(1)
                                    A
    */

    NetworkBuilder builder;
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

    TaskNetwork net = std::move(builder);
    assert(net.isValid());

    auto dff =
        std::dynamic_pointer_cast<typename NetworkBuilder::ParamTaskTypeMem>(
            net.node(2)->task());
    auto out = get<NetworkBuilder>(net, "output", "out", 0);

    // 1:
    SET_INPUT("reset", 0, 1);
    processAllGates(net);

    // 2:
    net.tick();
    assert(getOutput(dff) == 0);
    SET_INPUT("reset", 0, 0);
    processAllGates(net);
    ASSERT_OUTPUT_EQ("out", 0, 0);

    // 3:
    net.tick();
    assert(getOutput(dff) == 1);
    processAllGates(net);
    ASSERT_OUTPUT_EQ("out", 0, 1);

    // 4:
    net.tick();
    assert(getOutput(dff) == 0);
    processAllGates(net);
    ASSERT_OUTPUT_EQ("out", 0, 0);
}

template <class NetworkBuilder>
void testFromJSONtest_counter_4bit()
{
    const std::string fileName = "test/test-counter-4bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
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

    SET_INPUT("reset", 0, 1);
    processAllGates(net);

    SET_INPUT("reset", 0, 0);
    for (size_t i = 0; i < outvals.size(); i++) {
        net.tick();
        processAllGates(net);
        ASSERT_OUTPUT_EQ("io_out", 0, outvals[i][0]);
        ASSERT_OUTPUT_EQ("io_out", 1, outvals[i][1]);
        ASSERT_OUTPUT_EQ("io_out", 2, outvals[i][2]);
        ASSERT_OUTPUT_EQ("io_out", 3, outvals[i][3]);
    }
}

template <class NetworkBuilder>
void testFromJSONdiamond_core()
{
    const std::string fileName = "test/diamond-core.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
    assert(net.isValid());

    // 0: 74 80     lsi ra, 24
    // 2: 00 00     nop
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x00), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x01), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x02), 1);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x03), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x04), 1);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x05), 1);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x06), 1);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x07), 0);

    setInput(get<NetworkBuilder>(net, "rom", "0", 0x08), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x09), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x0a), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x0b), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x0c), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x0d), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x0e), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x0f), 1);

    setInput(get<NetworkBuilder>(net, "rom", "0", 0x10), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x11), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x12), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x13), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x14), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x15), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x16), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x17), 0);

    setInput(get<NetworkBuilder>(net, "rom", "0", 0x18), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x19), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x1a), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x1b), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x1c), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x1d), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x1e), 0);
    setInput(get<NetworkBuilder>(net, "rom", "0", 0x1f), 0);

    SET_INPUT("reset", 0, 1);
    processAllGates(net);

    SET_INPUT("reset", 0, 0);

    for (int i = 0; i < 5; i++) {
        net.tick();
        processAllGates(net);
    }

    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x00, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x01, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x02, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x03, 1);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x04, 1);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x05, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x06, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x07, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x08, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x09, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x0a, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x0b, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x0c, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x0d, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x0e, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x0f, 0);
}

template <class NetworkBuilder, class TaskROM, class NormalT, class ROMNetwork>
void testFromJSONdiamond_core_wo_rom(ROMNetwork rom)
{
    assert(rom.isValid());

    const std::string fileName = "test/diamond-core-wo-rom.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto core = readNetworkFromJSON<NetworkBuilder>(ifs);
    assert(core.isValid());

    auto net = core.template merge<NormalT>(
        rom,
        {
            {"io_romAddr", 0, "ROM", 0},
            {"io_romAddr", 1, "ROM", 1},
            {"io_romAddr", 2, "ROM", 2},
            {"io_romAddr", 3, "ROM", 3},
            {"io_romAddr", 4, "ROM", 4},
            {"io_romAddr", 5, "ROM", 5},
            {"io_romAddr", 6, "ROM", 6},
        },
        {
            {"ROM", 0, "io_romData", 0},   {"ROM", 1, "io_romData", 1},
            {"ROM", 2, "io_romData", 2},   {"ROM", 3, "io_romData", 3},
            {"ROM", 4, "io_romData", 4},   {"ROM", 5, "io_romData", 5},
            {"ROM", 6, "io_romData", 6},   {"ROM", 7, "io_romData", 7},
            {"ROM", 8, "io_romData", 8},   {"ROM", 9, "io_romData", 9},
            {"ROM", 10, "io_romData", 10}, {"ROM", 11, "io_romData", 11},
            {"ROM", 12, "io_romData", 12}, {"ROM", 13, "io_romData", 13},
            {"ROM", 14, "io_romData", 14}, {"ROM", 15, "io_romData", 15},
            {"ROM", 16, "io_romData", 16}, {"ROM", 17, "io_romData", 17},
            {"ROM", 18, "io_romData", 18}, {"ROM", 19, "io_romData", 19},
            {"ROM", 20, "io_romData", 20}, {"ROM", 21, "io_romData", 21},
            {"ROM", 22, "io_romData", 22}, {"ROM", 23, "io_romData", 23},
            {"ROM", 24, "io_romData", 24}, {"ROM", 25, "io_romData", 25},
            {"ROM", 26, "io_romData", 26}, {"ROM", 27, "io_romData", 27},
            {"ROM", 28, "io_romData", 28}, {"ROM", 29, "io_romData", 29},
            {"ROM", 30, "io_romData", 30}, {"ROM", 31, "io_romData", 31},
        });
    assert(net.isValid());

    // 0: 74 80     lsi ra, 24
    // 2: 00 00     nop
    setROM(*net.template get<TaskROM>("rom", "all", 0),
           std::vector<uint8_t>{0x74, 0x80});

    SET_INPUT("reset", 0, 1);
    processAllGates(net);

    SET_INPUT("reset", 0, 0);
    for (int i = 0; i < 5; i++) {
        net.tick();
        processAllGates(net);
    }

    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x00, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x01, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x02, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x03, 1);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x04, 1);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x05, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x06, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x07, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x08, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x09, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x0a, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x0b, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x0c, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x0d, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x0e, 0);
    ASSERT_OUTPUT_EQ("io_regOut_x0", 0x0f, 0);
}

//
#include "iyokan_plain.hpp"

void processAllGates(PlainNetwork& net,
                     std::shared_ptr<ProgressGraphMaker> graph = nullptr)
{
    processAllGates(net, std::thread::hardware_concurrency(), graph);
}

void setInput(std::shared_ptr<TaskPlainGateMem> task, int val)
{
    task->set(val);
}

void setROM(TaskPlainROM& rom, const std::vector<uint8_t>& src)
{
    for (int i = 0; i < 512 / 4; i++) {
        int val = 0;
        for (int j = 3; j >= 0; j--) {
            size_t offset = i * 4 + j;
            val = (val << 8) | (offset < src.size() ? src[offset] : 0x00);
        }
        rom.set4le(i << 2, val);
    }
}

int getOutput(std::shared_ptr<TaskPlainGateMem> task)
{
    return task->get();
}

void testProgressGraphMaker()
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

    auto graph = std::make_shared<ProgressGraphMaker>();

    processAllGates(net, graph);

    std::stringstream ss;
    graph->dumpDOT(ss);
    std::string dot = ss.str();
    assert(dot.find("n0 [label = \"{INPUT|reset[0]}\"]") != std::string::npos);
    assert(dot.find("n1 [label = \"{OUTPUT|out[0]}\"]") != std::string::npos);
    assert(dot.find("n2 [label = \"{DFF}\"]") != std::string::npos);
    assert(dot.find("n3 [label = \"{NOT}\"]") != std::string::npos);
    assert(dot.find("n4 [label = \"{ANDNOT}\"]") != std::string::npos);
    assert(dot.find("n2 -> n1") != std::string::npos);
    assert(dot.find("n4 -> n2") != std::string::npos);
    assert(dot.find("n2 -> n3") != std::string::npos);
    assert(dot.find("n0 -> n4") != std::string::npos);
    assert(dot.find("n3 -> n4") != std::string::npos);
}

#include "iyokan_tfhepp.hpp"

class TFHEppTestHelper {
private:
    std::shared_ptr<TFHEpp::SecretKey> sk_;
    std::shared_ptr<TFHEpp::GateKey> gk_;
    std::shared_ptr<TFHEpp::CircuitKey> ck_;
    TFHEpp::TLWElvl0 zero_, one_;

private:
    TFHEppTestHelper()
    {
        sk_ = std::make_shared<TFHEpp::SecretKey>();
        gk_ = std::make_shared<TFHEpp::GateKey>(*sk_);
        zero_ = TFHEpp::bootsSymEncrypt({0}, *sk_).at(0);
        one_ = TFHEpp::bootsSymEncrypt({1}, *sk_).at(0);
    }

public:
    static TFHEppTestHelper& instance()
    {
        static TFHEppTestHelper inst;
        return inst;
    }

    void prepareCircuitKey()
    {
        ck_ = std::make_shared<TFHEpp::CircuitKey>(*sk_);
    }

    TFHEppWorkerInfo wi() const
    {
        return TFHEppWorkerInfo{TFHEpp::lweParams{}, gk_, ck_};
    }

    const std::shared_ptr<TFHEpp::SecretKey>& sk() const
    {
        return sk_;
    }

    const TFHEpp::TLWElvl0& zero() const
    {
        return zero_;
    }

    const TFHEpp::TLWElvl0& one() const
    {
        return one_;
    }
};

void processAllGates(TFHEppNetwork& net,
                     std::shared_ptr<ProgressGraphMaker> graph = nullptr)
{
    processAllGates(net, std::thread::hardware_concurrency(),
                    TFHEppTestHelper::instance().wi(), graph);
}

void setInput(std::shared_ptr<TaskTFHEppGateMem> task, int val)
{
    auto& h = TFHEppTestHelper::instance();
    task->set(val ? h.one() : h.zero());
}

void setROM(TaskTFHEppROMUX& rom, const std::vector<uint8_t>& src)
{
    auto& h = TFHEppTestHelper::instance();
    auto params = h.wi().params;

    for (size_t i = 0; i < 512 / (params.N / 8); i++) {
        TFHEpp::Polynomiallvl1 pmu;
        for (size_t j = 0; j < params.N; j++) {
            size_t offset = i * params.N + j;
            size_t byteOffset = offset / 8, bitOffset = offset % 8;
            uint8_t val = byteOffset < src.size()
                              ? (src[byteOffset] >> bitOffset) & 1u
                              : 0;
            pmu[j] = val ? params.μ : -params.μ;
        }
        rom.set128le(
            i * (params.N / 8),
            TFHEpp::trlweSymEncryptlvl1(pmu, params.αbk, h.sk()->key.lvl1));
    }
}

int getOutput(std::shared_ptr<TaskTFHEppGateMem> task)
{
    return TFHEpp::bootsSymDecrypt({task->get()},
                                   *TFHEppTestHelper::instance().sk())[0];
}

void testTFHEppSerialization()
{
    auto& h = TFHEppTestHelper::instance();
    const std::shared_ptr<const TFHEpp::SecretKey>& sk = h.sk();
    const std::shared_ptr<const TFHEpp::GateKey>& gk = h.wi().gateKey;

    // Test for secret key
    {
        // Dump
        writeToArchive("_test_sk", *sk);
        // Load
        auto sk2 = std::make_shared<TFHEpp::SecretKey>();
        readFromArchive<TFHEpp::SecretKey>(*sk2, "_test_sk");

        auto zero = TFHEpp::bootsSymEncrypt({0}, *sk2).at(0);
        auto one = TFHEpp::bootsSymEncrypt({1}, *sk2).at(0);
        TFHEpp::TLWElvl0 res;
        TFHEpp::HomANDNY(res, zero, one, *gk);
        assert(TFHEpp::bootsSymDecrypt({res}, *sk2).at(0) == 1);
    }

    // Test for gate key
    {
        std::stringstream ss{std::ios::binary | std::ios::out | std::ios::in};

        // Dump
        writeToArchive(ss, *gk);
        // Load
        auto gk2 = std::make_shared<TFHEpp::GateKey>();
        readFromArchive<TFHEpp::GateKey>(*gk2, ss);

        auto zero = TFHEpp::bootsSymEncrypt({0}, *sk).at(0);
        auto one = TFHEpp::bootsSymEncrypt({1}, *sk).at(0);
        TFHEpp::TLWElvl0 res;
        TFHEpp::HomANDNY(res, zero, one, *gk2);
        assert(TFHEpp::bootsSymDecrypt({res}, *sk).at(0) == 1);
    }

    // Test for TLWE level 0
    {
        std::stringstream ss{std::ios::binary | std::ios::out | std::ios::in};

        {
            auto zero = TFHEpp::bootsSymEncrypt({0}, *sk).at(0);
            auto one = TFHEpp::bootsSymEncrypt({1}, *sk).at(0);
            writeToArchive(ss, zero);
            writeToArchive(ss, one);
            ss.seekg(0);
        }

        {
            TFHEpp::TLWElvl0 res, zero, one;
            readFromArchive(zero, ss);
            readFromArchive(one, ss);
            TFHEpp::HomANDNY(res, zero, one, *gk);
            assert(TFHEpp::bootsSymDecrypt({res}, *sk).at(0) == 1);
        }
    }
}

void testKVSPPacket()
{
    // Read packet
    const auto reqPacket = KVSPReqPacket::make(
        *TFHEppTestHelper::instance().sk(), parseELF("test/test00.elf"));

    // Load network
    auto net = []() {
        const std::string fileName = "test/diamond-core.json";
        std::ifstream ifs{fileName};
        assert(ifs);
        return readNetworkFromJSON<TFHEppNetworkBuilder>(ifs);
    }();
    assert(net.isValid());

    // Set ROM
    for (int addr = 0; addr < 128; addr++)
        for (int bit = 0; bit < 32; bit++)
            net.get<TaskTFHEppGateMem>("rom", std::to_string(addr), bit)
                ->set(reqPacket.rom.at(addr * 32 + bit));
    // Set RAM
    for (int addr = 0; addr < 512; addr++)
        for (int bit = 0; bit < 8; bit++)
            net.get<TaskTFHEppGateMem>("ram", std::to_string(addr), bit)
                ->set(reqPacket.ram.at(addr * 8 + bit));

    // Reset
    setInput(net.get<TaskTFHEppGateMem>("input", "reset", 0), 1);
    processAllGates(net);

    // Run
    setInput(net.get<TaskTFHEppGateMem>("input", "reset", 0), 0);
    for (int i = 0; i < 8; i++) {
        net.tick();
        processAllGates(net);
    }

    KVSPResPacket resPacket;
    // Get flags
    resPacket.flags.push_back(
        net.get<TaskTFHEppGateMem>("output", "io_finishFlag", 0)->get());
    // Get regs
    for (int regi = 0; regi < 16; regi++) {
        std::vector<TFHEpp::TLWElvl0> reg;
        for (int bit = 0; bit < 16; bit++)
            reg.push_back(
                net.get<TaskTFHEppGateMem>(
                       "output", detail::fok("io_regOut_x", regi), bit)
                    ->get());
        resPacket.regs.emplace_back(std::move(reg));
    }
    // Get RAM
    for (int addr = 0; addr < 512; addr++)
        for (int bit = 0; bit < 8; bit++)
            resPacket.ram.push_back(
                net.get<TaskTFHEppGateMem>("ram", std::to_string(addr), bit)
                    ->get());

    // Assert
    auto assertOutput = [&](int bit, int expected) {
        assert(getOutput(net.get<TaskTFHEppGateMem>("output", "io_regOut_x0",
                                                    bit)) == expected);
    };
    assertOutput(0x00, 0);
    assertOutput(0x01, 1);
    assertOutput(0x02, 0);
    assertOutput(0x03, 1);
    assertOutput(0x04, 0);
    assertOutput(0x05, 1);
    assertOutput(0x06, 0);
    assertOutput(0x07, 0);
    assertOutput(0x08, 0);
    assertOutput(0x09, 0);
    assertOutput(0x0a, 0);
    assertOutput(0x0b, 0);
    assertOutput(0x0c, 0);
    assertOutput(0x0d, 0);
    assertOutput(0x0e, 0);
    assertOutput(0x0f, 0);

    {
        KVSPPlainResPacket plain =
            decrypt(*TFHEppTestHelper::instance().sk(), resPacket);
        assert(plain.flags.at(0) == 1);
        assert(plain.regs.at(0) == 42);
    }
}

void testKVSPPlainPacket()
{
    // Read packet
    const KVSPPlainReqPacket reqPacket = parseELF("test/test00.elf");

    // Load network
    auto net = []() {
        const std::string fileName = "test/diamond-core.json";
        std::ifstream ifs{fileName};
        assert(ifs);
        return readNetworkFromJSON<PlainNetworkBuilder>(ifs);
    }();
    assert(net.isValid());

    // Set ROM
    for (int addr = 0; addr < 128; addr++)
        for (int bit = 0; bit < 32; bit++)
            net.get<TaskPlainGateMem>("rom", std::to_string(addr), bit)
                ->set((reqPacket.rom.at((addr * 32 + bit) / 8) >> (bit % 8)) &
                      1);
    // Set RAM
    for (int addr = 0; addr < 512; addr++)
        for (int bit = 0; bit < 8; bit++)
            net.get<TaskPlainGateMem>("ram", std::to_string(addr), bit)
                ->set((reqPacket.ram.at(addr) >> bit) & 1);

    // Reset
    setInput(net.get<TaskPlainGateMem>("input", "reset", 0), 1);
    processAllGates(net);

    // Run
    setInput(net.get<TaskPlainGateMem>("input", "reset", 0), 0);
    for (int i = 0; i < 8; i++) {
        net.tick();
        processAllGates(net);
    }

    // Assert
    auto assertOutput = [&](int bit, int expected) {
        assert(getOutput(net.get<TaskPlainGateMem>("output", "io_regOut_x0",
                                                   bit)) == expected);
    };
    assertOutput(0x00, 0);
    assertOutput(0x01, 1);
    assertOutput(0x02, 0);
    assertOutput(0x03, 1);
    assertOutput(0x04, 0);
    assertOutput(0x05, 1);
    assertOutput(0x06, 0);
    assertOutput(0x07, 0);
    assertOutput(0x08, 0);
    assertOutput(0x09, 0);
    assertOutput(0x0a, 0);
    assertOutput(0x0b, 0);
    assertOutput(0x0c, 0);
    assertOutput(0x0d, 0);
    assertOutput(0x0e, 0);
    assertOutput(0x0f, 0);
}

#ifdef IYOKAN_CUDA_ENABLED
#include "iyokan_cufhe.hpp"

class CUFHETestHelper {
private:
    std::shared_ptr<cufhe::PriKey> sk_;
    std::shared_ptr<cufhe::PubKey> gk_;
    cufhe::Ctxt zero_, one_;

private:
    CUFHETestHelper()
    {
        cufhe::SetSeed();

        sk_ = std::make_shared<cufhe::PriKey>();
        gk_ = std::make_shared<cufhe::PubKey>();
        cufhe::KeyGen(*gk_, *sk_);

        cufhe::Initialize(*gk_);

        cufhe::Ptxt p;
        p = 0;
        cufhe::Encrypt(zero_, p, *sk_);
        p = 1;
        cufhe::Encrypt(one_, p, *sk_);
    }

    ~CUFHETestHelper()
    {
        cufhe::CleanUp();
    }

public:
    static CUFHETestHelper& instance()
    {
        static CUFHETestHelper inst;
        return inst;
    }

    const std::shared_ptr<cufhe::PriKey>& sk() const
    {
        return sk_;
    }

    const cufhe::Ctxt& zero() const
    {
        return zero_;
    }

    const cufhe::Ctxt& one() const
    {
        return one_;
    }
};

void processAllGates(CUFHENetwork& net,
                     std::shared_ptr<ProgressGraphMaker> graph = nullptr)
{
    processAllGates(net, 240, graph);
}

void setInput(std::shared_ptr<TaskCUFHEGateMem> task, int val)
{
    auto& h = CUFHETestHelper::instance();
    task->set(val ? h.one() : h.zero());
}

int getOutput(std::shared_ptr<TaskCUFHEGateMem> task)
{
    cufhe::Ptxt p;
    cufhe::Decrypt(p, task->get(), *CUFHETestHelper::instance().sk());
    return p.get();
}
#endif

int main(int argc, char** argv)
{
    AsyncThread::setNumThreads(std::thread::hardware_concurrency());

    testNOT<PlainNetworkBuilder>();
    testMUX<PlainNetworkBuilder>();
    testBinopGates<PlainNetworkBuilder>();
    testFromJSONtest_pass_4bit<PlainNetworkBuilder>();
    testFromJSONtest_and_4bit<PlainNetworkBuilder>();
    testFromJSONtest_and_4_2bit<PlainNetworkBuilder>();
    testFromJSONtest_mux_4bit<PlainNetworkBuilder>();
    testFromJSONtest_addr_4bit<PlainNetworkBuilder>();
    testFromJSONtest_register_4bit<PlainNetworkBuilder>();
    testSequentialCircuit<PlainNetworkBuilder>();
    testFromJSONtest_counter_4bit<PlainNetworkBuilder>();
    testFromJSONdiamond_core<PlainNetworkBuilder>();
    testFromJSONdiamond_core_wo_rom<PlainNetworkBuilder, TaskPlainROM, uint8_t>(
        makePlainROMNetwork());
    testKVSPPlainPacket();

    testNOT<TFHEppNetworkBuilder>();
    testMUX<TFHEppNetworkBuilder>();
    testBinopGates<TFHEppNetworkBuilder>();
    testFromJSONtest_pass_4bit<TFHEppNetworkBuilder>();
    testFromJSONtest_pass_4bit<TFHEppNetworkBuilder>();
    testFromJSONtest_and_4bit<TFHEppNetworkBuilder>();
    testFromJSONtest_and_4_2bit<TFHEppNetworkBuilder>();
    testFromJSONtest_mux_4bit<TFHEppNetworkBuilder>();
    testFromJSONtest_addr_4bit<TFHEppNetworkBuilder>();
    testFromJSONtest_register_4bit<TFHEppNetworkBuilder>();
    testSequentialCircuit<TFHEppNetworkBuilder>();
    testFromJSONtest_counter_4bit<TFHEppNetworkBuilder>();
    testTFHEppSerialization();

#ifdef IYOKAN_CUDA_ENABLED
    testNOT<CUFHENetworkBuilder>();
    testMUX<CUFHENetworkBuilder>();
    testBinopGates<CUFHENetworkBuilder>();
    testFromJSONtest_pass_4bit<CUFHENetworkBuilder>();
    testFromJSONtest_and_4bit<CUFHENetworkBuilder>();
    testFromJSONtest_and_4_2bit<CUFHENetworkBuilder>();
    testFromJSONtest_mux_4bit<CUFHENetworkBuilder>();
    testFromJSONtest_addr_4bit<CUFHENetworkBuilder>();
    testFromJSONtest_register_4bit<CUFHENetworkBuilder>();
    testSequentialCircuit<CUFHENetworkBuilder>();
    testFromJSONtest_counter_4bit<CUFHENetworkBuilder>();
#endif

    testProgressGraphMaker();

    if (argc >= 2 && strcmp(argv[1], "slow") == 0) {
        TFHEppTestHelper::instance().prepareCircuitKey();

        testFromJSONdiamond_core<TFHEppNetworkBuilder>();
        testFromJSONdiamond_core_wo_rom<TFHEppNetworkBuilder, TaskTFHEppROMUX,
                                        TFHEpp::TLWElvl0>(
            makeTFHEppROMNetwork());
        testKVSPPacket();

#ifdef IYOKAN_CUDA_ENABLED
        testFromJSONdiamond_core<CUFHENetworkBuilder>();
#endif
    }
}
