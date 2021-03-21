#include "iyokan_cufhe2.hpp"
#include "packet.hpp"

#include <iostream>

namespace {
std::shared_ptr<cufhe::Ctxt> tfhepp2cufhe(const TFHEpp::TLWElvl0& src)
{
    // FIXME: Check if TFHEpp's parameters are the same as cuFHE's.
    const int32_t n = cufhe::GetDefaultParam()->lwe_n_;

    auto ctxt = std::make_shared<cufhe::Ctxt>();
    for (int i = 0; i < n + 1; i++)
        ctxt->lwe_sample_->data()[i] = src[i];

    return ctxt;
}

inline TFHEpp::TLWElvl0 cufhe2tfhepp(const cufhe::Ctxt& src)
{
    // FIXME: Check if TFHEpp's parameters are the same as cuFHE's.
    TFHEpp::TLWElvl0 tlwe;
    constexpr size_t n = tlwe.size();
    for (int i = 0; i < n + 1; i++)
        tlwe[i] = src.lwe_sample_->data()[i];

    return tlwe;
}

template <class NetworkBuilder>
void makeROMWithMUX_CUFHE2(NetworkBuilder& b, int inAddrWidth,
                           int outRdataWidth)
{
    // Create inputs
    std::vector<int> addrInputs;
    for (int i = 0; i < inAddrWidth; i++) {
        int id = b.INPUT("addr", i);
        addrInputs.push_back(id);
    }

    // Create 1bitROMs
    for (int i = 0; i < outRdataWidth; i++) {
        make1bitROMWithMUX(b, addrInputs, outRdataWidth, i);
    }
}

template <class NetworkBuilder>
void makeRAMWithMUX_CUFHE2(NetworkBuilder& b, int inAddrWidth, int dataWidth)
{
    b.setRAMDataWidth(dataWidth);

#define USE_PRECOMPILED_BINARY(addrW, dataW)                              \
    if (inAddrWidth == addrW && dataWidth == dataW) {                     \
        std::stringstream ss{std::string{                                 \
            _binary_mux_ram_##addrW##_##dataW##_##dataW##_min_json_start, \
            _binary_mux_ram_##addrW##_##dataW##_##dataW##_min_json_end}}; \
        readNetworkFromJSONImpl(b, ss);                                   \
        return;                                                           \
    }
    USE_PRECOMPILED_BINARY(8, 8);
    USE_PRECOMPILED_BINARY(8, 16);
    USE_PRECOMPILED_BINARY(9, 16);
#undef USE_PRECOMPILED_BINARY

    // Create inputs
    std::vector<int> addrInputs;
    for (int i = 0; i < inAddrWidth; i++) {
        int id = b.INPUT("addr", i);
        addrInputs.push_back(id);
    }
    int wrenInput = b.INPUT("wren", 0);

    // Create 1bitRAMs
    for (int i = 0; i < dataWidth; i++) {
        make1bitRAMWithMUX(b, addrInputs, wrenInput, i);
    }

    return;
}

struct CUFHE2RunParameter {
    NetworkBlueprint blueprint;
    int numCPUWorkers, numGPUWorkers, numGPU, numCycles;
    std::string bkeyFile, inputFile, outputFile;

    CUFHE2RunParameter()
    {
    }

    CUFHE2RunParameter(const Options& opt)
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
    }

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(blueprint, numCPUWorkers, numGPUWorkers, numGPU, numCycles, bkeyFile,
           inputFile, outputFile);
    }
};

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

class CUDAGraphBuilder {
private:
    cudaGraph_t graph_;

public:
    CUDAGraphBuilder()
    {
        cudaGraphCreate(&graph_, 0);
    }

    void addDep(cudaGraphNode_t from, cudaGraphNode_t to)
    {
        cudaGraphAddDependencies(graph_, &from, &to, 1);
    }

