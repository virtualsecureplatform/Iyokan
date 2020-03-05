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
    std::string blueprintFilePath;

    app.require_subcommand();
    enum class TYPE { PLAIN, TFHE } type;
    app.add_option("--blueprint", blueprintFilePath, "")
        ->required()
        ->check(CLI::ExistingFile);
    app.add_option("--cpu", opt.numCPUWorkers, "")->check(CLI::PositiveNumber);
    app.add_option("--gpu", opt.numGPUWorkers, "")->check(CLI::PositiveNumber);
    app.add_option("-i", opt.inputFile, "")
        ->required()
        ->check(CLI::ExistingFile);
    app.add_flag("--quiet", opt.quiet, "");

    {
        CLI::App *plain = app.add_subcommand("plain", "");
        plain->fallthrough();
        plain->parse_complete_callback([&] { type = TYPE::PLAIN; });
        plain->add_option("-c", opt.numCycles, "");
        plain->add_option("-o", opt.outputFile, "")->required();
        plain->add_option("--enable-dump-every-clock", opt.dumpEveryClock, "");
    }

    {
        CLI::App *tfhe = app.add_subcommand("tfhe", "");
        tfhe->fallthrough();
        tfhe->add_option("-c", opt.numCycles, "")->required();
        tfhe->add_option("-o", opt.outputFile, "")->required();
        tfhe->add_option("--secret-key", opt.secretKey, "")
            ->check(CLI::ExistingFile);
        tfhe->add_option("--enable-dump-every-clock", opt.dumpEveryClock, "")
            ->needs("--secret-key");
#ifdef IYOKAN_CUDA_ENABLED
        tfhe->add_flag("--enable-gpu", enableGPU, "");
#endif
        tfhe->parse_complete_callback([&] { type = TYPE::TFHE; });
    }

    CLI11_PARSE(app, argc, argv);

    opt.blueprint = NetworkBlueprint{blueprintFilePath};

    AsyncThread::setNumThreads(opt.numCPUWorkers);

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
