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
    Options opt_;
    TFHEPacket reqPacket_;
    int currentCycle_;

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

    template <class T = TaskTFHEppGate>
    std::shared_ptr<T> maybeGet(const blueprint::Port &port)
    {
        auto it = name2net_.find(port.nodeName);
        if (it == name2net_.end())
            return nullptr;
        return it->second->get_if<T>(port.portLabel);
    }

    template <class T = TaskTFHEppGateMem>
    std::shared_ptr<T> maybeGetAt(const std::string &kind,
                                  const std::string &portName, int portBit = 0)
    {
        auto port = opt_.blueprint->at(portName, portBit);
        if (!port || port->portLabel.kind != kind)
            return nullptr;
        return std::dynamic_pointer_cast<T>(maybeGet(*port));
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
            using RAM_TYPE = blueprint::BuiltinRAM::TYPE;
            switch (bp.type) {
            case RAM_TYPE::CMUX_MEMORY: {
                std::vector<TFHEpp::TRLWElvl1> &dst = resPacket.ram[bp.name];
                assert(dst.size() == 0);
                for (int bit = 0; bit < 8; bit++) {
                    auto &ram =
                        *get<TaskTFHEppRAMUX>({bp.name, {"ram", "", bit}});
                    if (dst.size() == 0)
                        dst.resize(ram.size() * 8);
                    else
                        assert(ram.size() == dst.size() / 8);
                    for (size_t addr = 0; addr < ram.size(); addr++)
                        dst.at(addr * 8 + bit) = *ram.get(addr);
                }
                break;
            }

            case RAM_TYPE::MUX: {
                std::vector<TFHEpp::TLWElvl0> &dst =
                    resPacket.ramInTLWE[bp.name];
                for (size_t i = 0; i < (1 << bp.inAddrWidth) * bp.outRdataWidth;
                     i++) {
                    const auto &ram = *get<TaskTFHEppGateMem>(
                        {bp.name, {"ram", "ramdata", static_cast<int>(i)}});
                    dst.push_back(ram.get());
                }
                break;
            }
            }
        }

        return resPacket;
    }

    void setInitialRAM()
    {
        for (const auto &bpram : opt_.blueprint->builtinRAMs()) {
            using RAM_TYPE = blueprint::BuiltinRAM::TYPE;
            switch (bpram.type) {
            case RAM_TYPE::CMUX_MEMORY: {
                auto it = reqPacket_.ram.find(bpram.name);
                if (it != reqPacket_.ram.end()) {
                    const auto &init = it->second;
                    for (int bit = 0; bit < 8; bit++) {
                        auto &ram = *get<TaskTFHEppRAMUX>(
                            {bpram.name, {"ram", "", bit}});
                        if (ram.size() != init.size() / 8)
                            error::die(
                                "Invalid request packet: wrong length of RAM");
                        for (size_t addr = 0; addr < ram.size(); addr++)
                            ram.set(addr, init.at(addr * 8 + bit));
                    }
                }
                break;
            }

            case RAM_TYPE::MUX: {
                auto it = reqPacket_.ramInTLWE.find(bpram.name);
                if (it != reqPacket_.ramInTLWE.end()) {
                    const auto &init = it->second;
                    if (init.size() !=
                        (1 << bpram.inAddrWidth) * bpram.outRdataWidth)
                        error::die(
                            "Invalid request packet: wrong length of RAM");

                    for (size_t i = 0; i < init.size(); i++) {
                        auto &ram = *get<TaskTFHEppGateMem>(
                            {bpram.name,
                             {"ram", "ramdata", static_cast<int>(i)}});
                        ram.set(init.at(i));
                    }
                }
                break;
            }
            }
        }
    }

    void setCircularInputs(int currentCycle)
    {
        // Find input @port
        for (auto &&[key, port] : opt_.blueprint->atPorts()) {
            if (port.portLabel.kind != "input")
                continue;
            auto &[atPortName, atPortBit] = key;
            auto it = reqPacket_.bits.find(atPortName);
            if (it == reqPacket_.bits.end())
                continue;
            const auto &bits = it->second;  // Found input bit stream

            // Calculate the index in the bit stream for the port.
            size_t index =
                (opt_.blueprint->atPortWidths().at(atPortName) * currentCycle +
                 atPortBit) %
                bits.size();

            get<TaskTFHEppGateMem>(port)->set(bits.at(index));
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
    TFHEppFrontend()
    {
    }

    TFHEppFrontend(const Options &opt)
        : opt_(opt),
          reqPacket_(readFromArchive<TFHEPacket>(opt.inputFile.value())),
          currentCycle_(0)
    {
        // Create network according to blueprint
        const NetworkBlueprint &bp = opt_.blueprint.value();

        // [[file]]
        for (const auto &file : bp.files())
            name2net_.emplace(file.name,
                              readNetwork<TFHEppNetworkBuilder>(file));

        // [[builtin]] type = ram | type = mux-ram
        for (const auto &ram : bp.builtinRAMs()) {
            assert(reqPacket_.ck);
            assert(ram.inAddrWidth == 8 && ram.inWdataWidth == 8 &&
                   ram.outRdataWidth == 8);

            using RAM_TYPE = blueprint::BuiltinRAM::TYPE;
            switch (ram.type) {
            case RAM_TYPE::CMUX_MEMORY: {
                auto net =
                    std::make_shared<TFHEppNetwork>(makeTFHEppRAMNetwork(""));
                name2net_.emplace(ram.name, net);
                break;
            }

            case RAM_TYPE::MUX:
                auto net = makeRAMWithMUX<TFHEppNetworkBuilder>(
                    ram.inAddrWidth, ram.outRdataWidth);
                name2net_.emplace(ram.name, net);
                break;
            }
        }

        // [[builtin]] type = rom | type = mux-rom
        for (const auto &bprom : bp.builtinROMs()) {
            using ROM_TYPE = blueprint::BuiltinROM::TYPE;
            switch (bprom.type) {
            case ROM_TYPE::CMUX_MEMORY: {
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
                        error::die(
                            "Invalid request packet: wrong length of ROM");

                    for (size_t i = 0; i < rom.size(); i++)
                        rom.set(i, init.at(i));
                }
                break;
            }

            case ROM_TYPE::MUX: {
                // Create ROM with MUX
                auto romnet = makeROMWithMUX<TFHEppNetworkBuilder>(
                    bprom.inAddrWidth, bprom.outRdataWidth);
                name2net_.emplace(bprom.name, romnet);

                // Set initial data
                if (auto it = reqPacket_.romInTLWE.find(bprom.name);
                    it != reqPacket_.romInTLWE.end()) {
                    std::vector<TFHEpp::TLWElvl0> &init = it->second;
                    if (init.size() !=
                        (1 << bprom.inAddrWidth) * bprom.outRdataWidth)
                        error::die(
                            "Invalid request packet: wrong length of ROM");

                    for (size_t i = 0; i < init.size(); i++) {
                        auto &rom = *romnet->get<TaskTFHEppGateMem>(
                            {"rom", "romdata", static_cast<int>(i)});
                        rom.set(init.at(i));
                    }
                }

                break;
            }
            }
        }

        // Print gate counts
        for (auto &&[name, net] : name2net_) {
            GateCountVisitor vis;
            net->visit(vis);

            if (vis.kind2count().empty())
                continue;

            spdlog::debug("{} :", name);
            for (auto &&[kind, count] : vis.kind2count())
                spdlog::debug("\t{}\t{}", count, kind);
            spdlog::debug("");
        }

        // [connect]
        for (const auto &[src, dst] : bp.edges()) {
            assert(src.portLabel.kind == "output");
            assert(dst.portLabel.kind == "input");
            auto srcTask = get(src);
            auto dstTask = get(dst);
            dstTask->acceptOneMoreInput();
            connectTasks(srcTask, dstTask);
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
    }

    void mergeOptions(const Options &rhs)
    {
        opt_.merge(rhs);
    }

    void go()
    {
        // Make runner
        TFHEppWorkerInfo wi{TFHEpp::lweParams{}, reqPacket_.gk, reqPacket_.ck};
        TFHEppNetworkRunner runner{opt_.numCPUWorkers.value(), wi};
        for (auto &&p : name2net_)
            runner.addNetwork(p.second);

        // Reset
        if (currentCycle_ == 0) {
            if (auto reset = maybeGetAt("input", "reset"); reset) {
                TFHEpp::TLWElvl0 one, zero;
                TFHEpp::HomCONSTANTONE(one);
                TFHEpp::HomCONSTANTZERO(zero);

                reset->set(one);
                runner.run();
                reset->set(zero);
            }
        }

        // Go computing
        {
            currentCycle_ = processCycles(
                opt_.numCycles.value(),
                [&](int currentCycle) {
                    mayDumpPacket(currentCycle);

                    runner.tick();

                    if (currentCycle == 0)
                        setInitialRAM();
                    setCircularInputs(currentCycle);

                    runner.run();

                    return false;
                },
                currentCycle_);
        }

        // Dump result packet
        TFHEPacket resPacket = makeResPacket(currentCycle_);
        writeToArchive(opt_.outputFile.value(), resPacket);
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(name2net_, opt_, reqPacket_, currentCycle_);
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
    auto frontend = opt.resumeFile
                        ? readFromArchive<TFHEppFrontend>(*opt.resumeFile)
                        : TFHEppFrontend{opt};
    if (opt.resumeFile)
        frontend.mergeOptions(opt);
    frontend.go();
    if (opt.snapshotFile)
        writeToArchive(*opt.snapshotFile, frontend);
}

bool isSerializedTFHEppFrontend(const std::string &filepath)
{
    return isCorrectArchive<TFHEppFrontend>(filepath);
}
