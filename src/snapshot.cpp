#include "iyokan_nt.hpp"

#include <cereal/archives/portable_binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>

#include <fstream>

namespace nt {

/* class Snapshot */

Snapshot::Snapshot(const RunParameter& pr,
                   const std::shared_ptr<Allocator>& alc)
    : pr_(pr), alc_(alc)
{
}

Snapshot::Snapshot(const std::string& snapshotFile) : pr_(), alc_(nullptr)
{
    LOG_DBG_SCOPE("READ SNAPSHOT");

    LOG_DBG << "OPEN";
    std::ifstream ifs{snapshotFile};
    if (!ifs)
        ERR_DIE("Can't open a snapshot file to read from: " << snapshotFile);
    cereal::PortableBinaryInputArchive ar{ifs};

    // Read header
    LOG_DBG << "READ HEADER";
    std::string header;
    ar(header);
    if (header != "IYSS")  // IYokan SnapShot
        ERR_DIE(
            "Can't read the snapshot file; incorrect header: " << snapshotFile);

    // Read run parameters
    LOG_DBG << "READ RUN PARAMS";
    ar(pr_.blueprintFile, pr_.inputFile, pr_.outputFile, pr_.numCPUWorkers,
       pr_.numCycles, pr_.currentCycle, pr_.sched);

    // Read allocator
    LOG_DBG << "READ ALLOCATOR";
    alc_.reset(new Allocator(ar));
}

const RunParameter& Snapshot::getRunParam() const
{
    return pr_;
}

const std::shared_ptr<Allocator>& Snapshot::getAllocator() const
{
    return alc_;
}

void Snapshot::dump(const std::string& snapshotFile) const
{
    std::ofstream ofs{snapshotFile};
    if (!ofs)
        ERR_DIE("Can't open a snapshot file to write in: " << snapshotFile);
    cereal::PortableBinaryOutputArchive ar{ofs};

    // Write header
    // FIXME: much better way to store the header?
    ar(std::string{"IYSS"});  // IYokan SnapShot

    // Serialize pr_ of class RunParameter
    ar(pr_.blueprintFile, pr_.inputFile, pr_.outputFile, pr_.numCPUWorkers,
       pr_.numCycles, pr_.currentCycle, pr_.sched);

    // Serialize alc_ of class Allocator
    alc_->dumpAllocatedData(ar);
}

void Snapshot::updateCurrentCycle(int currentCycle)
{
    pr_.currentCycle = currentCycle;
}

void Snapshot::updateNumCycles(int numCycles)
{
    pr_.numCycles = numCycles;
}

}  // namespace nt