    template <class Fun>
    cudaGraphNode_t addKernel(const std::vector<cudaGraphNode_t>& deps, Fun fun)
    {
        cufhe::Stream stream;
        stream.Create();

        cudaGraph_t emb_graph;
        cudaStreamBeginCapture(stream.st(), cudaStreamCaptureModeGlobal);
        fun(stream);
        cudaStreamEndCapture(stream.st(), &emb_graph);
        cudaGraphNode_t node;
        cudaGraphAddChildGraphNode(&node, graph_, deps.data(), deps.size(),
                                   emb_graph);

        stream.Destroy();
        return node;
    }

    template <class Fun>
    cudaGraphNode_t addKernel(Fun&& fun)
    {
        return addKernel({}, std::forward<Fun>(fun));
    }

    cudaGraphExec_t instantiate()
    {
        cudaGraphExec_t instance;
        cudaGraphInstantiate(&instance, graph_, NULL, NULL, 0);
        return instance;
    }
};

struct GateInfo {
    enum class Kind {
        AND,
        NAND,
        ANDNOT,
        OR,
        NOR,
        ORNOT,
        XOR,
        XNOR,
        MUX,
        NOT,
        DFF,
        INPUT,
        OUTPUT,
        RAM,
        ROM,
    } kind;
    std::vector<int> in;
};

class CUFHE2NetworkBuilder {
    friend class CUFHE2Network;

private:
    std::vector<GateInfo> gates_;
    std::string currentNodeName_;
    int currentRAMDataWidth_;
    std::map<std::tuple<std::string, std::string, int>, int> name2id_;

public:
    CUFHE2NetworkBuilder()
    {
    }

    void setNodeName(const std::string& nodeName)
    {
        currentNodeName_ = nodeName;
    }

    void setRAMDataWidth(int w)
    {
        currentRAMDataWidth_ = w;
    }

    void connectPorts(const blueprint::Port& src, const blueprint::Port& dst)
    {
        int src_id = name2id_.at(std::make_tuple(
            src.nodeName, src.portLabel.portName, src.portLabel.portBit));
        int dst_id = name2id_.at(std::make_tuple(
            dst.nodeName, dst.portLabel.portName, dst.portLabel.portBit));
        gates_.at(dst_id).in.push_back(src_id);
    }

    void connect(int from, int to)
    {
        gates_.at(to).in.push_back(from);
    }

#define DEFINE_GATE(name)                                     \
    int name()                                                \
    {                                                         \
        gates_.push_back(GateInfo{GateInfo::Kind::name, {}}); \
        return gates_.size() - 1;                             \
    }
    DEFINE_GATE(AND);
    DEFINE_GATE(NAND);
    DEFINE_GATE(ANDNOT);
    DEFINE_GATE(OR);
    DEFINE_GATE(NOR);
    DEFINE_GATE(ORNOT);
    DEFINE_GATE(XOR);
    DEFINE_GATE(XNOR);
    DEFINE_GATE(MUX);
    DEFINE_GATE(NOT);
    DEFINE_GATE(DFF);
#undef DEFINE_GATE

#define DEFINE_NAMED_GATE(name)                                           \
    int name(const std::string& portName, int portBit)                    \
    {                                                                     \
        gates_.push_back(GateInfo{GateInfo::Kind::name, {}});             \
        int id = gates_.size() - 1;                                       \
        auto name = std::make_tuple(currentNodeName_, portName, portBit); \
        name2id_.emplace(name, id);                                       \
        return id;                                                        \
    }
    DEFINE_NAMED_GATE(INPUT);
    DEFINE_NAMED_GATE(OUTPUT);
    DEFINE_NAMED_GATE(ROM);
#undef DEFINE_NAMED_GATE

