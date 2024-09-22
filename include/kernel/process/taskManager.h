#ifndef TASKMANAGER_H
#define TASKMANAGER_H

#include "process/task.h"
#include "process/scheduler.h"
#include "common/priorityQueue.h"
#include "x86_64.h"

namespace Task
{
    static constexpr uint64_t START_OF_PROCESS_KSTACKS = 0xFFFFCF8000000000;
    // Each task has a 64KiB kernel stack, so that deeply recursive kernel code such as the cScript interpreter has room.
    static constexpr uint64_t KERNEL_STACK_SIZE = 0x10000;

    class TaskManager
    {
        friend class Task;
        friend class Mutex;
    public:
        static TaskManager& instance()
        {
            static TaskManager instance;
            return instance;
        }
        void run();
        void add_task(Task task);
        void block_task();
        void unblock_task(TaskID taskID);
        Task create_new_task(SharableTaskResource<Memory::RegionableVirtualAddressSpace> tlTable, SharableTaskResource<FileDescriptorTable> openFileDescriptors,
                    SharableTaskResource<EventDescriptorTable> openEventDescriptors, uint64_t taskPriority = 512);
        Task create_new_kernel_task(uint64_t taskPriority = 512);
        // User-space programs each have their own address space, so that they cannot overwrite each other.
        Task create_new_user_task(uint64_t taskPriority = 512);
        // Add an event descriptor to a task.
        uint64_t add_ed(void* eventSender, void* param, TaskID task);
        // Remove an event descriptor from a task.
        void remove_ed(uint64_t ed, TaskID task);
        void lock() { m_spinlock.acquire(); }
        void unlock() { m_spinlock.release(); }
        static uint64_t get_current_task() { return TaskManager::instance().m_scheduler->get_current_task(); }
        Task* get_current_task_pointer() { return m_scheduler->get_task_from_tid(m_scheduler->get_current_task()); }
        Common::DoublyLinkedListIterator<Common::Pair<uint64_t, Events::EventDescriptor>> get_ed(uint64_t ped, TaskID tid);
        // Called by task switching assembly routine
        static void pit_tick();
        static void refresh();
        static uint64_t* get_safe_stack();
        static void save_kstack(void* kstack);
        static void save_kstack_with_tid(void* kstack, TaskID tid);
        static uint64_t* get_current_stack();
        static bool is_executing();
        static bool is_spinlock_acquired(); // need to check current cpu
        static bool is_scheduler_changing_task();
        static void terminate_task();
        // End the current task and wake the task waiting for it, if there is one. This does not return.
        void exit_task(uint64_t exitCode);
        // Block until a task exits, and return its exit code.
        uint64_t wait_for_task(TaskID tid);
        // Put current task to sleep until a later time.
        void sleep_until(uint64_t time);
        // Put current task to sleep for a duration.
        static void sleep_for(uint64_t duration);
    private:
        TaskManager();
        Scheduler* m_scheduler;
        static bool m_isActive;
        bool m_schedulerChangingTask = false;
        uint64_t m_numberOfTasks;
        void switch_to_task(Task* task);
        // Free everything that an exited task owns, and remove it from the scheduler.
        void reap_task(TaskID tid);
        Spinlock m_spinlock;
        // The FPU and SSE registers hold the state of this task, and new tasks start with the state saved at boot.
        TaskID m_fpuOwner = 0;
        uint8_t* m_initialFpuState;
        struct SleepingTask
        {
            TaskID task;
            uint64_t wakeTime;
            friend bool operator>(SleepingTask const& lhs, SleepingTask const& rhs)
            {
                return lhs.wakeTime < rhs.wakeTime;
            }
        };
        Common::PriorityQueue<SleepingTask> m_sleepingTasks;
    };
}

#endif
