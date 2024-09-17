#ifndef INTERRUPTS_H
#define INTERRUPTS_H
#include "types.h"
#include "cpu/interrupts/pic.h"
#include "print.h"

namespace CPU {

    class InterruptHandler
    {
    public:
        InterruptHandler() = default;
        InterruptHandler(uint8_t interruptNumber);
        // Add handler to interrupt table to be called when interruptNumber vector is received.
        void update_handler_in_interrupt_table(uint8_t interruptNumber);
        ~InterruptHandler();
        virtual void irq_handler() = 0;
    private:
        uint8_t m_interruptNumber;
        bool m_isPresentInTable = false;
        
    };

    class InterruptManager
    {
        friend class InterruptHandler;
    public:
        static constexpr uint64_t MAX_INTERRUPTS_IRQ = 256;
        static constexpr uint64_t MAX_EXCEPTIONS_IRQ = 32;
        static constexpr uint64_t MAX_PIC_IRQ = 48;
    private:
        class InterruptDescriptorTableEntry64
        {
        private:
            uint16_t m_offsetLow;
            uint16_t m_codeSegmentSelector;
            uint16_t m_flags;
            uint16_t m_offsetMid;
            uint32_t m_offsetHigh;
            uint32_t m_reserved;
        public:
            enum Flag
            {
                PRESENT = 0x80,
                USER = 0x60,
                DT64 = 0x0E
            };
            uint64_t get_offset()
            {
                return ((uint64_t)m_offsetHigh << 32) | (m_offsetMid << 16) |  m_offsetLow;
            }
            void set_offset(uint64_t offset)
            {
                m_offsetLow = offset & 0xFFFF;
                m_offsetMid = (offset >> 16) & 0xFFFF;
                m_offsetHigh = (offset >> 32) & 0xFFFFFFFF;
            }
            uint16_t get_code_segment_selector() { return m_codeSegmentSelector; }
            void set_code_segment_selector(uint16_t codeSegmentSelector) { m_codeSegmentSelector = codeSegmentSelector; }
            uint16_t get_flags() { return m_flags >> 8; }
            void set_flags(uint8_t flags) { m_flags = (flags << 8); }
        } __attribute__((packed));

        class InterruptDescriptorTableRegister64
        {
        private:
            uint16_t m_limit;
            uint64_t m_base;
        public:
            uint16_t get_limit() { return m_limit; }
            void set_limit(uint16_t limit) { m_limit = limit; }
            uint64_t get_base() { return m_base; }
            void set_base(uint64_t base) { m_base = base; }
        } __attribute__((packed));
        
        __attribute__((aligned(0x10)))
        InterruptDescriptorTableEntry64 m_idt64[MAX_INTERRUPTS_IRQ];

        uint16_t m_codeSegmentSelector;
        InterruptDescriptorTableRegister64 m_idtr;
    public:
        static InterruptManager& instance()
        {
            static InterruptManager instance;
            return instance;
        }
        InterruptManager(InterruptManager const&) = delete;
        InterruptManager& operator=(InterruptManager const&) = delete;

        // Initialise and enable interrupts.
        void initialise();
        void enable_interrupts();
        void disable_interrupts();
        void internal_interrupt_handler(uint8_t interrupt);
        static void handle_noerr_interrupt(uint8_t interrupt);
        static void handle_err_interrupt(uint8_t interrupt, uint8_t err);
    private:
        InterruptManager() {}
        void add_interrupt_handler(InterruptHandler* handler, uint8_t interrupt);
        void remove_interrupt_handler(uint8_t interrupt);
        InterruptHandler* m_interruptHandlerTable[MAX_INTERRUPTS_IRQ];
        uint64_t m_cliCount;

    };
}
#endif
