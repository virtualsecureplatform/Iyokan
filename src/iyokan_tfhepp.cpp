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

    void run(bool showCombProg)
    {
        if (graph_)
            graph_->reset();
        tfhepp_.prepareToRun();

        int prevCount = 0, nowCount = 0;
        while ((nowCount = tfhepp_.getNumFinishedTargets()) <
               tfhepp_.numNodes()) {
            assert(tfhepp_.isRunning() && "Detected infinite loop");

            if (showCombProg && nowCount - prevCount > 1000) {
                spdlog::info("Circuit Executing... {}/{}", nowCount,
                             tfhepp_.numNodes());
                prevCount = nowCount;
            }

            tfhepp_.update();
        }
    }

    void tick()
    {
        tfhepp_.tick();
    }

    void setSDFFInitialValue()
    {
        tfhepp_.setSDFFInitialValue<TaskTFHEppGateDFF>();
    }
};

struct TFHEppRunParameter {
    NetworkBlueprint blueprint;
    int numCPUWorkers, numCycles;
    std::string ekFile, inputFile, outputFile;
    SCHED sched;

    TFHEppRunParameter()
    {
    }

    TFHEppRunParameter(const Options& opt)
    {
        blueprint = opt.blueprint.value();
        numCPUWorkers =
            opt.numCPUWorkers.value_or(std::thread::hardware_concurrency());
        numCycles = opt.numCycles.value_or(-1);
        ekFile = opt.ekFile.value();
        inputFile = opt.inputFile.value();
        outputFile = opt.outputFile.value();
        sched = opt.sched == SCHED::UND ? SCHED::RANKU : opt.sched;
    }

    void overwrite(const Options& opt)
    {
#define OVERWRITE(name) \
    if (opt.name)       \
        name = *opt.name;
        OVERWRITE(blueprint);
        OVERWRITE(numCPUWorkers);
        OVERWRITE(numCycles);
        OVERWRITE(ekFile);
        OVERWRITE(inputFile);
        OVERWRITE(outputFile);
#undef OVERWRITE
    }

    void print() const
    {
        spdlog::info("Run Parameters");
        spdlog::info("\tMode: TFHEpp");
        spdlog::info("\tBlueprint: {}", blueprint.sourceFile());
        spdlog::info("\t# of CPU workers: {}", numCPUWorkers);
        spdlog::info("\t# of cycles: {}", numCycles);
        spdlog::info("\tEvalKey file: {}", ekFile);
        spdlog::info("\tInput file (request packet): {}", inputFile);
        spdlog::info("\tOutput file (result packet): {}", outputFile);
    }

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(blueprint, numCPUWorkers, numCycles, ekFile, inputFile,
           outputFile);
    }
};

class TFHEppFrontend {
private:
    TFHEppRunParameter pr_;
    Name2NetMap name2net_;
    TFHEPacket reqPacket_;
    int currentCycle_;

private:
    // Don't allow to copy this object.
    TFHEppFrontend(const TFHEppFrontend&);
    TFHEppFrontend& operator=(const TFHEppFrontend&);

