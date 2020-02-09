#include "iyokan_cufhe.hpp"
#include "packet.hpp"

auto get(CUFHENetwork& net, const std::string& kind,
         const std::string& portName, int portBit)
{
    auto ret = net.get<TaskCUFHEGateMem>(kind, portName, portBit);
    assert(ret && "Possibly invalid port name or port bit");
    return ret;
}

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

void doCUFHE(const Options& opt)
{
    // Read packet
    const auto reqPacket = readFromArchive<KVSPReqPacket>(opt.inputFile);

    // Prepare cuFHE
    cufhe::SetSeed();
    cufhe::Initialize(*tfhepp2cufhe(*reqPacket.gateKey));

    // Read network
    CUFHENetworkBuilder::NetworkType net = [&opt, &reqPacket] {
        std::ifstream ifs{opt.logicFile};
        assert(ifs);
        auto core = readNetworkFromJSON<CUFHENetworkBuilder>(ifs);
        assert(core.isValid());

        // Set RAM
        for (int addr = 0; addr < 512; addr++)
            for (int bit = 0; bit < 8; bit++)
                get(core, "ram", std::to_string(addr), bit)
                    ->set(*tfhepp2cufhe(reqPacket.ram.at(addr * 8 + bit)));

        // FIXME: Allow to use ROM with CB
        assert(opt.romPorts.empty());

        // Set ROM
        for (int addr = 0; addr < 128; addr++)
            for (int bit = 0; bit < 32; bit++)
                get(core, "rom", std::to_string(addr), bit)
                    ->set(*tfhepp2cufhe((reqPacket.rom.at(addr * 32 + bit))));
        return core;
    }();

    // Get #cycles
    int numCycles = std::numeric_limits<int>::max();
    if (opt.numCycles > 0)
        numCycles = opt.numCycles;

    // Turn reset on
    {
        cufhe::Ctxt one;
        cufhe::ConstantOne(one);
        cufhe::Synchronize();
        get(net, "input", "reset", 0)->set(one);
    }
    // Reset
    processAllGates(net, opt.numWorkers);

    // Turn reset off
    {
        cufhe::Ctxt zero;
        cufhe::ConstantZero(zero);
        cufhe::Synchronize();
        get(net, "input", "reset", 0)->set(zero);
    }
    // Go computing
    processCycles(numCycles, [&] {
        net.tick();
        processAllGates(net, opt.numWorkers);
        return false;
    });

    KVSPResPacket resPacket;
    resPacket.numCycles = numCycles;
    // Get values of flags
    resPacket.flags.push_back(
        cufhe2tfhepp(get(net, "output", "io_finishFlag", 0)->get()));
    // Get values of registers
    for (int reg = 0; reg < 16; reg++) {
        resPacket.regs.emplace_back();
        for (int bit = 0; bit < 16; bit++)
            resPacket.regs[reg].push_back(cufhe2tfhepp(
                get(net, "output", detail::fok("io_regOut_x", reg), bit)
                    ->get()));
    }
    // Get values of RAM
    for (int addr = 0; addr < 512; addr++)
        for (int bit = 0; bit < 8; bit++)
            resPacket.ram.push_back(cufhe2tfhepp(
                get(net, "ram", std::to_string(addr), bit)->get()));

    // Dump result packet
    writeToArchive(opt.outputFile, resPacket);

    // Clean cuFHE up
    cufhe::CleanUp();
}
