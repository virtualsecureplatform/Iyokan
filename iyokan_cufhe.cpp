#include "iyokan_cufhe.hpp"
#include "packet.hpp"

namespace {

using Name2TFHEppNetMap =
    std::unordered_map<std::string, std::shared_ptr<TFHEppNetwork>>;
using Name2CUFHENetMap =
    std::unordered_map<std::string, std::shared_ptr<CUFHENetwork>>;

template <class T>
void merge(std::vector<T>& dst, const std::vector<T>& src)
{
    dst.reserve(dst.size() + src.size());
    std::copy(src.begin(), src.end(), std::back_inserter(dst));
}

std::shared_ptr<cufhe::PubKey> tfhepp2cufhe(const TFHEpp::GateKey& src)
{
    auto pubkey = std::make_shared<cufhe::PubKey>();

    // FIXME: Check if TFHEpp's parameters are the same as cuFHE's.
    auto cufheParams = cufhe::GetDefaultParam();
    const int32_t n = cufheParams->lwe_n_;
    const int32_t N = cufheParams->tlwe_n_;
    const int32_t k = cufheParams->tlwe_k_;
    const int32_t l = cufheParams->tgsw_decomp_size_;
    const int32_t ksk_t = cufheParams->keyswitching_decomp_size_;
    const int32_t ksk_n = N * k;
    const int32_t ksk_base = (1 << cufheParams->keyswitching_decomp_bits_);

    // Read the bootstrapping key.
    for (int p = 0; p < n; p++) {
        const TFHEpp::TRGSWFFTlvl1& trgswfft = src.bkfftlvl01[p];
        for (int q = 0; q < (k + 1) * l; q++) {
            for (int r = 0; r < (k + 1); r++) {
                TFHEpp::Polynomiallvl1 poly;
                TFHEpp::TwistFFTlvl1(poly, trgswfft[q][r]);
                for (int s = 0; s < N; s++) {
                    int index = ((p * ((k + 1) * l) + q) * (k + 1) + r) * N + s;
                    pubkey->bk_->data()[index] = poly[s];
                }
            }
        }
    }

    // Read the key switch key.
    for (int p = 0; p < ksk_n; p++) {
        for (int q = 0; q < ksk_t; q++) {
            // r = 0
            {
                cufhe::LWESample to = pubkey->ksk_->ExtractLWESample(
                    pubkey->ksk_->GetLWESampleIndex(p, q, 0));
                for (int s = 0; s < n; s++)
                    to.data()[s] = 0;
                to.data()[n] = 0;
            }
            // r >= 1
            for (int r = 1; r < ksk_base; r++) {
                assert(static_cast<size_t>(p) < src.ksk.size());
                assert(static_cast<size_t>(q) < src.ksk[p].size());
                assert(static_cast<size_t>(r - 1) < src.ksk[p][q].size());

                const TFHEpp::TLWElvl0& from = src.ksk[p][q][r - 1];
                cufhe::LWESample to = pubkey->ksk_->ExtractLWESample(
                    pubkey->ksk_->GetLWESampleIndex(p, q, r));
                for (int s = 0; s < n; s++) {
                    assert(static_cast<size_t>(s) < from.size());
                    to.data()[s] = from[s];
                }
                to.data()[n] = from[n];
            }
        }
    }

    return pubkey;
}

struct CUFHENetworkWithTFHEpp {
    std::shared_ptr<CUFHENetwork> cufheNet;
    std::shared_ptr<TFHEppNetwork> tfheppNet;
    std::vector<std::shared_ptr<CUFHE2TFHEppBridge>> bridges0;
    std::vector<std::shared_ptr<TFHEpp2CUFHEBridge>> bridges1;
};

void capTFHEppNetWithCUFHEWIRE(CUFHENetworkWithTFHEpp& net)
{
    assert(net.tfheppNet);

    NetworkBuilderBase<TFHEppWorkerInfo> bt;
    NetworkBuilderBase<CUFHEWorkerInfo> bc;

    auto& brs0 = net.bridges0;
    auto& brs1 = net.bridges1;
    const auto& tfheppNamedMems = net.tfheppNet->getNamedMems();

    for (auto&& [name, mem] : tfheppNamedMems) {
        auto&& [kind, portName, portBit] = name;
        if (kind == "input") {
            /*
               CUFHE INPUT --> Bridge --> CUFHE2TFHEpp --> TFHEpp INPUT
            */
            // Create nodes
            auto tfheppINPUT = std::dynamic_pointer_cast<TaskTFHEppGate>(mem);
            assert(tfheppINPUT);
            auto cufheINPUT =
                bc.addINPUT<TaskCUFHEGateWIRE>(portName, portBit, false);
            auto cufhe2tfhepp =
                bt.emplaceTask<TaskCUFHE2TFHEpp>(NodeLabel{"cufhe2tfhepp", ""});
            // Connect nodes
            brs0.push_back(connectWithBridge(cufheINPUT, cufhe2tfhepp));
            tfheppINPUT->acceptOneMoreInput();
            connectTasks(cufhe2tfhepp, tfheppINPUT);
        }
        else if (kind == "output") {
            /*
                TFHEpp OUTPUT --> TFHEpp2CUFHE --> Bridge --> CUFHE OUTPUT
            */
            // Create nodes
            auto tfheppOUTPUT = std::dynamic_pointer_cast<TaskTFHEppGate>(mem);
            assert(tfheppOUTPUT);
            auto cufheOUTPUT =
                bc.addOUTPUT<TaskCUFHEGateWIRE>(portName, portBit, true);
            auto tfhepp2cufhe =
                bt.emplaceTask<TaskTFHEpp2CUFHE>(NodeLabel{"tfhepp2cufhe", ""});
            // Connect nodes
            connectTasks(tfheppOUTPUT, tfhepp2cufhe);
            brs1.push_back(connectWithBridge(tfhepp2cufhe, cufheOUTPUT));
        }
    }

    // merge networks
    *net.tfheppNet = net.tfheppNet->merge(std::move(bt));
    if (net.cufheNet)
        *net.cufheNet = net.cufheNet->merge(std::move(bc));
    else
        net.cufheNet = std::make_shared<CUFHENetwork>(std::move(bc));
}

void connectCUFHENetWithTFHEppNet(
    CUFHENetwork& cufhe, TFHEppNetwork& tfhepp,
    std::vector<
        std::shared_ptr<BridgeDepNode<CUFHEWorkerInfo, TFHEppWorkerInfo>>>&
        brCUFHE2TFHEpp,
    std::vector<
        std::shared_ptr<BridgeDepNode<TFHEppWorkerInfo, CUFHEWorkerInfo>>>&
        brTFHEpp2CUFHE,
    const std::vector<std::tuple<std::string, int, std::string, int>>& lhs2rhs,
    const std::vector<std::tuple<std::string, int, std::string, int>>& rhs2lhs)
{
    for (auto&& [lPortName, lPortBit, rPortName, rPortBit] : lhs2rhs) {
        /*
           CUFHE OUTPUT --> Bridge --> CUFHE2TFHEpp --> TFHEpp INPUT
        */
        auto in = cufhe.get<TaskCUFHEGate>("output", lPortName, lPortBit);
        auto out = tfhepp.get<TaskTFHEppGate>("input", rPortName, rPortBit);
        assert(in);
        assert(out);

        // Add the task cufhe2tfhepp to the network tfhepp.
        NetworkBuilderBase<TFHEppWorkerInfo> b;
        auto cufhe2tfhepp = std::make_shared<TaskCUFHE2TFHEpp>();
        b.addTask(NodeLabel{"cufhe2tfhepp", ""}, cufhe2tfhepp);
        TFHEppNetwork net = std::move(b);
        tfhepp = tfhepp.merge(net);

        // CUFHE OUTPUT --> Bridge --> CUFHE2TFHEpp
        brCUFHE2TFHEpp.push_back(connectWithBridge(in, cufhe2tfhepp));

        // CUFHE2TFHEpp --> TFHEpp INPUT
        out->acceptOneMoreInput();
        connectTasks(cufhe2tfhepp, out);
    }

    for (auto&& [rPortName, rPortBit, lPortName, lPortBit] : rhs2lhs) {
        /*
           TFHEpp OUTPUT --> TFHEpp2CUFHE --> Bridge --> CUFHE INPUT
        */
        auto in = tfhepp.get<TaskTFHEppGate>("output", rPortName, rPortBit);
        auto out = cufhe.get<TaskCUFHEGate>("input", lPortName, lPortBit);
        assert(in);
        assert(out);

        // Add the task tfhepp2cufhe to the network tfhepp.
        NetworkBuilderBase<TFHEppWorkerInfo> b;
        auto tfhepp2cufhe = std::make_shared<TaskTFHEpp2CUFHE>();
        b.addTask(NodeLabel{"tfhepp2cufhe", ""}, tfhepp2cufhe);
        TFHEppNetwork net = std::move(b);
        tfhepp = tfhepp.merge(net);

        // TFHEpp OUTPUT --> TFHEpp2CUFHE
        connectTasks(in, tfhepp2cufhe);

        // TFHEpp2CUFHE --> Bridge --> CUFHE INPUT
        out->acceptOneMoreInput();
        brTFHEpp2CUFHE.push_back(connectWithBridge(tfhepp2cufhe, out));
    }
}

void makeTFHEppRAMNetworkForCUFHEImpl(
    NetworkBuilderBase<CUFHEWorkerInfo>& bc,
    NetworkBuilderBase<TFHEppWorkerInfo>& bt,
    std::vector<
        std::shared_ptr<BridgeDepNode<CUFHEWorkerInfo, TFHEppWorkerInfo>>>&
        bridge0,
    std::vector<
        std::shared_ptr<BridgeDepNode<TFHEppWorkerInfo, CUFHEWorkerInfo>>>&
        bridge1,
    const std::string& ramPortName, int indexByte)
{
    /*
                   +------+
                   | CB   |
        ADDR[0] -- | with |---+
                   | Inv  |   |
                   +------+   |
                              |   +-+
                   +------+   +---| |
                   | CB   |       | |
        ADDR[1] -- | with |-------| |
                   | Inv  |       | |
                   +------+       | +-->B
                                  | |
            ...


        ===========================================================================

                                  +------+
              +-----+   WDATA ->--+ MUX  |
              | RAM |             | woSE |--->A
        B-> --+ UX  +-- SEI --+---+      |
              +-----+         |   +------+
                              |      ^
                              |      +--- Mem
                              |          Write
                              +--> RDATA

        ===========================================================================

               +-------+   +--------+    B          +-----------+   B
        B-> ---+ CMUXs |   | TFHEpp |    r          | Gate      |   r
               |  [0]  +---|   2    | -- i -- SEI --| Boot-     |-- i -- Setter
        A-> ---+       |   | cuFHE  |    d          | strapping |   d
               +-------+   +--------+    g          +-----------+   g
                                         e                          e

               +-------+   +--------+    B          +-----------+   B
        B-> ---+ CMUXs |   | TFHEpp |    r          | Gate      |   r
               |  [1]  +---|   2    | -- i -- SEI --| Boot-     |-- i -- Setter
        A-> ---+       |   | cuFHE  |    d          | strapping |   d
               +-------+   +--------+    g          +-----------+   g
                                         e                          e

                  ...

    */

    // Create inputs and CBs.
    std::vector<std::shared_ptr<TaskTFHEppCBWithInv>> cbs;
    for (size_t i = 0; i < TaskCUFHERAMUX::ADDRESS_BIT; i++) {
        auto taskINPUT = bt.getTask<TaskTFHEppGateWIRE>("input", "addr", i);
        auto taskCB = std::make_shared<TaskTFHEppCBWithInv>();
        bt.addTask(NodeLabel{"CBWithInv", utility::fok("[", i, "]")}, taskCB);
        connectTasks(taskINPUT, taskCB);
        cbs.push_back(taskCB);
    }

    // Create RAMUX.
    auto taskRAMUX = bt.emplaceTask<TaskCUFHERAMUX>(NodeLabel{"RAMUX", ""});
    bt.registerTask("ram", ramPortName, indexByte, taskRAMUX);

    // Connect CBs and RAMUX.
    for (auto&& cb : cbs)
        connectTasks(cb, taskRAMUX);

    // Create SEIs and connect with CBs.
    auto taskSEI0 = std::make_shared<TaskTFHEppSEI>(0);
    bt.addTask(NodeLabel{"SEI", "[0]"}, taskSEI0);
    connectTasks(taskRAMUX, taskSEI0);

    // Create output for read-out data and connect.
    auto taskOutputReadData =
        bt.getTask<TaskTFHEppGateWIRE>("output", "rdata", indexByte);
    connectTasks(taskSEI0, taskOutputReadData);

    // Create input for write-in data.
    auto taskInputWriteData =
        bt.getTask<TaskTFHEppGateWIRE>("input", "wdata", indexByte);
    auto taskInputWriteEnabled =
        bt.getTask<TaskTFHEppGateWIRE>("input", "wren", 0);

    // Create MUXWoSE and connect.
    auto taskMUXWoSE = std::make_shared<TaskTFHEppGateMUXWoSE>();
    bt.addTask(NodeLabel{"MUXWoSE", ""}, taskMUXWoSE);
    connectTasks(taskSEI0, taskMUXWoSE);
    connectTasks(taskInputWriteData, taskMUXWoSE);
    connectTasks(taskInputWriteEnabled, taskMUXWoSE);

    // Create links of CMUXs -> SEI -> GateBootstrapping.
    for (int i = 0; i < (1 << TaskCUFHERAMUX::ADDRESS_BIT); i++) {
        // Create components...
        auto taskCMUXs = bt.emplaceTask<TaskTFHEppRAMCMUXsForCUFHE>(
            NodeLabel{"CMUXs", utility::fok("[", i, "]")}, taskRAMUX->get(i),
            i);

        auto taskSEIAndKS = bc.emplaceTask<TaskCUFHERAMSEIAndKS>(
            NodeLabel{"SEI&KS", utility::fok("[", i, "]")});

        auto taskGB = bc.emplaceTask<TaskCUFHERAMGateBootstrapping>(
            NodeLabel{"GB", utility::fok("[", i, "]")}, taskRAMUX->get(i));

        // ... and connect them.
        connectTasks(taskMUXWoSE, taskCMUXs);
        for (auto&& cb : cbs)
            connectTasks(cb, taskCMUXs);
        bridge1.push_back(connectWithBridge(taskCMUXs, taskSEIAndKS));
        connectTasks(taskSEIAndKS, taskGB);
    }
}

CUFHENetworkWithTFHEpp makeTFHEppRAMNetworkForCUFHE(
    const std::string& ramPortName)
{
    NetworkBuilderBase<TFHEppWorkerInfo> bt;
    NetworkBuilderBase<CUFHEWorkerInfo> bc;
    std::vector<std::shared_ptr<CUFHE2TFHEppBridge>> bridge0;
    std::vector<std::shared_ptr<TFHEpp2CUFHEBridge>> bridge1;

    // Inputs for address.
    for (size_t i = 0; i < TaskCUFHERAMUX::ADDRESS_BIT; i++)
        bt.addINPUT<TaskTFHEppGateWIRE>("addr", i, false);

    // Input for write-in flag.
    bt.addINPUT<TaskTFHEppGateWIRE>("wren", 0, false);

    for (int indexByte = 0; indexByte < 8; indexByte++) {
        // Input for data to write into RAM.
        bt.addINPUT<TaskTFHEppGateWIRE>("wdata", indexByte, false);
        // Output for data to be read from RAM.
        bt.addOUTPUT<TaskTFHEppGateWIRE>("rdata", indexByte, true);

        makeTFHEppRAMNetworkForCUFHEImpl(bc, bt, bridge0, bridge1, ramPortName,
                                         indexByte);
    }

    auto ret = CUFHENetworkWithTFHEpp{
        std::make_shared<CUFHENetwork>(std::move(bc)),
        std::make_shared<TFHEppNetwork>(std::move(bt)), bridge0, bridge1};
    capTFHEppNetWithCUFHEWIRE(ret);
    return ret;
}

CUFHENetworkWithTFHEpp makeTFHEppROMNetworkForCUFHE(size_t inAddrWidth,
                                                    size_t log2OutRdataWidth)
{
    auto ret = CUFHENetworkWithTFHEpp{
        nullptr,
        std::make_shared<TFHEppNetwork>(
            makeTFHEppROMNetwork(inAddrWidth, log2OutRdataWidth)),
        {},
        {}};
    capTFHEppNetWithCUFHEWIRE(ret);
    return ret;
}

class TaskCUFHEMux1bitROM : public TaskCUFHEGateWIRE {
public:
    TaskCUFHEMux1bitROM() : TaskCUFHEGateWIRE(false)
    {
    }
};

struct CUFHERunParameter {
    NetworkBlueprint blueprint;
    int numCPUWorkers, numGPUWorkers, numGPU, numCycles;
    std::string bkeyFile, inputFile, outputFile;
    bool stdoutCSV;