    int RAM(int addr, int bit)
    {
        gates_.push_back(GateInfo{GateInfo::Kind::RAM, {}});
        int id = gates_.size() - 1;
        auto name = std::make_tuple(currentNodeName_, "ramdata",
                                    addr * currentRAMDataWidth_ + bit);
        name2id_.emplace(name, id);
        return id;
    }
};  // namespace

class CUFHE2Network {
private:
    CUFHE2Stream stream_;
    cudaGraphExec_t graphInst_;
    std::vector<std::shared_ptr<cufhe::Ctxt>> ctxts_;
    std::vector<std::optional<cudaGraphNode_t>> cudaNodes_;
    std::map<std::tuple<std::string, std::string, std::string, int>, int>
        name2id_;
    std::vector<int> dff_ids_;
    std::vector<GateInfo> gates_;

public:
    CUFHE2Network()
    {
    }

    CUFHE2Network(CUFHE2NetworkBuilder&& builder) : gates_(builder.gates_)
    {
        CUDAGraphBuilder cuda;
        const size_t nGates = gates_.size();

        ctxts_.reserve(nGates);
        for (auto&& g : gates_)
            ctxts_.emplace_back(std::make_shared<cufhe::Ctxt>());

        // Add nodes
        cudaNodes_.resize(nGates);
        for (size_t id = 0; id < nGates; id++) {
            auto& gate = gates_.at(id);
            switch (gate.kind) {
#define CASE_GATE_2IN_1OUT(name, func)             \
    case GateInfo::Kind::name: {                   \
        auto node = cuda.addKernel([&](auto&& s) { \
            assert(gate.in.size() == 2);           \
            auto& in0 = *ctxts_.at(gate.in[0]);    \
            auto& in1 = *ctxts_.at(gate.in[1]);    \
            auto& out = *ctxts_.at(id);            \
            func(out, in0, in1, s);                \
        });                                        \
        cudaNodes_.at(id).emplace(node);           \
        break;                                     \
    }
                CASE_GATE_2IN_1OUT(AND, cufhe::And);
                CASE_GATE_2IN_1OUT(NAND, cufhe::Nand);
                CASE_GATE_2IN_1OUT(ANDNOT, cufhe::AndYN);
                CASE_GATE_2IN_1OUT(OR, cufhe::Or);
                CASE_GATE_2IN_1OUT(NOR, cufhe::Nor);
                CASE_GATE_2IN_1OUT(ORNOT, cufhe::OrYN);
                CASE_GATE_2IN_1OUT(XOR, cufhe::Xor);
                CASE_GATE_2IN_1OUT(XNOR, cufhe::Xnor);
#undef CASE_GATE_2IN_1OUT

            case GateInfo::Kind::MUX: {
                auto node = cuda.addKernel([&](auto&& s) {
                    assert(gate.in.size() == 3);
                    auto& in0 = *ctxts_.at(gate.in[0]);
                    auto& in1 = *ctxts_.at(gate.in[1]);
                    auto& in2 = *ctxts_.at(gate.in[2]);
                    auto& out = *ctxts_.at(id);
                    cufhe::Mux(out, in2, in1, in0, s);
                });
                cudaNodes_.at(id).emplace(node);
                break;
            }

            case GateInfo::Kind::NOT: {
                auto node = cuda.addKernel([&](auto&& s) {
                    assert(gate.in.size() == 1);
                    auto& in0 = *ctxts_.at(gate.in[0]);
                    auto& out = *ctxts_.at(id);
                    cufhe::Not(out, in0, s);
                });
                cudaNodes_.at(id).emplace(node);
                break;
            }

            case GateInfo::Kind::DFF:
                assert(gate.in.size() == 1);
                dff_ids_.push_back(id);
                break;

            case GateInfo::Kind::INPUT:
                assert(gate.in.size() == 0 || gate.in.size() == 1);
                if (gate.in.size() == 1) {
                    auto node = cuda.addKernel([&](auto&& s) {
                        auto& in0 = *ctxts_.at(gate.in[0]);
                        auto& out = *ctxts_.at(id);
                        cufhe::Copy(out, in0, s);
                    });
                    cudaNodes_.at(id).emplace(node);
                }
                break;

            case GateInfo::Kind::OUTPUT: {
                auto node = cuda.addKernel([&](auto&& s) {
                    assert(gate.in.size() == 1);
                    auto& in0 = *ctxts_.at(gate.in[0]);
                    auto& out = *ctxts_.at(id);
                    cufhe::Copy(out, in0, s);
                });
                cudaNodes_.at(id).emplace(node);
                break;
            }

            case GateInfo::Kind::RAM:
                assert(gate.in.size() == 1);
                dff_ids_.push_back(id);
                break;

            case GateInfo::Kind::ROM:
                assert(gate.in.size() == 0);
                break;
            }
        }

        // Add dependencies
        for (size_t id = 0; id < nGates; id++) {
            auto& gate = gates_.at(id);
            auto& to = cudaNodes_.at(id);
            if (!to)
                continue;
            for (int dep : gate.in) {
                auto& from = cudaNodes_.at(dep);
                if (!from)
                    continue;
                cuda.addDep(*from, *to);
            }
        }

        // Instantiate the graph
        graphInst_ = cuda.instantiate();

        // Collect named ids
        for (auto&& [name, id] : builder.name2id_) {
            auto&& [nodeName, portName, portBit] = name;
            std::string kind;
            switch (gates_.at(id).kind) {
            case GateInfo::Kind::INPUT:
                kind = "input";
                break;
            case GateInfo::Kind::OUTPUT:
                kind = "output";
                break;
            case GateInfo::Kind::RAM:
                kind = "ram";
                break;
            case GateInfo::Kind::ROM:
                kind = "rom";
                break;
            default:
                error::die("unreachable");
            }
            name2id_.emplace(std::make_tuple(nodeName, kind, portName, portBit),
                             id);
        }
    }

