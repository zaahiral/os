#include "cpu/interrupts/interrupts.h"
#include "process/taskManager.h"
#include "memory/MemoryManager.h"

extern void* isr_table[];

namespace CPU {

    InterruptHandler::InterruptHandler(uint8_t interruptNumber)
    {
        m_interruptNumber = interruptNumber;
        InterruptManager::instance().add_interrupt_handler(this, interruptNumber);
        m_isPresentInTable = true;
        if (interruptNumber >= InterruptManager::MAX_EXCEPTIONS_IRQ && interruptNumber < InterruptManager::MAX_PIC_IRQ)
        {
            PIC::clear_irq_mask(interruptNumber - InterruptManager::MAX_EXCEPTIONS_IRQ);
        }
    }

    InterruptHandler::~InterruptHandler()
    {
        if (m_isPresentInTable)
        {
            InterruptManager::instance().remove_interrupt_handler(m_interruptNumber);
        }
    }

    void InterruptManager::initialise()
    {
        uint16_t codeSegmentSelector;
        asm("mov %%CS, %0" : "=r" (codeSegmentSelector));
        m_idtr.set_base(reinterpret_cast<uint64_t>(&m_idt64));
        m_idtr.set_limit(sizeof(InterruptDescriptorTableEntry64) * MAX_INTERRUPTS_IRQ - 1);
        m_codeSegmentSelector = codeSegmentSelector;
        for (int vector = 0; vector <= MAX_PIC_IRQ; vector++)
        {
            InterruptDescriptorTableEntry64* entry = &m_idt64[vector];
            entry->set_offset(reinterpret_cast<uint64_t>(isr_table[vector]));
            entry->set_code_segment_selector(m_codeSegmentSelector);
            entry->set_flags(InterruptDescriptorTableEntry64::PRESENT | InterruptDescriptorTableEntry64::DT64 | InterruptDescriptorTableEntry64::USER);
            //idt_set_entry(i, isr_table[i], 0x8E|0x60); // 0x8C for present, 00 DPL, 64-bit descriptor type
        }
        __asm__ volatile ("lidt %0" : : "m"(m_idtr));
        for (int i = 0; i < MAX_INTERRUPTS_IRQ; i++)
        {
            m_interruptHandlerTable[i] = nullptr;
        }
        enable_interrupts();
    }

    void InterruptManager::add_interrupt_handler(InterruptHandler* handler, uint8_t interrupt)
    {
        if (interrupt >= MAX_EXCEPTIONS_IRQ && interrupt <= MAX_PIC_IRQ)
        {
            PIC::clear_irq_mask(interrupt - MAX_EXCEPTIONS_IRQ);
        }
        m_interruptHandlerTable[interrupt] = handler;
    }

    void InterruptManager::remove_interrupt_handler(uint8_t interrupt)
    {
        if (interrupt >= MAX_EXCEPTIONS_IRQ && interrupt <= MAX_PIC_IRQ)
        {
            PIC::set_irq_mask(interrupt - MAX_EXCEPTIONS_IRQ);
        }
        m_interruptHandlerTable[interrupt] = nullptr;
    }

    void InterruptHandler::update_handler_in_interrupt_table(uint8_t interrupt)
    {
        if (m_isPresentInTable)
        {
            InterruptManager::instance().remove_interrupt_handler(m_interruptNumber);
        }
        m_interruptNumber = interrupt;
        InterruptManager::instance().add_interrupt_handler(this, m_interruptNumber);
        m_isPresentInTable = true;
    }

    void InterruptManager::internal_interrupt_handler(uint8_t interrupt, uint64_t codeSegment)
    {
        // An exception in ring 3 was caused by a user-space program, so we stop the program rather than the kernel.
        if (interrupt < MAX_EXCEPTIONS_IRQ && (codeSegment & 3) == 3)
        {
            if (interrupt == 14)
            {
                uint64_t pageFaultAddress;
                asm volatile ("mov %%cr2, %0" : "=r" (pageFaultAddress));
                printf("\nThe program was stopped by a page fault at address ");
                printf(pageFaultAddress);
            }
            else
            {
                printf("\nThe program was stopped by exception ");
                printf(interrupt);
            }
            printf(".\n");
            Task::TaskManager::instance().exit_task(-1);
        }
        switch (interrupt)
        {
            case 14:
            printf("PAGE FAULT");
            // If a page fault occurs, the page faulting address is stored in CR2.
            // We get it and check if it is mapped in the main address space, if so map it in the current address space.
            uint64_t pageFaultAddress;
            asm volatile ("mov %%cr2, %0" : "=r" (pageFaultAddress));
            printf(pageFaultAddress);
            while(1);
            uint64_t addressSpaceAddress;
            asm volatile ("mov %%cr3, %0" : "=r" (addressSpaceAddress));
            auto physicalAddress = Memory::MemoryManager::instance().get_physical_address(pageFaultAddress, Memory::MemoryManager::instance().get_main_address_space());
            // Check that the physical address exists.
            if (physicalAddress.get())
            {
                auto vaddrspace = Memory::VirtualAddressSpace(addressSpaceAddress);
                Memory::MemoryManager::instance().request_virtual_map(Memory::VirtualMemoryMapRequest(physicalAddress, Memory::VirtualAddress(BYTE_ALIGN_DOWN(pageFaultAddress, Memory::PAGE_4KiB))), vaddrspace);
            }
            return;
        }
        if (interrupt >= MAX_EXCEPTIONS_IRQ)
        {
            if (m_interruptHandlerTable[interrupt])
            {
                m_interruptHandlerTable[interrupt]->irq_handler();
            }
            if (interrupt <= MAX_PIC_IRQ)
            {
                PIC::send_eoi(interrupt-MAX_EXCEPTIONS_IRQ);
            }
        }
    }

    void InterruptManager::handle_noerr_interrupt(uint8_t interrupt, uint64_t codeSegment)
    {
        InterruptManager::instance().internal_interrupt_handler(interrupt, codeSegment);
    }

    void InterruptManager::handle_err_interrupt(uint8_t interrupt, uint8_t err, uint64_t codeSegment)
    {
        InterruptManager::instance().internal_interrupt_handler(interrupt, codeSegment);
    }

    void InterruptManager::enable_interrupts()
    {
        // Standard remapping of PIC.
        PIC::remap_and_init_to_offsets(0x20, 0x28);
        asm volatile ("sti");
    }

    void InterruptManager::disable_interrupts()
    {
        asm volatile ("cli");
    }
}
