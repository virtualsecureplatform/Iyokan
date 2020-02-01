#include <chrono>

#include <CLI/CLI.hpp>

#include "iyokan.hpp"
#include "packet.hpp"
#include "plain.hpp"
#include "tfhepp.hpp"

auto get(PlainNetwork &net, const std::string &kind,
         const std::string &portName, int portBit)
{
    auto ret = net.get<TaskPlainGateMem>(kind, portName, portBit);
    assert(ret && "Possibly invalid port name or portBit");
    return ret;
}

auto get(TFHEppNetwork &net, const std::string &kind,
         const std::string &portName, int portBit)
{
    auto ret = net.get<TaskTFHEppGateMem>(kind, portName, portBit);
    assert(ret && "Possibly invalid port name or port bit");
    return ret;
}

void processAllGates(PlainNetwork &net, int numWorkers,
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
                           [](auto &&w) { return w.isWorking(); }) ||
               !readyQueue.empty());

        for (auto &&w : workers)
            w.update();
    }

    assert(readyQueue.empty());
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

template <class Func>
int processCycles(int numCycles, Func func)
{
    for (int i = 0; i < numCycles; i++) {
        std::cout << "#" << (i + 1) << std::flush;

        auto begin = std::chrono::high_resolution_clock::now();
        bool shouldBreak = func();
        auto end = std::chrono::high_resolution_clock::now();

        std::cout << "\tdone. ("
                  << std::chrono::duration_cast<std::chrono::microseconds>(
                         end - begin)
                         .count()
                  << " us)" << std::endl;

        if (shouldBreak) {
            std::cout << "break." << std::endl;
            return i + 1;
        }
    }

    return numCycles;
}

// Thanks to: https://faithandbrave.hateblo.jp/entry/2014/05/01/171631
std::vector<std::string> split(const std::string &input, char delimiter)
{
    std::istringstream stream(input);

    std::string field;
    std::vector<std::string> result;
    while (std::getline(stream, field, delimiter))
        result.push_back(field);
    return result;
}

struct Options {
    // Processor's logic file.
    std::string logicFile;
    // Port corresponding between core and ROM if enabled.
    std::string enableROM;
    // Misc.
    std::string inputFile, outputFile;
    int numWorkers = std::thread::hardware_concurrency(), numCycles = 0;
};

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

        if (opt.enableROM.empty()) {
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
        std::vector<std::string> romPorts = split(opt.enableROM, ':');
        assert(romPorts.size() == 4);
        int numLHS2RHS = std::stoi(romPorts[1]),
            numRHS2LHS = std::stoi(romPorts[3]);
        assert(0 <= numLHS2RHS && 0 <= numRHS2LHS);
        for (int i = 0; i < numLHS2RHS; i++)
            lhs2rhs.emplace_back(romPorts[0], i, "ROM", i);
        for (int i = 0; i < numRHS2LHS; i++)
            rhs2lhs.emplace_back("ROM", i, romPorts[2], i);

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

        if (opt.enableROM.empty()) {
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
        std::vector<std::string> romPorts = split(opt.enableROM, ':');
        assert(romPorts.size() == 4);
        int numLHS2RHS = std::stoi(romPorts[1]),
            numRHS2LHS = std::stoi(romPorts[3]);
        assert(0 <= numLHS2RHS && 0 <= numRHS2LHS);
        for (int i = 0; i < numLHS2RHS; i++)
            lhs2rhs.emplace_back(romPorts[0], i, "ROM", i);
        for (int i = 0; i < numRHS2LHS; i++)
            rhs2lhs.emplace_back("ROM", i, romPorts[2], i);

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

int main(int argc, char **argv)
{
    CLI::App app{"Prallel FHE circuit evaluation engine."};

    Options opt;

    app.require_subcommand();
    enum class TYPE { PLAIN, TFHE } type;
    app.add_option("-l", opt.logicFile, "")
        ->required()
        ->check(CLI::ExistingFile);
    app.add_option("-t", opt.numWorkers, "")->check(CLI::PositiveNumber);
    app.add_option("--enable-rom", opt.enableROM, "");
    app.add_option("-i", opt.inputFile, "")
        ->required()
        ->check(CLI::ExistingFile);

    {
        CLI::App *plain = app.add_subcommand("plain", "");
        plain->fallthrough();
        plain->parse_complete_callback([&] { type = TYPE::PLAIN; });
        plain->add_option("-c", opt.numCycles, "");
    }

    {
        CLI::App *tfhe = app.add_subcommand("tfhe", "");
        tfhe->fallthrough();
        tfhe->add_option("-c", opt.numCycles, "")->required();
        tfhe->add_option("-o", opt.outputFile, "")->required();
        tfhe->parse_complete_callback([&] { type = TYPE::TFHE; });
    }

    CLI11_PARSE(app, argc, argv);

    AsyncThread::setNumThreads(opt.numWorkers);

    switch (type) {
    case TYPE::PLAIN:
        doPlain(opt);
        break;
    case TYPE::TFHE:
        doTFHE(opt);
        break;
    }
}
