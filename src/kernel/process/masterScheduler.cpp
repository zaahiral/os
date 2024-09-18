#include "process/masterScheduler.h"
#include "x86_64.h"
#include "cpu/io/pit.h"

/*
    MasterScheduler (meta-scheduler)
    
    All schedulers pre-empt schedulers with a lower scheduler priority.
    Scheduler 0 (highest priority) : highest priority task runs first, used for very low latency - IRQ tasklets, device drivers etc.
    Scheduler 1 : highest priority task runs first, used for low latency - GUI updates, network stack processing etc.
    Scheduler 2 (lowest priority) : variable frequency fixed timeslice, used for normal tasks - userspace tasks
*/

namespace Task
{

    Task* MasterScheduler::get_task_from_tid(TaskID tid)
    {
        auto it = m_taskHashmap.find(tid);
        if (it.is_end()) { return nullptr; }
        return &it->last;
    }

    TaskID MasterScheduler::get_current_task()
    {
        return m_currentTask;
    }

    TaskID MasterScheduler::get_next_task()
    {
        auto currentTaskID = get_current_task();
        if (currentTaskID) { add_task_to_queue(currentTaskID); }

        // Each queue pre-empts the previous queue.
        if (m_queue0.size())
        {
            auto ret = m_queue0.top().taskID;
            m_queue0.pop();
            return ret;
        }

        if (m_queue1.size())
        {
            auto ret = m_queue1.top().taskID;
            m_queue1.pop();
            return ret;
        }
        if (m_queue2.size()) { return m_queue2.get_next(); }
        return 0;
    }

    void MasterScheduler::move_to_next_task(Spinlock& taskSpinlock)
    {
        m_currentTask = get_next_task();
        
        while (!m_currentTask)
        {
            // If no tasks are runnable, we enable interrupts as this is the only way a task can become runnable.
            taskSpinlock.release();
            CPU::sti();
            CPU::hlt();
            CPU::cli();
            taskSpinlock.acquire();
            m_currentTask = get_next_task();
        }
    }

    void MasterScheduler::block_task()
    {
        get_task_from_tid(m_currentTask)->set_task_state(Task::TaskState::WAIT);
        m_currentTask = 0;
    }

    void MasterScheduler::add_task(Task task)
    {
        m_taskHashmap.insert(task.get_task_id(), task);
        add_task_to_queue(task.get_task_id());
    }
    
    void MasterScheduler::add_task_to_queue(TaskID tid)
    {
        auto task = get_task_from_tid(tid);
        auto priority = task->get_priority();
        auto queue = priority / 256;
        auto queuePriority = priority % 256;
        switch (queue)
        {
        case 0:
            m_queue0.push({tid, queuePriority});
            break;
        case 1:
            m_queue1.push({tid, queuePriority});
            break;
        case 2:
            m_queue2.add(tid, queuePriority);
            break;
        default:
            break;
        }
    }

    void MasterScheduler::unblock_task(TaskID tid)
    {
        auto task = get_task_from_tid(tid);
        task->set_task_state(Task::TaskState::READY);
        add_task_to_queue(tid);
    }

    void MasterScheduler::remove_task(TaskID tid) {}

}