    // nullopt means to disable that option.
    std::optional<std::string> secretKey, dumpPrefix;

    CUFHERunParameter()
    {
    }

    CUFHERunParameter(const Options& opt)
    {
        blueprint = opt.blueprint.value();
        numCPUWorkers =
            opt.numCPUWorkers.value_or(std::thread::hardware_concurrency());
        numGPUWorkers = opt.numGPUWorkers.value_or(80 * 10);
        numGPU = opt.numGPU.value_or(1);
        numCycles = opt.numCycles.value_or(-1);
        bkeyFile = opt.bkeyFile.value();
        inputFile = opt.inputFile.value();
        outputFile = opt.outputFile.value();
        stdoutCSV = opt.stdoutCSV.value_or(false);

        dumpPrefix = opt.dumpPrefix;
        secretKey = opt.secretKey;
    }

    void overwrite(const Options& opt)
    {
#define OVERWRITE(name) \
    if (opt.name)       \
        name = *opt.name;
        OVERWRITE(blueprint);
        OVERWRITE(numCPUWorkers);
        OVERWRITE(numGPUWorkers);
        OVERWRITE(numGPU);
        OVERWRITE(numCycles);
        OVERWRITE(bkeyFile);
        OVERWRITE(inputFile);
        OVERWRITE(outputFile);
        OVERWRITE(stdoutCSV);
        OVERWRITE(dumpPrefix);
        OVERWRITE(secretKey);
#undef OVERWRITE
    }

