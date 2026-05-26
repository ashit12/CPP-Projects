#pragma once
#include <utility>

// Type-erased task interface used by WorkerPool's queues.
//
// The queues store BaseTask* so they can hold any callable type behind
// a single pointer. Ownership of every BaseTask* placed into a queue
// transfers to whichever thread pops it.
class BaseTask 
{
    public:
        virtual ~BaseTask() = default;
        virtual void execute() = 0;

};

// Concrete task that owns a callable and invokes it on execute().
// Typically instantiated with std::packaged_task as the payload, which
// is single-shot: execute() must not be called more than once.
template<typename Task>
class DerivedTask final : public  BaseTask
{
    Task task_;
    public:
        explicit DerivedTask(Task t) : task_(std::move(t)) {}
        void execute() override {
            task_();
        }
        
};