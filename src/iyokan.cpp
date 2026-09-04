#include "iyokan.hpp"

namespace {
double jsonNumber(std::uint64_t value)
{
    return static_cast<double>(value);
}

picojson::array rankedDurations(
    const std::unordered_map<std::string, std::uint64_t>& durations,
    size_t limit = 8)
{
    std::vector<std::pair<std::string, std::uint64_t>> ordered(
        durations.begin(), durations.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto& left,
                                                 const auto& right) {
        return left.second != right.second ? left.second > right.second
                                           : left.first < right.first;
    });
    picojson::array result;
    for (size_t i = 0; i < std::min(limit, ordered.size()); ++i) {
        picojson::object entry;
        entry.emplace("name", ordered[i].first);
        entry.emplace("nanoseconds", jsonNumber(ordered[i].second));
        result.emplace_back(entry);
    }
    return result;
}
}  // namespace

void ProgressGraphMaker::dumpCriticalJSON(std::ostream& os, int cycle,
                                          unsigned cpuWorkers) const
{
#ifdef TANGOR_KVSP_STARPU_ASYNC
    const auto tangorSnapshot = Tangor::endIyokanCriticalProfileCycle();
    // The TFHE frontend queue deliberately uses the CUDA launch-slot count,
    // which may be much larger than StarPU's physical CPU worker pool.
    // Resource utilization must use the physical pool configured in Tangor.
    cpuWorkers = Tangor::iyokanStarpuCPUWorkerCount();
#endif
    std::lock_guard<std::mutex> lock(mtxWrite_);
    const auto cycleEnd = CriticalClock::now();
    const std::uint64_t wallNanoseconds = relativeNanoseconds(cycleEnd);
    const std::uint64_t processCPUUsedNanoseconds =
        processCPUStartNanoseconds_ == 0
            ? 0
            : processCPUNanoseconds() - processCPUStartNanoseconds_;
    const double cpuUtilization =
        wallNanoseconds == 0 || cpuWorkers == 0
            ? 0.0
            : static_cast<double>(processCPUUsedNanoseconds) /
                  (static_cast<double>(wallNanoseconds) * cpuWorkers);

    std::unordered_map<int, std::string> domains;
    const auto initialDomain = [](const std::string& kind) {
        static const std::set<std::string> ramKinds{
            "GB", "CMUXs", "RAMWriteChunk", "MUXWoSE", "CB", "CBInv",
            "CBWithInv", "RAMUX", "RAMWriter", "RAMReader"};
        static const std::set<std::string> romKinds{
            "ROMUX", "ROMNormalize", "ROM"};
        static const std::set<std::string> bridgeKinds{
            "bridge", "cufhe2tfhepp", "tfhepp2cufhe"};
        if (ramKinds.count(kind)) return std::string("ram");
        if (romKinds.count(kind)) return std::string("rom");
        if (bridgeKinds.count(kind)) return std::string("bridge");
        return std::string("core");
    };
    for (const auto& [id, n] : nodes_) {
        const auto configured = configuredDomains_.find(id);
        domains.emplace(id, configured == configuredDomains_.end()
                                ? initialDomain(n.label.kind)
                                : configured->second);
    }
    // Sample extraction is shared by the core and memory implementations.
    // Its consumer identifies the generated subsystem without changing the
    // serialized NodeLabel format.
    for (const auto& edge : edges_) {
        const auto source = nodes_.find(edge.from);
        const auto target = nodes_.find(edge.to);
        if (source == nodes_.end() || target == nodes_.end() ||
            source->second.label.kind != "SEI")
            continue;
        const std::string targetDomain = initialDomain(target->second.label.kind);
        if (!configuredDomains_.count(edge.from) &&
            (targetDomain == "ram" || targetDomain == "rom"))
            domains[edge.from] = targetDomain;
    }

    std::unordered_map<int, size_t> indegrees;
    std::unordered_map<int, std::vector<int>> outgoing;
    for (const auto& [id, _] : nodes_) indegrees.emplace(id, 0);
    for (const auto& edge : edges_) {
        if (!edge.causal || !nodes_.count(edge.from) || !nodes_.count(edge.to))
            continue;
        ++indegrees[edge.to];
        outgoing[edge.from].push_back(edge.to);
    }
    std::queue<int> ready;
    for (const auto& [id, degree] : indegrees)
        if (degree == 0) ready.push(id);
    size_t topologicalNodes = 0;
    while (!ready.empty()) {
        const int id = ready.front();
        ready.pop();
        ++topologicalNodes;
        for (const int target : outgoing[id])
            if (--indegrees[target] == 0) ready.push(target);
    }
    const bool graphValid = topologicalNodes == nodes_.size();

    std::optional<int> sink;
    for (const auto& [id, n] : nodes_) {
        if (!n.endCritical) continue;
        if (!sink || *n.endCritical > *nodes_.at(*sink).endCritical ||
            (*n.endCritical == *nodes_.at(*sink).endCritical && id < *sink))
            sink = id;
    }
    std::vector<int> reverseChain;
    std::set<int> chainSet;
    if (graphValid && sink) {
        std::optional<int> cursor = sink;
        while (cursor && chainSet.insert(*cursor).second) {
            reverseChain.push_back(*cursor);
            cursor = nodes_.at(*cursor).releasedBy;
        }
    }
    std::reverse(reverseChain.begin(), reverseChain.end());

    std::uint64_t chainServiceNanoseconds = 0;
    std::uint64_t chainQueueNanoseconds = 0;
    std::uint64_t chainThreadCPUNanoseconds = 0;
    std::uint64_t chainStarpuQueueNanoseconds = 0;
    std::uint64_t chainCudaPoolQueueNanoseconds = 0;
    std::uint64_t chainCudaKernelNanoseconds = 0;
    std::uint64_t chainH2DNanoseconds = 0, chainD2HNanoseconds = 0;
    std::unordered_map<std::string, std::uint64_t> totalByKind, totalByDomain,
        chainByKind, chainByDomain, cpuByKind, cpuByDomain, gpuByKind,
        gpuByDomain;
    std::vector<std::pair<std::uint64_t, int>> queueEvents;
    for (const auto& [id, n] : nodes_) {
        if (!n.startCritical || !n.endCritical) continue;
        const std::uint64_t service = relativeNanoseconds(*n.endCritical) -
                                      relativeNanoseconds(*n.startCritical);
        totalByKind[n.label.kind] += service;
        totalByDomain[domains[id]] += service;
        cpuByKind[n.label.kind] += n.cpuNanoseconds;
        cpuByDomain[domains[id]] += n.cpuNanoseconds;
        if (n.readyCritical && *n.readyCritical < *n.startCritical) {
            queueEvents.emplace_back(relativeNanoseconds(*n.readyCritical), 1);
            queueEvents.emplace_back(relativeNanoseconds(*n.startCritical), -1);
        }
        if (chainSet.count(id)) {
            chainServiceNanoseconds += service;
            chainThreadCPUNanoseconds += n.cpuNanoseconds;
            chainByKind[n.label.kind] += service;
            chainByDomain[domains[id]] += service;
            if (n.readyCritical)
                chainQueueNanoseconds +=
                    relativeNanoseconds(*n.startCritical) -
                    relativeNanoseconds(*n.readyCritical);
        }
    }
    std::sort(queueEvents.begin(), queueEvents.end());
    std::uint64_t backlogNanoseconds = 0, lastEvent = 0;
    int queued = 0;
    for (const auto& [timestamp, delta] : queueEvents) {
        if (queued > 0) backlogNanoseconds += timestamp - lastEvent;
        queued += delta;
        lastEvent = timestamp;
    }
    const double backlogFraction = wallNanoseconds == 0
        ? 0.0 : static_cast<double>(backlogNanoseconds) / wallNanoseconds;
    const double chainCoverage = wallNanoseconds == 0
        ? 0.0 : static_cast<double>(chainServiceNanoseconds) / wallNanoseconds;
    const double chainQueueFraction = wallNanoseconds == 0
        ? 0.0 : static_cast<double>(chainQueueNanoseconds) / wallNanoseconds;
    double maximumGPUActiveFraction = 0.0;
    double maximumGPUBacklogFraction = 0.0;
    picojson::array jsonOperations;
    picojson::array jsonCudaActivities;
    picojson::array jsonGPUs;
#ifdef TANGOR_KVSP_STARPU_ASYNC
    std::map<int, std::vector<std::pair<std::uint64_t, int>>> gpuActiveEvents,
        gpuBacklogEvents;
    std::map<std::pair<int, std::string>,
             std::vector<std::pair<std::uint64_t, int>>> poolActiveEvents,
        poolQueueEvents;
    std::unordered_map<std::uint64_t, int> operationNodes;
    for (const auto& operation : tangorSnapshot.operations) {
        operationNodes[operation.id] = operation.nodeId;
        picojson::object value;
        value.emplace("id", jsonNumber(operation.id));
        value.emplace("node_id", static_cast<double>(operation.nodeId));
        value.emplace("resource", operation.resource);
        value.emplace("kind", operation.kind);
        value.emplace("submitted_ns", jsonNumber(operation.submittedNs));
        value.emplace("queued_ns", jsonNumber(operation.queuedNs));
        value.emplace("started_ns", jsonNumber(operation.startedNs));
        value.emplace("finished_ns", jsonNumber(operation.finishedNs));
        value.emplace("thread_cpu_ns", jsonNumber(operation.threadCpuNs));
        value.emplace("device", static_cast<double>(operation.device));
        value.emplace("stream", jsonNumber(operation.stream));
        value.emplace("batch", static_cast<double>(operation.batch));
        jsonOperations.emplace_back(value);
        const auto operationNode = nodes_.find(operation.nodeId);
        if (operation.threadCpuNs != 0 && operationNode != nodes_.end()) {
            cpuByKind[operationNode->second.label.kind] += operation.threadCpuNs;
            cpuByDomain[domains[operation.nodeId]] += operation.threadCpuNs;
        }
        if (chainSet.count(operation.nodeId)) {
            chainThreadCPUNanoseconds += operation.threadCpuNs;
            const std::uint64_t operationQueue =
                operation.startedNs >= operation.queuedNs
                    ? operation.startedNs - operation.queuedNs : 0;
            if (operation.resource == "starpu_cpu")
                chainStarpuQueueNanoseconds += operationQueue;
            else if (operation.resource == "cuda_gate" ||
                     operation.resource == "cuda_ram")
                chainCudaPoolQueueNanoseconds += operationQueue;
            if (operation.queuedNs >= operation.submittedNs)
                chainStarpuQueueNanoseconds +=
                    operation.queuedNs - operation.submittedNs;
        }
        if (operation.device >= 0) {
            const auto pool = std::make_pair(operation.device,
                                             operation.resource);
            poolActiveEvents[pool].emplace_back(operation.startedNs, 1);
            poolActiveEvents[pool].emplace_back(operation.finishedNs, -1);
            poolQueueEvents[pool].emplace_back(operation.queuedNs, 1);
            poolQueueEvents[pool].emplace_back(operation.startedNs, -1);
            if (operation.startedNs >= operation.queuedNs) {
                gpuBacklogEvents[operation.device].emplace_back(operation.queuedNs, 1);
                gpuBacklogEvents[operation.device].emplace_back(operation.startedNs, -1);
            }
        }
    }
    for (const auto& activity : tangorSnapshot.cudaActivities) {
        picojson::object value;
        value.emplace("operation_id", jsonNumber(activity.operationId));
        value.emplace("kind", activity.kind);
        value.emplace("name", activity.name);
        value.emplace("start_ns", jsonNumber(activity.startNs));
        value.emplace("end_ns", jsonNumber(activity.endNs));
        value.emplace("bytes", jsonNumber(activity.bytes));
        value.emplace("device", static_cast<double>(activity.device));
        value.emplace("stream", jsonNumber(activity.stream));
        picojson::array grid{picojson::value(static_cast<double>(activity.gridX)),
                             picojson::value(static_cast<double>(activity.gridY)),
                             picojson::value(static_cast<double>(activity.gridZ))};
        picojson::array block{picojson::value(static_cast<double>(activity.blockX)),
                              picojson::value(static_cast<double>(activity.blockY)),
                              picojson::value(static_cast<double>(activity.blockZ))};
        value.emplace("grid", grid);
        value.emplace("block", block);
        jsonCudaActivities.emplace_back(value);
        const auto activityNodeId = operationNodes.find(activity.operationId);
        if (activityNodeId != operationNodes.end() &&
            chainSet.count(activityNodeId->second) &&
            activity.endNs >= activity.startNs) {
            const std::uint64_t duration = activity.endNs - activity.startNs;
            if (activity.name == "h2d") chainH2DNanoseconds += duration;
            if (activity.name == "d2h") chainD2HNanoseconds += duration;
        }
        if (activity.kind == "kernel" && activity.device >= 0 &&
            activity.endNs >= activity.startNs) {
            gpuActiveEvents[activity.device].emplace_back(activity.startNs, 1);
            gpuActiveEvents[activity.device].emplace_back(activity.endNs, -1);
            const auto operationNodeId = operationNodes.find(activity.operationId);
            if (operationNodeId != operationNodes.end()) {
                const auto operationNode = nodes_.find(operationNodeId->second);
                if (operationNode != nodes_.end()) {
                    const std::uint64_t duration =
                        activity.endNs - activity.startNs;
                    gpuByKind[operationNode->second.label.kind] += duration;
                    gpuByDomain[domains[operationNodeId->second]] += duration;
                    if (chainSet.count(operationNodeId->second))
                        chainCudaKernelNanoseconds += duration;
                }
            }
        }
    }
    const auto timelineStats = [](auto events) {
        std::sort(events.begin(), events.end());
        std::uint64_t duration = 0, integral = 0, previous = 0;
        int active = 0, maximum = 0;
        for (const auto& [timestamp, delta] : events) {
            if (active > 0) {
                duration += timestamp - previous;
                integral += static_cast<std::uint64_t>(active) *
                            (timestamp - previous);
            }
            active += delta;
            maximum = std::max(maximum, active);
            previous = timestamp;
        }
        return std::make_tuple(duration, integral, maximum);
    };
    for (const auto& [device, events] : gpuActiveEvents) {
        const auto [activeNs, activeKernelNs, maximumKernels] = timelineStats(events);
        const auto [gpuBacklogNs, backlogJobNs, maximumBacklog] =
            timelineStats(gpuBacklogEvents[device]);
        const double activeFraction = wallNanoseconds == 0
            ? 0.0 : static_cast<double>(activeNs) / wallNanoseconds;
        const double gpuBacklogFraction = wallNanoseconds == 0
            ? 0.0 : static_cast<double>(gpuBacklogNs) / wallNanoseconds;
        maximumGPUActiveFraction = std::max(maximumGPUActiveFraction,
                                            activeFraction);
        maximumGPUBacklogFraction = std::max(maximumGPUBacklogFraction,
                                             gpuBacklogFraction);
        picojson::object gpu;
        gpu.emplace("device", static_cast<double>(device));
        gpu.emplace("host_observed_active_ns", jsonNumber(activeNs));
        gpu.emplace("host_observed_active_fraction", activeFraction);
        gpu.emplace("backlog_ns", jsonNumber(gpuBacklogNs));
        gpu.emplace("backlog_fraction", gpuBacklogFraction);
        gpu.emplace("maximum_concurrent_kernels",
                    static_cast<double>(maximumKernels));
        gpu.emplace("maximum_backlog", static_cast<double>(maximumBacklog));
        gpu.emplace("active_kernel_ns", jsonNumber(activeKernelNs));
        gpu.emplace("backlog_job_ns", jsonNumber(backlogJobNs));
        const unsigned gateBudget =
            static_cast<size_t>(device) < tangorSnapshot.gateBlockBudgets.size()
                ? tangorSnapshot.gateBlockBudgets[device] : 0;
        const unsigned ramBudget =
            static_cast<size_t>(device) < tangorSnapshot.ramBlockBudgets.size()
                ? tangorSnapshot.ramBlockBudgets[device] : 0;
        gpu.emplace("gate_block_budget", static_cast<double>(gateBudget));
        gpu.emplace("ram_block_budget", static_cast<double>(ramBudget));
        for (const std::string& resource : {std::string("cuda_gate"),
                                            std::string("cuda_ram")}) {
            const auto key = std::make_pair(device, resource);
            const auto [poolBusy, poolIntegral, poolMaximum] =
                timelineStats(poolActiveEvents[key]);
            const auto [poolQueued, queueIntegral, queueMaximum] =
                timelineStats(poolQueueEvents[key]);
            picojson::object pool;
            pool.emplace("resource", resource);
            pool.emplace("busy_ns", jsonNumber(poolBusy));
            pool.emplace("active_job_ns", jsonNumber(poolIntegral));
            pool.emplace("maximum_active_jobs",
                         static_cast<double>(poolMaximum));
            pool.emplace("queued_job_ns", jsonNumber(queueIntegral));
            pool.emplace("queue_nonempty_ns", jsonNumber(poolQueued));
            pool.emplace("maximum_queue_depth",
                         static_cast<double>(queueMaximum));
            auto found = gpu.find("pools");
            if (found == gpu.end()) {
                gpu.emplace("pools", picojson::array{});
                found = gpu.find("pools");
            }
            found->second.get<picojson::array>().emplace_back(pool);
        }
        jsonGPUs.emplace_back(gpu);
    }
#endif
    const bool cpuSaturated =
        cpuUtilization >= 0.90 && backlogFraction >= 0.20;
    const bool gpuSaturated = maximumGPUActiveFraction >= 0.90 &&
                              maximumGPUBacklogFraction >= 0.20;
    const bool saturated = cpuSaturated || gpuSaturated;
    std::string classification = "inconclusive";
    if (saturated && chainCoverage >= 0.70)
        classification = "mixed";
    else if (saturated)
        classification = "throughput-bound";
    else if (chainCoverage >= 0.80 && chainQueueFraction <= 0.10)
        classification = "dependency-bound";
#ifdef TANGOR_KVSP_STARPU_ASYNC
    if (!tangorSnapshot.complete)
        classification = "inconclusive";
#endif
    const std::string confidence =
        classification == "inconclusive" ? "low" : "high";
    const std::string recommendation =
        classification == "throughput-bound"
            ? "reduce-total-work"
            : classification == "dependency-bound"
                  ? "shorten-critical-path"
                  : classification == "mixed"
                        ? "shorten-critical-path-and-reduce-total-work"
                        : "collect-more-complete-cycles";

    picojson::array jsonNodes;
    for (const auto& [id, n] : nodes_) {
        picojson::object value;
        value.emplace("id", static_cast<double>(id));
        value.emplace("index", static_cast<double>(n.index));
        value.emplace("kind", n.label.kind);
        value.emplace("description", n.label.desc);
        value.emplace("domain", domains[id]);
        value.emplace("critical", chainSet.count(id) != 0);
        value.emplace("thread_cpu_ns", jsonNumber(n.cpuNanoseconds));
        if (n.readyCritical)
            value.emplace("ready_ns",
                          jsonNumber(relativeNanoseconds(*n.readyCritical)));
        if (n.startCritical)
            value.emplace("start_ns",
                          jsonNumber(relativeNanoseconds(*n.startCritical)));
        if (n.endCritical)
            value.emplace("end_ns",
                          jsonNumber(relativeNanoseconds(*n.endCritical)));
        if (n.releasedBy)
            value.emplace("released_by", static_cast<double>(*n.releasedBy));
        jsonNodes.emplace_back(value);
    }
    picojson::array jsonEdges;
    for (const auto& edge : edges_) {
        picojson::object value;
        value.emplace("index", static_cast<double>(edge.index));
        value.emplace("from", static_cast<double>(edge.from));
        value.emplace("to", static_cast<double>(edge.to));
        value.emplace("notify_ns",
                      jsonNumber(relativeNanoseconds(edge.notifiedCritical)));
        value.emplace("causal", edge.causal);
        value.emplace("released", edge.released);
        jsonEdges.emplace_back(value);
    }
    picojson::array jsonChain;
    for (const int id : reverseChain) jsonChain.emplace_back(static_cast<double>(id));

    picojson::object resources;
    picojson::object cpu;
    cpu.emplace("workers", static_cast<double>(cpuWorkers));
    cpu.emplace("process_cpu_ns", jsonNumber(processCPUUsedNanoseconds));
    cpu.emplace("utilization", cpuUtilization);
    cpu.emplace("frontend_backlog_fraction", backlogFraction);
    resources.emplace("cpu", cpu);
    resources.emplace("gpus", jsonGPUs);

    picojson::object analysis;
    analysis.emplace("classification", classification);
    analysis.emplace("confidence", confidence);
    analysis.emplace("recommendation", recommendation);
    analysis.emplace("critical_chain", jsonChain);
    analysis.emplace("critical_chain_service_ns",
                     jsonNumber(chainServiceNanoseconds));
    analysis.emplace("critical_chain_queue_ns",
                     jsonNumber(chainQueueNanoseconds));
    analysis.emplace("critical_chain_coverage", chainCoverage);
    analysis.emplace("critical_chain_queue_fraction", chainQueueFraction);
    picojson::object breakdown;
    breakdown.emplace("frontend_ready_queue_ns",
                      jsonNumber(chainQueueNanoseconds));
    breakdown.emplace("thread_cpu_ns",
                      jsonNumber(chainThreadCPUNanoseconds));
    breakdown.emplace("starpu_queue_ns",
                      jsonNumber(chainStarpuQueueNanoseconds));
    breakdown.emplace("cuda_pool_queue_ns",
                      jsonNumber(chainCudaPoolQueueNanoseconds));
    breakdown.emplace("cuda_kernel_ns",
                      jsonNumber(chainCudaKernelNanoseconds));
    breakdown.emplace("h2d_ns", jsonNumber(chainH2DNanoseconds));
    breakdown.emplace("d2h_ns", jsonNumber(chainD2HNanoseconds));
    const std::uint64_t explained =
        std::min(chainServiceNanoseconds,
                 chainThreadCPUNanoseconds + chainStarpuQueueNanoseconds +
                     chainCudaPoolQueueNanoseconds +
                     chainCudaKernelNanoseconds + chainH2DNanoseconds +
                     chainD2HNanoseconds);
    breakdown.emplace("unexplained_node_service_ns",
                      jsonNumber(chainServiceNanoseconds - explained));
    analysis.emplace("critical_breakdown", breakdown);
    analysis.emplace("total_by_kind", rankedDurations(totalByKind));
    analysis.emplace("total_by_domain", rankedDurations(totalByDomain));
    analysis.emplace("critical_by_kind", rankedDurations(chainByKind));
    analysis.emplace("critical_by_domain", rankedDurations(chainByDomain));
    analysis.emplace("cpu_by_kind", rankedDurations(cpuByKind));
    analysis.emplace("cpu_by_domain", rankedDurations(cpuByDomain));
    analysis.emplace("gpu_by_kind", rankedDurations(gpuByKind));
    analysis.emplace("gpu_by_domain", rankedDurations(gpuByDomain));

    picojson::object completeness;
    bool profileComplete = graphValid;
#ifdef TANGOR_KVSP_STARPU_ASYNC
    profileComplete = profileComplete && tangorSnapshot.complete;
#endif
    completeness.emplace("complete", profileComplete);
    completeness.emplace("causal_graph_valid", graphValid);
    completeness.emplace("topological_nodes",
                         static_cast<double>(topologicalNodes));
    completeness.emplace("total_nodes", static_cast<double>(nodes_.size()));
#ifdef TANGOR_KVSP_STARPU_ASYNC
    completeness.emplace("runtime_records_complete", tangorSnapshot.complete);
    completeness.emplace("dropped_runtime_records",
                         jsonNumber(tangorSnapshot.droppedRecords));
#else
    completeness.emplace("runtime_records_complete", true);
    completeness.emplace("dropped_runtime_records", 0.0);
#endif

    picojson::object config;
    config.emplace("cpu_workers", static_cast<double>(cpuWorkers));
#ifdef TANGOR_KVSP_STARPU_ASYNC
    config.emplace("clock_calibration_uncertainty_ns",
                   jsonNumber(tangorSnapshot.clockCalibrationUncertaintyNs));
#endif

    picojson::object root;
    root.emplace("schema_version", 1.0);
    root.emplace("cycle", static_cast<double>(cycle));
    root.emplace("wall_ns", jsonNumber(wallNanoseconds));
    root.emplace("config", config);
    root.emplace("nodes", jsonNodes);
    root.emplace("edges", jsonEdges);
    root.emplace("operations", jsonOperations);
    root.emplace("cuda_activities", jsonCudaActivities);
    root.emplace("resources", resources);
    root.emplace("analysis", analysis);
    root.emplace("completeness", completeness);
    os << picojson::value(root);

    std::cerr << "CPROF:cycle=" << cycle
              << " wall_ms=" << static_cast<double>(wallNanoseconds) / 1.0e6
              << " chain_ms="
              << static_cast<double>(chainServiceNanoseconds) / 1.0e6
              << " cpu_util=" << cpuUtilization
              << " backlog=" << backlogFraction
              << " gpu_active=" << maximumGPUActiveFraction
              << " gpu_backlog=" << maximumGPUBacklogFraction << '\n';
    std::cerr << "CPROF:class=" << classification
              << " confidence=" << confidence
              << " complete=" << (profileComplete ? 1 : 0)
              << " recommendation=" << recommendation << '\n';
    const auto largestName = [](const auto& values) {
        if (values.empty()) return std::string("none");
        return std::max_element(
                   values.begin(), values.end(),
                   [](const auto& left, const auto& right) {
                       return left.second != right.second
                                  ? left.second < right.second
                                  : left.first > right.first;
                   })->first;
    };
    std::cerr << "CPROF:critical=" << largestName(chainByKind)
              << " work=" << largestName(totalByDomain) << '\n';
}

