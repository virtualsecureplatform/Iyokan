#include "iyokan_tfhepp.hpp"
#include "packet.hpp"

namespace {

using Name2NetMap =
    std::unordered_map<std::string, std::shared_ptr<TFHEppNetwork>>;

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
    TFHEPacket reqPacket_;

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

    TFHEPacket makeResPacket(int numCycles)
    {
        TFHEPacket resPacket;
        resPacket.numCycles = numCycles;

        // Get values of output @port
        for (auto &&[key, port] : opt_.blueprint->atPorts()) {
            if (port.portLabel.kind != "output")
                continue;
            auto &[atPortName, atPortBit] = key;
            auto &bits = resPacket.bits[atPortName];
            if (bits.size() < atPortBit + 1)
                bits.resize(atPortBit + 1);
            bits.at(atPortBit) = get<TaskTFHEppGateMem>(port)->get();
        }
        // Get values of RAM
        // FIXME: subset of RAMs?
        for (auto &&bp : opt_.blueprint->builtinRAMs()) {
            std::vector<TFHEpp::TRLWElvl1> &dst = resPacket.ram[bp.name];
            assert(dst.size() == 0);
            for (int bit = 0; bit < 8; bit++) {
                auto &ram = *get<TaskTFHEppRAMUX>({bp.name, {"ram", "", bit}});
                if (dst.size() == 0)
                    dst.resize(ram.size() * 8);
                else
                    assert(ram.size() == dst.size() / 8);
                for (size_t addr = 0; addr < ram.size(); addr++)
                    dst.at(addr * 8 + bit) = ram.get(addr);
            }
        }

        return resPacket;
    }

    void setInitialRAM()
    {
        for (auto &&[name, init] : reqPacket_.ram) {
            for (int bit = 0; bit < 8; bit++) {
                auto &ram = *get<TaskTFHEppRAMUX>({name, {"ram", "", bit}});
                if (ram.size() != init.size() / 8)
                    error::die("Invalid request packet: wrong length of RAM");
                for (size_t addr = 0; addr < ram.size(); addr++)
                    ram.set(addr, init.at(addr * 8 + bit));
            }
        }
    }

    void mayDumpPacket(int currentCycle)
    {
        if (!opt_.dumpPrefix)
            return;
        auto sk = std::make_shared<TFHEpp::SecretKey>();
        readFromArchive(*sk, opt_.secretKey.value());
        PlainPacket packet = makeResPacket(currentCycle).decrypt(*sk);
        writeToArchive(utility::fok(*opt_.dumpPrefix, "-", currentCycle),
                       packet);
    }

public:
    TFHEppFrontend(const Options &opt)
        : opt_(opt), reqPacket_(readFromArchive<TFHEPacket>(opt.inputFile))
    {
        assert(opt.blueprint);
    }

    void go()
    {
        // Create network according to blueprint
        const NetworkBlueprint &bp = *opt_.blueprint;

        // [[file]]
        for (const auto &file : bp.files())
            name2net_.emplace(file.name,
                              readNetwork<TFHEppNetworkBuilder>(file));

        // [[builtin]] type = ram
        for (const auto &ram : bp.builtinRAMs()) {
            assert(reqPacket_.ck);
            assert(ram.inAddrWidth == 8 && ram.inWdataWidth == 8 &&
                   ram.outRdataWidth == 8);

            auto net =
                std::make_shared<TFHEppNetwork>(makeTFHEppRAMNetwork(""));
            name2net_.emplace(ram.name, net);
        }

        // [[builtin]] type = rom
        for (const auto &bprom : bp.builtinROMs()) {
            assert(reqPacket_.ck);

            if (!utility::isPowerOfTwo(bprom.outRdataWidth))
                error::die("Invalid out_rdata_width of ROM \"", bprom.name,
                           "\": ", "must be a power of 2.");

            auto net = std::make_shared<TFHEppNetwork>(makeTFHEppROMNetwork(
                bprom.inAddrWidth, utility::log2(bprom.outRdataWidth)));
            name2net_.emplace(bprom.name, net);

            // Set initial ROM data
            if (auto it = reqPacket_.rom.find(bprom.name);
                it != reqPacket_.rom.end()) {
                std::vector<TFHEpp::TRLWElvl1> &init = it->second;
                auto &rom =
                    *get<TaskTFHEppROMUX>({bprom.name, {"rom", "all", 0}});

                if (rom.size() != init.size())
                    error::die("Invalid request packet: wrong length of ROM");

                for (size_t i = 0; i < rom.size(); i++)
                    rom.set(i, init.at(i));
            }
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

        // Set priority to each DepNode
        {
            GraphVisitor grvis;
            for (auto &&p : name2net_)
                p.second->visit(grvis);
            PrioritySetVisitor privis{graph::doTopologicalSort(grvis.getMap())};
            for (auto &&p : name2net_)
                p.second->visit(privis);
        }

        // Make runner
        TFHEppWorkerInfo wi{TFHEpp::lweParams{}, reqPacket_.gk, reqPacket_.ck};
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
        {
            std::stringstream devnull;
            std::ostream &os = opt_.quiet ? devnull : std::cout;

            processCycles(opt_.numCycles, os, [&](int currentCycle) {
                mayDumpPacket(currentCycle);

                runner.tick();

                if (currentCycle == 0)
                    setInitialRAM();

                runner.run();

                return false;
            });
        }

        // Dump result packet
        TFHEPacket resPacket = makeResPacket(opt_.numCycles);
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