    template <class T = TaskTFHEppGate>
    std::shared_ptr<T> get(const blueprint::Port& port)
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
    std::shared_ptr<T> get_at(const std::string& kind,
                              const std::string& portName, int portBit = 0)
    {
        auto port = pr_.blueprint.at(portName, portBit);
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
    std::shared_ptr<T> maybeGet(const blueprint::Port& port)
    {
        auto it = name2net_.find(port.nodeName);
        if (it == name2net_.end())
            return nullptr;
        return it->second->get_if<T>(port.portLabel);
    }

    template <class T = TaskTFHEppGateMem>
    std::shared_ptr<T> maybeGetAt(const std::string& kind,
                                  const std::string& portName, int portBit = 0)
    {
        auto port = pr_.blueprint.at(portName, portBit);
        if (!port || port->portLabel.kind != kind)
            return nullptr;
        return std::dynamic_pointer_cast<T>(maybeGet(*port));
    }

    TFHEPacket makeResPacket(int numCycles)
    {
        TFHEPacket resPacket;
        resPacket.numCycles = numCycles;

        // Get values of output @port
        for (auto&& [key, port] : pr_.blueprint.atPorts()) {
            if (port.portLabel.kind != "output")
                continue;
            auto& [atPortName, atPortBit] = key;
            auto& bits = resPacket.bits[atPortName];
            if (bits.size() < atPortBit + 1)
                bits.resize(atPortBit + 1);
            bits.at(atPortBit) = get<TaskTFHEppGateMem>(port)->get();
        }
        // Get values of RAM
        // FIXME: subset of RAMs?
        for (auto&& bp : pr_.blueprint.builtinRAMs()) {
            using RAM_TYPE = blueprint::BuiltinRAM::TYPE;
            switch (bp.type) {
            case RAM_TYPE::CMUX_MEMORY: {
                std::vector<TRLWELvl1>& dst = resPacket.ram[bp.name];
                assert(dst.size() == 0);
                const size_t dataWidth = bp.inWdataWidth;
                for (int bit = 0; bit < dataWidth; bit++) {
                    auto& ram =
                        *get<TaskTFHEppRAMUX>({bp.name, {"ram", "", bit}});
                    if (dst.size() == 0)
                        dst.resize(ram.size() * dataWidth);
                    else
                        assert(ram.size() == dst.size() / dataWidth);
                    for (size_t addr = 0; addr < ram.size(); addr++)
                        dst.at(addr * dataWidth + bit) = *ram.get(addr);
                }
                break;
            }

            case RAM_TYPE::MUX: {
                std::vector<TLWELvl0>& dst = resPacket.ramInTLWE[bp.name];
                for (size_t i = 0; i < (1 << bp.inAddrWidth) * bp.outRdataWidth;
                     i++) {
                    const auto& ram = *get<TaskTFHEppGateMem>(
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
        for (const auto& bpram : pr_.blueprint.builtinRAMs()) {
            using RAM_TYPE = blueprint::BuiltinRAM::TYPE;
            switch (bpram.type) {
            case RAM_TYPE::CMUX_MEMORY: {
                auto it = reqPacket_.ram.find(bpram.name);
                if (it != reqPacket_.ram.end()) {
                    const auto& init = it->second;
                    const size_t dataWidth = bpram.inWdataWidth;
                    for (int bit = 0; bit < dataWidth; bit++) {
                        auto& ram = *get<TaskTFHEppRAMUX>(
                            {bpram.name, {"ram", "", bit}});
                        if (ram.size() != init.size() / dataWidth)
                            error::die(
                                "Invalid request packet: wrong length of RAM");
                        for (size_t addr = 0; addr < ram.size(); addr++)
                            ram.set(addr, init.at(addr * dataWidth + bit));
                    }
                }
                break;
            }

            case RAM_TYPE::MUX: {
                auto it = reqPacket_.ramInTLWE.find(bpram.name);
                if (it != reqPacket_.ramInTLWE.end()) {
                    const auto& init = it->second;
                    if (init.size() !=
                        (1 << bpram.inAddrWidth) * bpram.outRdataWidth)
                        error::die(
                            "Invalid request packet: wrong length of RAM");

                    for (size_t i = 0; i < init.size(); i++) {
                        auto& ram = *get<TaskTFHEppGateMem>(
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
        for (auto&& [key, port] : pr_.blueprint.atPorts()) {
            if (port.portLabel.kind != "input")
                continue;
            auto& [atPortName, atPortBit] = key;
            auto it = reqPacket_.bits.find(atPortName);
            if (it == reqPacket_.bits.end())
                continue;
            if (atPortName == "reset")
                error::die("@reset cannot be set by user's input");
            const auto& bits = it->second;  // Found input bit stream

            // Calculate the index in the bit stream for the port.
            size_t index =
                (pr_.blueprint.atPortWidths().at(atPortName) * currentCycle +
                 atPortBit) %
                bits.size();

            get<TaskTFHEppGateMem>(port)->set(bits.at(index));
        }
    }

    void dumpDecryptedPacket(const std::string& dumpPrefix,
                             const std::string& secretKey, int currentCycle)
    {
        auto sk = std::make_shared<TFHEpp::SecretKey>();
        readFromArchive(*sk, secretKey);
        PlainPacket packet = makeResPacket(currentCycle).decrypt(*sk);
        writeToArchive(utility::fok(dumpPrefix, "-", currentCycle), packet);
    }

public:
    TFHEppFrontend()
    {
    }

    TFHEppFrontend(const Options& opt) : pr_(opt), currentCycle_(0)
    {
        reqPacket_ = readFromArchive<TFHEPacket>(pr_.inputFile);

        // Create network according to blueprint
        const NetworkBlueprint& bp = pr_.blueprint;

        // [[file]]
        for (const auto& file : bp.files())
            name2net_.emplace(file.name,
                              readNetwork<TFHEppNetworkBuilder>(file));

        // [[builtin]] type = ram | type = mux-ram
        for (const auto& ram : bp.builtinRAMs()) {
            // FIXME: relax this constraint
            if (ram.inWdataWidth != ram.outRdataWidth)
                error::die(
                    "Invalid RAM size; RAM that has different sizes of "
                    "wdata and rdata is not implemented.");

            using RAM_TYPE = blueprint::BuiltinRAM::TYPE;
            switch (ram.type) {
            case RAM_TYPE::CMUX_MEMORY: {
                auto net = std::make_shared<TFHEppNetwork>(makeTFHEppRAMNetwork(
                    ram.inAddrWidth, ram.inWdataWidth, ""));
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
        for (const auto& bprom : bp.builtinROMs()) {
            using ROM_TYPE = blueprint::BuiltinROM::TYPE;
            switch (bprom.type) {
            case ROM_TYPE::CMUX_MEMORY: {
                if (!utility::isPowerOfTwo(bprom.outRdataWidth))
                    error::die("Invalid out_rdata_width of ROM \"", bprom.name,
                               "\": ", "must be a power of 2.");

                auto net = std::make_shared<TFHEppNetwork>(makeTFHEppROMNetwork(
                    bprom.inAddrWidth, utility::log2(bprom.outRdataWidth)));
                name2net_.emplace(bprom.name, net);

                // Set initial ROM data
                if (auto it = reqPacket_.rom.find(bprom.name);
                    it != reqPacket_.rom.end()) {
                    std::vector<TRLWELvl1>& init = it->second;
                    auto& rom =
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
                    std::vector<TLWELvl0>& init = it->second;
                    if (init.size() !=
                        (1 << bprom.inAddrWidth) * bprom.outRdataWidth)
                        error::die(
                            "Invalid request packet: wrong length of ROM");

                    for (size_t i = 0; i < init.size(); i++) {
                        auto& rom = *romnet->get<TaskTFHEppGateMem>(
                            {"rom", "romdata", static_cast<int>(i)});
                        rom.set(init.at(i));
                    }
                }

                break;
            }
            }
        }

        // Print gate counts
        for (auto&& [name, net] : name2net_) {
            GateCountVisitor vis;
            net->visit(vis);

            if (vis.kind2count().empty())
                continue;

            spdlog::debug("{} :", name);
            for (auto&& [kind, count] : vis.kind2count())
                spdlog::debug("\t{}\t{}", count, kind);
            spdlog::debug("");
        }

        // [connect]
        // We need to treat "... = @..." and "@... = ..." differently from
        // "..." = ...".
        // First, check if ports that are connected to or from "@..." exist.
        for (auto&& [key, port] : bp.atPorts()) {
            get(port);  // Only checks if port exists
        }
        // Then, connect other ports. `get` checks if they also exist.
        for (const auto& [src, dst] : bp.edges()) {
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
            for (auto&& p : name2net_)
                p.second->visit(grvis);

            std::optional<PrioritySetVisitor> privis;
            switch (pr_.sched) {
            case SCHED::TOPO:
                privis.emplace(graph::doTopologicalSort(grvis.getMap()));
                break;
            case SCHED::RANKU:
                privis.emplace(graph::doRankuSort(grvis.getMap()));
                break;
            default:
                error::die("unreachable");
            }

            for (auto&& p : name2net_)
                p.second->visit(privis.value());
        }
    }

    void overwriteParams(const Options& rhs)
    {
        pr_.overwrite(rhs);
    }

    void go(const Options& opt)
    {
        pr_.print();

        // Read ek
        auto ek = readFromArchive<TFHEpp::EvalKey>(pr_.ekFile);

        // Check bkey is correct.
        if (!(&ek.getbkfft<Lvl01>())|| (pr_.blueprint.needsCircuitKey() && !(&ek.getbkfft<TFHEpp::lvl02param>())))
            error::die("Invalid bootstrapping key");

        // Make runner
        auto graph = opt.dumpTimeCSVPrefix || opt.dumpGraphJSONPrefix ||
                             opt.dumpGraphDOTPrefix
                         ? std::make_shared<ProgressGraphMaker>()
                         : nullptr;
        TFHEppWorkerInfo wi{std::make_shared<TFHEpp::EvalKey>(ek)};
        TFHEppNetworkRunner runner{pr_.numCPUWorkers, wi, graph};
        for (auto&& p : name2net_)
            runner.addNetwork(p.second);

        // Reset
        auto reset = maybeGetAt("input", "reset");
        bool shouldNegateReset = false;
        if (currentCycle_ == 0 && !opt.skipReset) {
            if (auto reset = maybeGetAt("input", "reset"); reset) {
                TLWELvl0 one;
                TFHEpp::HomCONSTANTONE<Lvl0>(one);

                reset->set(one);
                runner.run(opt.showCombinationalProgress);
                // NOTE: Don't negate reset here. It makes
                // test "dff-reset-23" fail.
                shouldNegateReset = true;
            }
        }

        // Go computing
        for (int i = 0; i < pr_.numCycles; i++, currentCycle_++) {
            using namespace utility;

            spdlog::info("#{}", currentCycle_ + 1);
            if (opt.stdoutCSV)
                std::cout << std::chrono::system_clock::now() << ",start,"
                          << currentCycle_ + 1 << std::endl;
            if (opt.dumpPrefix && opt.secretKey)
                dumpDecryptedPacket(*opt.dumpPrefix, *opt.secretKey,
                                    currentCycle_);

            auto duration = timeit([&] {
                // Tick
                runner.tick();

                if (i == 0 && shouldNegateReset) {
                    TLWELvl0 zero;
                    TFHEpp::HomCONSTANTZERO<Lvl0>(zero);
                    reset->set(zero);
                }

                // Set values to RAM and input ports if necessary
                if (currentCycle_ == 0)
                    setInitialRAM();

                // Reset SDFF value with initial value
                if (currentCycle_ == 0)
                    runner.setSDFFInitialValue();

                setCircularInputs(currentCycle_);

                // Run
                runner.run(opt.showCombinationalProgress);
            });

            if (opt.dumpTimeCSVPrefix) {
                assert(graph);
                const std::string filename = fmt::format(
                    "{}-{}.csv", *opt.dumpTimeCSVPrefix, currentCycle_);
                graph->dumpTimeCSV(*utility::openOfstream(filename));
            }
            if (opt.dumpGraphJSONPrefix) {
                assert(graph);
                const std::string filename = fmt::format(
                    "{}-{}.json", *opt.dumpGraphJSONPrefix, currentCycle_);
                graph->dumpJSON(*utility::openOfstream(filename));
            }
            if (opt.dumpGraphDOTPrefix) {
                assert(graph);
                const std::string filename = fmt::format(
                    "{}-{}.dot", *opt.dumpGraphDOTPrefix, currentCycle_);
                graph->dumpDOT(*utility::openOfstream(filename));
            }

            spdlog::info("\tdone. ({} us)", duration.count());
            if (opt.stdoutCSV)
                std::cout << std::chrono::system_clock::now() << ",end,"
                          << currentCycle_ + 1 << std::endl;
        }

        // Dump result packet
        TFHEPacket resPacket = makeResPacket(currentCycle_);
        writeToArchive(pr_.outputFile, resPacket);
    }

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(pr_, name2net_, reqPacket_, currentCycle_);
    }
};

}  // namespace

void processAllGates(TFHEppNetwork& net, int numWorkers, TFHEppWorkerInfo wi,
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
                           [](auto&& w) { return w.isWorking(); }) ||
               !readyQueue.empty());

        for (auto&& w : workers)
            w.update();
    }

    assert(readyQueue.empty());
}

void doTFHE(const Options& opt)
{
    std::optional<TFHEppFrontend> frontend;
    if (opt.resumeFile) {
        frontend.emplace();
        readFromArchive<TFHEppFrontend>(*frontend, *opt.resumeFile);
        frontend->overwriteParams(opt);
    }
    else {
        frontend.emplace(opt);
    }
    frontend->go(opt);
    if (opt.snapshotFile)
        writeToArchive(*opt.snapshotFile, *frontend);
}

bool isSerializedTFHEppFrontend(const std::string& filepath)
{
    return isCorrectArchive<TFHEppFrontend>(filepath);
}
