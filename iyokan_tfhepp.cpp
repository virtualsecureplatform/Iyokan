#include "iyokan_tfhepp.hpp"
#include "packet.hpp"

auto get(TFHEppNetwork &net, const std::string &kind,
         const std::string &portName, int portBit)
{
    auto ret = net.get<TaskTFHEppGateMem>(kind, portName, portBit);
    assert(ret && "Possibly invalid port name or port bit");
    return ret;
}

void processAllGates(TFHEppNetwork &net, int numWorkers, TFHEppWorkerInfo wi,
                     std::shared_ptr<ProgressGraphMaker> graph = nullptr)
{
    auto readyQueue = net.getReadyQueue();

    // Create workers.
    size_t numFinishedTargets = 0;
    std::vector<TFHEppWorker> workers;
    for (int i = 0; i < numWorkers; i++)
        workers.emplace_back(wi, readyQueue, numFinishedTargets, graph);

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
        auto core = readNetworkFromJSON<TFHEppNetworkBuilder>(ifs);
        assert(core.isValid());

        // Set RAM
        for (int addr = 0; addr < 512; addr++)
            for (int bit = 0; bit < 8; bit++)
                get(core, "ram", std::to_string(addr), bit)
                    ->set(reqPacket.ram.at(addr * 8 + bit));

        if (opt.romPorts.empty()) {
            // Set ROM
            for (int addr = 0; addr < 128; addr++)
                for (int bit = 0; bit < 32; bit++)
                    get(core, "rom", std::to_string(addr), bit)
                        ->set((reqPacket.rom.at(addr * 32 + bit)));
            return core;
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

        // Merge core and ROM
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

        return core.merge<TFHEpp::TLWElvl0>(rom, lhs2rhs, rhs2lhs);
    }();

    // Get #cycles
    int numCycles = std::numeric_limits<int>::max();
    if (opt.numCycles > 0)
        numCycles = opt.numCycles;

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
    processCycles(numCycles, [&] {
        net.tick();
        processAllGates(net, opt.numWorkers, wi);
        return false;
    });

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
    for (int addr = 0; addr < 512; addr++)
        for (int bit = 0; bit < 8; bit++)
            resPacket.ram.push_back(
                get(net, "ram", std::to_string(addr), bit)->get());

    // Dump result packet
    writeToArchive(opt.outputFile, resPacket);
}
