#include "iyokan_plain.hpp"
#include "packet.hpp"
#include "utility.hpp"

namespace {

using Name2NetMap =
    std::unordered_map<std::string, std::shared_ptr<PlainNetwork>>;

class PlainNetworkRunner {
private:
    NetworkRunner<PlainWorkerInfo, PlainWorker> plain_;
    std::shared_ptr<ProgressGraphMaker> graph_;

public:
    PlainNetworkRunner(int numCPUWorkers,
                       std::shared_ptr<ProgressGraphMaker> graph = nullptr)
        : graph_(graph)
    {
        for (int i = 0; i < numCPUWorkers; i++)
            plain_.addWorker(graph_);
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

struct PlainRunParameter {
    NetworkBlueprint blueprint;
    int numCPUWorkers, numCycles;
    std::string inputFile, outputFile;
    bool stdoutCSV;

    // nullopt means to disable that option.
    std::optional<std::string> dumpPrefix;

    PlainRunParameter()
    {
    }

    PlainRunParameter(const Options &opt)
    {
        blueprint = opt.blueprint.value();
        numCPUWorkers =
            opt.numCPUWorkers.value_or(std::thread::hardware_concurrency());
        numCycles = opt.numCycles.value_or(-1);
        inputFile = opt.inputFile.value();
        outputFile = opt.outputFile.value();
        stdoutCSV = opt.stdoutCSV.value_or(false);

        dumpPrefix = opt.dumpPrefix;
    }

    void overwrite(const Options &opt)
    {
#define OVERWRITE(name) \
    if (opt.name)       \
        name = *opt.name;
        OVERWRITE(blueprint);
        OVERWRITE(numCPUWorkers);
        OVERWRITE(numCycles);
        OVERWRITE(inputFile);
        OVERWRITE(outputFile);
        OVERWRITE(stdoutCSV);
        OVERWRITE(dumpPrefix);
#undef OVERWRITE
    }

    void print() const
    {
        spdlog::info("Run Parameters");
        spdlog::info("\tMode: Plain");
        spdlog::info("\tBlueprint: {}", blueprint.sourceFile());
        spdlog::info("\t# of CPU workers: {}", numCPUWorkers);
        spdlog::info("\t# of cycles: {}", numCycles);
        spdlog::info("\tInput file (request packet): {}", inputFile);
        spdlog::info("\tOutput file (result packet): {}", outputFile);
        spdlog::info("\t--stdoutCSV: {}", stdoutCSV);
        spdlog::info("\t--dump-prefix: {}", dumpPrefix.value_or("(none)"));
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(blueprint, numCPUWorkers, numCycles, inputFile, outputFile,
           stdoutCSV, dumpPrefix);
    }
};

class PlainFrontend {
private:
    PlainRunParameter pr_;
    Name2NetMap name2net_;
    PlainPacket reqPacket_;
    int currentCycle_;

private:
    PlainFrontend(const PlainFrontend &);
    PlainFrontend &operator=(const PlainFrontend &);

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

    template <class T = TaskPlainGate>
    std::shared_ptr<T> maybeGet(const blueprint::Port &port)
    {
        auto it = name2net_.find(port.nodeName);
        if (it == name2net_.end())
            return nullptr;
        return it->second->get_if<T>(port.portLabel);
    }

    template <class T = TaskPlainGateMem>
    std::shared_ptr<T> maybeGetAt(const std::string &kind,
                                  const std::string &portName, int portBit = 0)
    {
        auto port = pr_.blueprint.at(portName, portBit);
        if (!port || port->portLabel.kind != kind)
            return nullptr;
        return std::dynamic_pointer_cast<T>(maybeGet(*port));
    }

    PlainPacket makeResPacket(int numCycles)
    {
        PlainPacket resPacket;
        resPacket.numCycles = numCycles;
        // Get values of output @port
        for (auto &&[key, port] : pr_.blueprint.atPorts()) {
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
        for (auto &&bp : pr_.blueprint.builtinRAMs()) {
            using RAM_TYPE = blueprint::BuiltinRAM::TYPE;
            switch (bp.type) {
            case RAM_TYPE::CMUX_MEMORY: {
                auto &ram = *get<TaskPlainRAM>({bp.name, {"ram", "", 0}});
                std::vector<Bit> &dst = resPacket.ram[bp.name];
                assert(dst.size() == 0);
                for (size_t addr = 0; addr < ram.size(); addr++) {
                    uint8_t val = ram.get(addr);
                    for (int i = 0; i < 8; i++)
                        dst.push_back(((val >> i) & 1u) != 0 ? 1_b : 0_b);
                }
                break;
            }

            case RAM_TYPE::MUX: {
                std::vector<Bit> &dst = resPacket.ram[bp.name];
                for (size_t i = 0; i < (1 << bp.inAddrWidth) * bp.outRdataWidth;
                     i++) {
                    const auto &ram = *get<TaskPlainGateMem>(
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
        for (auto &&[name, init] : reqPacket_.ram) {
            if (auto ramPtr = maybeGet<TaskPlainRAM>({name, {"ram", "", 0}});
                ramPtr) {  // CMUX Memory
                auto &ram = *ramPtr;
                if (ram.size() * 8 != init.size())
                    error::die("Invalid request packet: wrong length of RAM");
                for (size_t addr = 0; addr < ram.size(); addr++) {
                    uint8_t val = 0;
                    for (int i = 0; i < 8; i++)
                        val |= static_cast<uint8_t>(init[addr * 8 + i]) << i;
                    ram.set(addr, val);
                }
            }
            else {  // MUX
                for (size_t i = 0; i < init.size(); i++) {
                    auto &ram = *get<TaskPlainGateMem>(
                        {name, {"ram", "ramdata", static_cast<int>(i)}});
                    ram.set(init.at(i));
                }
            }
        }
    }

    void setCircularInputs(int currentCycle)
    {
        // Find input @port
        for (auto &&[key, port] : pr_.blueprint.atPorts()) {
            if (port.portLabel.kind != "input")
                continue;
            auto &[atPortName, atPortBit] = key;
            auto it = reqPacket_.bits.find(atPortName);
            if (it == reqPacket_.bits.end())
                continue;
            const auto &bits = it->second;  // Found input bit stream

            // Calculate the index in the bit stream for the port.
            size_t index =
                (pr_.blueprint.atPortWidths().at(atPortName) * currentCycle +
                 atPortBit) %
                bits.size();

            get<TaskPlainGateMem>(port)->set(bits.at(index));
        }
    }

public:
    PlainFrontend()
    {
    }

    PlainFrontend(const Options &opt) : pr_(opt), currentCycle_(0)
    {
        reqPacket_ = readFromArchive<PlainPacket>(pr_.inputFile);

        // Create network according to blueprint and request packet
        const NetworkBlueprint &bp = pr_.blueprint;

        // [[file]]
        for (const auto &file : bp.files())
            name2net_.emplace(file.name,
                              readNetwork<PlainNetworkBuilder>(file));

        // [[builtin]] type = ram | type = mux-ram
        for (const auto &ram : bp.builtinRAMs()) {
            using RAM_TYPE = blueprint::BuiltinRAM::TYPE;
            switch (ram.type) {
            case RAM_TYPE::CMUX_MEMORY: {
                // FIXME: relax this constraint
                assert(ram.inAddrWidth == 8 && ram.inWdataWidth == 8 &&
                       ram.outRdataWidth == 8);
                auto net =
                    std::make_shared<PlainNetwork>(makePlainRAMNetwork(""));
                name2net_.emplace(ram.name, net);
                break;
            }
            case RAM_TYPE::MUX: {
                auto net = makeRAMWithMUX<PlainNetworkBuilder>(
                    ram.inAddrWidth, ram.outRdataWidth);
                name2net_.emplace(ram.name, net);
                break;
            }
            }
        }

        // [[builtin]] type = rom | type = mux-rom
        for (const auto &bprom : bp.builtinROMs()) {
            using ROM_TYPE = blueprint::BuiltinROM::TYPE;
            switch (bprom.type) {
            case ROM_TYPE::CMUX_MEMORY: {
                auto net = std::make_shared<PlainNetwork>(makePlainROMNetwork(
                    bprom.inAddrWidth, bprom.outRdataWidth));
                name2net_.emplace(bprom.name, net);

                // Set initial data
                if (auto it = reqPacket_.rom.find(bprom.name);
                    it != reqPacket_.rom.end()) {
                    const size_t inAddrSpaceSize = 1 << bprom.inAddrWidth;

                    std::vector<Bit> &init = it->second;
                    if (init.size() != inAddrSpaceSize * bprom.outRdataWidth)
                        error::die(
                            "Invalid request packet: wrong length of ROM");

                    for (int addr = 0; addr < inAddrSpaceSize; addr++) {
                        for (int ibit = 0; ibit < bprom.outRdataWidth; ibit++) {
                            auto &rom = *get<TaskPlainROMUX>(
                                {bprom.name, {"rom", "all", ibit}});
                            assert(rom.size() == inAddrSpaceSize);
                            rom.set(addr,
                                    init.at(addr * bprom.outRdataWidth + ibit));
                        }
                    }
                }
                break;
            }

            case ROM_TYPE::MUX: {
                // Create ROM with MUX
                auto romnet = makeROMWithMUX<PlainNetworkBuilder>(
                    bprom.inAddrWidth, bprom.outRdataWidth);
                name2net_.emplace(bprom.name, romnet);

                // Set initial data
                if (auto it = reqPacket_.rom.find(bprom.name);
                    it != reqPacket_.rom.end()) {
                    std::vector<Bit> &init = it->second;
                    if (init.size() !=
                        (1 << bprom.inAddrWidth) * bprom.outRdataWidth)
                        error::die(
                            "Invalid request packet: wrong length of ROM");

                    for (size_t i = 0; i < init.size(); i++) {
                        auto &rom = *romnet->get<TaskPlainGateMem>(
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

    void overwriteParams(const Options &rhs)
    {
        pr_.overwrite(rhs);
    }

    void go()
    {
        pr_.print();

        // Make runner
        PlainNetworkRunner runner{pr_.numCPUWorkers};
        for (auto &&p : name2net_)
            runner.addNetwork(p.second);

        // Reset
        if (currentCycle_ == 0) {
            if (auto reset = maybeGetAt("input", "reset"); reset) {
                reset->set(1_b);
                runner.run();
                reset->set(0_b);
            }
        }

        // Get # of cycles
        int numCycles = pr_.numCycles;
        if (numCycles < 0)
            numCycles = std::numeric_limits<int>::max();

        // Go computing
        {
            auto finflag = maybeGetAt("output", "finflag");

            for (int i = 0; i < numCycles; i++, currentCycle_++) {
                using namespace utility;

                spdlog::info("#{}", currentCycle_ + 1);
                if (pr_.stdoutCSV)
                    std::cout << std::chrono::system_clock::now() << ",start,"
                              << currentCycle_ + 1 << std::endl;

                if (pr_.dumpPrefix)
                    writeToArchive(
                        utility::fok(*pr_.dumpPrefix, "-", currentCycle_),
                        makeResPacket(currentCycle_));

                auto duration = timeit([&] {
                    runner.tick();
                    if (currentCycle_ == 0)
                        setInitialRAM();
                    setCircularInputs(currentCycle_);
                    runner.run();
                });
                spdlog::info("\tdone. ({} us)", duration.count());
                if (pr_.stdoutCSV)
                    std::cout << std::chrono::system_clock::now() << ",end,"
                              << currentCycle_ + 1 << std::endl;

                if (finflag && finflag->get() == 1_b) {
                    spdlog::info("break.");
                    currentCycle_++;
                    break;
                }
            }
        }

        // Print the results
        PlainPacket resPacket = makeResPacket(currentCycle_);
        writeToArchive(pr_.outputFile, resPacket);
    }

    template <class Archive>
    void serialize(Archive &ar)
    {
        ar(pr_, name2net_, reqPacket_, currentCycle_);
    }
};

}  // namespace

void processAllGates(PlainNetwork &net, int numWorkers,
                     std::shared_ptr<ProgressGraphMaker> graph)
{
    ReadyQueue<PlainWorkerInfo> readyQueue;
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
    opt.print();

    std::optional<PlainFrontend> frontend;
    if (opt.resumeFile) {
        frontend.emplace();
        readFromArchive<PlainFrontend>(*frontend, *opt.resumeFile);
        frontend->overwriteParams(opt);
    }
    else {
        frontend.emplace(opt);
    }
    frontend->go();
    if (opt.snapshotFile)
        writeToArchive(*opt.snapshotFile, *frontend);
}

bool isSerializedPlainFrontend(const std::string &filepath)
{
    return isCorrectArchive<PlainFrontend>(filepath);
}
