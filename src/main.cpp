#include <chrono>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>

#include <CLI/CLI.hpp>

#include "iyokan_plain.hpp"
#include "iyokan_tfhepp.hpp"

#ifdef IYOKAN_CUDA_ENABLED
#include "iyokan_cufhe.hpp"
#endif

namespace {

unsigned defaultCPUWorkerCount()
{
    const unsigned logical =
        std::max(1u, std::thread::hardware_concurrency());

#ifdef __linux__
    // `hardware_concurrency` includes SMT siblings.  StarPU binds one CPU
    // worker per logical thread, which oversubscribes this workload on SMT
    // machines.  Prefer one worker per physical core when topology is
    // available; keep the portable logical-thread fallback for other hosts.
    std::set<std::pair<int, int>> physicalCores;
    std::error_code ec;
    const std::filesystem::path cpuRoot{"/sys/devices/system/cpu"};
    for (std::filesystem::directory_iterator it(cpuRoot, ec), end;
         !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind("cpu", 0) != 0 || name.size() == 3 ||
            !std::all_of(name.begin() + 3, name.end(), [](unsigned char c) {
                return std::isdigit(c) != 0;
            }))
            continue;

        int package = 0, core = 0;
        std::ifstream packageFile(it->path() / "topology/physical_package_id");
        std::ifstream coreFile(it->path() / "topology/core_id");
        if ((packageFile >> package) && (coreFile >> core))
            physicalCores.emplace(package, core);
    }
    if (!physicalCores.empty())
        return static_cast<unsigned>(physicalCores.size());
#endif

    return logical;
}

}  // namespace

