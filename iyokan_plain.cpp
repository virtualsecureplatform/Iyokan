#include "iyokan_plain.hpp"
#include "packet.hpp"

namespace {

auto get(PlainNetwork &net, const std::string &kind,
         const std::string &portName, int portBit)
{
    auto ret = net.get<TaskPlainGateMem>(kind, portName, portBit);
    assert(ret && "Possibly invalid port name or portBit");
    return ret;
}

KVSPPlainResPacket makeResPacket(PlainNetwork &net, int numCycles,
                                 bool ramEnabled)
{
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
    if (ramEnabled) {
        auto ramA = net.get<TaskPlainRAM>("ram", "A", 0),
             ramB = net.get<TaskPlainRAM>("ram", "B", 0);
        assert(ramA && ramB);
        for (int addr = 0; addr < 512; addr++) {
            resPacket.ram.push_back(
                (addr % 2 == 1 ? ramA : ramB)->get(addr / 2));
        }
    }
    else {
        for (int addr = 0; addr < 512; addr++) {
            uint8_t &val = resPacket.ram.emplace_back(0);
            for (int bit = 0; bit < 8; bit++)
                val |= get(net, "ram", std::to_string(addr), bit)->get() << bit;
        }
    }

    return resPacket;
}

}  // namespace

void processAllGates(PlainNetwork &net, int numWorkers,
                     std::shared_ptr<ProgressGraphMaker> graph)
{
    ReadyQueue<uint8_t> readyQueue;
    net.pushReadyTasks(readyQueue);

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
        auto net = readNetworkFromJSON<PlainNetworkBuilder>(ifs);
        assert(net.isValid());

        if (opt.ramEnabled) {
            // Create RAM
            auto ramAnet = makePlainRAMNetwork("A"),
                 ramBnet = makePlainRAMNetwork("B");

            // Merge RAM
            net = net.merge<uint8_t>(ramAnet,
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
                      .merge<uint8_t>(ramBnet,
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

            // Set initial RAM data
            auto ramA = net.get<TaskPlainRAM>("ram", "A", 0),
                 ramB = net.get<TaskPlainRAM>("ram", "B", 0);
            assert(ramA && ramB);
            for (int addr = 0; addr < 512; addr++) {
                (addr % 2 == 1 ? ramA : ramB)
                    ->set(addr / 2, reqPacket.ram.at(addr));
            }
        }
        else {
            // Set RAM
            for (int addr = 0; addr < 512; addr++)
                for (int bit = 0; bit < 8; bit++)
                    get(net, "ram", std::to_string(addr), bit)
                        ->set((reqPacket.ram.at(addr) >> bit) & 1);
        }

        if (opt.romPorts.empty()) {
            // Set ROM
            for (int addr = 0; addr < 128; addr++)
                for (int bit = 0; bit < 32; bit++)
                    get(net, "rom", std::to_string(addr), bit)
                        ->set((reqPacket.rom.at((addr * 32 + bit) / 8) >>
                               (bit % 8)) &
                              1);
            return net;
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

        return net.merge<uint8_t>(rom, lhs2rhs, rhs2lhs);
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
    {
        std::stringstream devnull;
        std::ostream &os = opt.quiet ? devnull : std::cout;
        numCycles = processCycles(numCycles, os, [&] {
            net.tick();
            processAllGates(net, opt.numWorkers);

            bool hasFinished = get(net, "output", "io_finishFlag", 0)->get();
            return hasFinished;
        });
    }

    // Print the results
    KVSPPlainResPacket resPacket =
        makeResPacket(net, numCycles, opt.ramEnabled);
    if (opt.enableJSONPrint) {
        resPacket.printAsJSON(std::cout);
    }
    else {
        resPacket.print(std::cout);
    }
}