    void copyCtxt(cufhe::Ctxt& dst, const cufhe::Ctxt& src)
    {
        cufhe::Copy(dst, src, stream_);
        cufhe::Synchronize();
    }

    void setROMData(const std::string& nodeName, int bitIndex,
                    const cufhe::Ctxt& src)
    {
        auto name = std::make_tuple(nodeName, "rom", "romdata", bitIndex);
        auto id = name2id_.at(name);
        auto& dst = *ctxts_.at(id);
        copyCtxt(dst, src);
    }

    void setRAMData(const std::string& nodeName, int bitIndex,
                    const cufhe::Ctxt& src)
    {
        auto name = std::make_tuple(nodeName, "ram", "ramdata", bitIndex);
        auto id = name2id_.at(name);
        auto& dst = *ctxts_.at(id);
        copyCtxt(dst, src);
    }

    void getRAMData(cufhe::Ctxt& dst, const std::string& nodeName, int bitIndex)
    {
        auto name = std::make_tuple(nodeName, "ram", "ramdata", bitIndex);
        auto id = name2id_.at(name);
        auto& src = *ctxts_.at(id);
        copyCtxt(dst, src);
    }

    void setInputData(const std::string& nodeName, const std::string& portName,
                      int portBit, const cufhe::Ctxt& src)
    {
        auto name = std::make_tuple(nodeName, "input", portName, portBit);
        auto id = name2id_.at(name);
        auto& dst = *ctxts_.at(id);
        copyCtxt(dst, src);
    }

    void getOutputData(cufhe::Ctxt& dst, const std::string& nodeName,
                       const std::string& portName, int portBit)
    {
        auto name = std::make_tuple(nodeName, "output", portName, portBit);
        auto id = name2id_.at(name);
        auto& src = *ctxts_.at(id);
        copyCtxt(dst, src);
    }

    void tick()
    {
        for (int id : dff_ids_) {
            assert(gates_.at(id).in.size() == 1);
            int dep = gates_.at(id).in[0];
            auto& dst = *ctxts_.at(id);
            auto& src = *ctxts_.at(dep);
            cufhe::Copy(dst, src, stream_);
        }
        cufhe::Synchronize();
    }

