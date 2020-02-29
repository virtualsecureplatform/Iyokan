#include "iyokan_tfhepp.hpp"
#include "packet.hpp"

namespace {

using Name2NetMap =
    std::unordered_map<std::string, std::shared_ptr<TFHEppNetwork>>;

auto get(TFHEppNetwork &net, const std::string &kind,
         const std::string &portName, int portBit)
{
    auto ret = net.get<TaskTFHEppGateMem>(kind, portName, portBit);
    assert(ret && "Possibly invalid port name or port bit");
    return ret;
}

KVSPResPacket makeResPacket(Name2NetMap &name2net, int numCycles,
                            bool ramEnabled)
{
    auto &core = *name2net.at("core");

    KVSPResPacket resPacket;
    resPacket.numCycles = numCycles;
    // Get values of flags
    resPacket.flags.push_back(get(core, "output", "io_finishFlag", 0)->get());
    // Get values of registers
    for (int reg = 0; reg < 16; reg++) {
        resPacket.regs.emplace_back();
        for (int bit = 0; bit < 16; bit++)
            resPacket.regs[reg].push_back(
                get(core, "output", detail::fok("io_regOut_x", reg), bit)
                    ->get());
    }
    // Get values of RAM
    if (ramEnabled) {
        auto &ramA = *name2net.at("ramA");
        auto &ramB = *name2net.at("ramB");

        for (int addr = 0; addr < 512; addr++) {
            for (int bit = 0; bit < 8; bit++) {
                auto ram = addr % 2 == 1
                               ? ramA.get<TaskTFHEppRAMUX>("ram", "A", bit)
                               : ramB.get<TaskTFHEppRAMUX>("ram", "B", bit);
                assert(ram);
                resPacket.ramCk.push_back(ram->get(addr / 2));
            }
        }
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

void setInitialRAM(Name2NetMap &name2net, const KVSPReqPacket &reqPacket)
{
    auto &ramA = *name2net.at("ramA"), &ramB = *name2net.at("ramB");
    for (int addr = 0; addr < 512; addr++)
        for (int bit = 0; bit < 8; bit++)
            (addr % 2 == 1 ? ramA.get<TaskTFHEppRAMUX>("ram", "A", bit)
                           : ramB.get<TaskTFHEppRAMUX>("ram", "B", bit))
                ->set(addr / 2, reqPacket.ramCk.at(addr * 8 + bit));
}

void connect(TFHEppNetwork &srcNet, const std::string &srcPortName,
             int srcPortBit, TFHEppNetwork &dstNet,
             const std::string &dstPortName, int dstPortBit)
{
    auto dst = dstNet.get<TaskTFHEppGate>("input", dstPortName, dstPortBit);
    assert(dst);
    auto src = srcNet.get<TaskTFHEppGate>("output", srcPortName, srcPortBit);
    assert(src);

    dst->acceptOneMoreInput();
    NetworkBuilderBase<TFHEppWorkerInfo>::connectTasks(src, dst);
}

class TFHEppNetworkRunner {
private:
    NetworkRunner<TFHEppWorkerInfo, TFHEppWorker> tfhepp_;
    std::shared_ptr<ProgressGraphMaker> graph_;

public:
    TFHEppNetworkRunner(int numCPUWorkers, TFHEppWorkerInfo wi,
                        std::shared_ptr<ProgressGraphMaker> graph = nullptr)
        : graph_(graph)
    {
        for (int i = 0; i < numCPUWorkers; i++)
            tfhepp_.addWorker(wi, graph_);
    }

    bool isValid()
    {
        return tfhepp_.isValid();
    }

    void addNetwork(std::shared_ptr<TFHEppNetwork> net)
    {
        tfhepp_.addNetwork(net);
    }

    void run()
    {
        if (graph_)
            graph_->reset();
        tfhepp_.prepareToRun();

        while (tfhepp_.getNumFinishedTargets() < tfhepp_.numNodes()) {
            assert(tfhepp_.isRunning() && "Detected infinite loop");
            tfhepp_.update();
        }
    }

    void tick()
    {
        tfhepp_.tick();
    }
};

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

    // Create nodes
    Name2NetMap name2net;

    {
        std::ifstream ifs{opt.logicFile};
        assert(ifs);
        auto net = std::make_shared<TFHEppNetwork>(
            readNetworkFromJSON<TFHEppNetworkBuilder>(ifs));
        assert(net->isValid());
        name2net.emplace("core", net);
    }

    if (opt.ramEnabled) {
        auto ramANet =
                 std::make_shared<TFHEppNetwork>(makeTFHEppRAMNetwork("A")),
             ramBNet =
                 std::make_shared<TFHEppNetwork>(makeTFHEppRAMNetwork("B"));
        name2net.emplace("ramA", ramANet);
        name2net.emplace("ramB", ramBNet);
    }

    if (!opt.romPorts.empty()) {
        auto romNet = std::make_shared<TFHEppNetwork>(makeTFHEppROMNetwork());
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
        assert(reqPacket.circuitKey);
        auto &rom = *name2net.at("rom");

        const int ROM_UNIT = 1024 / 8;
        assert(reqPacket.romCk.size() == 512 / ROM_UNIT);
        for (int i = 0; i < 512 / ROM_UNIT; i++) {
            int offset = ROM_UNIT * i;
            rom.get<TaskTFHEppROMUX>("rom", "all", 0)
                ->set128le(offset, reqPacket.romCk[i]);
        }
    }

    // Make runner
    TFHEppWorkerInfo wi{TFHEpp::lweParams{}, reqPacket.gateKey,
                        reqPacket.circuitKey};
    TFHEppNetworkRunner runner{opt.numWorkers, wi};
    for (auto &&p : name2net)
        runner.addNetwork(p.second);

    // Reset
    {
        TFHEpp::TLWElvl0 one, zero;
        TFHEpp::HomCONSTANTONE(one);
        TFHEpp::HomCONSTANTZERO(zero);

        auto &core = *name2net.at("core");
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
        std::ostream &os = opt.quiet ? devnull : std::cout;

        processCycles(opt.numCycles, os, [&](bool first) {
            auto &core = *name2net.at("core");

            if (dumpOS) {
                KVSPResPacket resPacket =
                    makeResPacket(name2net, opt.numCycles, opt.ramEnabled);
                dumpResultAsJSON(*dumpOS, resPacket, opt.secretKey.value());
            }

            runner.tick();

            if (first && opt.ramEnabled)
                setInitialRAM(name2net, reqPacket);

            runner.run();

            return false;
        });
    }

    // Dump result packet
    KVSPResPacket resPacket =
        makeResPacket(name2net, opt.numCycles, opt.ramEnabled);
    if (dumpOS)
        dumpResultAsJSON(*dumpOS, resPacket, opt.secretKey.value());
    writeToArchive(opt.outputFile, resPacket);
}
