//#include "iyokan.hpp"
//#include "tfhepp_cufhe_wrapper.hpp"
//
//#include <fmt/printf.h>
//#include <fstream>
//
// template <class Network>
// void assertNetValid(Network&& net)
//{
//    error::Stack err;
//    net.checkValid(err);
//    if (err.empty())
//        return;
//
//    std::cerr << err.str() << std::endl;
//    assert(0);
//}
//
// template <class NetworkBuilder, class TaskNetwork>
// auto get(TaskNetwork& net, const std::string& kind, const std::string&
// portName,
//         int portBit)
//{
//    return net.template get<typename NetworkBuilder::ParamTaskTypeWIRE>(
//        kind, portName, portBit);
//}
//
// template <class NetworkBuilder>
// typename NetworkBuilder::NetworkType readNetworkFromJSON(std::istream& is)
//{
//    // FIXME: Assume the stream `is` emits Iyokan-L1 JSON
//    NetworkBuilder builder;
//    IyokanL1JSONReader{}.read<NetworkBuilder>(builder, is);
//    return typename NetworkBuilder::NetworkType{std::move(builder)};
//}
//
//// Assume variable names 'NetworkBuilder' and 'net'
//#define ASSERT_OUTPUT_EQ(portName, portBit, expected) \
//    assert(getOutput(get<NetworkBuilder>(net, "output", portName, portBit)) ==
//    \
//           (expected))
//#define SET_INPUT(portName, portBit, val) \
//    setInput(get<NetworkBuilder>(net, "input", portName, portBit), val)
//
// template <class NetworkBuilder>
// void testNOT()
//{
//    NetworkBuilder builder;
//    int id0 = builder.INPUT("A", 0);
//    int id1 = builder.NOT();
//    int id2 = builder.OUTPUT("out", 0);
//    builder.connect(id0, id1);
//    builder.connect(id1, id2);
//
//    TaskNetwork net = std::move(builder);
//    auto out = get<NetworkBuilder>(net, "output", "out", 0);
//
//    std::array<std::tuple<int, int>, 8> invals{{{0, 1}, {0, 1}}};
//    for (int i = 0; i < 2; i++) {
//        // Set inputs.
//        SET_INPUT("A", 0, std::get<0>(invals[i]));
//
//        processAllGates(net);
//
//        // Check if results are okay.
//        assert(getOutput(out) == std::get<1>(invals[i]));
//
//        net.tick();
//    }
//}
//
// template <class NetworkBuilder>
// void testMUX()
//{
//    NetworkBuilder builder;
//    int id0 = builder.INPUT("A", 0);
//    int id1 = builder.INPUT("B", 0);
//    int id2 = builder.INPUT("S", 0);
//    int id3 = builder.MUX();
//    int id4 = builder.OUTPUT("out", 0);
//    builder.connect(id0, id3);
//    builder.connect(id1, id3);
//    builder.connect(id2, id3);
//    builder.connect(id3, id4);
//
//    TaskNetwork net = std::move(builder);
//
//    std::array<std::tuple<int, int, int, int>, 8> invals{{/*A,B, S, O*/
//                                                          {0, 0, 0, 0},
//                                                          {0, 0, 1, 0},
//                                                          {0, 1, 0, 0},
//                                                          {0, 1, 1, 1},
//                                                          {1, 0, 0, 1},
//                                                          {1, 0, 1, 0},
//                                                          {1, 1, 0, 1},
//                                                          {1, 1, 1, 1}}};
//    for (int i = 0; i < 8; i++) {
//        // Set inputs.
//        SET_INPUT("A", 0, std::get<0>(invals[i]));
//        SET_INPUT("B", 0, std::get<1>(invals[i]));
//        SET_INPUT("S", 0, std::get<2>(invals[i]));
//
//        processAllGates(net);
//
//        // Check if results are okay.
//        ASSERT_OUTPUT_EQ("out", 0, std::get<3>(invals[i]));
//
//        net.tick();
//    }
//}
//
// template <class NetworkBuilder>
// void testBinopGates()
//{
//    NetworkBuilder builder;
//    int id0 = builder.INPUT("in0", 0);
//    int id1 = builder.INPUT("in1", 0);
//
//    std::unordered_map<std::string, std::array<uint8_t, 4 /* 00, 01, 10, 11 */
//                                               >>
//        id2res;
//
//#define DEFINE_BINOP_GATE_TEST(name, e00, e01, e10, e11) \
//    do {                                                 \
//        int gateId = builder.name();                     \
//        int outputId = builder.OUTPUT("out_" #name, 0);  \
//        builder.connect(id0, gateId);                    \
//        builder.connect(id1, gateId);                    \
//        builder.connect(gateId, outputId);               \
//        id2res["out_" #name] = {e00, e01, e10, e11};     \
//    } while (false);
//    DEFINE_BINOP_GATE_TEST(AND, 0, 0, 0, 1);
//    DEFINE_BINOP_GATE_TEST(NAND, 1, 1, 1, 0);
//    DEFINE_BINOP_GATE_TEST(ANDNOT, 0, 0, 1, 0);
//    DEFINE_BINOP_GATE_TEST(OR, 0, 1, 1, 1);
//    DEFINE_BINOP_GATE_TEST(ORNOT, 1, 0, 1, 1);
//    DEFINE_BINOP_GATE_TEST(XOR, 0, 1, 1, 0);
//    DEFINE_BINOP_GATE_TEST(XNOR, 1, 0, 0, 1);
//#undef DEFINE_BINOP_GATE_TEST
//
//    TaskNetwork net = std::move(builder);
//
//    std::array<std::pair<int, int>, 4> invals{{{0, 0}, {0, 1}, {1, 0}, {1,
//    1}}}; for (int i = 0; i < 4; i++) {
//        // Set inputs.
//        SET_INPUT("in0", 0, invals[i].first ? 1 : 0);
//        SET_INPUT("in1", 0, invals[i].second ? 1 : 0);
//
//        processAllGates(net);
//
//        // Check if results are okay.
//        for (auto&& [portName, res] : id2res)
//            ASSERT_OUTPUT_EQ(portName, 0, res[i]);
//
//        net.tick();
//    }
//}
//
// template <class NetworkBuilder>
// void testFromJSONtest_pass_4bit()
//{
//    const std::string fileName = "test/iyokanl1-json/pass-4bit-iyokanl1.json";
//    std::ifstream ifs{fileName};
//    assert(ifs);
//
//    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
//    assertNetValid(net);
//
//    SET_INPUT("io_in", 0, 0);
//    SET_INPUT("io_in", 1, 1);
//    SET_INPUT("io_in", 2, 1);
//    SET_INPUT("io_in", 3, 0);
//
//    processAllGates(net);
//
//    ASSERT_OUTPUT_EQ("io_out", 0, 0);
//    ASSERT_OUTPUT_EQ("io_out", 1, 1);
//    ASSERT_OUTPUT_EQ("io_out", 2, 1);
//    ASSERT_OUTPUT_EQ("io_out", 3, 0);
//}
//
// template <class NetworkBuilder>
// void testFromJSONtest_and_4bit()
//{
//    const std::string fileName = "test/iyokanl1-json/and-4bit-iyokanl1.json";
//    std::ifstream ifs{fileName};
//    assert(ifs);
//
//    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
//    assertNetValid(net);
//
//    SET_INPUT("io_inA", 0, 0);
//    SET_INPUT("io_inA", 1, 0);
//    SET_INPUT("io_inA", 2, 1);
//    SET_INPUT("io_inA", 3, 1);
//    SET_INPUT("io_inB", 0, 0);
//    SET_INPUT("io_inB", 1, 1);
//    SET_INPUT("io_inB", 2, 0);
//    SET_INPUT("io_inB", 3, 1);
//
//    processAllGates(net);
//
//    ASSERT_OUTPUT_EQ("io_out", 0, 0);
//    ASSERT_OUTPUT_EQ("io_out", 1, 0);
//    ASSERT_OUTPUT_EQ("io_out", 2, 0);
//    ASSERT_OUTPUT_EQ("io_out", 3, 1);
//}
//
// template <class NetworkBuilder>
// void testFromJSONtest_and_4_2bit()
//{
//    const std::string fileName =
//    "test/iyokanl1-json/and-4_2bit-iyokanl1.json"; std::ifstream
//    ifs{fileName}; assert(ifs);
//
//    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
//    assertNetValid(net);
//
//    SET_INPUT("io_inA", 0, 1);
//    SET_INPUT("io_inA", 1, 0);
//    SET_INPUT("io_inA", 2, 1);
//    SET_INPUT("io_inA", 3, 1);
//    SET_INPUT("io_inB", 0, 1);
//    SET_INPUT("io_inB", 1, 1);
//    SET_INPUT("io_inB", 2, 1);
//    SET_INPUT("io_inB", 3, 1);
//
//    processAllGates(net);
//
//    ASSERT_OUTPUT_EQ("io_out", 0, 1);
//    ASSERT_OUTPUT_EQ("io_out", 1, 0);
//}
//
// template <class NetworkBuilder>
// void testFromJSONtest_mux_4bit()
//{
//    const std::string fileName = "test/iyokanl1-json/mux-4bit-iyokanl1.json";
//    std::ifstream ifs{fileName};
//    assert(ifs);
//
//    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
//    assertNetValid(net);
//
//    SET_INPUT("io_inA", 0, 0);
//    SET_INPUT("io_inA", 1, 0);
//    SET_INPUT("io_inA", 2, 1);
//    SET_INPUT("io_inA", 3, 1);
//    SET_INPUT("io_inB", 0, 0);
//    SET_INPUT("io_inB", 1, 1);
//    SET_INPUT("io_inB", 2, 0);
//    SET_INPUT("io_inB", 3, 1);
//
//    SET_INPUT("io_sel", 0, 0);
//    processAllGates(net);
//    ASSERT_OUTPUT_EQ("io_out", 0, 0);
//    ASSERT_OUTPUT_EQ("io_out", 1, 0);
//    ASSERT_OUTPUT_EQ("io_out", 2, 1);
//    ASSERT_OUTPUT_EQ("io_out", 3, 1);
//    net.tick();
//
//    SET_INPUT("io_sel", 0, 1);
//    processAllGates(net);
//    ASSERT_OUTPUT_EQ("io_out", 0, 0);
//    ASSERT_OUTPUT_EQ("io_out", 1, 1);
//    ASSERT_OUTPUT_EQ("io_out", 2, 0);
//    ASSERT_OUTPUT_EQ("io_out", 3, 1);
//}
//
// template <class NetworkBuilder>
// void testFromJSONtest_addr_4bit()
//{
//    const std::string fileName = "test/iyokanl1-json/addr-4bit-iyokanl1.json";
//    std::ifstream ifs{fileName};
//    assert(ifs);
//
//    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
//    assertNetValid(net);
//
//    SET_INPUT("io_inA", 0, 0);
//    SET_INPUT("io_inA", 1, 0);
//    SET_INPUT("io_inA", 2, 1);
//    SET_INPUT("io_inA", 3, 1);
//    SET_INPUT("io_inB", 0, 0);
//    SET_INPUT("io_inB", 1, 1);
//    SET_INPUT("io_inB", 2, 0);
//    SET_INPUT("io_inB", 3, 1);
//
//    processAllGates(net);
//
//    ASSERT_OUTPUT_EQ("io_out", 0, 0);
//    ASSERT_OUTPUT_EQ("io_out", 1, 1);
//    ASSERT_OUTPUT_EQ("io_out", 2, 1);
//    ASSERT_OUTPUT_EQ("io_out", 3, 0);
//}
//
// template <class NetworkBuilder>
// void testFromJSONtest_register_4bit()
//{
//    const std::string fileName =
//        "test/iyokanl1-json/register-4bit-iyokanl1.json";
//    std::ifstream ifs{fileName};
//    assert(ifs);
//
//    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
//    assertNetValid(net);
//
//    SET_INPUT("io_in", 0, 0);
//    SET_INPUT("io_in", 1, 0);
//    SET_INPUT("io_in", 2, 1);
//    SET_INPUT("io_in", 3, 1);
//
//    // 1: Reset all DFFs.
//    SET_INPUT("reset", 0, 1);
//    processAllGates(net);
//    net.tick();
//
//    // 2: Store values into DFFs.
//    SET_INPUT("reset", 0, 0);
//    processAllGates(net);
//    net.tick();
//
//    ASSERT_OUTPUT_EQ("io_out", 0, 0);
//    ASSERT_OUTPUT_EQ("io_out", 1, 0);
//    ASSERT_OUTPUT_EQ("io_out", 2, 0);
//    ASSERT_OUTPUT_EQ("io_out", 3, 0);
//
//    // 3: Get outputs.
//    SET_INPUT("reset", 0, 0);
//    processAllGates(net);
//    net.tick();
//
//    ASSERT_OUTPUT_EQ("io_out", 0, 0);
//    ASSERT_OUTPUT_EQ("io_out", 1, 0);
//    ASSERT_OUTPUT_EQ("io_out", 2, 1);
//    ASSERT_OUTPUT_EQ("io_out", 3, 1);
//}
//
// template <class NetworkBuilder>
// void testSequentialCircuit()
//{
//    /*
//                    B               D
//       reset(0) >---> ANDNOT(4) >---> DFF(2)
//                        ^ A            v Q
//                        |              |
//                        *--< NOT(3) <--*-----> OUTPUT(1)
//                                    A
//    */
//
//    NetworkBuilder builder;
//    int id0 = builder.INPUT("reset", 0);
//    int id1 = builder.OUTPUT("out", 0);
//    int id2 = builder.DFF();
//    int id3 = builder.NOT();
//    int id4 = builder.ANDNOT();
//    builder.connect(id2, id1);
//    builder.connect(id4, id2);
//    builder.connect(id2, id3);
//    builder.connect(id3, id4);
//    builder.connect(id0, id4);
//
//    TaskNetwork net = std::move(builder);
//    assertNetValid(net);
//
//    auto dff =
//        std::dynamic_pointer_cast<typename NetworkBuilder::ParamTaskTypeMem>(
//            net.node(id2)->task());
//    auto out = get<NetworkBuilder>(net, "output", "out", 0);
//
//    // 1:
//    SET_INPUT("reset", 0, 1);
//    processAllGates(net);
//
//    // 2:
//    net.tick();
//    assert(getOutput(dff) == 0);
//    SET_INPUT("reset", 0, 0);
//    processAllGates(net);
//    ASSERT_OUTPUT_EQ("out", 0, 0);
//
//    // 3:
//    net.tick();
//    assert(getOutput(dff) == 1);
//    processAllGates(net);
//    ASSERT_OUTPUT_EQ("out", 0, 1);
//
//    // 4:
//    net.tick();
//    assert(getOutput(dff) == 0);
//    processAllGates(net);
//    ASSERT_OUTPUT_EQ("out", 0, 0);
//}
//
// template <class NetworkBuilder>
// void testFromJSONtest_counter_4bit()
//{
//    const std::string fileName =
//        "test/iyokanl1-json/counter-4bit-iyokanl1.json";
//    std::ifstream ifs{fileName};
//    assert(ifs);
//
//    auto net = readNetworkFromJSON<NetworkBuilder>(ifs);
//    assertNetValid(net);
//
//    std::vector<std::array<int, 4>> outvals{{{0, 0, 0, 0},
//                                             {1, 0, 0, 0},
//                                             {0, 1, 0, 0},
//                                             {1, 1, 0, 0},
//                                             {0, 0, 1, 0},
//                                             {1, 0, 1, 0},
//                                             {0, 1, 1, 0},
//                                             {1, 1, 1, 0},
//                                             {0, 0, 0, 1},
//                                             {1, 0, 0, 1},
//                                             {0, 1, 0, 1},
//                                             {1, 1, 0, 1},
//                                             {0, 0, 1, 1},
//                                             {1, 0, 1, 1},
//                                             {0, 1, 1, 1},
//                                             {1, 1, 1, 1}}};
//
//    SET_INPUT("reset", 0, 1);
//    processAllGates(net);
//
//    SET_INPUT("reset", 0, 0);
//    for (size_t i = 0; i < outvals.size(); i++) {
//        net.tick();
//        processAllGates(net);
//        ASSERT_OUTPUT_EQ("io_out", 0, outvals[i][0]);
//        ASSERT_OUTPUT_EQ("io_out", 1, outvals[i][1]);
//        ASSERT_OUTPUT_EQ("io_out", 2, outvals[i][2]);
//        ASSERT_OUTPUT_EQ("io_out", 3, outvals[i][3]);
//    }
//}
//
// template <class NetworkBuilder>
// void testPrioritySetVisitor()
//{
//    NetworkBuilder builder;
//    int id0 = builder.INPUT("A", 0);
//    int id1 = builder.NOT();
//    int id2 = builder.OUTPUT("out", 0);
//    builder.connect(id0, id1);
//    builder.connect(id1, id2);
//
//    TaskNetwork net = std::move(builder);
//    auto depnode = get<NetworkBuilder>(net, "output", "out", 0)->depnode();
//    assert(depnode->priority() == -1);
//
//    // Set priority to each DepNode
//    GraphVisitor grvis;
//    net.visit(grvis);
//    PrioritySetVisitor privis{graph::doTopologicalSort(grvis.getMap())};
//    net.visit(privis);
//
//    assert(depnode->priority() == 2);
//}
//
////
//#include "iyokan_plain.hpp"
//
// void processAllGates(PlainNetwork& net,
//                     std::shared_ptr<ProgressGraphMaker> graph = nullptr)
//{
//    processAllGates(net, std::thread::hardware_concurrency(), graph);
//}
//
// void setInput(std::shared_ptr<TaskPlainGateMem> task, int val)
//{
//    task->set(val != 0 ? 1_b : 0_b);
//}
//
// int getOutput(std::shared_ptr<TaskPlainGateMem> task)
//{
//    return task->get() == 1_b ? 1 : 0;
//}
//
// void testProgressGraphMaker()
//{
//    /*
//                    B               D
//       reset(0) >---> ANDNOT(4) >---> DFF(2)
//                        ^ A            v Q
//                        |              |
//                        *--< NOT(3) <--*-----> OUTPUT(1)
//                                    A
//    */
//
//    PlainNetworkBuilder builder;
//    int id0 = builder.INPUT("reset", 0);
//    int id1 = builder.OUTPUT("out", 0);
//    int id2 = builder.DFF();
//    int id3 = builder.NOT();
//    int id4 = builder.ANDNOT();
//    builder.connect(id2, id1);
//    builder.connect(id4, id2);
//    builder.connect(id2, id3);
//    builder.connect(id3, id4);
//    builder.connect(id0, id4);
//
//    PlainNetwork net = std::move(builder);
//    assertNetValid(net);
//
//    auto graph = std::make_shared<ProgressGraphMaker>();
//
//    processAllGates(net, graph);
//
//    std::stringstream ss;
//    graph->dumpDOT(ss);
//    std::string dot = ss.str();
//    assert(dot.find(fmt::sprintf("n%d [label = \"{INPUT|reset[0]}\"]", id0))
//    !=
//           std::string::npos);
//    assert(dot.find(fmt::sprintf("n%d [label = \"{OUTPUT|out[0]}\"]", id1)) !=
//           std::string::npos);
//    assert(dot.find(fmt::sprintf("n%d [label = \"{DFF}\"]", id2)) !=
//           std::string::npos);
//    assert(dot.find(fmt::sprintf("n%d [label = \"{NOT}\"]", id3)) !=
//           std::string::npos);
//    assert(dot.find(fmt::sprintf("n%d [label = \"{ANDNOT}\"]", id4)) !=
//           std::string::npos);
//    assert(dot.find(fmt::sprintf("n%d -> n%d", id2, id1)) !=
//    std::string::npos); assert(dot.find(fmt::sprintf("n%d -> n%d", id4, id2))
//    != std::string::npos); assert(dot.find(fmt::sprintf("n%d -> n%d", id2,
//    id3)) != std::string::npos); assert(dot.find(fmt::sprintf("n%d -> n%d",
//    id0, id4)) != std::string::npos); assert(dot.find(fmt::sprintf("n%d ->
//    n%d", id3, id4)) != std::string::npos);
//}
//
//#include "iyokan_tfhepp.hpp"
//
// class TFHEppTestHelper {
// private:
//    std::shared_ptr<SecretKey> sk_;
//    std::shared_ptr<GateKeyFFT> gk_;
//    std::shared_ptr<CircuitKey> ck_;
//    TLWELvl0 zero_, one_;
//
// private:
//    TFHEppTestHelper()
//    {
//        sk_ = std::make_shared<SecretKey>();
//        gk_ = std::make_shared<GateKeyFFT>(*sk_);
//        zero_ = TFHEpp::bootsSymEncrypt({0}, *sk_).at(0);
//        one_ = TFHEpp::bootsSymEncrypt({1}, *sk_).at(0);
//    }
//
// public:
//    static TFHEppTestHelper& instance()
//    {
//        static TFHEppTestHelper inst;
//        return inst;
//    }
//
//    void prepareCircuitKey()
//    {
//        ck_ = std::make_shared<CircuitKey>(*sk_);
//    }
//
//    TFHEppWorkerInfo wi() const
//    {
//        return TFHEppWorkerInfo{gk_, ck_};
//    }
//
//    const std::shared_ptr<SecretKey>& sk() const
//    {
//        return sk_;
//    }
//
//    const std::shared_ptr<GateKeyFFT>& gk() const
//    {
//        return gk_;
//    }
//
//    const TLWELvl0& zero() const
//    {
//        return zero_;
//    }
//
//    const TLWELvl0& one() const
//    {
//        return one_;
//    }
//};
//
// void processAllGates(TFHEppNetwork& net,
//                     std::shared_ptr<ProgressGraphMaker> graph = nullptr)
//{
//    processAllGates(net, std::thread::hardware_concurrency(),
//                    TFHEppTestHelper::instance().wi(), graph);
//}
//
// void setInput(std::shared_ptr<TaskTFHEppGateMem> task, int val)
//{
//    auto& h = TFHEppTestHelper::instance();
//    task->set(val ? h.one() : h.zero());
//}
//
// int getOutput(std::shared_ptr<TaskTFHEppGateMem> task)
//{
//    return TFHEpp::bootsSymDecrypt({task->get()},
//                                   *TFHEppTestHelper::instance().sk())[0];
//}
//
// void testTFHEppSerialization()
//{
//    auto& h = TFHEppTestHelper::instance();
//    const std::shared_ptr<const SecretKey>& sk = h.sk();
//    const std::shared_ptr<const GateKeyFFT>& gk = h.wi().gateKey;
//
//    // Test for secret key
//    {
//        // Dump
//        writeToArchive("_test_sk", *sk);
//        // Load
//        auto sk2 = std::make_shared<SecretKey>();
//        readFromArchive<SecretKey>(*sk2, "_test_sk");
//
//        auto zero = TFHEpp::bootsSymEncrypt({0}, *sk2).at(0);
//        auto one = TFHEpp::bootsSymEncrypt({1}, *sk2).at(0);
//        TLWELvl0 res;
//        TFHEpp::HomANDNY(res, zero, one, *gk);
//        assert(TFHEpp::bootsSymDecrypt({res}, *sk2).at(0) == 1);
//    }
//
//    // Test for gate key
//    {
//        std::stringstream ss{std::ios::binary | std::ios::out | std::ios::in};
//
//        // Dump
//        writeToArchive(ss, *gk);
//        // Load
//        auto gk2 = std::make_shared<GateKeyFFT>();
//        readFromArchive<GateKeyFFT>(*gk2, ss);
//
//        auto zero = TFHEpp::bootsSymEncrypt({0}, *sk).at(0);
//        auto one = TFHEpp::bootsSymEncrypt({1}, *sk).at(0);
//        TLWELvl0 res;
//        TFHEpp::HomANDNY(res, zero, one, *gk2);
//        assert(TFHEpp::bootsSymDecrypt({res}, *sk).at(0) == 1);
//    }
//
//    // Test for TLWE level 0
//    {
//        std::stringstream ss{std::ios::binary | std::ios::out | std::ios::in};
//
//        {
//            auto zero = TFHEpp::bootsSymEncrypt({0}, *sk).at(0);
//            auto one = TFHEpp::bootsSymEncrypt({1}, *sk).at(0);
//            writeToArchive(ss, zero);
//            writeToArchive(ss, one);
//            ss.seekg(0);
//        }
//
//        {
//            TLWELvl0 res, zero, one;
//            readFromArchive(zero, ss);
//            readFromArchive(one, ss);
//            TFHEpp::HomANDNY(res, zero, one, *gk);
//            assert(TFHEpp::bootsSymDecrypt({res}, *sk).at(0) == 1);
//        }
//    }
//}
//
//#ifdef IYOKAN_CUDA_ENABLED
//#include "iyokan_cufhe.hpp"
//
// class CUFHETestHelper {
// private:
//    std::shared_ptr<GateKey> gk_;
//    cufhe::Ctxt zero_, one_;
//
// private:
//    CUFHETestHelper()
//    {
//        gk_ = std::make_shared<GateKey>();
//        ifftGateKey(*gk_, *TFHEppTestHelper::instance().gk());
//        setCtxtZero(zero_);
//        setCtxtOne(one_);
//    }
//
// public:
//    class CUFHEManager {
//    public:
//        CUFHEManager()
//        {
//            cufhe::Initialize(*CUFHETestHelper::instance().gk_);
//        }
//
//        ~CUFHEManager()
//        {
//            cufhe::CleanUp();
//        }
//    };
//
// public:
//    static CUFHETestHelper& instance()
//    {
//        static CUFHETestHelper inst;
//        return inst;
//    }
//};
//
// void processAllGates(CUFHENetwork& net,
//                     std::shared_ptr<ProgressGraphMaker> graph = nullptr)
//{
//    processAllGates(net, 240, graph);
//}
//
// void setInput(std::shared_ptr<TaskCUFHEGateMem> task, int val)
//{
//    TLWELvl0 c;
//    if (val)
//        setTLWELvl0Trivial1(c);
//    else
//        setTLWELvl0Trivial0(c);
//    task->set(c);
//}
//
// int getOutput(std::shared_ptr<TaskCUFHEGateMem> task)
//{
//    return decryptTLWELvl0(task->get(), *TFHEppTestHelper::instance().sk());
//}
//
// void testBridgeBetweenCUFHEAndTFHEpp()
//{
//    auto& ht = TFHEppTestHelper::instance();
//
//    // FIXME: The network constructed here does not have any meanings anymore,
//    // but it is enough to check if bridges work correctly.
//    // We may need another network here.
//    NetworkBuilderBase<CUFHEWorkerInfo> b0;
//    NetworkBuilderBase<TFHEppWorkerInfo> b1;
//    auto t0 = b0.addINPUT<TaskCUFHEGateWIRE>("in", 0, false);
//    auto t1 = std::make_shared<TaskTFHEpp2CUFHE>();
//    b1.addTask(NodeLabel{"tfhepp2cufhe", ""}, t1);
//    auto t2 = std::make_shared<TaskCUFHE2TFHEpp>();
//    b1.addTask(NodeLabel{"cufhe2tfhepp", ""}, t2);
//    auto t3 = b0.addOUTPUT<TaskCUFHEGateWIRE>("out", 0, true);
//    connectTasks(t1, t2);
//
//    auto net0 = std::make_shared<TaskNetwork<CUFHEWorkerInfo>>(std::move(b0));
//    auto net1 =
//    std::make_shared<TaskNetwork<TFHEppWorkerInfo>>(std::move(b1)); auto
//    bridge0 = connectWithBridge(t0, t1); auto bridge1 = connectWithBridge(t2,
//    t3);
//
//    CUFHENetworkRunner runner{1, 1, ht.wi()};
//    runner.addNetwork(net0);
//    runner.addNetwork(net1);
//    runner.addBridge(bridge0);
//    runner.addBridge(bridge1);
//
//    t0->set(ht.one());
//    runner.run(false);
//    assert(t3->get() == ht.one());
//
//    net0->tick();
//    net1->tick();
//    bridge0->tick();
//    bridge1->tick();
//
//    t0->set(ht.zero());
//    runner.run(false);
//    assert(t3->get() == ht.zero());
//}
//#endif
//
// void testBlueprint()
//{
//    using namespace blueprint;
//
//    NetworkBlueprint blueprint{"test/config-toml/cahp-diamond.toml"};
//
//    {
//        const auto& files = blueprint.files();
//        assert(files.size() == 1);
//        assert(files[0].type == File::TYPE::YOSYS_JSON);
//        assert(std::filesystem::canonical(files[0].path) ==
//               std::filesystem::canonical(
//                   "test/yosys-json/cahp-diamond-core-yosys.json"));
//        assert(files[0].name == "core");
//    }
//
//    {
//        const auto& roms = blueprint.builtinROMs();
//        assert(roms.size() == 1);
//        assert(roms[0].name == "rom");
//        assert(roms[0].inAddrWidth == 7);
//        assert(roms[0].outRdataWidth == 32);
//    }
//
//    {
//        const auto& rams = blueprint.builtinRAMs();
//        assert(rams.size() == 2);
//        assert((rams[0].name == "ramA" && rams[1].name == "ramB") ||
//               (rams[1].name == "ramA" && rams[0].name == "ramB"));
//        assert(rams[0].inAddrWidth == 8);
//        assert(rams[0].inWdataWidth == 8);
//        assert(rams[0].outRdataWidth == 8);
//        assert(rams[1].inAddrWidth == 8);
//        assert(rams[1].inWdataWidth == 8);
//        assert(rams[1].outRdataWidth == 8);
//    }
//
//    {
//        const auto& edges = blueprint.edges();
//        auto assertIn = [&edges](std::string fNodeName, std::string fPortName,
//                                 std::string tNodeName, std::string tPortName,
//                                 int size) {
//            for (int i = 0; i < size; i++) {
//                auto v =
//                    std::make_pair(Port{fNodeName, {"output", fPortName, i}},
//                                   Port{tNodeName, {"input", tPortName, i}});
//                auto it = std::find(edges.begin(), edges.end(), v);
//                assert(it != edges.end());
//            }
//        };
//        assertIn("core", "io_romAddr", "rom", "addr", 7);
//        assertIn("rom", "rdata", "core", "io_romData", 32);
//        assertIn("core", "io_memA_writeEnable", "ramA", "wren", 1);
//        assertIn("core", "io_memA_address", "ramA", "addr", 8);
//        assertIn("core", "io_memA_in", "ramA", "wdata", 8);
//        assertIn("ramA", "rdata", "core", "io_memA_out", 8);
//        assertIn("core", "io_memB_writeEnable", "ramB", "wren", 1);
//        assertIn("core", "io_memB_address", "ramB", "addr", 8);
//        assertIn("core", "io_memB_in", "ramB", "wdata", 8);
//        assertIn("ramB", "rdata", "core", "io_memB_out", 8);
//    }
//
//    {
//        const Port port = blueprint.at("reset").value();
//        assert(port.nodeName == "core");
//        assert(port.portLabel.kind == "input");
//        assert(port.portLabel.portName == "reset");
//        assert(port.portLabel.portBit == 0);
//    }
//
//    {
//        const Port port = blueprint.at("finflag").value();
//        assert(port.nodeName == "core");
//        assert(port.portLabel.kind == "output");
//        assert(port.portLabel.portName == "io_finishFlag");
//        assert(port.portLabel.portBit == 0);
//    }
//
//    for (int ireg = 0; ireg < 16; ireg++) {
//        for (int ibit = 0; ibit < 16; ibit++) {
//            const Port port =
//                blueprint.at(utility::fok("reg_x", ireg), ibit).value();
//            assert(port.nodeName == "core");
//            assert(port.portLabel.portName ==
//                   utility::fok("io_regOut_x", ireg));
//            assert(port.portLabel.portBit == ibit);
//        }
//    }
//}