    void run()
    {
        cufhe::Stream st = stream_;
        cudaGraphLaunch(graphInst_, st.st());
        cudaStreamSynchronize(st.st());
    }
};

class CUFHE2Frontend {
private:
    CUFHE2RunParameter pr_;
    std::optional<CUFHE2Network> network_;
    TFHEPacket reqPacket_;
    TFHEppBKey bkey_;
    int currentCycle_;
    bool cufheInitialized_;

private:
    // Don't allow to copy this object.
    CUFHE2Frontend(const CUFHE2Frontend&);
    CUFHE2Frontend& operator=(const CUFHE2Frontend&);

    void initializeCUFHE()
    {
        assert(!cufheInitialized_);
        cufhe::SetGPUNum(pr_.numGPU);
        cufhe::SetSeed();
        cufhe::Initialize(*tfhepp2cufhe(*bkey_.gk));
        cufheInitialized_ = true;
    }

    void setInitialROM()
    {
        // Set initial data
        for (const auto& bprom : pr_.blueprint.builtinROMs()) {
            if (auto it = reqPacket_.romInTLWE.find(bprom.name);
                it != reqPacket_.romInTLWE.end()) {
                std::vector<TFHEpp::TLWElvl0>& init = it->second;
                if (init.size() !=
                    (1 << bprom.inAddrWidth) * bprom.outRdataWidth)
                    error::die("Invalid request packet: wrong length of ROM");

                for (size_t i = 0; i < init.size(); i++) {
                    network_->setROMData(bprom.name, static_cast<int>(i),
                                         *::tfhepp2cufhe(init.at(i)));
                }
            }
        }
    }

    void setInitialRAM()
    {
        for (const auto& bpram : pr_.blueprint.builtinRAMs()) {
            using RAM_TYPE = blueprint::BuiltinRAM::TYPE;
            switch (bpram.type) {
            case RAM_TYPE::CMUX_MEMORY: {
                error::die("Not implemented");
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
                        network_->setRAMData(bpram.name, static_cast<int>(i),
                                             *::tfhepp2cufhe(init.at(i)));
                    }
                }
                break;
            }
            }
        }
    }

