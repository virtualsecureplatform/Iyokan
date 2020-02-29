#include "iyokan_cufhe.hpp"
#include "packet.hpp"

namespace {

using Name2TFHEppNetMap =
    std::unordered_map<std::string, std::shared_ptr<TFHEppNetwork>>;
using Name2CUFHENetMap =
    std::unordered_map<std::string, std::shared_ptr<CUFHENetwork>>;

struct CUFHENetworkManager {
    std::shared_ptr<CUFHENetwork> core;
    std::shared_ptr<TFHEppNetwork> ramA, ramB;
};

auto get(CUFHENetwork& net, const std::string& kind,
         const std::string& portName, int portBit)
{
    auto ret = net.get<TaskCUFHEGateMem>(kind, portName, portBit);
    assert(ret && "Possibly invalid port name or port bit");
    return ret;
}

KVSPResPacket makeResPacket(Name2CUFHENetMap& name2cnet,
                            Name2TFHEppNetMap& name2tnet, int numCycles,
                            bool ramEnabled)
{
    auto& core = *name2cnet.at("core");

    KVSPResPacket resPacket;
    resPacket.numCycles = numCycles;
    // Get values of flags
    resPacket.flags.push_back(
        cufhe2tfhepp(get(core, "output", "io_finishFlag", 0)->get()));
    // Get values of registers
    for (int reg = 0; reg < 16; reg++) {
        resPacket.regs.emplace_back();
        for (int bit = 0; bit < 16; bit++)
            resPacket.regs[reg].push_back(cufhe2tfhepp(
                get(core, "output", detail::fok("io_regOut_x", reg), bit)
                    ->get()));
    }
    // Get values of RAM
    if (ramEnabled) {
        auto &ramA = *name2tnet.at("ramA"), &ramB = *name2tnet.at("ramB");

        for (int addr = 0; addr < 512; addr++) {
            for (int bit = 0; bit < 8; bit++) {
                auto ram = addr % 2 == 1
                               ? ramA.get<TaskCUFHERAMUX>("ram", "A", bit)
                               : ramB.get<TaskCUFHERAMUX>("ram", "B", bit);
                assert(ram);
                resPacket.ramCk.push_back(ram->get(addr / 2).trlwehost);
            }
        }
    }
    else {
        for (int addr = 0; addr < 512; addr++)
            for (int bit = 0; bit < 8; bit++)
                resPacket.ram.push_back(cufhe2tfhepp(
                    get(core, "ram", std::to_string(addr), bit)->get()));
    }

    return resPacket;
}

void dumpResultAsJSON(std::ostream& os, const KVSPResPacket& resPacket,
                      const std::string& fileSecretKey)
{
    auto sk = std::make_shared<TFHEpp::SecretKey>();
    readFromArchive(*sk, fileSecretKey);
    decrypt(*sk, resPacket).printAsJSON(os);
}

void setInitialRAM(Name2CUFHENetMap& name2cnet, Name2TFHEppNetMap& name2tnet,
                   const KVSPReqPacket& reqPacket, bool ramEnabled)
{
    if (ramEnabled) {
        auto &ramA = *name2tnet.at("ramA"), &ramB = *name2tnet.at("ramB");

        for (int addr = 0; addr < 512; addr++)
            for (int bit = 0; bit < 8; bit++)
                (addr % 2 == 1 ? ramA.get<TaskCUFHERAMUX>("ram", "A", bit)
                               : ramB.get<TaskCUFHERAMUX>("ram", "B", bit))
                    ->set(addr / 2, reqPacket.ramCk.at(addr * 8 + bit));
    }
    else {
        auto& core = *name2cnet.at("core");

        for (int addr = 0; addr < 512; addr++)
            for (int bit = 0; bit < 8; bit++)
                get(core, "ram", std::to_string(addr), bit)
                    ->set(*tfhepp2cufhe(reqPacket.ram.at(addr * 8 + bit)));
    }
}

template <class T>
void merge(std::vector<T>& dst, const std::vector<T>& src)
{
    dst.reserve(dst.size() + src.size());
    std::copy(src.begin(), src.end(), std::back_inserter(dst));
}

void connect(CUFHENetwork& srcNet, const std::string& srcPortName,
             int srcPortBit, CUFHENetwork& dstNet,
             const std::string& dstPortName, int dstPortBit)
{
    auto dst = dstNet.get<TaskCUFHEGate>("input", dstPortName, dstPortBit);
    assert(dst);
    auto src = srcNet.get<TaskCUFHEGate>("output", srcPortName, srcPortBit);
    assert(src);

    dst->acceptOneMoreInput();
    NetworkBuilderBase<CUFHEWorkerInfo>::connectTasks(src, dst);
}

}  // namespace