#include "iyokan_nt.hpp"
#include "iyokan_nt_plain.hpp"
#include "tfhepp_cufhe_wrapper.hpp"

class TFHEppTestHelper {
private:
    std::shared_ptr<SecretKey> sk_;
    std::shared_ptr<GateKeyFFT> gk_;
    std::shared_ptr<CircuitKey> ck_;
    TLWELvl0 zero_, one_;

private:
    TFHEppTestHelper()
    {
        sk_ = std::make_shared<SecretKey>();
        gk_ = std::make_shared<GateKeyFFT>(*sk_);
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
        ck_ = std::make_shared<CircuitKey>(*sk_);
    }

    const std::shared_ptr<SecretKey>& sk() const
    {
        return sk_;
    }

    const std::shared_ptr<GateKeyFFT>& gk() const
    {
        return gk_;
    }

    const TLWELvl0& zero() const
    {
        return zero_;
    }

    const TLWELvl0& one() const
    {
        return one_;
    }
};

namespace nt {
void testAllocator()
{
    {
        Allocator alc;
        {
            TLWELvl0* zero = alc.make<TLWELvl0>();
            TLWELvl0* one = alc.make<TLWELvl0>();
            *zero = TFHEppTestHelper::instance().zero();
            *one = TFHEppTestHelper::instance().one();
        }
        {
            TLWELvl0* zero = alc.get<TLWELvl0>(0);
            TLWELvl0* one = alc.get<TLWELvl0>(1);
            assert(*zero == TFHEppTestHelper::instance().zero());
            assert(*one == TFHEppTestHelper::instance().one());
        }
    }
    {
        Allocator alc;
        {
            TLWELvl0* zero = alc.make<TLWELvl0>();
            TLWELvl0* one = alc.make<TLWELvl0>();
            *zero = TFHEppTestHelper::instance().zero();
            *one = TFHEppTestHelper::instance().one();
        }
        {
            TLWELvl0* zero = alc.get<TLWELvl0>(0);
            TLWELvl0* one = alc.get<TLWELvl0>(1);
            assert(*zero == TFHEppTestHelper::instance().zero());
            assert(*one == TFHEppTestHelper::instance().one());
        }
    }
}

}  // namespace nt