int main(int argc, char** argv)
{
    error::initialize("iyokan");

    // Show build config
    spdlog::info("Build config");
#if defined(NDEBUG)
    spdlog::info("\tType: Release");
#else
    spdlog::info("\tType: Debug");
#endif
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
    const unsigned defaultCPUWorkers = defaultCPUWorkerCount();
#ifdef IYOKAN_CUDA_ENABLED
    bool enableGPU = false;
#endif
    bool verbose = false, quiet = false;

    std::map<std::string, SCHED> mapSched{{"topo", SCHED::TOPO},
                                          {"ranku", SCHED::RANKU}};

    {
        CLI::App* plain = app.add_subcommand("plain", "");
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
        plain->add_option("--dump-graph-dot-prefix", opt.dumpGraphDOTPrefix,
                          "");
        plain->add_option("--critical-profile-prefix",
                          opt.criticalProfilePrefix, "");
        plain->add_option("--sched", opt.sched, "")
            ->transform(CLI::CheckedTransformer(mapSched, CLI::ignore_case));
        plain->add_flag("--skip-reset", opt.skipReset, "");
        plain->add_flag("--show-combinational-progress",
                        opt.showCombinationalProgress, "");

        auto ogroups = plain->add_option_group("run in plaintext",
                                               "Run in plaintext mode");
        ogroups->require_option(1);

        auto newRun = ogroups->add_option_group("new run", "A new run");
        newRun
            ->add_option_function<std::string>(
                "--blueprint",
                [&](auto&& filepath) {
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
        CLI::App* tfhe = app.add_subcommand("tfhe", "");
        tfhe->parse_complete_callback([&] { type = TYPE::TFHE; });
        tfhe->add_option("--evalkey", opt.ekFile, "")->required();
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
        tfhe->add_option("--dump-graph-dot-prefix", opt.dumpGraphDOTPrefix, "");
        tfhe->add_option("--critical-profile-prefix",
                         opt.criticalProfilePrefix, "");
        tfhe->add_option("--sched", opt.sched, "")
            ->transform(CLI::CheckedTransformer(mapSched, CLI::ignore_case));

        tfhe->add_option("--secret-key", opt.secretKey, "")
            ->check(CLI::ExistingFile);
        tfhe->add_option("--dump-prefix", opt.dumpPrefix, "")
            ->needs("--secret-key");
        tfhe->add_flag("--skip-reset", opt.skipReset, "");
        tfhe->add_flag("--show-combinational-progress",
                       opt.showCombinationalProgress, "");

#ifdef IYOKAN_CUDA_ENABLED
        tfhe->add_option("--gpu", opt.numGPUWorkers, "")
            ->check(CLI::PositiveNumber);
        tfhe->add_option_function<int>(
                "--gpu_num",
                [&](const int& v) {
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
                [&](auto&& filepath) {
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

    // Materialize the automatic value so the frontend, StarPU configuration,
    // and AsyncThread fallback pool agree.  CPU-only evaluation is best with
    // one worker per physical core.  In Tangor GPU mode, however, the CPU
    // workers mainly feed independent RAM/ROM preparation and CUDA launches;
    // using the available SMT siblings removes that producer bottleneck on
    // the measured A100 host.  An explicit --cpu always takes precedence.
    unsigned automaticCPUWorkers = defaultCPUWorkers;
#ifdef TANGOR_KVSP_STARPU_ASYNC
#ifdef IYOKAN_CUDA_ENABLED
    if (type == TYPE::TFHE && enableGPU)
        automaticCPUWorkers =
            std::max(1u, std::thread::hardware_concurrency());
#endif
#endif
    if (!opt.numCPUWorkers)
        opt.numCPUWorkers.emplace(static_cast<int>(automaticCPUWorkers));

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
    if (opt.ekFile)
        spdlog::info("\tEvalKey file: {}", *opt.ekFile);
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
    if (opt.dumpGraphDOTPrefix)
        spdlog::info("\t--dump-graph-dot-prefix: {}", *opt.dumpGraphDOTPrefix);
    if (opt.criticalProfilePrefix)
        spdlog::info("\t--critical-profile-prefix: {}",
                     *opt.criticalProfilePrefix);
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
    if (opt.skipReset)
        spdlog::info("\t--skip-reset: {}", opt.skipReset);
    if (opt.showCombinationalProgress)
        spdlog::info("\t--show-combinational-progess: {}",
                     opt.showCombinationalProgress);

    // Process depending on the options chosen.
    if (quiet)
        spdlog::set_level(spdlog::level::err);
    if (verbose)
        spdlog::set_level(spdlog::level::debug);

    if (opt.resumeFile) {
        const auto snapshotKind = detectFrontendSnapshotKind(*opt.resumeFile);
        switch (type) {
        case TYPE::PLAIN:
            if (snapshotKind == FrontendSnapshotKind::Plain)
                break;
            if (snapshotKind != FrontendSnapshotKind::Untagged ||
                !isSerializedPlainFrontend(*opt.resumeFile))
                error::die("Invalid resume file: ", *opt.resumeFile);
            break;

        case TYPE::TFHE:
            switch (snapshotKind) {
            case FrontendSnapshotKind::TFHEpp:
                break;
#ifdef IYOKAN_CUDA_ENABLED
            case FrontendSnapshotKind::CUFHE:
                enableGPU = true;
                break;
#endif
            case FrontendSnapshotKind::Untagged:
#ifdef IYOKAN_CUDA_ENABLED
                if (opt.numGPU) {
                    enableGPU = true;
                    break;
                }
#endif
                if (!isSerializedTFHEppFrontend(*opt.resumeFile)) {
#ifdef IYOKAN_CUDA_ENABLED
                    if (isSerializedCUFHEFrontend(*opt.resumeFile))
                        enableGPU = true;
                    else
#endif
                        error::die("Invalid resume file: ", *opt.resumeFile);
                }
                break;
            default:
                error::die("Invalid resume file: ", *opt.resumeFile);
            }
            break;
        }
    }

#ifdef IYOKAN_CUDA_ENABLED
    // GPU worker/device counts alone must not silently leave a new run on the
    // CPU backend.  A CUDA snapshot enables GPU execution on resume, but a
    // fresh run must state that intent explicitly.
    if (type == TYPE::TFHE && !opt.resumeFile &&
        (opt.numGPUWorkers || opt.numGPU) && !enableGPU) {
        error::die("--gpu, --gpu_num, and --num-gpu require --enable-gpu");
    }
#endif

#ifdef TANGOR_KVSP_STARPU_ASYNC
    unsigned tangorCudaDevices = 0;
#ifdef IYOKAN_CUDA_ENABLED
    if (type == TYPE::TFHE && enableGPU)
        tangorCudaDevices = static_cast<unsigned>(opt.numGPU.value_or(1));
#endif
    Tangor::configureIyokanStarpu(
        static_cast<unsigned>(opt.numCPUWorkers.value_or(
            defaultCPUWorkers)),
        tangorCudaDevices,
        tangorCudaDevices == 0
            ? static_cast<unsigned>(opt.numCPUWorkers.value_or(
                  defaultCPUWorkers))
            // Match Iyokan's established cuFHE default: these are logical
            // CUDA launch slots, not CPU threads. `--gpu` remains the tuning
            // override while Tangor keeps StarPU's physical CPU pool bounded.
            : static_cast<unsigned>(opt.numGPUWorkers.value_or(800)));
    Tangor::setIyokanCriticalProfileEnabled(
        opt.criticalProfilePrefix.has_value());
#endif

#ifdef TANGOR_KVSP_STARPU_ASYNC
    // Not every TFHEpp frontend operation has a StarPU codelet yet: circuit
    // bootstrapping and the RAM MUX preparation path still execute through
    // AsyncThread.  Serialising that fallback pool turns a KVSP cycle into a
    // long CPU bottleneck even when StarPU has CUDA workers available. Keep
    // the same physical-core budget for those independent operations while
    // StarPU schedules the offloaded gate graph.
    AsyncThread::setNumThreads(
        opt.numCPUWorkers.value_or(defaultCPUWorkers));
#else
    AsyncThread::setNumThreads(defaultCPUWorkers);
#endif

    switch (type) {
    case TYPE::PLAIN:
        doPlain(opt);
        break;
    case TYPE::TFHE:
#ifdef TANGOR_KVSP_STARPU_ASYNC
        // Tangor always runs the TFHEpp circuit graph. When requested, StarPU
        // places its individual codelets on both CPU and CUDA workers.
        doTFHE(opt);
#else
#ifdef IYOKAN_CUDA_ENABLED
        if (enableGPU)
            doCUFHE(opt);
        else
#endif
            doTFHE(opt);
#endif
        break;
    }
#ifdef TANGOR_KVSP_STARPU_ASYNC
    Tangor::shutdownIyokanStarpu();
#endif
}
