#include "iyokan_tfhepp.hpp"
#include "packet.hpp"

namespace {
auto get(TFHEppNetwork &net, const std::string &kind,
         const std::string &portName, int portBit)
{
    auto ret = net.get<TaskTFHEppGateMem>(kind, portName, portBit);
    assert(ret && "Possibly invalid port name or port bit");
    return ret;
}

KVSPResPacket makeResPacket(TFHEppNetwork &net, int numCycles, bool ramEnabled)
{
    KVSPResPacket resPacket;
    resPacket.numCycles = numCycles;
    // Get values of flags
    resPacket.flags.push_back(get(net, "output", "io_finishFlag", 0)->get());
    // Get values of registers
    for (int reg = 0; reg < 16; reg++) {
        resPacket.regs.emplace_back();
        for (int bit = 0; bit < 16; bit++)
            resPacket.regs[reg].push_back(
                get(net, "output", detail::fok("io_regOut_x", reg), bit)
                    ->get());
    }
    // Get values of RAM
    if (ramEnabled) {
        for (int addr = 0; addr < 512; addr++) {
            for (int bit = 0; bit < 8; bit++) {
                auto ram = net.get<TaskTFHEppRAMUX>(
                    "ram", (addr % 2 == 1 ? "A" : "B"), bit);
                assert(ram);
                resPacket.ramCk.push_back(ram->get(addr / 2));
            }
        }
    }
    else {
        for (int addr = 0; addr < 512; addr++)
            for (int bit = 0; bit < 8; bit++)
                resPacket.ram.push_back(
                    get(net, "ram", std::to_string(addr), bit)->get());
    }

    return resPacket;
}

void dumpResultAsJSON(std::ostream &os, const KVSPResPacket &resPacket,
                      const std::string &fileSecretKey)
{
    auto sk = std::make_shared<TFHEpp::SecretKey>();
    readFromArchive(*sk, fileSecretKey);
    decrypt(*sk, resPacket).printAsJSON(os);
}

void setInitialRAM(TFHEppNetwork &net, const KVSPReqPacket &reqPacket,
                   bool ramEnabled)
{
    if (ramEnabled) {
        for (int addr = 0; addr < 512; addr++)
            for (int bit = 0; bit < 8; bit++)
                net.get<TaskTFHEppRAMUX>("ram", addr % 2 == 1 ? "A" : "B", bit)
                    ->set(addr / 2, reqPacket.ramCk.at(addr * 8 + bit));
    }
    else {
        // Set RAM
        for (int addr = 0; addr < 512; addr++)
            for (int bit = 0; bit < 8; bit++)
                get(net, "ram", std::to_string(addr), bit)
                    ->set(reqPacket.ram.at(addr * 8 + bit));
    }
}

}  // namespace

void processAllGates(TFHEppNetwork &net, int numWorkers, TFHEppWorkerInfo wi,
                     std::shared_ptr<ProgressGraphMaker> graph)
{
    ReadyQueue<TFHEppWorkerInfo> readyQueue;
    net.pushReadyTasks(readyQueue);

    // Create workers.
    size_t numFinishedTargets = 0;
    std::vector<TFHEppWorker> workers;
    for (int i = 0; i < numWorkers; i++)
        workers.emplace_back(readyQueue, numFinishedTargets, wi, graph);

    // Process all targets.
    while (numFinishedTargets < net.numNodes()) {
        // Detect infinite loops.
        assert(std::any_of(workers.begin(), workers.end(),
                           [](auto &&w) { return w.isWorking(); }) ||
               !readyQueue.empty());

        for (auto &&w : workers)
            w.update();
    }

    assert(readyQueue.empty());
}

void doTFHE(const Options &opt)
{
    // Read packet
    const auto reqPacket = readFromArchive<KVSPReqPacket>(opt.inputFile);

    // Read network
    TFHEppNetworkBuilder::NetworkType net = [&opt, &reqPacket] {
        std::ifstream ifs{opt.logicFile};
        assert(ifs);
        auto net = readNetworkFromJSON<TFHEppNetworkBuilder>(ifs);
        assert(net.isValid());

        if (opt.ramEnabled) {
            // Create RAM
            auto ramAnet = makeTFHEppRAMNetwork("A"),
                 ramBnet = makeTFHEppRAMNetwork("B");

            // Merge RAM
            net = net.merge<TFHEpp::TLWElvl0>(
                         ramAnet,
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
                         })
                      .merge<TFHEpp::TLWElvl0>(
                          ramBnet,
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
        }

        if (opt.romPorts.empty()) {
            // Set ROM
            for (int addr = 0; addr < 128; addr++)
                for (int bit = 0; bit < 32; bit++)
                    get(net, "rom", std::to_string(addr), bit)
                        ->set((reqPacket.rom.at(addr * 32 + bit)));
            return net;
        }

        assert(reqPacket.circuitKey);

        // Create ROM as external module and set data
        auto rom = makeTFHEppROMNetwork();
        const int ROM_UNIT = 1024 / 8;
        assert(reqPacket.romCk.size() == 512 / ROM_UNIT);
        for (int i = 0; i < 512 / ROM_UNIT; i++) {
            int offset = ROM_UNIT * i;
            rom.get<TaskTFHEppROMUX>("rom", "all", 0)
                ->set128le(offset, reqPacket.romCk[i]);
        }

        // Merge ROM
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

        return net.merge<TFHEpp::TLWElvl0>(rom, lhs2rhs, rhs2lhs);
    }();

    // Turn reset on
    get(net, "input", "reset", 0)->set([] {
        TFHEpp::TLWElvl0 one;
        TFHEpp::HomCONSTANTONE(one);
        return one;
    }());
    // Reset
    TFHEppWorkerInfo wi{TFHEpp::lweParams{}, reqPacket.gateKey,
                        reqPacket.circuitKey};
    processAllGates(net, opt.numWorkers, wi);

    // Turn reset off
    get(net, "input", "reset", 0)->set([] {
        TFHEpp::TLWElvl0 zero;
        TFHEpp::HomCONSTANTZERO(zero);
        return zero;
    }());
    // Go computing
    std::optional<std::ofstream> dumpOS;
    if (opt.dumpEveryClock) {
        dumpOS = std::ofstream{*opt.dumpEveryClock};
        assert(*dumpOS);
    }

    {
        std::stringstream devnull;
        std::ostream &os = opt.quiet ? devnull : std::cout;

        processCycles(opt.numCycles, os, [&](bool first) {
            if (dumpOS) {
                KVSPResPacket resPacket =
                    makeResPacket(net, opt.numCycles, opt.ramEnabled);
                dumpResultAsJSON(*dumpOS, resPacket, opt.secretKey.value());
            }

            net.tick();

            if (first)
                setInitialRAM(net, reqPacket, opt.ramEnabled);

            processAllGates(net, opt.numWorkers, wi);
            return false;
        });
    }

    // Dump result packet
    KVSPResPacket resPacket = makeResPacket(net, opt.numCycles, opt.ramEnabled);
    if (dumpOS)
        dumpResultAsJSON(*dumpOS, resPacket, opt.secretKey.value());
    writeToArchive(opt.outputFile, resPacket);
}
