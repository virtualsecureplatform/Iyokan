#include "iyokan_cufhe.hpp"
#include "packet.hpp"

auto get(CUFHENetwork& net, const std::string& kind,
         const std::string& portName, int portBit)
{
    auto ret = net.get<TaskCUFHEGateMem>(kind, portName, portBit);
    assert(ret && "Possibly invalid port name or port bit");
    return ret;
}

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

    void addToRunner(CUFHENetworkRunner& runner)
    {
        if (cufheNet)
            runner.addNetwork(cufheNet);
        if (tfheppNet)
            runner.addNetwork(tfheppNet);
        for (auto&& bridge0 : bridges0)
            runner.addBridge(bridge0);
        for (auto&& bridge1 : bridges1)
            runner.addBridge(bridge1);
    }
};

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
        b.addTask(NodeLabel{b.genid(), "cufhe2tfhepp", ""}, 0, cufhe2tfhepp);
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
        b.addTask(NodeLabel{b.genid(), "tfhepp2cufhe", ""}, 0, tfhepp2cufhe);
        TFHEppNetwork net = std::move(b);
        tfhepp = tfhepp.merge(net);

        // TFHEpp OUTPUT --> TFHEpp2CUFHE
        NetworkBuilderBase<TFHEppWorkerInfo>::connectTasks(in, tfhepp2cufhe);

        // TFHEpp2CUFHE --> Bridge --> CUFHE INPUT
        out->acceptOneMoreInput();
        brTFHEpp2CUFHE.push_back(connectWithBridge(tfhepp2cufhe, out));
    }
}


CUFHENetworkWithTFHEpp makeTFHEppRAMNetworkForCUFHE(
    const std::string& romPortName)
{
    auto tfheppNet =
        std::make_shared<TFHEppNetwork>(makeTFHEppRAMNetwork(romPortName));
    return CUFHENetworkWithTFHEpp{nullptr, tfheppNet, {}, {}};
}

CUFHENetworkWithTFHEpp makeTFHEppROMNetworkForCUFHE()
{
    auto tfheppNet = std::make_shared<TFHEppNetwork>(makeTFHEppROMNetwork());
    return CUFHENetworkWithTFHEpp{nullptr, tfheppNet, {}, {}};
}

struct CUFHENetworkManager {
    std::shared_ptr<CUFHENetwork> core;
    std::shared_ptr<TFHEppNetwork> ramA, ramB;
};

