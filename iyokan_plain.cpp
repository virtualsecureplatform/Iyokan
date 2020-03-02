#include "iyokan_plain.hpp"
#include "packet.hpp"

namespace {

using Name2NetMap =
    std::unordered_map<std::string, std::shared_ptr<PlainNetwork>>;

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

class PlainFrontend {
private:
    Name2NetMap name2net_;
    const Options &opt_;
    KVSPPlainReqPacket reqPacket_;

private:
    template <class T = TaskPlainGate>
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

    template <class T = TaskPlainGateMem>
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

    KVSPPlainResPacket makeResPacket(int numCycles)
    {
        KVSPPlainResPacket resPacket;
        resPacket.numCycles = numCycles;
        // Get values of flags
        resPacket.flags.emplace_back(get_at("output", "finflag")->get());
        // Get values of registers
        for (int reg = 0; reg < 16; reg++) {
            uint16_t &val = resPacket.regs.emplace_back(0);
            for (int bit = 0; bit < 16; bit++) {
                int n = get_at("output", detail::fok("reg_x", reg), bit)->get();
                val |= (n & 1u) << bit;
            }
        }
        // Get values of RAM
        if (auto atRAMOpt = opt_.blueprint->atRAM(); atRAMOpt) {
            using namespace blueprint;
            const RAM_AB *atRAM = std::get_if<RAM_AB>(&*atRAMOpt);
            assert(atRAM);
            auto &ramA = *get<TaskPlainRAM>({atRAM->nodeAName, {"ram", "", 0}}),
                 &ramB = *get<TaskPlainRAM>({atRAM->nodeBName, {"ram", "", 0}});
            for (int addr = 0; addr < 512; addr++) {
                resPacket.ram.push_back(
                    (addr % 2 == 1 ? ramA : ramB).get(addr / 2));
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
            auto &ramA = *get<TaskPlainRAM>({atRAM->nodeAName, {"ram", "", 0}}),
                 &ramB = *get<TaskPlainRAM>({atRAM->nodeBName, {"ram", "", 0}});
            for (int addr = 0; addr < 512; addr++) {
                (addr % 2 == 1 ? ramA : ramB)
                    .set(addr / 2, reqPacket_.ram.at(addr));
            }
        }
    }

public:
    PlainFrontend(const Options &opt)
        : opt_(opt),
          reqPacket_(readFromArchive<KVSPPlainReqPacket>(opt.inputFile))
    {
        assert(opt.blueprint);
    }

    KVSPPlainResPacket go()
    {
        // Create network according to blueprint
        const NetworkBlueprint &bp = *opt_.blueprint;

        // [[file]]
        for (const auto &file : bp.files()) {
            std::ifstream ifs{file.path, std::ios::binary};
            if (!ifs)
                error::die("Invalid [[file]] path: ", file.path);
            auto net = std::make_shared<PlainNetwork>(
                readNetworkFromJSON<PlainNetworkBuilder>(ifs));
            if (!net->isValid())
                error::die("Invalid network named: ", file.name);
            name2net_.emplace(file.name, net);
        }

        // [[builtin]] type = ram
        for (const auto &ram : bp.builtinRAMs()) {
            assert(ram.inAddrWidth == 8 && ram.inWdataWidth == 8 &&
                   ram.outRdataWidth == 8);
            auto net = std::make_shared<PlainNetwork>(makePlainRAMNetwork(""));
            name2net_.emplace(ram.name, net);
        }

        // [[builtin]] type = rom
        for (const auto &rom : bp.builtinROMs()) {
            assert(rom.inAddrWidth == 7 && rom.outRdataWidth == 32);
            auto net = std::make_shared<PlainNetwork>(makePlainROMNetwork());
            name2net_.emplace(rom.name, net);
        }

        // [connect]
        for (const auto &[src, dst] : bp.edges()) {
            assert(src.portLabel.kind == "output");
            assert(dst.portLabel.kind == "input");
            auto srcTask = get(src);
            auto dstTask = get(dst);
            dstTask->acceptOneMoreInput();
            NetworkBuilderBase<uint8_t>::connectTasks(srcTask, dstTask);
        }

        // Set initial ROM data
        if (auto atROMOpt = bp.atROM(); atROMOpt) {
            using namespace blueprint;
            const SingleROM *atROM = std::get_if<SingleROM>(&atROMOpt.value());
            assert(atROM);
            auto &rom =
                *get<TaskPlainROM>({atROM->nodeName, {"rom", "all", 0}});

            for (int i = 0; i < 512 / 4; i++) {
                int val = 0;
                for (int j = 3; j >= 0; j--) {
                    size_t offset = i * 4 + j;
                    val = (val << 8) | (offset < reqPacket_.rom.size()
                                            ? reqPacket_.rom[offset]
                                            : 0x00);
                }
                rom.set4le(i << 2, val);
            }
        }

        // Make runner
        PlainNetworkRunner runner{opt_.numCPUWorkers};
        for (auto &&p : name2net_)
            runner.addNetwork(p.second);

        // Get #cycles
        int numCycles = std::numeric_limits<int>::max();
        if (opt_.numCycles > 0)
            numCycles = opt_.numCycles;

        // Reset
        {
            auto reset = get_at("input", "reset");
            assert(reset);
            reset->set(1);
            runner.run();
            reset->set(0);
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

            auto finflag = get_at("output", "finflag");
            assert(finflag);

            numCycles = processCycles(numCycles, os, [&](bool first) {
                if (dumpOS)
                    makeResPacket(opt_.numCycles).printAsJSON(*dumpOS);

                runner.tick();

                if (first)
                    setInitialRAM();

                runner.run();

                return finflag->get();
            });
        }

        // Print the results
        KVSPPlainResPacket resPacket = makeResPacket(numCycles);
        if (dumpOS)
            resPacket.printAsJSON(*dumpOS);
        return resPacket;
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
    return PlainFrontend{opt}.go();
}
