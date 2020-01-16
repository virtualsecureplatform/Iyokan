#include "main.hpp"

//
#include <fstream>

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
    auto out = net.output("out", 0).task;

    std::array<std::tuple<int, int>, 8> invals{{{0, 1}, {0, 1}}};
    for (int i = 0; i < 2; i++) {
        // Set inputs.
        setInput(net.input("A", 0).task, std::get<0>(invals[i]));

        processAllGates(net, 2);

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
    auto out = net.output("out", 0).task;

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
        setInput(net.input("A", 0).task, std::get<0>(invals[i]));
        setInput(net.input("B", 0).task, std::get<1>(invals[i]));
        setInput(net.input("S", 0).task, std::get<2>(invals[i]));

        processAllGates(net, 2);

        // Check if results are okay.
        assert(getOutput(out) == std::get<3>(invals[i]));

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
        setInput(net.input("in0", 0).task, invals[i].first ? 1 : 0);
        setInput(net.input("in1", 0).task, invals[i].second ? 1 : 0);

        processAllGates(net, 1);

        // Check if results are okay.
        for (auto&& [portName, res] : id2res)
            assert(res[i] == getOutput(net.output(portName, 0).task));

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

    setInput(net.input("io_in", 0).task, 0);
    setInput(net.input("io_in", 1).task, 1);
    setInput(net.input("io_in", 2).task, 1);
    setInput(net.input("io_in", 3).task, 0);

    processAllGates(net, 2);

    assert(getOutput(net.output("io_out", 0).task) == 0);
    assert(getOutput(net.output("io_out", 1).task) == 1);
    assert(getOutput(net.output("io_out", 2).task) == 1);
    assert(getOutput(net.output("io_out", 3).task) == 0);
}

template <class NetworkBuilder>
void testFromJSONtest_and_4bit()
{
    const std::string fileName = "test/test-and-4bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
    assert(net.isValid());

    setInput(net.input("io_inA", 0).task, 0);
    setInput(net.input("io_inA", 1).task, 0);
    setInput(net.input("io_inA", 2).task, 1);
    setInput(net.input("io_inA", 3).task, 1);
    setInput(net.input("io_inB", 0).task, 0);
    setInput(net.input("io_inB", 1).task, 1);
    setInput(net.input("io_inB", 2).task, 0);
    setInput(net.input("io_inB", 3).task, 1);

    processAllGates(net, 3);

    assert(getOutput(net.output("io_out", 0).task) == 0);
    assert(getOutput(net.output("io_out", 1).task) == 0);
    assert(getOutput(net.output("io_out", 2).task) == 0);
    assert(getOutput(net.output("io_out", 3).task) == 1);
}

template <class NetworkBuilder>
void testFromJSONtest_and_4_2bit()
{
    const std::string fileName = "test/test-and-4_2bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
    assert(net.isValid());

    setInput(net.input("io_inA", 0).task, 1);
    setInput(net.input("io_inA", 1).task, 0);
    setInput(net.input("io_inA", 2).task, 1);
    setInput(net.input("io_inA", 3).task, 1);
    setInput(net.input("io_inB", 0).task, 1);
    setInput(net.input("io_inB", 1).task, 1);
    setInput(net.input("io_inB", 2).task, 1);
    setInput(net.input("io_inB", 3).task, 1);

    processAllGates(net, 3);

    assert(getOutput(net.output("io_out", 0).task) == 1);
    assert(getOutput(net.output("io_out", 1).task) == 0);
}

template <class NetworkBuilder>
void testFromJSONtest_mux_4bit()
{
    const std::string fileName = "test/test-mux-4bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
    assert(net.isValid());

    setInput(net.input("io_inA", 0).task, 0);
    setInput(net.input("io_inA", 1).task, 0);
    setInput(net.input("io_inA", 2).task, 1);
    setInput(net.input("io_inA", 3).task, 1);
    setInput(net.input("io_inB", 0).task, 0);
    setInput(net.input("io_inB", 1).task, 1);
    setInput(net.input("io_inB", 2).task, 0);
    setInput(net.input("io_inB", 3).task, 1);

    setInput(net.input("io_sel", 0).task, 0);
    processAllGates(net, 3);
    assert(getOutput(net.output("io_out", 0).task) == 0);
    assert(getOutput(net.output("io_out", 1).task) == 0);
    assert(getOutput(net.output("io_out", 2).task) == 1);
    assert(getOutput(net.output("io_out", 3).task) == 1);
    net.tick();

    setInput(net.input("io_sel", 0).task, 1);
    processAllGates(net, 3);
    assert(getOutput(net.output("io_out", 0).task) == 0);
    assert(getOutput(net.output("io_out", 1).task) == 1);
    assert(getOutput(net.output("io_out", 2).task) == 0);
    assert(getOutput(net.output("io_out", 3).task) == 1);
}

