#include "iyokan_plain.hpp"
#include "packet.hpp"

namespace {

using Name2NetMap =
    std::unordered_map<std::string, std::shared_ptr<PlainNetwork>>;

auto get(PlainNetwork &net, const std::string &kind,
         const std::string &portName, int portBit)
{
    auto ret = net.get<TaskPlainGateMem>(kind, portName, portBit);
    assert(ret && "Possibly invalid port name or portBit");
    return ret;
}

KVSPPlainResPacket makeResPacket(Name2NetMap name2net, int numCycles,
                                 bool ramEnabled)
{
    auto &core = *name2net.at("core");

    KVSPPlainResPacket resPacket;
    resPacket.numCycles = numCycles;
    // Get values of flags
    resPacket.flags.emplace_back(
        get(core, "output", "io_finishFlag", 0)->get());
    // Get values of registers
    for (int reg = 0; reg < 16; reg++) {
        uint16_t &val = resPacket.regs.emplace_back(0);
        for (int bit = 0; bit < 16; bit++) {
            int n = get(core, "output", detail::fok("io_regOut_x", reg), bit)
                        ->get();
            val |= (n & 1u) << bit;
        }
    }
    // Get values of RAM
    if (ramEnabled) {
        auto ramA = name2net.at("ramA")->get<TaskPlainRAM>("ram", "A", 0),
             ramB = name2net.at("ramB")->get<TaskPlainRAM>("ram", "B", 0);
        assert(ramA && ramB);
        for (int addr = 0; addr < 512; addr++) {
            resPacket.ram.push_back(
                (addr % 2 == 1 ? ramA : ramB)->get(addr / 2));
        }
    }

    return resPacket;
}

void setInitialRAM(Name2NetMap &name2net, const KVSPPlainReqPacket &reqPacket)
{
    auto ramA = name2net.at("ramA")->get<TaskPlainRAM>("ram", "A", 0),
         ramB = name2net.at("ramB")->get<TaskPlainRAM>("ram", "B", 0);
    assert(ramA && ramB);
    for (int addr = 0; addr < 512; addr++) {
        (addr % 2 == 1 ? ramA : ramB)->set(addr / 2, reqPacket.ram.at(addr));
    }
}

void connect(PlainNetwork &srcNet, const std::string &srcPortName,
             int srcPortBit, PlainNetwork &dstNet,
             const std::string &dstPortName, int dstPortBit)
{
    auto dst = dstNet.get<TaskPlainGate>("input", dstPortName, dstPortBit);
    assert(dst);
    auto src = srcNet.get<TaskPlainGate>("output", srcPortName, srcPortBit);
    assert(src);

    dst->acceptOneMoreInput();
    NetworkBuilderBase<uint8_t>::connectTasks(src, dst);
}

class PlainNetworkRunner {
private:
    NetworkRunner<uint8_t, PlainWorker> plain_;
    std::shared_ptr<ProgressGraphMaker> graph_;

public:
    PlainNetworkRunner(int numCPUWorkers,
                       std::shared_ptr<ProgressGraphMaker> graph = nullptr)
        : graph_(graph)
    {
        for (int i = 0; i < numCPUWorkers; i++)
            plain_.addWorker(graph_);
    }

    bool isValid()
    {
        return plain_.isValid();
    }

    void addNetwork(std::shared_ptr<PlainNetwork> net)
    {
        plain_.addNetwork(net);
    }

    void run()
    {
        if (graph_)
            graph_->reset();
        plain_.prepareToRun();

        while (plain_.getNumFinishedTargets() < plain_.numNodes()) {
            assert(plain_.isRunning() && "Detected infinite loop");
            plain_.update();
        }
    }

    void tick()
    {
        plain_.tick();
    }
};

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

KVSPPlainResPacket doPlain(const Options &opt)
{
    // Read packet
    const auto reqPacket = readFromArchive<KVSPPlainReqPacket>(opt.inputFile);

    // Create nodes
    Name2NetMap name2net;

    {
        std::ifstream ifs{opt.logicFile};
        assert(ifs);
        auto net = std::make_shared<PlainNetwork>(
            readNetworkFromJSON<PlainNetworkBuilder>(ifs));
        assert(net->isValid());
        name2net.emplace("core", net);
    }

    if (opt.ramEnabled) {
        auto ramANet = std::make_shared<PlainNetwork>(makePlainRAMNetwork("A")),
             ramBNet = std::make_shared<PlainNetwork>(makePlainRAMNetwork("B"));
        name2net.emplace("ramA", ramANet);
        name2net.emplace("ramB", ramBNet);
    }

    if (!opt.romPorts.empty()) {
        auto romNet = std::make_shared<PlainNetwork>(makePlainROMNetwork());
        name2net.emplace("rom", romNet);
    }

    // Create edges
    if (opt.ramEnabled) {
        auto &core = *name2net.at("core"), &ramA = *name2net.at("ramA"),
             &ramB = *name2net.at("ramB");

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
        auto &core = *name2net.at("core"), &rom = *name2net.at("rom");

        for (int i = 0; i < 7; i++)
            connect(core, "io_romAddr", i, rom, "ROM", i);
        for (int i = 0; i < 32; i++)
            connect(rom, "ROM", i, core, "io_romData", i);
    }

    // Set initial ROM data
    if (!opt.romPorts.empty()) {
        auto &rom = *name2net.at("rom");

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
    }

    // Make runner
    PlainNetworkRunner runner{opt.numWorkers};
    for (auto &&p : name2net)
        runner.addNetwork(p.second);

    // Get #cycles
    int numCycles = std::numeric_limits<int>::max();
    if (opt.numCycles > 0)
        numCycles = opt.numCycles;

    // Reset
    {
        auto &core = *name2net.at("core");
        get(core, "input", "reset", 0)->set(1);
        runner.run();
        get(core, "input", "reset", 0)->set(0);
    }

    // Go computing
    std::optional<std::ofstream> dumpOS;
    if (opt.dumpEveryClock) {
        dumpOS = std::ofstream{*opt.dumpEveryClock};
        assert(*dumpOS);
    }

    {
        std::stringstream devnull;
        std::ostream &os = opt.quiet ? devnull : std::cout;

        numCycles = processCycles(numCycles, os, [&](bool first) {
            auto &core = *name2net.at("core");

            if (dumpOS)
                makeResPacket(name2net, opt.numCycles, opt.ramEnabled)
                    .printAsJSON(*dumpOS);

            runner.tick();

            if (first && opt.ramEnabled)
                setInitialRAM(name2net, reqPacket);

            runner.run();

            bool hasFinished = get(core, "output", "io_finishFlag", 0)->get();
            return hasFinished;
        });
    }

    // Print the results
    KVSPPlainResPacket resPacket =
        makeResPacket(name2net, numCycles, opt.ramEnabled);
    if (dumpOS)
        resPacket.printAsJSON(*dumpOS);
    return resPacket;
}
