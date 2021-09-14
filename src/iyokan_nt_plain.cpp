#include "iyokan_nt_plain.hpp"
#include "iyokan_nt.hpp"
#include "packet.hpp"

namespace nt {
namespace plain {

class WorkerInfo {
};

class Worker : public nt::Worker {
private:
    WorkerInfo wi_;

protected:
    bool canExecute(Task* task) override
    {
        return task->canRunPlain();
    }

    void startTask(Task* task) override
    {
        task->startAsynchronously(wi_);
    }
};

class TaskConstZero : public TaskCommon<Bit> {
public:
    TaskConstZero(Label label, Allocator& alc) : TaskCommon<Bit>(label, alc, 0)
    {
    }

    ~TaskConstZero()
    {
    }

    void startAsynchronously(WorkerInfo&) override
    {
        output() = 0_b;
    }

    bool hasFinished() const override
    {
        return true;
    }

    bool canRunPlain() const override
    {
        return true;
    }
};

class TaskConstOne : public TaskCommon<Bit> {
public:
    TaskConstOne(Label label, Allocator& alc) : TaskCommon<Bit>(label, alc, 0)
    {
    }

    ~TaskConstOne()
    {
    }

    void startAsynchronously(WorkerInfo&) override
    {
        output() = 1_b;
    }

    bool hasFinished() const override
    {
        return true;
    }

    bool canRunPlain() const override
    {
        return true;
    }
};

class TaskNand : public TaskCommon<Bit> {
public:
    TaskNand(Label label, Allocator& alc) : TaskCommon<Bit>(label, alc, 2)
    {
    }

    ~TaskNand()
    {
    }

    void startAsynchronously(WorkerInfo&) override
    {
        output() = ~(input(0) & input(1));
    }

    bool hasFinished() const override
    {
        return true;
    }

    bool canRunPlain() const override
    {
        return true;
    }
};

class NetworkBuilder : public nt::NetworkBuilder {
private:
    Allocator& alc_;
    std::unordered_map<UID, std::unique_ptr<TaskCommon<Bit>>> uid2common_;
    UID nextUID_;

private:
    UID genUID()
    {
        return nextUID_++;
    }

public:
    NetworkBuilder(Allocator& alc) : alc_(alc), uid2common_(), nextUID_(0)
    {
    }

    ~NetworkBuilder()
    {
    }

    // createNetwork destructs the builder
    Network createNetwork() override
    {
        std::unordered_map<UID, std::unique_ptr<Task>> uid2task;
        for (auto&& [uid, task] : uid2common_)
            uid2task.emplace(uid, std::move(task));
        uid2common_.clear();
        return Network{std::move(uid2task)};
    }

    void connect(UID fromUID, UID toUID) override
    {
        auto &from = uid2common_.at(fromUID), &to = uid2common_.at(toUID);
        to->addInput(from.get());
    }

#define DEF_COMMON_TASK(TaskType, capName, camelName)           \
    UID capName(const std::string& alcKey) override             \
    {                                                           \
        Allocator& subalc = alc_.subAllocator(alcKey);          \
        UID uid = genUID();                                     \
        auto task = std::make_unique<TaskType>(                 \
            Label{uid, {{TAG_KEY::KIND, #camelName}}}, subalc); \
        uid2common_.emplace(uid, std::move(task));              \
        return uid;                                             \
    }
    DEF_COMMON_TASK(TaskConstOne, CONSTONE, ConstOne);
    DEF_COMMON_TASK(TaskConstZero, CONSTZERO, ConstZero);
    DEF_COMMON_TASK(TaskNand, NAND, Nand);
#undef DEF_COMMON_TASK
};

/**************************************************/
/***** TEST ***************************************/
/**************************************************/

void test0()
{
    WorkerInfo wi;

    {
        Allocator root;
        TaskConstOne t{Label{1, {}}, root};
        t.startAsynchronously(wi);
        assert(t.hasFinished());
        assert(t.getOutput() == 1_b);
    }

    {
        Allocator root;
        TaskConstZero t{Label{1, {}}, root};
        t.startAsynchronously(wi);
        assert(t.hasFinished());
        assert(t.getOutput() == 0_b);
    }

    {
        Allocator root;
        Allocator &sub0 = root.subAllocator("0"),
                  &sub1 = root.subAllocator("1"),
                  &sub2 = root.subAllocator("2");
        TaskConstZero t0{Label{0, {}}, sub0};
        TaskConstOne t1{Label{1, {}}, sub1};
        TaskNand t2{Label{2, {}}, sub2};
        t2.addInput(&t0);
        t2.addInput(&t1);
        t0.startAsynchronously(wi);
        t1.startAsynchronously(wi);
        t2.startAsynchronously(wi);
        assert(t0.hasFinished() && t1.hasFinished() && t2.hasFinished());
        assert(t2.getOutput() == 1_b);
    }

    {
        Allocator root;
        NetworkBuilder nb{root};
        UID id0 = nb.CONSTZERO("0"), id1 = nb.CONSTONE("1"), id2 = nb.NAND("2");
        nb.connect(id0, id2);
        nb.connect(id1, id2);
        Network nw{nb.createNetwork()};
        Task* t0 = nw.findByUID(id0);
        Task* t1 = nw.findByUID(id1);
        Task* t2 = nw.findByUID(id2);
        assert(t0 != nullptr && t1 != nullptr && t2 != nullptr);
        t0->startAsynchronously(wi);
        t1->startAsynchronously(wi);
        t2->startAsynchronously(wi);
        assert(t0->hasFinished() && t1->hasFinished() && t2->hasFinished());
        assert(dynamic_cast<TaskCommon<Bit>*>(t2)->getOutput() == 1_b);
    }

    {
        Allocator root;
        NetworkBuilder nb{root};
        UID id0 = nb.CONSTZERO("0"), id1 = nb.CONSTONE("1"), id2 = nb.NAND("2");
        nb.connect(id0, id2);
        nb.connect(id1, id2);

        std::vector<std::unique_ptr<nt::Worker>> workers;
        workers.emplace_back(std::make_unique<Worker>());

        NetworkRunner runner{nb.createNetwork(), std::move(workers)};
        runner.prepareToRun();

        while (runner.numFinishedTargets() < runner.network().size()) {
            assert(runner.isRunning());
            runner.update();
        }

        Task* t0 = runner.network().findByUID(id0);
        Task* t1 = runner.network().findByUID(id1);
        Task* t2 = runner.network().findByUID(id2);
        assert(t0->hasFinished() && t1->hasFinished() && t2->hasFinished());
        assert(dynamic_cast<TaskCommon<Bit>*>(t2)->getOutput() == 1_b);
    }
}

}  // namespace plain
}  // namespace nt