    void print() const
    {
        spdlog::info("Run Parameters");
        spdlog::info("\tMode: cuFHE");
        spdlog::info("\tBlueprint: {}", blueprint.sourceFile());
        spdlog::info("\t# of CPU workers: {}", numCPUWorkers);
        spdlog::info("\t# of GPU workers: {}", numGPUWorkers);
        spdlog::info("\t# of GPUs: {}", numGPU);
        spdlog::info("\t# of cycles: {}", numCycles);
        spdlog::info("\tBKey file: {}", bkeyFile);
        spdlog::info("\tInput file (request packet): {}", inputFile);
        spdlog::info("\tOutput file (result packet): {}", outputFile);
        spdlog::info("\t--stdoutCSV: {}", stdoutCSV);
        spdlog::info("\t--secret-key: {}", secretKey.value_or("(none)"));
        spdlog::info("\t--dump-prefix: {}", dumpPrefix.value_or("(none)"));
    }

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(blueprint, numCPUWorkers, numGPUWorkers, numGPU, numCycles, bkeyFile,
           inputFile, outputFile, stdoutCSV, secretKey, dumpPrefix);
    }
};

class CUFHEFrontend {
private:
    CUFHERunParameter pr_;
    Name2TFHEppNetMap name2tnet_;
    Name2CUFHENetMap name2cnet_;
    std::vector<std::shared_ptr<CUFHE2TFHEppBridge>> bridges0_;
    std::vector<std::shared_ptr<TFHEpp2CUFHEBridge>> bridges1_;
    TFHEPacket reqPacket_;
    TFHEppBKey bkey_;
    int currentCycle_;
    bool cufheInitialized_;

private:
    // Don't allow to copy this object.
    CUFHEFrontend(const CUFHEFrontend&);
    CUFHEFrontend& operator=(const CUFHEFrontend&);

