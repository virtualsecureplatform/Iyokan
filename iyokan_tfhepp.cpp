#include "iyokan_tfhepp.hpp"
#include "packet.hpp"

namespace {

using Name2NetMap =
    std::unordered_map<std::string, std::shared_ptr<TFHEppNetwork>>;

void dumpResultAsJSON(std::ostream &os, const KVSPResPacket &resPacket,
                      const std::string &fileSecretKey)
{
    auto sk = std::make_shared<TFHEpp::SecretKey>();
    readFromArchive(*sk, fileSecretKey);
    decrypt(*sk, resPacket).printAsJSON(os);
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

class TFHEppFrontend {
private:
    Name2NetMap name2net_;
    const Options &opt_;
    KVSPReqPacket reqPacket_;

private:
    template <class T = TaskTFHEppGate>
    std::shared_ptr<T> get(const blueprint::Port &port)
    {
        auto it = name2net_.find(port.nodeName);
        if (it == name2net_.end())
            error::die("Invalid network. Not found: ", port.nodeName);
        auto task = it->second->get_if<T>(port.portLabel);
        if (!task)
            error::die("Invalid network. Not found: ", port.nodeName, "/",
                       port.portLabel.portName, "[", port.portLabel.portBit,
                       "]");
        return task;
    }

    template <class T = TaskTFHEppGateMem>
    std::shared_ptr<T> get_at(const std::string &kind,
                              const std::string &portName, int portBit = 0)
    {
        auto port = opt_.blueprint->at(portName, portBit);
        if (!port || port->portLabel.kind != kind)
            error::die("Invalid network. Not found: ", portName, "[", portBit,
                       "]");
        auto task = std::dynamic_pointer_cast<T>(get(*port));
        if (!task)
            error::die("Invalid network. Not found: ", portName, "[", portBit,
                       "]");
        return task;
    }

    KVSPResPacket makeResPacket()
    {
        KVSPResPacket resPacket;
        resPacket.numCycles = opt_.numCycles;
        // Get values of flags
        resPacket.flags.push_back(get_at("output", "finflag")->get());
        // Get values of registers
        for (int reg = 0; reg < 16; reg++) {
            resPacket.regs.emplace_back();
            for (int bit = 0; bit < 16; bit++)
                resPacket.regs[reg].push_back(
                    get_at("output", detail::fok("reg_x", reg), bit)->get());
        }
        // Get values of RAM
        if (auto atRAMOpt = opt_.blueprint->atRAM(); atRAMOpt) {
            using namespace blueprint;
            const RAM_AB *atRAM = std::get_if<RAM_AB>(&*atRAMOpt);
            assert(atRAM);
            for (int addr = 0; addr < 512; addr++) {
                for (int bit = 0; bit < 8; bit++) {
                    auto &ram = addr % 2 == 1
                                    ? *get<TaskTFHEppRAMUX>(
                                          {atRAM->nodeAName, {"ram", "", bit}})
                                    : *get<TaskTFHEppRAMUX>(
                                          {atRAM->nodeBName, {"ram", "", bit}});
                    resPacket.ramCk.push_back(ram.get(addr / 2));
                }
            }
        }

        return resPacket;
    }

    void setInitialRAM()
    {
        if (auto atRAMOpt = opt_.blueprint->atRAM(); atRAMOpt) {
            using namespace blueprint;
            const RAM_AB *atRAM = std::get_if<RAM_AB>(&*atRAMOpt);
            assert(atRAM);
            for (int addr = 0; addr < 512; addr++) {
                for (int bit = 0; bit < 8; bit++) {
                    auto &ram = addr % 2 == 1
                                    ? *get<TaskTFHEppRAMUX>(
                                          {atRAM->nodeAName, {"ram", "", bit}})
                                    : *get<TaskTFHEppRAMUX>(
                                          {atRAM->nodeBName, {"ram", "", bit}});
                    ram.set(addr / 2, reqPacket_.ramCk.at(addr * 8 + bit));
                }
            }
        }
    }

public:
    TFHEppFrontend(const Options &opt)
        : opt_(opt), reqPacket_(readFromArchive<KVSPReqPacket>(opt.inputFile))
    {
        assert(opt.blueprint);
    }

    void go()
    {
        // Create network according to blueprint
        const NetworkBlueprint &bp = *opt_.blueprint;

        // [[file]]
        for (const auto &file : bp.files()) {
            std::ifstream ifs{file.path, std::ios::binary};
            if (!ifs)
                error::die("Invalid [[file]] path: ", file.path);
            auto net = std::make_shared<TFHEppNetwork>(
                readNetworkFromJSON<TFHEppNetworkBuilder>(ifs));
            if (!net->isValid())
                error::die("Invalid network named: ", file.name);
            name2net_.emplace(file.name, net);
        }

        // [[builtin]] type = ram
        for (const auto &ram : bp.builtinRAMs()) {
            assert(reqPacket_.circuitKey);
            assert(ram.inAddrWidth == 8 && ram.inWdataWidth == 8 &&
                   ram.outRdataWidth == 8);
            auto net =
                std::make_shared<TFHEppNetwork>(makeTFHEppRAMNetwork(""));
            name2net_.emplace(ram.name, net);
        }

        // [[builtin]] type = rom
        for (const auto &rom : bp.builtinROMs()) {
            assert(reqPacket_.circuitKey);
            assert(rom.inAddrWidth == 7 && rom.outRdataWidth == 32);
            auto net = std::make_shared<TFHEppNetwork>(makeTFHEppROMNetwork());
            name2net_.emplace(rom.name, net);
        }

        // [connect]
        for (const auto &[src, dst] : bp.edges()) {
            assert(src.portLabel.kind == "output");
            assert(dst.portLabel.kind == "input");
            auto srcTask = get(src);
            auto dstTask = get(dst);
            dstTask->acceptOneMoreInput();
            NetworkBuilderBase<TFHEpp::TLWElvl0>::connectTasks(srcTask,
                                                               dstTask);
        }

        // Set initial ROM data
        if (auto atROMOpt = bp.atROM(); atROMOpt) {
            using namespace blueprint;
            const SingleROM *atROM = std::get_if<SingleROM>(&atROMOpt.value());
            assert(atROM);
            auto &rom =
                *get<TaskTFHEppROMUX>({atROM->nodeName, {"rom", "all", 0}});

            const int ROM_UNIT = 1024 / 8;
            assert(reqPacket_.romCk.size() == 512 / ROM_UNIT);
            for (int i = 0; i < 512 / ROM_UNIT; i++) {
                int offset = ROM_UNIT * i;
                rom.set128le(offset, reqPacket_.romCk[i]);
            }
        }

        // Make runner
        TFHEppWorkerInfo wi{TFHEpp::lweParams{}, reqPacket_.gateKey,
                            reqPacket_.circuitKey};
        TFHEppNetworkRunner runner{opt_.numCPUWorkers, wi};
        for (auto &&p : name2net_)
            runner.addNetwork(p.second);

        // Reset
        {
            TFHEpp::TLWElvl0 one, zero;
            TFHEpp::HomCONSTANTONE(one);
            TFHEpp::HomCONSTANTZERO(zero);

            auto &reset = *get_at("input", "reset");
            reset.set(one);
            runner.run();
            reset.set(zero);
        }

        // Go computing
        std::optional<std::ofstream> dumpOS;
        if (opt_.dumpEveryClock) {
            dumpOS = std::ofstream{*opt_.dumpEveryClock};
            assert(*dumpOS);
        }

        {
            std::stringstream devnull;
            std::ostream &os = opt_.quiet ? devnull : std::cout;

            processCycles(opt_.numCycles, os, [&](bool first) {
                if (dumpOS)
                    dumpResultAsJSON(*dumpOS, makeResPacket(),
                                     opt_.secretKey.value());

                runner.tick();

                if (first)
                    setInitialRAM();

                runner.run();

                return false;
            });
        }

        // Dump result packet
        KVSPResPacket resPacket = makeResPacket();
        if (dumpOS)
            dumpResultAsJSON(*dumpOS, resPacket, opt_.secretKey.value());
        writeToArchive(opt_.outputFile, resPacket);
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
    TFHEppFrontend{opt}.go();
}
