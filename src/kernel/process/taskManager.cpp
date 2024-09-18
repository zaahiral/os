#include "process/task.h"
#include "process/taskManager.h"
#include "cpu/tss.h"
#include "memory/AddressSpace.h"
#include "process/masterScheduler.h"
#include "x86_64.h"
#include "memory/MemoryManager.h"
#include "cpu/io/pit.h"

namespace Task
{
    extern "C" void enable_syscall_sysret();
    extern "C" void start_kernel_task_iretq(void* entryPoint, void* kstack);
    extern "C" void return_to_task(void* tlTable);
    extern "C" void send_pit_eoi();
    extern "C" void block_task_and_refresh(uint64_t tid);

    bool TaskManager::m_isActive = false;

    TaskManager::TaskManager()
    {
        enable_syscall_sysret();
        Memory::MemoryManager::instance().alloc_page(Memory::VirtualMemoryAllocationRequest(Memory::VirtualAddress(START_OF_PROCESS_KSTACKS), true));
        CPU::update_kernel_stack_pointer(get_safe_stack());
        CPU::refresh_task_state_segment();
        m_scheduler = new MasterScheduler();
    }

    Task TaskManager::create_new_task(SharableTaskResource<Memory::RegionableVirtualAddressSpace> tlTable, SharableTaskResource<FileDescriptorTable> openFileDescriptors,
                    SharableTaskResource<EventDescriptorTable> openEventDescriptors, uint64_t taskPriority)
    {
        auto task = Task(tlTable, openFileDescriptors, openEventDescriptors);
        task.set_task_id(++m_numberOfTasks);
        // Queue 0: 0-255
        // Queue 1: 256-511
        // Queue 2: 512-767
        task.set_priority(taskPriority);

        // At the moment, task stacks are allocated contiguously so we can allocate the stack at a position based on the task id.
        auto stackBottom = (START_OF_PROCESS_KSTACKS + KERNEL_STACK_SIZE * (task.get_task_id()));

        // We need to map a safe kernel stack and the task's kernel stack into the task's address space.
        Memory::MemoryManager::instance().request_virtual_map(Memory::VirtualMemoryMapRequest(Memory::MemoryManager::instance().get_physical_address(START_OF_PROCESS_KSTACKS), Memory::VirtualAddress(START_OF_PROCESS_KSTACKS), true), task.get_tlTable());
        for (auto page = stackBottom; page < stackBottom + KERNEL_STACK_SIZE; page += Memory::PAGE_4KiB)
        {
            Memory::MemoryManager::instance().alloc_page(Memory::VirtualMemoryAllocationRequest(Memory::VirtualAddress(page), true));
            Memory::MemoryManager::instance().request_virtual_map(Memory::VirtualMemoryMapRequest(Memory::MemoryManager::instance().get_physical_address(page), Memory::VirtualAddress(page), true), task.get_tlTable());
        }

        task.set_kstack(reinterpret_cast<void*>(stackBottom + KERNEL_STACK_SIZE));
        return task;
    }

    Task TaskManager::create_new_kernel_task(uint64_t taskPriority)
    {
        return create_new_task(SharableTaskResource<Memory::RegionableVirtualAddressSpace>(Memory::RegionableVirtualAddressSpace(Memory::MemoryManager::instance().get_main_address_space())), SharableTaskResource<FileDescriptorTable>(FileDescriptorTable()), SharableTaskResource<EventDescriptorTable>(EventDescriptorTable()), taskPriority);
    }

    Task TaskManager::create_new_user_task(uint64_t taskPriority)
    {
        return create_new_task(SharableTaskResource<Memory::RegionableVirtualAddressSpace>(Memory::RegionableVirtualAddressSpace(Memory::MemoryManager::instance().create_user_address_space())), SharableTaskResource<FileDescriptorTable>(FileDescriptorTable()), SharableTaskResource<EventDescriptorTable>(EventDescriptorTable()), taskPriority);
    }