    template <class T = TaskCUFHEGate>
    std::shared_ptr<T> get(const blueprint::Port& port)
    {
        if constexpr (std::is_same_v<typename T::ParamWorkerInfo,
                                     CUFHEWorkerInfo>) {
            // CUFHE
            auto it = name2cnet_.find(port.nodeName);
            if (it == name2cnet_.end())
                error::die("Invalid network. Not found: ", port.nodeName);
            auto task = it->second->get_if<T>(port.portLabel);
            if (!task)
                error::die("Invalid network. Not found: ", port.nodeName, "/",
                           port.portLabel.portName, "[", port.portLabel.portBit,
                           "]");
            return task;
        }
        else {
            // TFHEpp
            auto it = name2tnet_.find(port.nodeName);
            if (it == name2tnet_.end())
                error::die("Invalid network. Not found: ", port.nodeName);
            auto task = it->second->get_if<T>(port.portLabel);
            if (!task)
                error::die("Invalid network. Not found: ", port.nodeName, "/",
                           port.portLabel.portName, "[", port.portLabel.portBit,
                           "]");
            return task;
        }
    }

    template <class T = TaskCUFHEGateMem>
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

    template <class T = TaskCUFHEGateMem>
    std::shared_ptr<T> maybeGet(const blueprint::Port& port)
    {
        if constexpr (std::is_same_v<typename T::ParamWorkerInfo,
                                     CUFHEWorkerInfo>) {
            // CUFHE
            auto it = name2cnet_.find(port.nodeName);
            if (it == name2cnet_.end())
                return nullptr;
            return it->second->get_if<T>(port.portLabel);
        }
        else {
            // TFHEpp
            auto it = name2tnet_.find(port.nodeName);
            if (it == name2tnet_.end())
                return nullptr;
            return it->second->get_if<T>(port.portLabel);
        }
    }

