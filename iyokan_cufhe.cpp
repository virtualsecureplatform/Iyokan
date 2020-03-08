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
            auto cufheINPUT = bc.addINPUT<TaskCUFHEGateWIRE>(
                detail::genid(), portName, portBit, false);
            auto cufhe2tfhepp = bt.emplaceTask<TaskCUFHE2TFHEpp>(
                NodeLabel{detail::genid(), "cufhe2tfhepp", ""});
            // Connect nodes
            brs0.push_back(connectWithBridge(cufheINPUT, cufhe2tfhepp));
            tfheppINPUT->acceptOneMoreInput();
            bt.connectTasks(cufhe2tfhepp, tfheppINPUT);
        }
        else if (kind == "output") {
            /*
                TFHEpp OUTPUT --> TFHEpp2CUFHE --> Bridge --> CUFHE OUTPUT
            */
            // Create nodes
            auto tfheppOUTPUT = std::dynamic_pointer_cast<TaskTFHEppGate>(mem);
            assert(tfheppOUTPUT);
            auto cufheOUTPUT = bc.addOUTPUT<TaskCUFHEGateWIRE>(
                detail::genid(), portName, portBit, true);
            auto tfhepp2cufhe = bt.emplaceTask<TaskTFHEpp2CUFHE>(
                NodeLabel{detail::genid(), "tfhepp2cufhe", ""});
            // Connect nodes
            bt.connectTasks(tfheppOUTPUT, tfhepp2cufhe);
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
        b.addTask(NodeLabel{detail::genid(), "cufhe2tfhepp", ""}, cufhe2tfhepp);
        TFHEppNetwork net = std::move(b);
        tfhepp = tfhepp.merge(net);

        // CUFHE OUTPUT --> Bridge --> CUFHE2TFHEpp
        brCUFHE2TFHEpp.push_back(connectWithBridge(in, cufhe2tfhepp));

        // CUFHE2TFHEpp --> TFHEpp INPUT
        out->acceptOneMoreInput();
        NetworkBuilderBase<TFHEppWorkerInfo>::connectTasks(cufhe2tfhepp, out);
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
        b.addTask(NodeLabel{detail::genid(), "tfhepp2cufhe", ""}, tfhepp2cufhe);
        TFHEppNetwork net = std::move(b);
        tfhepp = tfhepp.merge(net);

        // TFHEpp OUTPUT --> TFHEpp2CUFHE
        NetworkBuilderBase<TFHEppWorkerInfo>::connectTasks(in, tfhepp2cufhe);

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
               |  [0]  +---|   2    | -- i -- SEI --| Boot-     |-- i -- Setter
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
        bt.addTask(
            NodeLabel{detail::genid(), "CBWithInv", utility::fok("[", i, "]")},
            taskCB);
        bt.connectTasks(taskINPUT, taskCB);
        cbs.push_back(taskCB);
    }

    // Create RAMUX.
    auto taskRAMUX =
        bt.emplaceTask<TaskCUFHERAMUX>(NodeLabel{detail::genid(), "RAMUX", ""});
    bt.registerTask("ram", ramPortName, indexByte, taskRAMUX);

    // Connect CBs and RAMUX.
    for (auto&& cb : cbs)
        bt.connectTasks(cb, taskRAMUX);

    // Create SEIs and connect with CBs.
    auto taskSEI0 = std::make_shared<TaskTFHEppSEI>(0);
    bt.addTask(NodeLabel{detail::genid(), "SEI", "[0]"}, taskSEI0);
    bt.connectTasks(taskRAMUX, taskSEI0);

    // Create output for read-out data and connect.
    auto taskOutputReadData =
        bt.getTask<TaskTFHEppGateWIRE>("output", "rdata", indexByte);
    bt.connectTasks(taskSEI0, taskOutputReadData);

    // Create input for write-in data.
    auto taskInputWriteData =
        bt.getTask<TaskTFHEppGateWIRE>("input", "wdata", indexByte);
    auto taskInputWriteEnabled =
        bt.getTask<TaskTFHEppGateWIRE>("input", "wren", 0);

    // Create MUXWoSE and connect.
    auto taskMUXWoSE = std::make_shared<TaskTFHEppGateMUXWoSE>();
    bt.addTask(NodeLabel{detail::genid(), "MUXWoSE", ""}, taskMUXWoSE);
    bt.connectTasks(taskSEI0, taskMUXWoSE);
    bt.connectTasks(taskInputWriteData, taskMUXWoSE);
    bt.connectTasks(taskInputWriteEnabled, taskMUXWoSE);

    // Create links of CMUXs -> SEI -> GateBootstrapping.
    for (int i = 0; i < (1 << TaskCUFHERAMUX::ADDRESS_BIT); i++) {
        // Create components...
        auto taskCMUXs = bt.emplaceTask<TaskTFHEppRAMCMUXsForCUFHE>(
            NodeLabel{detail::genid(), "CMUXs", utility::fok("[", i, "]")},
            taskRAMUX->get(i).trlwehost, i);

        auto taskSEIAndKS = bc.emplaceTask<TaskCUFHERAMSEIAndKS>(
            NodeLabel{detail::genid(), "SEI&KS", utility::fok("[", i, "]")});

        auto taskGB = bc.emplaceTask<TaskCUFHERAMGateBootstrapping>(
            NodeLabel{detail::genid(), "GB", utility::fok("[", i, "]")},
            taskRAMUX->get(i));

        // ... and connect them.
        bt.connectTasks(taskMUXWoSE, taskCMUXs);
        for (auto&& cb : cbs)
            bt.connectTasks(cb, taskCMUXs);
        bridge1.push_back(connectWithBridge(taskCMUXs, taskSEIAndKS));
        bc.connectTasks(taskSEIAndKS, taskGB);
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
        bt.addINPUT<TaskTFHEppGateWIRE>(detail::genid(), "addr", i, false);

    // Input for write-in flag.
    bt.addINPUT<TaskTFHEppGateWIRE>(detail::genid(), "wren", 0, false);

    for (int indexByte = 0; indexByte < 8; indexByte++) {
        // Input for data to write into RAM.
        bt.addINPUT<TaskTFHEppGateWIRE>(detail::genid(), "wdata", indexByte,
                                        false);
        // Output for data to be read from RAM.
        bt.addOUTPUT<TaskTFHEppGateWIRE>(detail::genid(), "rdata", indexByte,
                                         true);

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

class CUFHEFrontend {
private:
    Name2TFHEppNetMap name2tnet_;
    Name2CUFHENetMap name2cnet_;
    std::vector<std::shared_ptr<CUFHE2TFHEppBridge>> bridges0_;
    std::vector<std::shared_ptr<TFHEpp2CUFHEBridge>> bridges1_;
    const Options& opt_;
    TFHEPacket reqPacket_;

private:
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
        for (auto&& [key, port] : opt_.blueprint->atPorts()) {
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
        for (auto&& bp : opt_.blueprint->builtinRAMs()) {
            std::vector<TFHEpp::TRLWElvl1>& dst = resPacket.ram[bp.name];
            assert(dst.size() == 0);
            for (int bit = 0; bit < 8; bit++) {
                auto& ram = *get<TaskCUFHERAMUX>({bp.name, {"ram", "", bit}});
                if (dst.size() == 0)
                    dst.resize(ram.size() * 8);
                else
                    assert(ram.size() == dst.size() / 8);
                for (size_t addr = 0; addr < ram.size(); addr++)
                    dst.at(addr * 8 + bit) = ram.get(addr).trlwehost;
            }
        }

        return resPacket;
    }

    void setInitialRAM()
    {
        for (auto&& [name, init] : reqPacket_.ram) {
            for (int bit = 0; bit < 8; bit++) {
                auto& ram = *get<TaskCUFHERAMUX>({name, {"ram", "", bit}});
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
    CUFHEFrontend(const Options& opt)
        : opt_(opt), reqPacket_(readFromArchive<TFHEPacket>(opt.inputFile))
    {
        assert(opt.blueprint);
    }

    void go()
    {
        const NetworkBlueprint& bp = *opt_.blueprint;

        // Prepare cuFHE
        cufhe::SetGPUNum(1);
        cufhe::SetSeed();
        cufhe::Initialize(*tfhepp2cufhe(*reqPacket_.gk));

        // [[file]]
        for (const auto& file : bp.files())
            name2cnet_.emplace(file.name,
                               readNetwork<CUFHENetworkBuilder>(file));

        // [[builtin]] type = ram
        for (const auto& ram : bp.builtinRAMs()) {
            assert(reqPacket_.ck);
            assert(ram.inAddrWidth == 8 && ram.inWdataWidth == 8 &&
                   ram.outRdataWidth == 8);

            auto net = makeTFHEppRAMNetworkForCUFHE("");
            name2cnet_.emplace(ram.name, net.cufheNet);
            name2tnet_.emplace(ram.name, net.tfheppNet);
            merge(bridges0_, net.bridges0);
            merge(bridges1_, net.bridges1);
        }

        // [[builtin]] type = rom
        for (const auto& bprom : bp.builtinROMs()) {
            assert(reqPacket_.ck);

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
                    error::die("Invalid request packet: wrong length of ROM");

                for (size_t i = 0; i < rom.size(); i++)
                    rom.set(i, init.at(i));
            }
        }

        // [connect]
        for (const auto& [src, dst] : bp.edges()) {
            assert(src.portLabel.kind == "output");
            assert(dst.portLabel.kind == "input");
            auto srcTask = get(src);
            auto dstTask = get(dst);
            dstTask->acceptOneMoreInput();
            NetworkBuilderBase<cufhe::Ctxt>::connectTasks(srcTask, dstTask);
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

        // Make runner
        CUFHENetworkRunner runner{
            opt_.numGPUWorkers, opt_.numCPUWorkers,
            TFHEppWorkerInfo{TFHEpp::lweParams{}, reqPacket_.gk,
                             reqPacket_.ck}};
        for (auto&& p : name2cnet_)
            runner.addNetwork(p.second);
        for (auto&& p : name2tnet_)
            runner.addNetwork(p.second);
        for (auto&& bridge0 : bridges0_)
            runner.addBridge(bridge0);
        for (auto&& bridge1 : bridges1_)
            runner.addBridge(bridge1);

        // Reset
        {
            cufhe::Ctxt one, zero;
            cufhe::ConstantOne(one);
            cufhe::ConstantZero(zero);
            cufhe::Synchronize();

            auto& reset = *get_at("input", "reset");
            reset.set(one);
            runner.run();
            reset.set(zero);
        }

        // Go computing
        {
            std::stringstream devnull;
            std::ostream& os = opt_.quiet ? devnull : std::cout;

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

        // Clean cuFHE up
        cufhe::CleanUp();
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
    CUFHEFrontend{opt}.go();
}