public:
    CUFHE2Frontend() : cufheInitialized_(false)
    {
    }

    CUFHE2Frontend(const Options& opt)
        : pr_(opt), currentCycle_(0), cufheInitialized_(false)
    {
        reqPacket_ = readFromArchive<TFHEPacket>(pr_.inputFile);
        bkey_ = readFromArchive<TFHEppBKey>(pr_.bkeyFile);

        // Prepare cuFHE
        initializeCUFHE();

        const NetworkBlueprint& bp = pr_.blueprint;
        CUFHE2NetworkBuilder builder;

        // [[file]]
        for (const auto& file : bp.files()) {
            std::ifstream ifs{file.path, std::ios::binary};
            if (!ifs)
                error::die("Invalid [[file]] path: ", file.path);
            builder.setNodeName(file.name);
            readNetworkFromJSONImpl(builder, ifs);
        }

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
                error::die("Not implemented");
                break;
            }

            case RAM_TYPE::MUX:
                builder.setNodeName(ram.name);
                makeRAMWithMUX_CUFHE2(builder, ram.inAddrWidth,
                                      ram.outRdataWidth);
                break;
            }
        }

        // [[builtin]] type = rom | type = mux-rom
        for (const auto& bprom : bp.builtinROMs()) {
            using ROM_TYPE = blueprint::BuiltinROM::TYPE;
            switch (bprom.type) {
            case ROM_TYPE::CMUX_MEMORY: {
                error::die("Not implemented");
            }

            case ROM_TYPE::MUX: {
                // Create ROM with MUX
                builder.setNodeName(bprom.name);
                makeROMWithMUX_CUFHE2(builder, bprom.inAddrWidth,
                                      bprom.outRdataWidth);

                break;
            }
            }
        }

        // [connect]
        for (const auto& [src, dst] : bp.edges()) {
            assert(src.portLabel.kind == "output");
            assert(dst.portLabel.kind == "input");
            builder.connectPorts(src, dst);
        }

        network_.emplace(std::move(builder));
    }

    ~CUFHE2Frontend()
    {
        if (cufheInitialized_)
            cufhe::CleanUp();
    }

    void tick()
    {
        network_->tick();
    }

    void run()
    {
        network_->run();
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

            network_->setInputData(port.nodeName, port.portLabel.portName,
                                   port.portLabel.portBit,
                                   *::tfhepp2cufhe(bits.at(index)));
        }
    }

    TFHEPacket makeResPacket(int numCycles)
    {
        TFHEPacket resPacket;
        resPacket.numCycles = numCycles;

        // Get values of output @port
        cufhe::Ctxt dst;
        for (auto&& [key, port] : pr_.blueprint.atPorts()) {
            if (port.portLabel.kind != "output")
                continue;
            auto& [atPortName, atPortBit] = key;
            auto& bits = resPacket.bits[atPortName];
            if (bits.size() < atPortBit + 1)
                bits.resize(atPortBit + 1);
            network_->getOutputData(dst, port.nodeName, port.portLabel.portName,
                                    port.portLabel.portBit);
            bits.at(atPortBit) = cufhe2tfhepp(dst);
        }
        // Get values of RAM
        // FIXME: subset of RAMs?
        cufhe::Ctxt src;
        for (auto&& bp : pr_.blueprint.builtinRAMs()) {
            using RAM_TYPE = blueprint::BuiltinRAM::TYPE;
            switch (bp.type) {
            case RAM_TYPE::CMUX_MEMORY: {
                error::die("unreachable");
                break;
            }

            case RAM_TYPE::MUX: {
                std::vector<TFHEpp::TLWElvl0>& dst =
                    resPacket.ramInTLWE[bp.name];
                for (size_t i = 0; i < (1 << bp.inAddrWidth) * bp.outRdataWidth;
                     i++) {
                    network_->getRAMData(src, bp.name, i);
                    dst.push_back(cufhe2tfhepp(src));
                }
                break;
            }
            }
        }

        return resPacket;
    }

    void go(const Options& opt)
    {
        pr_.print();

        // Check bkey is correct.
        if (!bkey_.gk || (pr_.blueprint.needsCircuitKey() && !bkey_.ck))
            error::die("Invalid bootstrapping key");

        setInitialROM();

        if (currentCycle_ == 0) {
            auto port = pr_.blueprint.at("reset", 0);
            if (port && port->portLabel.kind == "input") {
                cufhe::Ctxt one, zero;
                cufhe::ConstantOne(one);
                cufhe::ConstantZero(zero);
                cufhe::Synchronize();

                network_->setInputData(port->nodeName, port->portLabel.portName,
                                       port->portLabel.portBit, one);
                run();
                network_->setInputData(port->nodeName, port->portLabel.portName,
                                       port->portLabel.portBit, zero);
            }
        }

        // Go computing
        for (int i = 0; i < pr_.numCycles; i++, currentCycle_++) {
            spdlog::info("#{}", currentCycle_ + 1);
            auto duration = timeit([&] {
                // Tick
                tick();

                // Set values to RAM and input ports if necessary
                if (currentCycle_ == 0)
                    setInitialRAM();
                setCircularInputs(currentCycle_);

                // Run
                run();
            });

            spdlog::info("\tdone. ({} us)", duration.count());
        }

        // Dump result packet
        TFHEPacket resPacket = makeResPacket(currentCycle_);
        writeToArchive(pr_.outputFile, resPacket);
    }
};
}  // namespace

void doCUFHE2(const Options& opt)
{
    CUFHE2Frontend frontend{opt};
    frontend.go(opt);
}
