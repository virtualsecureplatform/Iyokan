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
    PlainPacket reqPacket_;

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

    PlainPacket makeResPacket(int numCycles)
    {
        PlainPacket resPacket;
        resPacket.numCycles = numCycles;
        // Get values of output @port
        for (auto &&[key, port] : opt_.blueprint->atPorts()) {
            if (port.portLabel.kind != "output")
                continue;
            auto &[atPortName, atPortBit] = key;
            auto &bits = resPacket.bits[atPortName];
            if (bits.size() < atPortBit + 1)
                bits.resize(atPortBit + 1);
            bits.at(atPortBit) = get<TaskPlainGateMem>(port)->get();
        }
        // Get values of RAM
        // FIXME: subset of RAMs?
        for (auto &&bp : opt_.blueprint->builtinRAMs()) {
            auto &ram = *get<TaskPlainRAM>({bp.name, {"ram", "", 0}});
            std::vector<uint8_t> &dst = resPacket.ram[bp.name];
            assert(dst.size() == 0);
            for (size_t addr = 0; addr < ram.size(); addr++)
                dst.push_back(ram.get(addr));
        }

        return resPacket;
    }

    void setInitialRAM()
    {
        for (auto &&[name, init] : reqPacket_.ram) {
            auto &ram = *get<TaskPlainRAM>({name, {"ram", "", 0}});
            if (ram.size() != init.size())
                error::die("Invalid request packet: wrong length of RAM");
            for (size_t addr = 0; addr < ram.size(); addr++)
                ram.set(addr, init[addr]);
        }
    }

public:
    PlainFrontend(const Options &opt)
        : opt_(opt), reqPacket_(readFromArchive<PlainPacket>(opt.inputFile))
    {
        assert(opt.blueprint);
    }

    void go()
    {
        // Create network according to blueprint and request packet
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
        for (const auto &bprom : bp.builtinROMs()) {
            assert(bprom.inAddrWidth == 7 && bprom.outRdataWidth == 32);
            auto net = std::make_shared<PlainNetwork>(makePlainROMNetwork());
            name2net_.emplace(bprom.name, net);

            // Set initial data
            if (auto it = reqPacket_.rom.find(bprom.name);
                it != reqPacket_.rom.end()) {
                std::vector<uint8_t> &init = it->second;
                auto &rom = *get<TaskPlainROM>({bprom.name, {"rom", "all", 0}});

                if (rom.size() != init.size())
                    error::die("Invalid request packet: wrong length of ROM");
                if (rom.size() % 4 != 0)
                    error::die("Invalid ROM size: must be a multiple of 4");

                for (size_t i = 0; i < rom.size() / 4; i++) {
                    int val = 0;
                    for (int j = 3; j >= 0; j--) {
                        size_t offset = i * 4 + j;
                        val = (val << 8) |
                              (offset < init.size() ? init[offset] : 0x00);
                    }
                    rom.set4le(i << 2, val);
                }
            }
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
                    writeToArchive(*dumpOS, makeResPacket(opt_.numCycles));

                runner.tick();

                if (first)
                    setInitialRAM();

                runner.run();

                return finflag->get();
            });
        }

        // Print the results
        PlainPacket resPacket = makeResPacket(numCycles);
        if (dumpOS)
            writeToArchive(*dumpOS, resPacket);
        writeToArchive(opt_.outputFile, resPacket);
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

void doPlain(const Options &opt)
{
    PlainFrontend{opt}.go();
}
