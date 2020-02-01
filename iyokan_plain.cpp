#include "iyokan_plain.hpp"
#include "packet.hpp"

auto get(PlainNetwork &net, const std::string &kind,
         const std::string &portName, int portBit)
{
    auto ret = net.get<TaskPlainGateMem>(kind, portName, portBit);
    assert(ret && "Possibly invalid port name or portBit");
    return ret;
}

void processAllGates(PlainNetwork &net, int numWorkers,
                     std::shared_ptr<ProgressGraphMaker> graph)
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
                           [](auto &&w) { return w.isWorking(); }) ||
               !readyQueue.empty());

        for (auto &&w : workers)
            w.update();
    }

    assert(readyQueue.empty());
}

void doPlain(const Options &opt)
{
    // Read packet
    const auto reqPacket = readFromArchive<KVSPPlainReqPacket>(opt.inputFile);

    // Read network
    PlainNetworkBuilder::NetworkType net = [&opt, &reqPacket] {
        std::ifstream ifs{opt.logicFile};
        assert(ifs);
        auto core = readNetworkFromJSON<PlainNetworkBuilder>(ifs);
        assert(core.isValid());

        // Set RAM
        for (int addr = 0; addr < 512; addr++)
            for (int bit = 0; bit < 8; bit++)
                get(core, "ram", std::to_string(addr), bit)
                    ->set((reqPacket.ram.at(addr) >> bit) & 1);

        if (opt.romPorts.empty()) {
            // Set ROM
            for (int addr = 0; addr < 128; addr++)
                for (int bit = 0; bit < 32; bit++)
                    get(core, "rom", std::to_string(addr), bit)
                        ->set((reqPacket.rom.at((addr * 32 + bit) / 8) >>
                               (bit % 8)) &
                              1);
            return core;
        }

        // Create ROM as external module and set data
        auto rom = makePlainROMNetwork();
        for (int i = 0; i < 512 / 4; i++) {
            int val = 0;
            for (int j = 3; j >= 0; j--) {
                size_t offset = i * 4 + j;
                val = (val << 8) |
                      (offset < reqPacket.rom.size() ? reqPacket.rom[offset]
                                                     : 0x00);
            }
            rom.get<TaskPlainROM>("rom", "all", 0)->set4le(i << 2, val);
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

        return core.merge<uint8_t>(rom, lhs2rhs, rhs2lhs);
    }();

    // Get #cycles
    int numCycles = std::numeric_limits<int>::max();
    if (opt.numCycles > 0)
        numCycles = opt.numCycles;

    // Reset
    get(net, "input", "reset", 0)->set(1);
    processAllGates(net, opt.numWorkers);

    // Go computing
    get(net, "input", "reset", 0)->set(0);
    numCycles = processCycles(numCycles, [&] {
        net.tick();
        processAllGates(net, opt.numWorkers);

        bool hasFinished = get(net, "output", "io_finishFlag", 0)->get();
        return hasFinished;
    });

    KVSPPlainResPacket resPacket;
    resPacket.numCycles = numCycles;
    // Get values of flags
    resPacket.flags.emplace_back(get(net, "output", "io_finishFlag", 0)->get());
    // Get values of registers
    for (int reg = 0; reg < 16; reg++) {
        uint16_t &val = resPacket.regs.emplace_back(0);
        for (int bit = 0; bit < 16; bit++) {
            int n =
                get(net, "output", detail::fok("io_regOut_x", reg), bit)->get();
            val |= (n & 1u) << bit;
        }
    }
    // Get values of RAM
    for (int addr = 0; addr < 512; addr++) {
        uint8_t &val = resPacket.ram.emplace_back(0);
        for (int bit = 0; bit < 8; bit++)
            val |= get(net, "ram", std::to_string(addr), bit)->get() << bit;
    }

    // Print the results
    resPacket.print(std::cout);
}
