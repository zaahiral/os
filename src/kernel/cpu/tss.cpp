#include "cpu/tss.h"
#include "print.h"

extern "C" uint64_t syscall_kernel_stack;

namespace CPU {
    extern "C" void asm_flush_tss();

    namespace 
    {
        static TaskStateSegment64* m_tss;
        static constexpr int TSS_GDT_ENTRY_NUM = 6;
    }

    void set_task_state_segment(TaskStateSegment64* tss)
    {
        m_tss = tss;
        GlobalDescriptorTableEntry64 tssGdtEntry = GlobalDescriptorTableEntry64();
        tssGdtEntry.set_limit(sizeof(TaskStateSegment64));
        tssGdtEntry.set_base(reinterpret_cast<uint64_t>(m_tss));
        // These flags must be set for the TSS entry in the GDT.
        tssGdtEntry.set_flags(0b10001001);
        set_global_descriptor_table_entry(tssGdtEntry, TSS_GDT_ENTRY_NUM);
    }

    void refresh_task_state_segment() { asm_flush_tss(); }

    // The syscall entry point uses the same kernel stack as interrupts from ring 3.
    void update_kernel_stack_pointer(void* stackPointer)
    {
        m_tss->set_rsp0(reinterpret_cast<uint64_t>(stackPointer));
        syscall_kernel_stack = reinterpret_cast<uint64_t>(stackPointer);
    }
}
