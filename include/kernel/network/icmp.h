#ifndef ICMP_H
#define ICMP_H

#include "network/ipv4.h"

namespace Networking
{
    namespace InternetProtocolV4
    {
        class InternetControlMessageProtocolMessage
        {
        public:
            uint8_t get_type() { return m_type; }
            void set_type(uint8_t type) { m_type = type; }
            uint8_t get_code() { return m_code; }
            void set_code(uint8_t code) { m_code = code; }
            uint16_t get_checksum() { return m_checksum; }
            void set_checksum(uint16_t checksum) { m_checksum = checksum; }
            uint32_t get_data() { return m_data; }
            void set_data(uint32_t data) { m_data = data; }
        private:
            uint8_t m_type;
            uint8_t m_code;
            uint16_t m_checksum;
            uint32_t m_data;
        } __attribute__ ((packed));

        class InternetControlMessageProtocolManager : InternetProtocolHandler
        {
        public:
            InternetControlMessageProtocolManager();
            void on_receive_ip(InternetProtocolV4Address sourceIPAddressBigEndian, InternetProtocolV4Address destinationIPAddressBigEndian, uint8_t* payload, uint32_t size);
            void request_echo_reply(InternetProtocolV4Address IPAddressBigEndian);
        };
    }
}

#endif
