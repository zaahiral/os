#ifndef TASK_H
#define TASK_H

#include "fs/vfs.h"
#include "common/doublyLinkedList.h"
#include "event/dispatcher.h"
#include "common/utilities.h"
#include "memory/MemoryManager.h"
#include "memory/AddressSpace.h"

namespace Task
{
    static constexpr uint64_t SIZE_OF_IRETQ_STACK = 20*8;
    using TaskID = uint64_t;

    // A SharableTaskResource manages a resource between tasks using reference counting.
    template <class T>
    class SharableTaskResource
    {
    public:
        SharableTaskResource(const T& resource)
        {
            // Allocate a new control block with the resource.
            m_controlBlock = new SharableTaskResourceControlBlock(resource);
        }

        SharableTaskResource(const SharableTaskResource& sharedResource)
        {
            // We are constructing with another shared resource, so copy the control block and increment refcount.
            m_controlBlock = sharedResource.m_controlBlock;
            m_controlBlock->m_refCount++;
        }

        SharableTaskResource& operator=(SharableTaskResource sharedResource)
        {
            // We are constructing with another shared resource, so copy the control block and increment refcount.
            auto tempControlBlock = sharedResource.m_controlBlock;
            sharedResource.m_controlBlock = this->m_controlBlock;
            this->m_controlBlock = tempControlBlock;
            return *this;
        }

        SharableTaskResource(SharableTaskResource&& sharedResource)
        {
            // Using move semantics, we steal the control block from the old shared resource.
            m_controlBlock = sharedResource.m_controlBlock;
            sharedResource.m_controlBlock = nullptr;
        }

        SharableTaskResource& operator=(SharableTaskResource&& sharedResource)
        {
            // Using move semantics, we steal the control block from the old shared resource.
            m_controlBlock = sharedResource.m_controlBlock;
            sharedResource.m_controlBlock = nullptr;
            return *this;
        }

        T& operator*()
        {
            // Return the underlying resource
            return m_controlBlock->m_resource;
        }

        ~SharableTaskResource()
        {
            if (m_controlBlock)
            {
                m_controlBlock->m_refCount--;
                if (m_controlBlock->m_refCount == 0)
                {
                    delete m_controlBlock;
                }
            }
        }

    private:
        class SharableTaskResourceControlBlock
        {
            friend class SharableTaskResource<T>;
            T m_resource;
            uint64_t m_refCount;
            SharableTaskResourceControlBlock(const T& resource) : m_resource(resource), m_refCount(1) {}
        };
        SharableTaskResourceControlBlock* m_controlBlock;
    };

    template <class T>
    class DescriptorTable
    {
    public:
        Common::DoublyLinkedListIterator<Common::Pair<uint64_t, T>> insert(T descriptor)
        {
            m_hashmap.insert(m_nextAvailID, descriptor);
            return m_hashmap.find(m_nextAvailID++);
        }
        void insert_at(uint64_t id, T descriptor)
        {
            m_hashmap.insert(id, descriptor);
            if (id >= m_nextAvailID) { m_nextAvailID = id + 1; }
        }
        void remove(uint64_t id) { m_hashmap.remove(id); }
        // IDs are given out in order, so every descriptor in the table has an ID below this.
        uint64_t get_id_limit() { return m_nextAvailID; }
        Common::DoublyLinkedListIterator<Common::Pair<uint64_t, T>> get(uint64_t descriptorID) { return m_hashmap.find(descriptorID); }
    private:
        uint64_t m_nextAvailID = 0;
        Common::Hashmap<uint64_t, T> m_hashmap;
    };

    using FileDescriptorTable = DescriptorTable<Filesystem::taskfd>;
    using EventDescriptorTable = DescriptorTable<Events::EventDescriptor>;

    class Task
    {
        friend class TaskManager;
    public:
        enum TaskState { NOT_STARTED, READY, WAIT };
        TaskState get_task_state() { return m_state; }
        void set_task_state(TaskState state) { m_state = state; }
        uint64_t get_task_id() const { return m_tid; }
        void set_task_id(TaskID tid) { m_tid = tid; }
        void* get_kstack() { return m_kstack; }
        void set_kstack(void* kstack) { m_kstack = kstack; }
        void* get_kernel_stack_top() { return m_kernelStackTop; }
        void set_kernel_stack_top(void* kernelStackTop) { m_kernelStackTop = kernelStackTop; }
        Memory::RegionableVirtualAddressSpace& get_tlTable() { return *m_tlTable; }
        void set_tlTable(Memory::RegionableVirtualAddressSpace& tlTable) { *m_tlTable = tlTable; }
        void* get_entry_point() { return m_entryPoint; }
        void set_entry_point(void* entryPoint) { m_entryPoint = entryPoint; }
        void* get_argument() { return m_argument; }
        void set_argument(void* argument) { m_argument = argument; }
        uint64_t get_heap_start() { return m_heapStart; }
        void set_heap_start(uint64_t heapStart) { m_heapStart = heapStart; }
        uint64_t get_program_break() { return m_programBreak; }
        void set_program_break(uint64_t programBreak) { m_programBreak = programBreak; }
        uint64_t get_thread_pointer() { return m_threadPointer; }
        void set_thread_pointer(uint64_t threadPointer) { m_threadPointer = threadPointer; }
        uint64_t get_next_map_address() { return m_nextMapAddress; }
        void set_next_map_address(uint64_t nextMapAddress) { m_nextMapAddress = nextMapAddress; }
        uint64_t get_priority() { return m_priority; }
        void set_priority(uint64_t priority) { m_priority = priority; }
        SharableTaskResource<FileDescriptorTable>& get_file_descriptor_table() { return m_openFileDescriptors; }
        SharableTaskResource<EventDescriptorTable> get_event_descriptor_table() { return m_openEventDescriptors; }
    private:
        Task(SharableTaskResource<Memory::RegionableVirtualAddressSpace> tlTable, SharableTaskResource<FileDescriptorTable> openFileDescriptors,
                    SharableTaskResource<EventDescriptorTable> openEventDescriptors) : m_tlTable(tlTable), m_openFileDescriptors(openFileDescriptors), m_openEventDescriptors(openEventDescriptors)
        {}
        SharableTaskResource<Memory::RegionableVirtualAddressSpace> m_tlTable;
        SharableTaskResource<FileDescriptorTable> m_openFileDescriptors;
        SharableTaskResource<EventDescriptorTable> m_openEventDescriptors;
        void* m_stack;
        void* m_kstack;
        // The kernel stack is empty whenever the task runs in ring 3, so ring 3 interrupts and system calls start at its top.
        void* m_kernelStackTop;
        uint64_t m_tid = 0;
        void* m_entryPoint;
        void* m_argument = nullptr;
        // The task's saved FPU and SSE registers, aligned to 16 bytes within the allocation.
        uint8_t* m_fpuStateAllocation = nullptr;
        uint8_t* m_fpuState = nullptr;
        // A user-space program's heap runs from the heap start to the program break, and mmap hands out memory above it.
        uint64_t m_heapStart = 0;
        uint64_t m_programBreak = 0;
        // The FS base of a user-space program, which points to its thread-local variables, or 0 if it has none.
        uint64_t m_threadPointer = 0;
        uint64_t m_nextMapAddress = 0;
        TaskState m_state = NOT_STARTED;
        // When the task exits, its exit code is kept for the task that waits for it.
        bool m_hasExited = false;
        uint64_t m_exitCode = 0;
        TaskID m_waitingTask = 0;
        uint64_t m_priority;
    };

}

#endif
