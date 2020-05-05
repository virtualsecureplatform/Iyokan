#include <chrono>

#include <CLI/CLI.hpp>

#include "iyokan_plain.hpp"
#include "iyokan_tfhepp.hpp"

#ifdef IYOKAN_CUDA_ENABLED
#include "iyokan_cufhe.hpp"
#endif

int main(int argc, char **argv)
{
    spdlog::set_level(spdlog::level::info);

    CLI::App app{"Prallel FHE circuit evaluation engine."};
    app.require_subcommand();

    enum class TYPE { PLAIN, TFHE } type;
    Options opt;
    bool enableGPU = false;
    std::string blueprintFilePath;

    {
        CLI::App *plain = app.add_subcommand("plain", "");
        plain->parse_complete_callback([&] { type = TYPE::PLAIN; });
        plain->add_option("--blueprint", blueprintFilePath, "")
            ->required()
            ->check(CLI::ExistingFile);
        plain->add_option("-c", opt.numCycles, "");
        plain->add_option("--cpu", opt.numCPUWorkers, "")
            ->check(CLI::PositiveNumber);
        plain->add_option("-i,--in", opt.inputFile, "")
            ->required()
            ->check(CLI::ExistingFile);
        plain->add_option("-o,--out", opt.outputFile, "")->required();
        plain->add_flag_callback(
            "--quiet", [] { spdlog::set_level(spdlog::level::err); }, "");
        plain->add_flag_callback(
            "--verbose", [] { spdlog::set_level(spdlog::level::debug); }, "");

        plain->add_option("--dump-prefix", opt.dumpPrefix, "");
    }

    {
        CLI::App *tfhe = app.add_subcommand("tfhe", "");
        tfhe->parse_complete_callback([&] { type = TYPE::TFHE; });
        tfhe->add_option("--blueprint", blueprintFilePath, "")
            ->required()
            ->check(CLI::ExistingFile);
        tfhe->add_option("-c", opt.numCycles, "")->required();
        tfhe->add_option("--cpu", opt.numCPUWorkers, "")
            ->check(CLI::PositiveNumber);
        tfhe->add_option("-i,--in", opt.inputFile, "")
            ->required()
            ->check(CLI::ExistingFile);
        tfhe->add_option("-o,--out", opt.outputFile, "")->required();
        tfhe->add_flag_callback(
            "--quiet", [] { spdlog::set_level(spdlog::level::err); }, "");
        tfhe->add_flag_callback(
            "--verbose", [] { spdlog::set_level(spdlog::level::debug); }, "");

        tfhe->add_option("--secret-key", opt.secretKey, "")
            ->check(CLI::ExistingFile);
        tfhe->add_option("--dump-prefix", opt.dumpPrefix, "")
            ->needs("--secret-key");

#ifdef IYOKAN_CUDA_ENABLED
        tfhe->add_flag("--enable-gpu", enableGPU, "");
        tfhe->add_option("--gpu", opt.numGPUWorkers, "")
            ->check(CLI::PositiveNumber);
        tfhe->add_option("--gpu_num", opt.numGPU, "")
            ->check(CLI::PositiveNumber);
#endif
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