    void TaskManager::switch_to_task(Task* task)
    {
        // Acquire spinlock if we do not already have it.
        if (!is_spinlock_acquired()) { m_spinlock.acquire(); }

        if (task->m_state == Task::TaskState::NOT_STARTED)
        {
            // If the task has not already been started then we need to fake an iret stack.
            task->m_state = Task::TaskState::READY;
            Memory::MemoryManager::instance().switch_to_address_space(task->get_tlTable());
            CPU::update_kernel_stack_pointer(task->get_kstack());
            auto entryPoint = task->get_entry_point();
            auto kstack = task->get_kstack();
            m_spinlock.release();
            start_kernel_task_iretq(entryPoint, kstack);
        }
        else
        {
            // Update kernel stack pointer but add the size of the iretq stack as this will be popped.
            CPU::update_kernel_stack_pointer(reinterpret_cast<void*>(reinterpret_cast<uint64_t>(task->get_kstack()) + SIZE_OF_IRETQ_STACK));
            auto tlTable = (uint64_t*)((*(task->m_tlTable)).get_physical_address().get());
            m_spinlock.release();
            return_to_task(tlTable);
        }
    }

    void TaskManager::add_task(Task task)
    {
        m_spinlock.acquire();
        m_scheduler->add_task(task);
        m_spinlock.release();
    }

    void TaskManager::pit_tick()
    {
        send_pit_eoi();
        auto currentTime = CPU::PIT::time_since_boot();

        // Check for sleeping tasks and unblock any that have slept for the correct amount of time.
        while (TaskManager::instance().m_sleepingTasks.size() > 0)
        {
            auto task = TaskManager::instance().m_sleepingTasks.top();
            if (currentTime >= task.wakeTime)
            {
                TaskManager::instance().m_sleepingTasks.pop();
                TaskManager::instance().unblock_task(task.task);
            }
            else
                break;
        }
        // If the scheduler is already changing task, let the scheduler refresh.
        if (TaskManager::instance().is_scheduler_changing_task())
            return;
        TaskManager::instance().refresh();
    }

    void TaskManager::sleep_for(uint64_t duration)
    {
        auto currentTime = CPU::PIT::time_since_boot();
        TaskManager::instance().sleep_until(currentTime + duration);
    }

    void TaskManager::sleep_until(uint64_t time)
    {
        m_spinlock.acquire();
        auto currentTime = CPU::PIT::time_since_boot();
        if (currentTime >= time)
        {
            m_spinlock.release();
            return;
        }

        SleepingTask sleepingTask;
        sleepingTask.task = get_current_task();
        sleepingTask.wakeTime = time;
        // Add sleeping task to list of sleeping tasks that is checked on every timer interrupt.
        m_sleepingTasks.push(sleepingTask);
        block_task();
    }

    void TaskManager::refresh()
    {
        if (!TaskManager::instance().is_spinlock_acquired()) { TaskManager::instance().m_spinlock.acquire(); }

        TaskManager::instance().m_schedulerChangingTask = true;
        TaskManager::instance().m_scheduler->move_to_next_task(TaskManager::instance().m_spinlock);
        TaskManager::instance().m_schedulerChangingTask = false;
        auto currentTask = TaskManager::instance().m_scheduler->get_current_task();
        TaskManager::instance().switch_to_task(TaskManager::instance().m_scheduler->get_task_from_tid(currentTask));
    }

    void TaskManager::run()
    {
        m_spinlock.acquire();
        m_schedulerChangingTask = true;
        m_scheduler->move_to_next_task(m_spinlock);
        m_schedulerChangingTask = false;
        m_isActive = true;
        switch_to_task(m_scheduler->get_task_from_tid(m_scheduler->get_current_task()));
    }

    uint64_t* TaskManager::get_safe_stack() { return reinterpret_cast<uint64_t*>(START_OF_PROCESS_KSTACKS + 0x1000); }

    void TaskManager::save_kstack(void* kstack)
    {
        TaskManager::instance().m_spinlock.acquire();
        TaskManager::instance().m_scheduler->get_task_from_tid(TaskManager::instance().m_scheduler->get_current_task())->set_kstack(kstack);
        TaskManager::instance().m_spinlock.release();
    }

