#include <chrono>

#include <CLI/CLI.hpp>

#include "iyokan_plain.hpp"
#include "iyokan_tfhepp.hpp"

#ifdef IYOKAN_CUDA_ENABLED
#include "iyokan_cufhe.hpp"
#endif

int main(int argc, char **argv)
{
    error::initialize("iyokan");

    // Show build config
    spdlog::info("Build config");
#if defined(IYOKAN_GIT_REVISION)
    spdlog::info("\tGit revision: " IYOKAN_GIT_REVISION);
#else
    spdlog::info("\tGit revision: unknown");
#endif
#if defined(USE_80BIT_SECURITY)
    spdlog::info("\tTFHE security parameter: CGGI16 (80bit)");
#elif defined(USE_CGGI19)
    spdlog::info("\tTFHE security parameter: CGGI19");
#else
    spdlog::info("\tTFHE security parameter: 128bit");
#endif
#ifdef IYOKAN_CUDA_ENABLED
    spdlog::info("\tGPU support: enabled");
#else
    spdlog::info("\tGPU support: disabled");
#endif

    // Parse command-line arguments
    CLI::App app{"Prallel FHE circuit evaluation engine."};
    app.require_subcommand();

    enum class TYPE { PLAIN, TFHE } type;
    Options opt;
#ifdef IYOKAN_CUDA_ENABLED
    bool enableGPU = false;
#endif
    bool verbose = false, quiet = false;

    std::map<std::string, SCHED> mapSched{{"topo", SCHED::TOPO},
                                          {"ranku", SCHED::RANKU}};

    {
        CLI::App *plain = app.add_subcommand("plain", "");
        plain->parse_complete_callback([&] { type = TYPE::PLAIN; });
        plain->add_option("-c", opt.numCycles, "");
        plain->add_option("--cpu", opt.numCPUWorkers, "")
            ->check(CLI::PositiveNumber);
        plain->add_option("--dump-prefix", opt.dumpPrefix, "");
        auto optO = plain->add_option("-o,--out", opt.outputFile, "");
        plain->add_flag_function(
            "--stdout-csv,!--no-stdout-csv",
            [&](int64_t count) { opt.stdoutCSV = count > 0 ? true : false; },
            "");
        plain->add_option("--snapshot", opt.snapshotFile, "");
        plain->add_flag("--quiet", quiet, "");
        plain->add_flag("--verbose", verbose, "");
        plain->add_option("--dump-time-csv-prefix", opt.dumpTimeCSVPrefix, "");
        plain->add_option("--dump-graph-json-prefix", opt.dumpGraphJSONPrefix,
                          "");
        plain->add_option("--sched", opt.sched, "")
            ->transform(CLI::CheckedTransformer(mapSched, CLI::ignore_case));

        auto ogroups = plain->add_option_group("run in plaintext",
                                               "Run in plaintext mode");
        ogroups->require_option(1);

        auto newRun = ogroups->add_option_group("new run", "A new run");
        newRun
            ->add_option_function<std::string>(
                "--blueprint",
                [&](auto &&filepath) {
                    opt.blueprint = NetworkBlueprint{filepath};
                })
            ->required()
            ->check(CLI::ExistingFile);
        newRun->add_option("-i,--in", opt.inputFile, "")
            ->required()
            ->needs(optO)
            ->check(CLI::ExistingFile);

        auto resume =
            ogroups->add_option_group("resume", "Resume from a saved snapshot");
        resume->add_option("--resume", opt.resumeFile, "")->required();
    }

    {
        CLI::App *tfhe = app.add_subcommand("tfhe", "");
        tfhe->parse_complete_callback([&] { type = TYPE::TFHE; });
        tfhe->add_option("--bkey", opt.bkeyFile, "")->required();
        auto optC = tfhe->add_option("-c", opt.numCycles, "");
        tfhe->add_option("--cpu", opt.numCPUWorkers, "")
            ->check(CLI::PositiveNumber);
        auto optO = tfhe->add_option("-o,--out", opt.outputFile, "");
        tfhe->add_flag_function(
            "--stdout-csv,!--no-stdout-csv",
            [&](int64_t count) { opt.stdoutCSV = count > 0 ? true : false; },
            "");
        tfhe->add_option("--snapshot", opt.snapshotFile, "");
        tfhe->add_flag("--quiet", quiet, "");
        tfhe->add_flag("--verbose", verbose, "");
        tfhe->add_option("--dump-time-csv-prefix", opt.dumpTimeCSVPrefix, "");
        tfhe->add_option("--dump-graph-json-prefix", opt.dumpGraphJSONPrefix,
                         "");
        tfhe->add_option("--sched", opt.sched, "")
            ->transform(CLI::CheckedTransformer(mapSched, CLI::ignore_case));

        tfhe->add_option("--secret-key", opt.secretKey, "")
            ->check(CLI::ExistingFile);
        tfhe->add_option("--dump-prefix", opt.dumpPrefix, "")
            ->needs("--secret-key");

#ifdef IYOKAN_CUDA_ENABLED
        tfhe->add_option("--gpu", opt.numGPUWorkers, "")
            ->check(CLI::PositiveNumber);
        tfhe->add_option_function<int>(
                "--gpu_num",
                [&](const int &v) {
                    spdlog::warn(
                        "Option --gpu_num is deprecated. Use --num-gpu "
                        "instead.");
                    opt.numGPU.emplace(v);
                },
                "")
            ->check(CLI::PositiveNumber);
        tfhe->add_option("--num-gpu", opt.numGPU, "")
            ->check(CLI::PositiveNumber);
#endif

        auto ogroups =
            tfhe->add_option_group("run in TFHE mode", "Run in TFHE mode");
        ogroups->require_option(1);

        auto newRun = ogroups->add_option_group("new run", "A new run");
        newRun
            ->add_option_function<std::string>(
                "--blueprint",
                [&](auto &&filepath) {
                    opt.blueprint = NetworkBlueprint{filepath};
                })
            ->required()
            ->check(CLI::ExistingFile);
        newRun->add_option("-i,--in", opt.inputFile, "")
            ->required()
            ->needs(optC, optO)
            ->check(CLI::ExistingFile);
#ifdef IYOKAN_CUDA_ENABLED
        newRun->add_flag("--enable-gpu", enableGPU, "");
#endif

        auto resume =
            ogroups->add_option_group("resume", "Resume from a saved snapshot");
        resume->add_option("--resume", opt.resumeFile, "")->required();
    }

    CLI11_PARSE(app, argc, argv);

    // Print what options are selected.
    spdlog::info("Options");
    if (opt.blueprint)
        spdlog::info("\tBlueprint: {}", opt.blueprint->sourceFile());
    if (opt.numCPUWorkers)
        spdlog::info("\t# of CPU workers: {}", *opt.numCPUWorkers);
    if (opt.numGPUWorkers)
        spdlog::info("\t# of GPU workers: {}", *opt.numGPUWorkers);
    if (opt.numGPU)
        spdlog::info("\t# of GPUs: {}", *opt.numGPU);
    if (opt.numCycles)
        spdlog::info("\t# of cycles: {}", *opt.numCycles);
    if (opt.bkeyFile)
        spdlog::info("\tBKey file: {}", *opt.bkeyFile);
    if (opt.inputFile)
        spdlog::info("\tInput file (request packet): {}", *opt.inputFile);
    if (opt.outputFile)
        spdlog::info("\tOutput file (result packet): {}", *opt.outputFile);
    if (opt.secretKey)
        spdlog::info("\t--secret-key: {}", *opt.secretKey);
    if (opt.dumpPrefix)
        spdlog::info("\t--dump-prefix: {}", *opt.dumpPrefix);
    if (opt.snapshotFile)
        spdlog::info("\t--snapshot: {}", *opt.snapshotFile);
    if (opt.resumeFile)
        spdlog::info("\t--resume: {}", *opt.resumeFile);
    if (opt.stdoutCSV)
        spdlog::info("\t--stdout-csv: {}", opt.stdoutCSV);
    spdlog::info("\t--verbose: {}", verbose);
    spdlog::info("\t--quiet: {}", quiet);
    if (opt.dumpTimeCSVPrefix)
        spdlog::info("\t--dump-time-csv-prefix: {}", *opt.dumpTimeCSVPrefix);
    if (opt.dumpGraphJSONPrefix)
        spdlog::info("\t--dump-graph-json-prefix: {}",
                     *opt.dumpGraphJSONPrefix);
    if (opt.sched != SCHED::UND) {
        std::string str;
        switch (opt.sched) {
        case SCHED::TOPO:
            str = "topo";
            break;
        case SCHED::RANKU:
            str = "ranku";
            break;
        default:
            error::die("unreachable");
        }
        spdlog::info("\t--sched: {}", str);
    }

    // Process depending on the options chosen.
    if (quiet)
        spdlog::set_level(spdlog::level::err);
    if (verbose)
        spdlog::set_level(spdlog::level::debug);

    if (opt.resumeFile) {
        switch (type) {
        case TYPE::PLAIN:
            if (!isSerializedPlainFrontend(*opt.resumeFile))
                error::die("Invalid resume file: ", *opt.resumeFile);
            break;

        case TYPE::TFHE:
            if (!isSerializedTFHEppFrontend(*opt.resumeFile)) {
#ifdef IYOKAN_CUDA_ENABLED
                if (isSerializedCUFHEFrontend(*opt.resumeFile))
                    enableGPU = true;
                else
#endif
                    error::die("Invalid resume file: ", *opt.resumeFile);
            }
            break;
        }
    }

    AsyncThread::setNumThreads(std::thread::hardware_concurrency());

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