int main()
{
    // AsyncThread::setNumThreads(std::thread::hardware_concurrency());

    //    testNOT<PlainNetworkBuilder>();
    //    testMUX<PlainNetworkBuilder>();
    //    testBinopGates<PlainNetworkBuilder>();
    //    testFromJSONtest_pass_4bit<PlainNetworkBuilder>();
    //    testFromJSONtest_and_4bit<PlainNetworkBuilder>();
    //    testFromJSONtest_and_4_2bit<PlainNetworkBuilder>();
    //    testFromJSONtest_mux_4bit<PlainNetworkBuilder>();
    //    testFromJSONtest_addr_4bit<PlainNetworkBuilder>();
    //    testFromJSONtest_register_4bit<PlainNetworkBuilder>();
    //    testSequentialCircuit<PlainNetworkBuilder>();
    //    testFromJSONtest_counter_4bit<PlainNetworkBuilder>();
    //    testPrioritySetVisitor<PlainNetworkBuilder>();
    //
    //    testNOT<TFHEppNetworkBuilder>();
    //    testMUX<TFHEppNetworkBuilder>();
    //    testBinopGates<TFHEppNetworkBuilder>();
    //    testFromJSONtest_pass_4bit<TFHEppNetworkBuilder>();
    //    testFromJSONtest_pass_4bit<TFHEppNetworkBuilder>();
    //    testFromJSONtest_and_4bit<TFHEppNetworkBuilder>();
    //    testFromJSONtest_and_4_2bit<TFHEppNetworkBuilder>();
    //    testFromJSONtest_mux_4bit<TFHEppNetworkBuilder>();
    //    testFromJSONtest_addr_4bit<TFHEppNetworkBuilder>();
    //    testFromJSONtest_register_4bit<TFHEppNetworkBuilder>();
    //    testSequentialCircuit<TFHEppNetworkBuilder>();
    //    testFromJSONtest_counter_4bit<TFHEppNetworkBuilder>();
    //    testPrioritySetVisitor<TFHEppNetworkBuilder>();
    //    testTFHEppSerialization();
    //
    //#ifdef IYOKAN_CUDA_ENABLED
    //    {
    //        CUFHETestHelper::CUFHEManager man;
    //
    //        testNOT<CUFHENetworkBuilder>();
    //        testMUX<CUFHENetworkBuilder>();
    //        testBinopGates<CUFHENetworkBuilder>();
    //        testFromJSONtest_pass_4bit<CUFHENetworkBuilder>();
    //        testFromJSONtest_and_4bit<CUFHENetworkBuilder>();
    //        testFromJSONtest_and_4_2bit<CUFHENetworkBuilder>();
    //        testFromJSONtest_mux_4bit<CUFHENetworkBuilder>();
    //        testFromJSONtest_addr_4bit<CUFHENetworkBuilder>();
    //        testFromJSONtest_register_4bit<CUFHENetworkBuilder>();
    //        testSequentialCircuit<CUFHENetworkBuilder>();
    //        testFromJSONtest_counter_4bit<CUFHENetworkBuilder>();
    //        testPrioritySetVisitor<CUFHENetworkBuilder>();
    //        testBridgeBetweenCUFHEAndTFHEpp();
    //    }
    //#endif

    // testProgressGraphMaker();
    // testBlueprint();

    nt::testAllocator();
    nt::plain::test0();
}