    void TaskManager::save_kstack_with_tid(void* kstack, TaskID tid)
    {
        TaskManager::instance().m_spinlock.acquire();
        TaskManager::instance().m_scheduler->get_task_from_tid(tid)->set_kstack(kstack);
        TaskManager::instance().m_spinlock.release();
    }

    uint64_t* TaskManager::get_current_stack() {
        TaskManager::instance().m_spinlock.acquire();
        auto kstack = reinterpret_cast<uint64_t*>(TaskManager::instance().m_scheduler->get_task_from_tid(TaskManager::instance().m_scheduler->get_current_task())->get_kstack());
        TaskManager::instance().m_spinlock.release();
        return kstack;
    }

    bool TaskManager::is_executing()
    {
        return m_isActive;
    }

    bool TaskManager::is_spinlock_acquired()
    {
        return TaskManager::instance().m_spinlock.is_acquired();
    }

    bool TaskManager::is_scheduler_changing_task()
    {
        return TaskManager::instance().m_schedulerChangingTask;
    }

    void TaskManager::block_task()
    {
        if (!is_spinlock_acquired()) { m_spinlock.acquire(); }
        auto currentTask = m_scheduler->get_current_task();
        m_scheduler->block_task();
        // Releasing the spinlock re-enables interrupts, so we stop a timer interrupt from switching away from this blocked task.
        m_schedulerChangingTask = true;
        m_spinlock.release();
        block_task_and_refresh(currentTask);
    }

    void TaskManager::unblock_task(TaskID taskID)
    {
        m_spinlock.acquire();
        m_scheduler->unblock_task(taskID);
        m_spinlock.release();
    }

    void TaskManager::terminate_task()
    {
        if (!m_isActive) { return; }
        TaskManager::instance().block_task();
    }

    void TaskManager::exit_task(uint64_t exitCode)
    {
        m_spinlock.acquire();
        auto task = get_current_task_pointer();
        task->m_hasExited = true;
        task->m_exitCode = exitCode;
        auto waitingTask = task->m_waitingTask;
        m_spinlock.release();
        if (waitingTask) { unblock_task(waitingTask); }

        // TODO: Free the task's kernel stack and memory, which are kept for now.
        while (1) { block_task(); }
    }

    uint64_t TaskManager::wait_for_task(TaskID tid)
    {
        m_spinlock.acquire();
        auto task = m_scheduler->get_task_from_tid(tid);
        if (!task || tid == get_current_task())
        {
            m_spinlock.release();
            return -1;
        }
        // If the task has not exited yet, we block until it wakes us as it exits.
        if (!task->m_hasExited)
        {
            task->m_waitingTask = get_current_task();
            block_task();
            m_spinlock.acquire();
            task = m_scheduler->get_task_from_tid(tid);
        }
        auto exitCode = task->m_exitCode;
        m_spinlock.release();
        return exitCode;
    }

    uint64_t TaskManager::add_ed(void* eventSender, void* param, TaskID taskID)
    {
        m_spinlock.acquire();
        auto task = m_scheduler->get_task_from_tid(taskID);
        Events::EventDescriptor tasked;
        tasked.eventSender = eventSender;
        tasked.param = param;
        tasked.process = (void*)taskID;
        auto edNumber = (*task->m_openEventDescriptors).insert(tasked)->first;
        m_spinlock.release();
        return edNumber;
    }

    Common::DoublyLinkedListIterator<Common::Pair<uint64_t, Events::EventDescriptor>> TaskManager::get_ed(uint64_t ped, TaskID tid)
    {
        auto task = m_scheduler->get_task_from_tid(tid);
        return (*task->m_openEventDescriptors).get(ped);
    }

    void TaskManager::remove_ed(uint64_t ed, TaskID taskID)
    {
        m_spinlock.acquire();
        auto task = m_scheduler->get_task_from_tid(taskID);
        (*task->m_openEventDescriptors).remove(ed);
        m_spinlock.release();
    }
}