template <class NetworkBuilder>
void testFromJSONtest_addr_4bit()
{
    const std::string fileName = "test/test-addr-4bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
    assert(net.isValid());

    setInput(net.input("io_inA", 0).task, 0);
    setInput(net.input("io_inA", 1).task, 0);
    setInput(net.input("io_inA", 2).task, 1);
    setInput(net.input("io_inA", 3).task, 1);
    setInput(net.input("io_inB", 0).task, 0);
    setInput(net.input("io_inB", 1).task, 1);
    setInput(net.input("io_inB", 2).task, 0);
    setInput(net.input("io_inB", 3).task, 1);

    processAllGates(net, 3);

    assert(getOutput(net.output("io_out", 0).task) == 0);
    assert(getOutput(net.output("io_out", 1).task) == 1);
    assert(getOutput(net.output("io_out", 2).task) == 1);
    assert(getOutput(net.output("io_out", 3).task) == 0);
}

template <class NetworkBuilder>
void testFromJSONtest_register_4bit()
{
    const std::string fileName = "test/test-register-4bit.json";
    std::ifstream ifs{fileName};
    assert(ifs);

    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
    assert(net.isValid());

    setInput(net.input("io_in", 0).task, 0);
    setInput(net.input("io_in", 1).task, 0);
    setInput(net.input("io_in", 2).task, 1);
    setInput(net.input("io_in", 3).task, 1);

    // 1: Reset all DFFs.
    setInput(net.input("reset", 0).task, 1);
    processAllGates(net, 3);
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
    setInput(net.input("reset", 0).task, 0);
    processAllGates(net, 3);
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

    assert(getOutput(net.output("io_out", 0).task) == 0);
    assert(getOutput(net.output("io_out", 1).task) == 0);
    assert(getOutput(net.output("io_out", 2).task) == 0);
    assert(getOutput(net.output("io_out", 3).task) == 0);

    // 3: Get outputs.
    setInput(net.input("reset", 0).task, 0);
    processAllGates(net, 3);
    net.tick();

    assert(getOutput(net.output("io_out", 0).task) == 0);
    assert(getOutput(net.output("io_out", 1).task) == 0);
    assert(getOutput(net.output("io_out", 2).task) == 1);
    assert(getOutput(net.output("io_out", 3).task) == 1);
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
    auto out = net.output("out", 0).task;

    // 1:
    setInput(net.input("reset", 0).task, 1);
    processAllGates(net, 3);

    // 2:
    net.tick();
    assert(getOutput(dff) == 0);
    setInput(net.input("reset", 0).task, 0);
    processAllGates(net, 3);
    assert(getOutput(out) == 0);

    // 3:
    net.tick();
    assert(getOutput(dff) == 1);
    processAllGates(net, 3);
    assert(getOutput(out) == 1);

    // 4:
    net.tick();
    assert(getOutput(dff) == 0);
    processAllGates(net, 3);
    assert(getOutput(out) == 0);
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

    setInput(net.input("reset", 0).task, 1);
    processAllGates(net, 3);

    setInput(net.input("reset", 0).task, 0);
    for (size_t i = 0; i < outvals.size(); i++) {
        net.tick();
        processAllGates(net, 3);
        assert(getOutput(net.output("io_out", 0).task) == outvals[i][0]);
        assert(getOutput(net.output("io_out", 1).task) == outvals[i][1]);
        assert(getOutput(net.output("io_out", 2).task) == outvals[i][2]);
        assert(getOutput(net.output("io_out", 3).task) == outvals[i][3]);
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
    setInput(net.mem("rom", "0", 0x00).task, 0);
    setInput(net.mem("rom", "0", 0x01).task, 0);
    setInput(net.mem("rom", "0", 0x02).task, 1);
    setInput(net.mem("rom", "0", 0x03).task, 0);
    setInput(net.mem("rom", "0", 0x04).task, 1);
    setInput(net.mem("rom", "0", 0x05).task, 1);
    setInput(net.mem("rom", "0", 0x06).task, 1);
    setInput(net.mem("rom", "0", 0x07).task, 0);

    setInput(net.mem("rom", "0", 0x08).task, 0);
    setInput(net.mem("rom", "0", 0x09).task, 0);
    setInput(net.mem("rom", "0", 0x0a).task, 0);
    setInput(net.mem("rom", "0", 0x0b).task, 0);
    setInput(net.mem("rom", "0", 0x0c).task, 0);
    setInput(net.mem("rom", "0", 0x0d).task, 0);
    setInput(net.mem("rom", "0", 0x0e).task, 0);
    setInput(net.mem("rom", "0", 0x0f).task, 1);

    setInput(net.mem("rom", "0", 0x10).task, 0);
    setInput(net.mem("rom", "0", 0x11).task, 0);
    setInput(net.mem("rom", "0", 0x12).task, 0);
    setInput(net.mem("rom", "0", 0x13).task, 0);
    setInput(net.mem("rom", "0", 0x14).task, 0);
    setInput(net.mem("rom", "0", 0x15).task, 0);
    setInput(net.mem("rom", "0", 0x16).task, 0);
    setInput(net.mem("rom", "0", 0x17).task, 0);

    setInput(net.mem("rom", "0", 0x18).task, 0);
    setInput(net.mem("rom", "0", 0x19).task, 0);
    setInput(net.mem("rom", "0", 0x1a).task, 0);
    setInput(net.mem("rom", "0", 0x1b).task, 0);
    setInput(net.mem("rom", "0", 0x1c).task, 0);
    setInput(net.mem("rom", "0", 0x1d).task, 0);
    setInput(net.mem("rom", "0", 0x1e).task, 0);
    setInput(net.mem("rom", "0", 0x1f).task, 0);

    setInput(net.input("reset", 0).task, 1);
    processAllGates(net, 7);

    setInput(net.input("reset", 0).task, 0);

    for (int i = 0; i < 5; i++) {
        net.tick();
        processAllGates(net, 7);
    }

    assert(getOutput(net.output("io_regOut_x0", 0x00).task) == 0);
    assert(getOutput(net.output("io_regOut_x0", 0x01).task) == 0);
    assert(getOutput(net.output("io_regOut_x0", 0x02).task) == 0);
    assert(getOutput(net.output("io_regOut_x0", 0x03).task) == 1);
    assert(getOutput(net.output("io_regOut_x0", 0x04).task) == 1);
    assert(getOutput(net.output("io_regOut_x0", 0x05).task) == 0);
    assert(getOutput(net.output("io_regOut_x0", 0x06).task) == 0);
    assert(getOutput(net.output("io_regOut_x0", 0x07).task) == 0);
    assert(getOutput(net.output("io_regOut_x0", 0x08).task) == 0);
    assert(getOutput(net.output("io_regOut_x0", 0x09).task) == 0);
    assert(getOutput(net.output("io_regOut_x0", 0x0a).task) == 0);
    assert(getOutput(net.output("io_regOut_x0", 0x0b).task) == 0);
    assert(getOutput(net.output("io_regOut_x0", 0x0c).task) == 0);
    assert(getOutput(net.output("io_regOut_x0", 0x0d).task) == 0);
    assert(getOutput(net.output("io_regOut_x0", 0x0e).task) == 0);
    assert(getOutput(net.output("io_regOut_x0", 0x0f).task) == 0);
}

//
#include "plain.hpp"

void processAllGates(PlainNetwork& net, int numWorkers,
                     std::shared_ptr<ProgressGraphMaker> graph = nullptr)
{
    auto readyQueue = net.getReadyQueue();

    // Create workers.
    size_t numFinishedTargets = 0;
    std::vector<PlainWorker> workers;
    for (int i = 0; i < numWorkers; i++)
        workers.emplace_back(readyQueue, numFinishedTargets, graph);

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

void setInput(std::shared_ptr<TaskPlainGateMem> task, int val)
{
    task->set(val);
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

    processAllGates(net, 1, graph);

    std::stringstream ss;
    graph->dumpDOT(ss);
    std::string dot = ss.str();
    assert(dot.find("n0 [label = \"{WIRE|reset 0}\"]") != std::string::npos);
    assert(dot.find("n1 [label = \"{WIRE|out 0}\"]") != std::string::npos);
    assert(dot.find("n2 [label = \"{DFF|}\"]") != std::string::npos);
    assert(dot.find("n3 [label = \"{NOT|}\"]") != std::string::npos);
    assert(dot.find("n4 [label = \"{ANDNOT|}\"]") != std::string::npos);
    assert(dot.find("n2 -> n1") != std::string::npos);
    assert(dot.find("n4 -> n2") != std::string::npos);
    assert(dot.find("n2 -> n3") != std::string::npos);
    assert(dot.find("n0 -> n4") != std::string::npos);
    assert(dot.find("n3 -> n4") != std::string::npos);
}

#include "tfhepp.hpp"

class TFHEppTestHelper {
private:
    std::shared_ptr<TFHEpp::SecretKey> sk_;
    std::shared_ptr<TFHEpp::GateKey> gk_;
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

    const std::shared_ptr<TFHEpp::SecretKey>& sk() const
    {
        return sk_;
    }

    const std::shared_ptr<TFHEpp::GateKey>& gk() const
    {
        return gk_;
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

void processAllGates(TFHEppNetwork& net, int numWorkers,
                     std::shared_ptr<ProgressGraphMaker> graph = nullptr)
{
    auto readyQueue = net.getReadyQueue();

    // Create workers.
    size_t numFinishedTargets = 0;
    std::vector<TFHEppWorker> workers;
    for (int i = 0; i < numWorkers; i++)
        workers.emplace_back(TFHEppTestHelper::instance().gk(), readyQueue,
                             numFinishedTargets, graph);

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

void setInput(std::shared_ptr<TaskTFHEppGateMem> task, int val)
{
    auto& h = TFHEppTestHelper::instance();
    task->set(val ? h.one() : h.zero());
}

int getOutput(std::shared_ptr<TaskTFHEppGateMem> task)
{
    return TFHEpp::bootsSymDecrypt({task->get()},
                                   *TFHEppTestHelper::instance().sk())[0];
}

int main()
{
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
    testFromJSONdiamond_core<TFHEppNetworkBuilder>();

    testProgressGraphMaker();
}