namespace graph {
std::unordered_map<int, int> doRankuSort(
    const std::unordered_map<int, graph::NodePtr>& id2node)
{
    // c.f. https://en.wikipedia.org/wiki/Heterogeneous_Earliest_Finish_Time
    // FIXME: Take communication costs into account
    // FIXME: Tune computation costs by dynamic measurements

    std::unordered_map<std::string, int> compCost = {
        {"DFF", 0},          {"WIRE", 0},     {"INPUT", 0},
        {"OUTPUT", 0},       {"AND", 10},     {"NAND", 10},
        {"ANDNOT", 10},      {"OR", 10},      {"NOR", 10},
        {"ORNOT", 10},       {"XOR", 10},     {"XNOR", 10},
        {"MUX", 20},         {"NOT", 0},      {"CONSTONE", 0},
        {"CONSTZERO", 0},    {"CB", 100},     {"CBInv", 100},
        {"CBWithInv", 100},  {"MUXWoSE", 20}, {"CMUXs", 10},
        {"SEI", 0},          {"GB", 10},      {"ROMUX", 10},
        {"ROMNormalize", 10},
        {"RAMWriteChunk", 10},
        {"RAMUX", 10},       {"SEI&KS", 5},   {"cufhe2tfhepp", 0},
        {"tfhepp2cufhe", 0}, {"bridge", 0},   {"RAMWriter", 0},
        {"RAMReader", 0},    {"ROM", 0},      {"SDFF", 0},
    };

    auto isPseudoInit = [&](int id) {
        return id2node.at(id)->hasNoInputsToWaitFor;
    };

    // Make a map from id to the number of ready children of the node
    std::unordered_map<NodePtr, int> numReadyChildren;
    for (auto&& [id, node] : id2node) {
        size_t n = std::count_if(node->children.begin(), node->children.end(),
                                 isPseudoInit);
        numReadyChildren.emplace(node, n);
    }

    std::queue<NodePtr> que;
    for (auto&& [id, node] : id2node) {
        // Initial nodes should be "terminals", that is,
        // they have no children OR all of their children has no inputs to wait
        // for.
        if (std::all_of(node->children.begin(), node->children.end(),
                        isPseudoInit))
            que.push(node);
    }
    assert(!que.empty());

    std::unordered_map<NodePtr, int> node2pri;
    while (!que.empty()) {
        auto node = que.front();
        que.pop();

        // Calculate the priority for the node
        int pri = 0;
        for (auto&& childId : node->children) {
            NodePtr child = id2node.at(childId);
            if (!child->hasNoInputsToWaitFor)
                pri = std::max(pri, node2pri.at(child));
        }
        auto it = compCost.find(node->label.kind);
        if (it == compCost.end())
            error::die("Internal error: compCost does not have key: ",
                       node->label.kind);
        int w = it->second;
        auto [it2, inserted] = node2pri.emplace(node, pri + w);
        assert(inserted);

        if (node->hasNoInputsToWaitFor)
            continue;

        for (auto parentId : node->parents) {
            NodePtr parent = id2node.at(parentId);
            numReadyChildren.at(parent)++;
            assert(parent->children.size() >= numReadyChildren.at(parent));
            if (parent->children.size() == numReadyChildren.at(parent))
                que.push(parent);
        }
    }
    if (id2node.size() > node2pri.size()) {
        spdlog::debug("id2node {} != node2pri {}", id2node.size(),
                      node2pri.size());
        for (auto&& [id, node] : id2node) {
            auto it = node2pri.find(node);
            if (it == node2pri.end()) {
                spdlog::debug("\t{} {} {}", node->label.id, node->label.kind,
                              node->label.desc);
            }
        }
        error::die("Invalid network; some nodes will not be executed.");
    }
    assert(id2node.size() == node2pri.size());

    std::unordered_map<int, int> id2pri;
    for (auto&& [node, pri] : node2pri)
        id2pri[node->label.id] = pri;

    return id2pri;
}

std::unordered_map<int, int> doTopologicalSort(
    const std::unordered_map<int, graph::NodePtr>& id2node)
{
    // Make a map from id to the number of ready parents of the node
    std::unordered_map<NodePtr, int> numReadyParents;
    for (auto&& [id, node] : id2node)
        numReadyParents[node] = 0;

    // Make the initial queue for sorting
    std::queue<NodePtr> que;
    for (auto&& [id, node] : id2node)
        if (node->hasNoInputsToWaitFor)
            que.push(node);

    // Do topological sort
    std::unordered_map<NodePtr, int> node2index;
    while (!que.empty()) {
        auto node = que.front();
        que.pop();

        // Get the index for node
        int index = -1;
        if (!node->hasNoInputsToWaitFor) {
            for (auto&& parentId : node->parents) {
                NodePtr parent = id2node.at(parentId);
                index = std::max(index, node2index.at(parent));
            }
        }
        node2index[node] = index + 1;

        for (auto&& childId : node->children) {
            NodePtr child = id2node.at(childId);
            if (child->hasNoInputsToWaitFor)  // false parent-child
                                              // relationship
                continue;
            numReadyParents.at(child)++;
            assert(child->parents.size() >= numReadyParents.at(child));
            if (child->parents.size() == numReadyParents.at(child))
                que.push(child);
        }
    }

    if (id2node.size() > node2index.size()) {
        spdlog::debug("id2node {} != node2index {}", id2node.size(),
                      node2index.size());
        for (auto&& [id, node] : id2node) {
            auto it = node2index.find(node);
            if (it == node2index.end()) {
                spdlog::debug("\t{} {} {}", node->label.id, node->label.kind,
                              node->label.desc);
            }
        }
        error::die("Invalid network; some nodes will not be executed.");
    }
    assert(id2node.size() == node2index.size());

    std::unordered_map<int, int> id2index;
    for (auto&& [node, index] : node2index)
        id2index[node->label.id] = index;

    return id2index;
}
}  // namespace graph