void doCUFHE(const Options& opt)
{
    // Read packet
    const auto reqPacket = readFromArchive<KVSPReqPacket>(opt.inputFile);

    // Prepare cuFHE
    cufhe::SetGPUNum(1);
    cufhe::SetSeed();
    cufhe::Initialize(*tfhepp2cufhe(*reqPacket.gateKey));

    // Make runner
    CUFHENetworkRunner runner{
        opt.numWorkers, static_cast<int>(std::thread::hardware_concurrency()),
        TFHEppWorkerInfo{TFHEpp::lweParams{}, reqPacket.gateKey,
                         reqPacket.circuitKey}};

    // Read network core
    CUFHENetworkManager net;
    {
        std::ifstream ifs{opt.logicFile};
        assert(ifs);
        net.core = std::make_shared<CUFHENetwork>(
            readNetworkFromJSON<CUFHENetworkBuilder>(ifs));
        assert(net.core);
        assert(net.core->isValid());
    }

    if (opt.ramEnabled) {
        assert(reqPacket.circuitKey);

        // Create RAM
        auto ramA = makeTFHEppRAMNetworkForCUFHE("A");
        auto ramB = makeTFHEppRAMNetworkForCUFHE("B");
        net.ramA = ramA.tfheppNet;
        net.ramB = ramB.tfheppNet;

        // Set initial RAM data
        for (int addr = 0; addr < 512; addr++)
            for (int bit = 0; bit < 8; bit++)
                (addr % 2 == 1 ? net.ramA : net.ramB)
                    ->get<TaskTFHEppRAMUX>("ram", addr % 2 == 1 ? "A" : "B",
                                           bit)
                    ->set(addr / 2, reqPacket.ramCk.at(addr * 8 + bit));

        // Connect RAM to core
        connectCUFHENetWithTFHEppNet(*net.core, *net.ramA, ramA.bridges0,
                                     ramA.bridges1,
                                     {
                                         {"io_memA_writeEnable", 0, "wren", 0},
                                         {"io_memA_address", 0, "addr", 0},
                                         {"io_memA_address", 1, "addr", 1},
                                         {"io_memA_address", 2, "addr", 2},
                                         {"io_memA_address", 3, "addr", 3},
                                         {"io_memA_address", 4, "addr", 4},
                                         {"io_memA_address", 5, "addr", 5},
                                         {"io_memA_address", 6, "addr", 6},
                                         {"io_memA_address", 7, "addr", 7},
                                         {"io_memA_in", 0, "wdata", 0},
                                         {"io_memA_in", 1, "wdata", 1},
                                         {"io_memA_in", 2, "wdata", 2},
                                         {"io_memA_in", 3, "wdata", 3},
                                         {"io_memA_in", 4, "wdata", 4},
                                         {"io_memA_in", 5, "wdata", 5},
                                         {"io_memA_in", 6, "wdata", 6},
                                         {"io_memA_in", 7, "wdata", 7},
                                     },
                                     {
                                         {"rdata", 0, "io_memA_out", 0},
                                         {"rdata", 1, "io_memA_out", 1},
                                         {"rdata", 2, "io_memA_out", 2},
                                         {"rdata", 3, "io_memA_out", 3},
                                         {"rdata", 4, "io_memA_out", 4},
                                         {"rdata", 5, "io_memA_out", 5},
                                         {"rdata", 6, "io_memA_out", 6},
                                         {"rdata", 7, "io_memA_out", 7},
                                     });
        connectCUFHENetWithTFHEppNet(*net.core, *net.ramB, ramB.bridges0,
                                     ramB.bridges1,
                                     {
                                         {"io_memB_writeEnable", 0, "wren", 0},
                                         {"io_memB_address", 0, "addr", 0},
                                         {"io_memB_address", 1, "addr", 1},
                                         {"io_memB_address", 2, "addr", 2},
                                         {"io_memB_address", 3, "addr", 3},
                                         {"io_memB_address", 4, "addr", 4},
                                         {"io_memB_address", 5, "addr", 5},
                                         {"io_memB_address", 6, "addr", 6},
                                         {"io_memB_address", 7, "addr", 7},
                                         {"io_memB_in", 0, "wdata", 0},
                                         {"io_memB_in", 1, "wdata", 1},
                                         {"io_memB_in", 2, "wdata", 2},
                                         {"io_memB_in", 3, "wdata", 3},
                                         {"io_memB_in", 4, "wdata", 4},
                                         {"io_memB_in", 5, "wdata", 5},
                                         {"io_memB_in", 6, "wdata", 6},
                                         {"io_memB_in", 7, "wdata", 7},
                                     },
                                     {
                                         {"rdata", 0, "io_memB_out", 0},
                                         {"rdata", 1, "io_memB_out", 1},
                                         {"rdata", 2, "io_memB_out", 2},
                                         {"rdata", 3, "io_memB_out", 3},
                                         {"rdata", 4, "io_memB_out", 4},
                                         {"rdata", 5, "io_memB_out", 5},
                                         {"rdata", 6, "io_memB_out", 6},
                                         {"rdata", 7, "io_memB_out", 7},
                                     });

        // Add RAMs to runner
        ramA.addToRunner(runner);
        ramB.addToRunner(runner);
    }
    else {
        // Set RAM
        for (int addr = 0; addr < 512; addr++)
            for (int bit = 0; bit < 8; bit++)
                get(*net.core, "ram", std::to_string(addr), bit)
                    ->set(*tfhepp2cufhe(reqPacket.ram.at(addr * 8 + bit)));
    }

    if (opt.romPorts.empty()) {
        // Set ROM
        for (int addr = 0; addr < 128; addr++)
            for (int bit = 0; bit < 32; bit++)
                get(*net.core, "rom", std::to_string(addr), bit)
                    ->set(*tfhepp2cufhe((reqPacket.rom.at(addr * 32 + bit))));
    }
    else {
        assert(reqPacket.circuitKey);

        // Create ROM as external module
        auto rom = makeTFHEppROMNetworkForCUFHE();

        // Set data
        const int ROM_UNIT = 1024 / 8;
        assert(reqPacket.romCk.size() == 512 / ROM_UNIT);
        for (int i = 0; i < 512 / ROM_UNIT; i++) {
            int offset = ROM_UNIT * i;
            rom.tfheppNet->get<TaskTFHEppROMUX>("rom", "all", 0)
                ->set128le(offset, reqPacket.romCk[i]);
        }

        // Connect ROM to core
        std::vector<std::tuple<std::string, int, std::string, int>> lhs2rhs,
            rhs2lhs;
        assert(opt.romPorts.size() == 4);
        int numLHS2RHS = std::stoi(opt.romPorts[1]),
            numRHS2LHS = std::stoi(opt.romPorts[3]);
        assert(0 <= numLHS2RHS && 0 <= numRHS2LHS);
        for (int i = 0; i < numLHS2RHS; i++)
            lhs2rhs.emplace_back(opt.romPorts[0], i, "ROM", i);
        for (int i = 0; i < numRHS2LHS; i++)
            rhs2lhs.emplace_back("ROM", i, opt.romPorts[2], i);

        connectCUFHENetWithTFHEppNet(*net.core, *rom.tfheppNet, rom.bridges0,
                                     rom.bridges1, lhs2rhs, rhs2lhs);

        // Add ROM to runner
        rom.addToRunner(runner);
    }

    // Add core to runner
    runner.addNetwork(net.core);

    // Get #cycles
    int numCycles = std::numeric_limits<int>::max();
    if (opt.numCycles > 0)
        numCycles = opt.numCycles;

    // Turn reset on
    {
        cufhe::Ctxt one;
        cufhe::ConstantOne(one);
        cufhe::Synchronize();
        get(*net.core, "input", "reset", 0)->set(one);
    }
    // Reset
    runner.run();

    // Turn reset off
    {
        cufhe::Ctxt zero;
        cufhe::ConstantZero(zero);
        cufhe::Synchronize();
        get(*net.core, "input", "reset", 0)->set(zero);
    }
    // Go computing
    processCycles(numCycles, [&] {
        runner.tick();
        runner.run();

        return false;
    });

    KVSPResPacket resPacket;
    resPacket.numCycles = numCycles;
    // Get values of flags
    resPacket.flags.push_back(
        cufhe2tfhepp(get(*net.core, "output", "io_finishFlag", 0)->get()));
    // Get values of registers
    for (int reg = 0; reg < 16; reg++) {
        resPacket.regs.emplace_back();
        for (int bit = 0; bit < 16; bit++)
            resPacket.regs[reg].push_back(cufhe2tfhepp(
                get(*net.core, "output", detail::fok("io_regOut_x", reg), bit)
                    ->get()));
    }
    // Get values of RAM
    if (opt.ramEnabled) {
        for (int addr = 0; addr < 512; addr++) {
            for (int bit = 0; bit < 8; bit++) {
                auto ram = (addr % 2 == 1 ? net.ramA : net.ramB)
                               ->get<TaskTFHEppRAMUX>(
                                   "ram", (addr % 2 == 1 ? "A" : "B"), bit);
                assert(ram);
                resPacket.ramCk.push_back(ram->get(addr / 2));
            }
        }
    }
    else {
        for (int addr = 0; addr < 512; addr++)
            for (int bit = 0; bit < 8; bit++)
                resPacket.ram.push_back(cufhe2tfhepp(
                    get(*net.core, "ram", std::to_string(addr), bit)->get()));
    }

    // Dump result packet
    writeToArchive(opt.outputFile, resPacket);

    // Clean cuFHE up
    cufhe::CleanUp();
}
