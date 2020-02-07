#include <chrono>

#include <CLI/CLI.hpp>

#include "iyokan_plain.hpp"
#include "iyokan_tfhepp.hpp"

#ifdef IYOKAN_CUDA_ENABLED
#include "iyokan_cufhe.hpp"
#endif

int main(int argc, char **argv)
{
    CLI::App app{"Prallel FHE circuit evaluation engine."};

    Options opt;
    bool enableGPU = false;

    app.require_subcommand();
    enum class TYPE { PLAIN, TFHE } type;
    app.add_option("-l", opt.logicFile, "")
        ->required()
        ->check(CLI::ExistingFile);
    app.add_option("-t", opt.numWorkers, "")->check(CLI::PositiveNumber);
    app.add_option("--enable-rom", opt.romPorts, "")->delimiter(':');
    app.add_flag("--enable-ram", opt.ramEnabled, "");
    app.add_option("-i", opt.inputFile, "")
        ->required()
        ->check(CLI::ExistingFile);

    {
        CLI::App *plain = app.add_subcommand("plain", "");
        plain->fallthrough();
        plain->parse_complete_callback([&] { type = TYPE::PLAIN; });
        plain->add_option("-c", opt.numCycles, "");
    }

    {
        CLI::App *tfhe = app.add_subcommand("tfhe", "");
        tfhe->fallthrough();
        tfhe->add_option("-c", opt.numCycles, "")->required();
        tfhe->add_option("-o", opt.outputFile, "")->required();
#ifdef IYOKAN_CUDA_ENABLED
        tfhe->add_flag("--enable-gpu", enableGPU, "");
#endif
        tfhe->parse_complete_callback([&] { type = TYPE::TFHE; });
    }

    CLI11_PARSE(app, argc, argv);

    assert(opt.romPorts.empty() || opt.romPorts.size() == 4);

    if (opt.numWorkers < 0) {
        if (enableGPU)
            opt.numWorkers = 240;
        else
            opt.numWorkers = std::thread::hardware_concurrency();
    }

    AsyncThread::setNumThreads(opt.numWorkers);

    switch (type) {
    case TYPE::PLAIN:
        doPlain(opt);
        break;
    case TYPE::TFHE:
#ifdef IYOKAN_CUDA_ENABLED
        if (enableGPU)
            doCUFHE(opt);
        else
#endif
            doTFHE(opt);
        break;
    }
}
