#ifndef GDT_H
#define GDT_H
#include "types.h"

namespace CPU {

    class GlobalDescriptorTableEntry32
    {
    private:
        uint16_t m_limit;
        uint16_t m_base0;
        uint8_t m_base1;
        uint16_t m_flags;
        uint8_t m_base2;
    public:
        uint16_t get_limit() { return m_limit; }

        void set_limit(uint16_t limit) { m_limit = limit; }

        uint32_t get_base() { return m_base0 | (m_base1 << 16) | (m_base2 << 24); }

        void set_base(uint32_t base)
        {
            m_base0 = base & 0xFFFF;
            m_base1 = (base >> 16) & 0xFF;
            m_base2 = (base >> 24) & 0xFF;
        }

        uint16_t get_flags() { return m_flags; }

        void set_flags(uint16_t flags) { m_flags = flags; }

    } __attribute__((packed));

    class GlobalDescriptorTableEntry64
    {
    private:
        uint16_t m_limit;
        uint16_t m_base0;
        uint8_t m_base1;
        uint16_t m_flags;
        uint8_t m_base2;
        uint32_t m_base3;
    public:
        uint16_t get_limit() { return m_limit; }

        void set_limit(uint16_t limit) { m_limit = limit; }

        uint64_t get_base() { return m_base0 | (m_base1 << 16) | (m_base2 << 24) | ((uint64_t)m_base3 << 32); }

        void set_base(uint64_t base)
        {
            m_base0 = base & 0xFFFF;
            m_base1 = (base >> 16) & 0xFF;
            m_base2 = (base >> 24) & 0xFF;
            m_base3 = (base >> 32) & 0xFFFFFFFF;
        }

        uint16_t get_flags() { return m_flags; }

        void set_flags(uint16_t flags) { m_flags = flags; }
    } __attribute__((packed));

    void set_global_descriptor_table(void* globalDescriptorTable);

    void set_global_descriptor_table_entry(GlobalDescriptorTableEntry32 entry, int entryNum);

    void set_global_descriptor_table_entry(GlobalDescriptorTableEntry64 entry, int entryNum);
}

#endif