void processAllGates(CUFHENetwork& net, int numWorkers,
                     std::shared_ptr<ProgressGraphMaker> graph)
{
    ReadyQueue<CUFHEWorkerInfo> readyQueue;
    net.pushReadyTasks(readyQueue);

    // Create workers.
    size_t numFinishedTargets = 0;
    std::vector<CUFHEWorker> workers;
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

std::shared_ptr<cufhe::PubKey> tfhepp2cufhe(const TFHEpp::GateKey& src)
{
    auto pubkey = std::make_shared<cufhe::PubKey>();

    // FIXME: Check if TFHEpp's parameters are the same as cuFHE's.
    auto cufheParams = cufhe::GetDefaultParam();
    const int32_t n = cufheParams->lwe_n_;
    const int32_t N = cufheParams->tlwe_n_;
    const int32_t k = cufheParams->tlwe_k_;
    const int32_t l = cufheParams->tgsw_decomp_size_;
    const int32_t ksk_t = cufheParams->keyswitching_decomp_size_;
    const int32_t ksk_n = N * k;
    const int32_t ksk_base = (1 << cufheParams->keyswitching_decomp_bits_);

    // Read the bootstrapping key.
    for (int p = 0; p < n; p++) {
        const TFHEpp::TRGSWFFTlvl1& trgswfft = src.bkfftlvl01[p];
        for (int q = 0; q < (k + 1) * l; q++) {
            for (int r = 0; r < (k + 1); r++) {
                TFHEpp::Polynomiallvl1 poly;
                TFHEpp::TwistFFTlvl1(poly, trgswfft[q][r]);
                for (int s = 0; s < N; s++) {
                    int index = ((p * ((k + 1) * l) + q) * (k + 1) + r) * N + s;
                    pubkey->bk_->data()[index] = poly[s];
                }
            }
        }
    }

    // Read the key switch key.
    for (int p = 0; p < ksk_n; p++) {
        for (int q = 0; q < ksk_t; q++) {
            // r = 0
            {
                cufhe::LWESample to = pubkey->ksk_->ExtractLWESample(
                    pubkey->ksk_->GetLWESampleIndex(p, q, 0));
                for (int s = 0; s < n; s++)
                    to.data()[s] = 0;
                to.data()[n] = 0;
            }
            // r >= 1
            for (int r = 1; r < ksk_base; r++) {
                assert(static_cast<size_t>(p) < src.ksk.size());
                assert(static_cast<size_t>(q) < src.ksk[p].size());
                assert(static_cast<size_t>(r - 1) < src.ksk[p][q].size());

                const TFHEpp::TLWElvl0& from = src.ksk[p][q][r - 1];
                cufhe::LWESample to = pubkey->ksk_->ExtractLWESample(
                    pubkey->ksk_->GetLWESampleIndex(p, q, r));
                for (int s = 0; s < n; s++) {
                    assert(static_cast<size_t>(s) < from.size());
                    to.data()[s] = from[s];
                }
                to.data()[n] = from[n];
            }
        }
    }

    return pubkey;
}

struct CUFHENetworkWithTFHEpp {
    std::shared_ptr<CUFHENetwork> cufheNet;
    std::shared_ptr<TFHEppNetwork> tfheppNet;
    std::vector<std::shared_ptr<CUFHE2TFHEppBridge>> bridges0;
    std::vector<std::shared_ptr<TFHEpp2CUFHEBridge>> bridges1;
};

void capTFHEppNetWithCUFHEWIRE(CUFHENetworkWithTFHEpp& net)
{
    assert(net.tfheppNet);

    NetworkBuilderBase<TFHEppWorkerInfo> bt;
    NetworkBuilderBase<CUFHEWorkerInfo> bc;

    auto& brs0 = net.bridges0;
    auto& brs1 = net.bridges1;
    const auto& tfheppNamedMems = net.tfheppNet->getNamedMems();

    for (auto&& [name, mem] : tfheppNamedMems) {
        auto&& [kind, portName, portBit] = name;
        if (kind == "input") {
            /*
               CUFHE INPUT --> Bridge --> CUFHE2TFHEpp --> TFHEpp INPUT
            */
            // Create nodes
            auto tfheppINPUT = std::dynamic_pointer_cast<TaskTFHEppGate>(mem);
            assert(tfheppINPUT);
            auto cufheINPUT = bc.addINPUT<TaskCUFHEGateWIRE>(
                detail::genid(), /* FIXME */ 0, portName, portBit, false);
            auto cufhe2tfhepp = bt.emplaceTask<TaskCUFHE2TFHEpp>(
                NodeLabel{detail::genid(), "cufhe2tfhepp", ""}, 0);
            // Connect nodes
            brs0.push_back(connectWithBridge(cufheINPUT, cufhe2tfhepp));
            tfheppINPUT->acceptOneMoreInput();
            bt.connectTasks(cufhe2tfhepp, tfheppINPUT);
        }
        else if (kind == "output") {
            /*
                TFHEpp OUTPUT --> TFHEpp2CUFHE --> Bridge --> CUFHE OUTPUT
            */
            // Create nodes
            auto tfheppOUTPUT = std::dynamic_pointer_cast<TaskTFHEppGate>(mem);
            assert(tfheppOUTPUT);
            auto cufheOUTPUT = bc.addOUTPUT<TaskCUFHEGateWIRE>(
                detail::genid(), /* FIXME */ 0, portName, portBit, true);
            auto tfhepp2cufhe = bt.emplaceTask<TaskTFHEpp2CUFHE>(
                NodeLabel{detail::genid(), "tfhepp2cufhe", ""}, 0);
            // Connect nodes
            bt.connectTasks(tfheppOUTPUT, tfhepp2cufhe);
            brs1.push_back(connectWithBridge(tfhepp2cufhe, cufheOUTPUT));
        }
    }

    // merge networks
    *net.tfheppNet = net.tfheppNet->merge(std::move(bt));
    if (net.cufheNet)
        *net.cufheNet = net.cufheNet->merge(std::move(bc));
    else
        net.cufheNet = std::make_shared<CUFHENetwork>(std::move(bc));
}

void connectCUFHENetWithTFHEppNet(
    CUFHENetwork& cufhe, TFHEppNetwork& tfhepp,
    std::vector<
        std::shared_ptr<BridgeDepNode<CUFHEWorkerInfo, TFHEppWorkerInfo>>>&
        brCUFHE2TFHEpp,
    std::vector<
        std::shared_ptr<BridgeDepNode<TFHEppWorkerInfo, CUFHEWorkerInfo>>>&
        brTFHEpp2CUFHE,
    const std::vector<std::tuple<std::string, int, std::string, int>>& lhs2rhs,
    const std::vector<std::tuple<std::string, int, std::string, int>>& rhs2lhs)
{
    for (auto&& [lPortName, lPortBit, rPortName, rPortBit] : lhs2rhs) {
        /*
           CUFHE OUTPUT --> Bridge --> CUFHE2TFHEpp --> TFHEpp INPUT
        */
        auto in = cufhe.get<TaskCUFHEGate>("output", lPortName, lPortBit);
        auto out = tfhepp.get<TaskTFHEppGate>("input", rPortName, rPortBit);
        assert(in);
        assert(out);

        // Add the task cufhe2tfhepp to the network tfhepp.
        NetworkBuilderBase<TFHEppWorkerInfo> b;
        auto cufhe2tfhepp = std::make_shared<TaskCUFHE2TFHEpp>();
        b.addTask(NodeLabel{detail::genid(), "cufhe2tfhepp", ""}, 0,
                  cufhe2tfhepp);
        TFHEppNetwork net = std::move(b);
        tfhepp = tfhepp.merge(net);

        // CUFHE OUTPUT --> Bridge --> CUFHE2TFHEpp
        brCUFHE2TFHEpp.push_back(connectWithBridge(in, cufhe2tfhepp));

        // CUFHE2TFHEpp --> TFHEpp INPUT
        out->acceptOneMoreInput();
        NetworkBuilderBase<TFHEppWorkerInfo>::connectTasks(cufhe2tfhepp, out);
    }

    for (auto&& [rPortName, rPortBit, lPortName, lPortBit] : rhs2lhs) {
        /*
           TFHEpp OUTPUT --> TFHEpp2CUFHE --> Bridge --> CUFHE INPUT
        */
        auto in = tfhepp.get<TaskTFHEppGate>("output", rPortName, rPortBit);
        auto out = cufhe.get<TaskCUFHEGate>("input", lPortName, lPortBit);
        assert(in);
        assert(out);

        // Add the task tfhepp2cufhe to the network tfhepp.
        NetworkBuilderBase<TFHEppWorkerInfo> b;
        auto tfhepp2cufhe = std::make_shared<TaskTFHEpp2CUFHE>();
        b.addTask(NodeLabel{detail::genid(), "tfhepp2cufhe", ""}, 0,
                  tfhepp2cufhe);
        TFHEppNetwork net = std::move(b);
        tfhepp = tfhepp.merge(net);

        // TFHEpp OUTPUT --> TFHEpp2CUFHE
        NetworkBuilderBase<TFHEppWorkerInfo>::connectTasks(in, tfhepp2cufhe);

        // TFHEpp2CUFHE --> Bridge --> CUFHE INPUT
        out->acceptOneMoreInput();
        brTFHEpp2CUFHE.push_back(connectWithBridge(tfhepp2cufhe, out));
    }
}

void makeTFHEppRAMNetworkForCUFHEImpl(
    NetworkBuilderBase<CUFHEWorkerInfo>& bc,
    NetworkBuilderBase<TFHEppWorkerInfo>& bt,
    std::vector<
        std::shared_ptr<BridgeDepNode<CUFHEWorkerInfo, TFHEppWorkerInfo>>>&
        bridge0,
    std::vector<
        std::shared_ptr<BridgeDepNode<TFHEppWorkerInfo, CUFHEWorkerInfo>>>&
        bridge1,
    const std::string& ramPortName, int indexByte)
{
    /*
                   +------+
                   | CB   |
        ADDR[0] -- | with |---+
                   | Inv  |   |
                   +------+   |
                              |   +-+
                   +------+   +---| |
                   | CB   |       | |
        ADDR[1] -- | with |-------| |
                   | Inv  |       | |
                   +------+       | +-->B
                                  | |
            ...


        ===========================================================================

                                  +------+
              +-----+   WDATA ->--+ MUX  |
              | RAM |             | woSE |--->A
        B-> --+ UX  +-- SEI --+---+      |
              +-----+         |   +------+
                              |      ^
                              |      +--- Mem
                              |          Write
                              +--> RDATA

        ===========================================================================

               +-------+   +--------+    B          +-----------+   B
        B-> ---+ CMUXs |   | TFHEpp |    r          | Gate      |   r
               |  [0]  +---|   2    | -- i -- SEI --| Boot-     |-- i -- Setter
        A-> ---+       |   | cuFHE  |    d          | strapping |   d
               +-------+   +--------+    g          +-----------+   g
                                         e                          e

               +-------+   +--------+    B          +-----------+   B
        B-> ---+ CMUXs |   | TFHEpp |    r          | Gate      |   r
               |  [0]  +---|   2    | -- i -- SEI --| Boot-     |-- i -- Setter
        A-> ---+       |   | cuFHE  |    d          | strapping |   d
               +-------+   +--------+    g          +-----------+   g
                                         e                          e

                  ...

    */

    // Create inputs and CBs.
    std::vector<std::shared_ptr<TaskTFHEppCBWithInv>> cbs;
    for (size_t i = 0; i < TaskCUFHERAMUX::ADDRESS_BIT; i++) {
        auto taskINPUT = bt.getTask<TaskTFHEppGateWIRE>("input", "addr", i);
        auto taskCB = std::make_shared<TaskTFHEppCBWithInv>();
        bt.addTask(
            NodeLabel{detail::genid(), "CBWithInv", detail::fok("[", i, "]")},
            0, taskCB);
        bt.connectTasks(taskINPUT, taskCB);
        cbs.push_back(taskCB);
    }

    // Create RAMUX.
    auto taskRAMUX = bt.emplaceTask<TaskCUFHERAMUX>(
        NodeLabel{detail::genid(), "RAMUX", ""}, 0);
    bt.registerTask("ram", ramPortName, indexByte, taskRAMUX);

    // Connect CBs and RAMUX.
    for (auto&& cb : cbs)
        bt.connectTasks(cb, taskRAMUX);

    // Create SEIs and connect with CBs.
    auto taskSEI0 = std::make_shared<TaskTFHEppSEI>(0);
    bt.addTask(NodeLabel{detail::genid(), "SEI", "[0]"}, 0, taskSEI0);
    bt.connectTasks(taskRAMUX, taskSEI0);

    // Create output for read-out data and connect.
    auto taskOutputReadData =
        bt.getTask<TaskTFHEppGateWIRE>("output", "rdata", indexByte);
    bt.connectTasks(taskSEI0, taskOutputReadData);

    // Create input for write-in data.
    auto taskInputWriteData =
        bt.getTask<TaskTFHEppGateWIRE>("input", "wdata", indexByte);
    auto taskInputWriteEnabled =
        bt.getTask<TaskTFHEppGateWIRE>("input", "wren", 0);

    // Create MUXWoSE and connect.
    auto taskMUXWoSE = std::make_shared<TaskTFHEppGateMUXWoSE>();
    bt.addTask(NodeLabel{detail::genid(), "MUXWoSE", ""}, 0, taskMUXWoSE);
    bt.connectTasks(taskSEI0, taskMUXWoSE);
    bt.connectTasks(taskInputWriteData, taskMUXWoSE);
    bt.connectTasks(taskInputWriteEnabled, taskMUXWoSE);

    // Create links of CMUXs -> SEI -> GateBootstrapping.
    for (int i = 0; i < (1 << TaskCUFHERAMUX::ADDRESS_BIT); i++) {
        // Create components...
        auto taskCMUXs = bt.emplaceTask<TaskTFHEppRAMCMUXsForCUFHE>(
            NodeLabel{detail::genid(), "CMUXs", detail::fok("[", i, "]")}, 0,
            taskRAMUX->get(i).trlwehost, i);

        auto taskSEIAndKS = bc.emplaceTask<TaskCUFHERAMSEIAndKS>(
            NodeLabel{detail::genid(), "SEI&KS", detail::fok("[", i, "]")}, 0);

        auto taskGB = bc.emplaceTask<TaskCUFHERAMGateBootstrapping>(
            NodeLabel{detail::genid(), "GB", detail::fok("[", i, "]")}, 0,
            taskRAMUX->get(i));

        // ... and connect them.
        bt.connectTasks(taskMUXWoSE, taskCMUXs);
        for (auto&& cb : cbs)
            bt.connectTasks(cb, taskCMUXs);
        bridge1.push_back(connectWithBridge(taskCMUXs, taskSEIAndKS));
        bc.connectTasks(taskSEIAndKS, taskGB);
    }
}

CUFHENetworkWithTFHEpp makeTFHEppRAMNetworkForCUFHE(
    const std::string& ramPortName)
{
    NetworkBuilderBase<TFHEppWorkerInfo> bt;
    NetworkBuilderBase<CUFHEWorkerInfo> bc;
    std::vector<std::shared_ptr<CUFHE2TFHEppBridge>> bridge0;
    std::vector<std::shared_ptr<TFHEpp2CUFHEBridge>> bridge1;

    // Inputs for address.
    for (size_t i = 0; i < TaskCUFHERAMUX::ADDRESS_BIT; i++)
        bt.addINPUT<TaskTFHEppGateWIRE>(detail::genid(), 0, "addr", i, false);

    // Input for write-in flag.
    bt.addINPUT<TaskTFHEppGateWIRE>(detail::genid(), 0, "wren", 0, false);

    for (int indexByte = 0; indexByte < 8; indexByte++) {
        // Input for data to write into RAM.
        bt.addINPUT<TaskTFHEppGateWIRE>(detail::genid(), 0, "wdata", indexByte,
                                        false);
        // Output for data to be read from RAM.
        bt.addOUTPUT<TaskTFHEppGateWIRE>(detail::genid(), 0, "rdata", indexByte,
                                         true);

        makeTFHEppRAMNetworkForCUFHEImpl(bc, bt, bridge0, bridge1, ramPortName,
                                         indexByte);
    }

    auto ret = CUFHENetworkWithTFHEpp{
        std::make_shared<CUFHENetwork>(std::move(bc)),
        std::make_shared<TFHEppNetwork>(std::move(bt)), bridge0, bridge1};
    capTFHEppNetWithCUFHEWIRE(ret);
    return ret;
}

CUFHENetworkWithTFHEpp makeTFHEppROMNetworkForCUFHE()
{
    auto ret = CUFHENetworkWithTFHEpp{
        nullptr,
        std::make_shared<TFHEppNetwork>(makeTFHEppROMNetwork()),
        {},
        {}};
    capTFHEppNetWithCUFHEWIRE(ret);
    return ret;
}

void doCUFHE(const Options& opt)
{
    // Read packet
    const auto reqPacket = readFromArchive<KVSPReqPacket>(opt.inputFile);

    // Prepare cuFHE
    cufhe::SetGPUNum(1);
    cufhe::SetSeed();
    cufhe::Initialize(*tfhepp2cufhe(*reqPacket.gateKey));

    // Create nodes
    Name2TFHEppNetMap name2tnet;
    Name2CUFHENetMap name2cnet;
    std::vector<std::shared_ptr<CUFHE2TFHEppBridge>> bridges0;
    std::vector<std::shared_ptr<TFHEpp2CUFHEBridge>> bridges1;

    {
        std::ifstream ifs{opt.logicFile};
        assert(ifs);
        auto net = std::make_shared<CUFHENetwork>(
            readNetworkFromJSON<CUFHENetworkBuilder>(ifs));
        assert(net->isValid());
        name2cnet.emplace("core", net);
    }

    if (opt.ramEnabled) {
        auto ramA = makeTFHEppRAMNetworkForCUFHE("A"),
             ramB = makeTFHEppRAMNetworkForCUFHE("B");
        name2tnet.emplace("ramA", ramA.tfheppNet);
        name2cnet.emplace("ramA", ramA.cufheNet);
        merge(bridges0, ramA.bridges0);
        merge(bridges1, ramA.bridges1);
        name2tnet.emplace("ramB", ramB.tfheppNet);
        name2cnet.emplace("ramB", ramB.cufheNet);
        merge(bridges0, ramB.bridges0);
        merge(bridges1, ramB.bridges1);
    }

    if (!opt.romPorts.empty()) {
        auto rom = makeTFHEppROMNetworkForCUFHE();
        name2tnet.emplace("rom", rom.tfheppNet);
        name2cnet.emplace("rom", rom.cufheNet);
        merge(bridges0, rom.bridges0);
        merge(bridges1, rom.bridges1);
    }

    // Create edges
    if (opt.ramEnabled) {
        auto &core = *name2cnet.at("core"), &ramA = *name2cnet.at("ramA"),
             &ramB = *name2cnet.at("ramB");

        connect(core, "io_memA_writeEnable", 0, ramA, "wren", 0);
        for (int i = 0; i < 8; i++)
            connect(core, "io_memA_address", i, ramA, "addr", i);
        for (int i = 0; i < 8; i++)
            connect(core, "io_memA_in", i, ramA, "wdata", i);
        for (int i = 0; i < 8; i++)
            connect(ramA, "rdata", i, core, "io_memA_out", i);

        connect(core, "io_memB_writeEnable", 0, ramB, "wren", 0);
        for (int i = 0; i < 8; i++)
            connect(core, "io_memB_address", i, ramB, "addr", i);
        for (int i = 0; i < 8; i++)
            connect(core, "io_memB_in", i, ramB, "wdata", i);
        for (int i = 0; i < 8; i++)
            connect(ramB, "rdata", i, core, "io_memB_out", i);
    }

    if (!opt.romPorts.empty()) {
        auto &core = *name2cnet.at("core"), &rom = *name2cnet.at("rom");

        for (int i = 0; i < 7; i++)
            connect(core, "io_romAddr", i, rom, "ROM", i);
        for (int i = 0; i < 32; i++)
            connect(rom, "ROM", i, core, "io_romData", i);
    }

    // Set initial ROM data
    if (opt.romPorts.empty()) {
        auto& core = *name2cnet.at("core");

        for (int addr = 0; addr < 128; addr++)
            for (int bit = 0; bit < 32; bit++)
                get(core, "rom", std::to_string(addr), bit)
                    ->set(*tfhepp2cufhe((reqPacket.rom.at(addr * 32 + bit))));
    }
    else {
        assert(reqPacket.circuitKey);
        auto& rom = *name2tnet.at("rom");

        const int ROM_UNIT = 1024 / 8;
        assert(reqPacket.romCk.size() == 512 / ROM_UNIT);
        for (int i = 0; i < 512 / ROM_UNIT; i++) {
            int offset = ROM_UNIT * i;
            rom.get<TaskTFHEppROMUX>("rom", "all", 0)
                ->set128le(offset, reqPacket.romCk[i]);
        }
    }

    // Make runner
    CUFHENetworkRunner runner{
        opt.numWorkers, static_cast<int>(std::thread::hardware_concurrency()),
        TFHEppWorkerInfo{TFHEpp::lweParams{}, reqPacket.gateKey,
                         reqPacket.circuitKey}};
    for (auto&& p : name2cnet)
        runner.addNetwork(p.second);
    for (auto&& p : name2tnet)
        runner.addNetwork(p.second);
    for (auto&& bridge0 : bridges0)
        runner.addBridge(bridge0);
    for (auto&& bridge1 : bridges1)
        runner.addBridge(bridge1);

    // Reset
    {
        cufhe::Ctxt one, zero;
        cufhe::ConstantOne(one);
        cufhe::ConstantZero(zero);
        cufhe::Synchronize();

        auto& core = *name2cnet.at("core");
        get(core, "input", "reset", 0)->set(one);
        runner.run();
        get(core, "input", "reset", 0)->set(zero);
    }

    // Go computing
    std::optional<std::ofstream> dumpOS;
    if (opt.dumpEveryClock) {
        dumpOS = std::ofstream{*opt.dumpEveryClock};
        assert(*dumpOS);
    }

    {
        std::stringstream devnull;
        std::ostream& os = opt.quiet ? devnull : std::cout;

        processCycles(opt.numCycles, os, [&](bool first) {
            if (dumpOS) {
                KVSPResPacket resPacket = makeResPacket(
                    name2cnet, name2tnet, opt.numCycles, opt.ramEnabled);
                dumpResultAsJSON(*dumpOS, resPacket, opt.secretKey.value());
            }

            runner.tick();

            if (first)
                setInitialRAM(name2cnet, name2tnet, reqPacket, opt.ramEnabled);

            runner.run();
            return false;
        });
    }

    // Dump result packet
    KVSPResPacket resPacket =
        makeResPacket(name2cnet, name2tnet, opt.numCycles, opt.ramEnabled);
    if (dumpOS)
        dumpResultAsJSON(*dumpOS, resPacket, opt.secretKey.value());
    writeToArchive(opt.outputFile, resPacket);

    // Clean cuFHE up
    cufhe::CleanUp();
}