    template <class T = TaskCUFHEGateMem>
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
            bits.at(atPortBit) =
                cufhe2tfhepp(get<TaskCUFHEGateMem>(port)->get());
        }
        // Get values of RAM
        // FIXME: subset of RAMs?
        for (auto&& bp : pr_.blueprint.builtinRAMs()) {
            using RAM_TYPE = blueprint::BuiltinRAM::TYPE;
            switch (bp.type) {
            case RAM_TYPE::CMUX_MEMORY: {
                std::vector<TFHEpp::TRLWElvl1>& dst = resPacket.ram[bp.name];
                assert(dst.size() == 0);
                for (int bit = 0; bit < 8; bit++) {
                    auto& ram =
                        *get<TaskCUFHERAMUX>({bp.name, {"ram", "", bit}});
                    if (dst.size() == 0)
                        dst.resize(ram.size() * 8);
                    else
                        assert(ram.size() == dst.size() / 8);
                    for (size_t addr = 0; addr < ram.size(); addr++)
                        dst.at(addr * 8 + bit) = ram.get(addr)->trlwehost;
                }
                break;
            }

            case RAM_TYPE::MUX: {
                std::vector<TFHEpp::TLWElvl0>& dst =
                    resPacket.ramInTLWE[bp.name];
                for (size_t i = 0; i < (1 << bp.inAddrWidth) * bp.outRdataWidth;
                     i++) {
                    const auto& ram = *get<TaskCUFHEGateMem>(
                        {bp.name, {"ram", "ramdata", static_cast<int>(i)}});
                    dst.push_back(cufhe2tfhepp(ram.get()));
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
                    for (int bit = 0; bit < 8; bit++) {
                        auto& ram = *get<TaskCUFHERAMUX>(
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
                    const auto& init = it->second;
                    if (init.size() !=
                        (1 << bpram.inAddrWidth) * bpram.outRdataWidth)
                        error::die(
                            "Invalid request packet: wrong length of RAM");

                    for (size_t i = 0; i < init.size(); i++) {
                        auto& ram = *get<TaskCUFHEGateMem>(
                            {bpram.name,
                             {"ram", "ramdata", static_cast<int>(i)}});
                        ram.set(*::tfhepp2cufhe(init.at(i)));
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
            const auto& bits = it->second;  // Found input bit stream

            // Calculate the index in the bit stream for the port.
            size_t index =
                (pr_.blueprint.atPortWidths().at(atPortName) * currentCycle +
                 atPortBit) %
                bits.size();

            get<TaskCUFHEGateMem>(port)->set(*::tfhepp2cufhe(bits.at(index)));
        }
    }

    void mayDumpPacket(int currentCycle)
    {
        if (!pr_.dumpPrefix)
            return;
        auto sk = std::make_shared<TFHEpp::SecretKey>();
        readFromArchive(*sk, pr_.secretKey.value());
        PlainPacket packet = makeResPacket(currentCycle).decrypt(*sk);
        writeToArchive(utility::fok(*pr_.dumpPrefix, "-", currentCycle),
                       packet);
    }

    void initializeCUFHE()
    {
        assert(!cufheInitialized_);
        cufhe::SetGPUNum(pr_.numGPU);
        cufhe::SetSeed();
        cufhe::Initialize(*tfhepp2cufhe(*bkey_.gk));
        cufheInitialized_ = true;
    }

public:
    CUFHEFrontend() : cufheInitialized_(false)
    {
    }

    CUFHEFrontend(const Options& opt)
        : pr_(opt), currentCycle_(0), cufheInitialized_(false)
    {
        reqPacket_ = readFromArchive<TFHEPacket>(pr_.inputFile);
        bkey_ = readFromArchive<TFHEppBKey>(pr_.bkeyFile);

        // Prepare cuFHE
        initializeCUFHE();

        const NetworkBlueprint& bp = pr_.blueprint;

        // [[file]]
        for (const auto& file : bp.files())
            name2cnet_.emplace(file.name,
                               readNetwork<CUFHENetworkBuilder>(file));

        // [[builtin]] type = ram | type = mux-ram
        for (const auto& ram : bp.builtinRAMs()) {
            assert(ram.inAddrWidth == 8 && ram.inWdataWidth == 8 &&
                   ram.outRdataWidth == 8);

            using RAM_TYPE = blueprint::BuiltinRAM::TYPE;
            switch (ram.type) {
            case RAM_TYPE::CMUX_MEMORY: {
                auto net = makeTFHEppRAMNetworkForCUFHE("");
                name2cnet_.emplace(ram.name, net.cufheNet);
                name2tnet_.emplace(ram.name, net.tfheppNet);
                merge(bridges0_, net.bridges0);
                merge(bridges1_, net.bridges1);
                break;
            }

            case RAM_TYPE::MUX:
                auto net = makeRAMWithMUX<CUFHENetworkBuilder>(
                    ram.inAddrWidth, ram.outRdataWidth);
                name2cnet_.emplace(ram.name, net);
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

                auto net = makeTFHEppROMNetworkForCUFHE(
                    bprom.inAddrWidth, utility::log2(bprom.outRdataWidth));
                name2cnet_.emplace(bprom.name, net.cufheNet);
                name2tnet_.emplace(bprom.name, net.tfheppNet);
                merge(bridges0_, net.bridges0);
                merge(bridges1_, net.bridges1);

                // Set initial ROM data
                if (auto it = reqPacket_.rom.find(bprom.name);
                    it != reqPacket_.rom.end()) {
                    std::vector<TFHEpp::TRLWElvl1>& init = it->second;
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
                auto romnet = makeROMWithMUX<CUFHENetworkBuilder>(
                    bprom.inAddrWidth, bprom.outRdataWidth);
                name2cnet_.emplace(bprom.name, romnet);

                // Set initial data
                if (auto it = reqPacket_.romInTLWE.find(bprom.name);
                    it != reqPacket_.romInTLWE.end()) {
                    std::vector<TFHEpp::TLWElvl0>& init = it->second;
                    if (init.size() !=
                        (1 << bprom.inAddrWidth) * bprom.outRdataWidth)
                        error::die(
                            "Invalid request packet: wrong length of ROM");

                    for (size_t i = 0; i < init.size(); i++) {
                        auto& rom = *romnet->get<TaskCUFHEGateMem>(
                            {"rom", "romdata", static_cast<int>(i)});
                        rom.set(*::tfhepp2cufhe(init.at(i)));
                    }
                }

                break;
            }
            }
        }

        // Print gate counts
        for (auto&& [name, net] : name2tnet_) {
            GateCountVisitor vis;
            net->visit(vis);

            if (vis.kind2count().empty())
                continue;

            spdlog::debug("{} (TFHEpp) :", name);
            for (auto&& [kind, count] : vis.kind2count())
                spdlog::debug("\t{}\t{}", count, kind);
            spdlog::debug("");
        }
        for (auto&& [name, net] : name2cnet_) {
            GateCountVisitor vis;
            net->visit(vis);

            if (vis.kind2count().empty())
                continue;

            spdlog::debug("{} (cuFHE) :", name);
            for (auto&& [kind, count] : vis.kind2count())
                spdlog::debug("\t{}\t{}", count, kind);
            spdlog::debug("");
        }

        // [connect]
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
            for (auto&& p : name2tnet_)
                p.second->visit(grvis);
            for (auto&& p : name2cnet_)
                p.second->visit(grvis);
            PrioritySetVisitor privis{graph::doTopologicalSort(grvis.getMap())};
            for (auto&& p : name2tnet_)
                p.second->visit(privis);
            for (auto&& p : name2cnet_)
                p.second->visit(privis);
        }
    }

    ~CUFHEFrontend()
    {
        if (cufheInitialized_)
            cufhe::CleanUp();
    }

    void overwriteParams(const Options& rhs)
    {
        pr_.overwrite(rhs);
    }

    void go()
    {
        pr_.print();

        // Check bkey is correct.
        if (!bkey_.gk || (pr_.blueprint.needsCircuitKey() && !bkey_.ck))
            error::die("Invalid bootstrapping key");

        // Make runner
        CUFHENetworkRunner runner{
            pr_.numGPUWorkers, pr_.numCPUWorkers,
            TFHEppWorkerInfo{TFHEpp::lweParams{}, bkey_.gk, bkey_.ck}};
        for (auto&& p : name2cnet_)
            runner.addNetwork(p.second);
        for (auto&& p : name2tnet_)
            runner.addNetwork(p.second);
        for (auto&& bridge0 : bridges0_)
            runner.addBridge(bridge0);
        for (auto&& bridge1 : bridges1_)
            runner.addBridge(bridge1);

        // Reset
        if (currentCycle_ == 0) {
            if (auto reset = maybeGetAt("input", "reset"); reset) {
                cufhe::Ctxt one, zero;
                cufhe::ConstantOne(one);
                cufhe::ConstantZero(zero);
                cufhe::Synchronize();

                reset->set(one);
                runner.run();
                reset->set(zero);
            }
        }

        // Go computing
        for (int i = 0; i < pr_.numCycles; i++, currentCycle_++) {
            using namespace utility;

            spdlog::info("#{}", currentCycle_ + 1);
            if (pr_.stdoutCSV)
                std::cout << std::chrono::system_clock::now() << ",start,"
                          << currentCycle_ + 1 << std::endl;

            mayDumpPacket(currentCycle_);

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
        }

        // Dump result packet
        TFHEPacket resPacket = makeResPacket(currentCycle_);
        writeToArchive(pr_.outputFile, resPacket);
    }

    template <class Archive>
    void load(Archive& ar)
    {
        ar(pr_, reqPacket_);
        bkey_ = readFromArchive<TFHEppBKey>(pr_.bkeyFile);
        initializeCUFHE();
        ar(name2tnet_, name2cnet_, bridges0_, bridges1_, currentCycle_);
    }

    template <class Archive>
    void save(Archive& ar) const
    {
        ar(pr_, reqPacket_);
        ar(name2tnet_, name2cnet_, bridges0_, bridges1_, currentCycle_);
    }
};

}  // namespace

void processAllGates(CUFHENetwork& net, int numWorkers,
                     std::shared_ptr<ProgressGraphMaker> graph)
{
    ReadyQueue<CUFHEWorkerInfo> readyQueue;
    net.pushReadyTasks(readyQueue);

    // Create workers.
    size_t numFinishedTargets = 0;
    std::vector<CUFHEWorker> workers;
    for (int i = 0; i < numWorkers; i++)
        workers.emplace_back(readyQueue, numFinishedTargets, graph);

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

void doCUFHE(const Options& opt)
{
    opt.print();

    std::optional<CUFHEFrontend> frontend;
    if (opt.resumeFile) {
        frontend.emplace();
        readFromArchive<CUFHEFrontend>(*frontend, *opt.resumeFile);
        frontend->overwriteParams(opt);
    }
    else {
        frontend.emplace(opt);
    }
    frontend->go();
    if (opt.snapshotFile)
        writeToArchive(*opt.snapshotFile, *frontend);
}

bool isSerializedCUFHEFrontend(const std::string& path)
{
    return isCorrectArchive<CUFHEFrontend>(path);
}
