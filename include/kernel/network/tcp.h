#ifndef TCP_H
#define TCP_H

#include "network/ipv4.h"

namespace Networking
{
    namespace TransmissionControlProtocol
    {
        using InternetProtocolV4Address = uint32_t;

        class TransmissionControlProtocolPseudoHeader
        {
        public:
            uint32_t get_source_ip() { return m_sourceIP; }
            void set_source_ip(uint32_t ip) { m_sourceIP = ip; }
            uint32_t get_dest_ip() { return m_destIP; }
            void set_dest_ip(uint32_t ip) { m_destIP = ip; }
            uint32_t get_length() { return m_length; }
            void set_length(uint16_t length) { m_length = length; }
            uint16_t get_protocol() { return m_protocol; }
            void set_protocol(uint16_t protocol) { m_protocol = protocol; }
        private:
            uint32_t m_sourceIP;
            uint32_t m_destIP;
            uint16_t m_protocol;
            uint16_t m_length;
        } __attribute__((packed)) ;

        class TransmissionControlProtocolHeader
        {
        public:
            enum Flag : uint8_t
            {
                FIN = 0x01,
                SYN = 0x02,
                RST = 0x04,
                PSH = 0x08,
                ACK = 0x10,
                URG = 0x20,
                ECE = 0x40,
                CWR = 0x80
            };
            uint16_t get_source_port() { return m_sourcePort; }
            void set_source_port(uint16_t port) { m_sourcePort = port; }
            uint16_t get_dest_port() { return m_destPort; }
            void set_dest_port(uint16_t port) { m_destPort = port; }
            uint32_t get_seq_number() { return m_sequenceNumber; }
            void set_seq_number(uint32_t seq) { m_sequenceNumber = seq; }
            uint32_t get_ack_number() { return m_ackNumber; }
            void set_ack_number(uint32_t ack) { m_ackNumber = ack; }
            uint8_t get_data_offset() { return m_dataOffset >> 4; }
            void set_data_offset(uint8_t dataOffset) { m_dataOffset = dataOffset << 4;}
            uint8_t get_flags() { return m_flags; }
            void set_flags(uint8_t flags) { m_flags = flags; }
            uint16_t get_window_size() { return m_windowSize; }
            void set_window_size(uint16_t windowSize) { m_windowSize = windowSize; }
            uint16_t get_checksum() { return m_checksum; }
            void set_checksum(uint16_t checksum) { m_checksum = checksum; }
            uint16_t get_urgent_pointer() { return m_urgentPointer; }
            void set_urgent_pointer(uint16_t urgentPointer) { m_urgentPointer = urgentPointer;}
            uint32_t get_options() { return m_options; }
            void set_options(uint32_t options) { m_options = options;}
        
        private:
            uint16_t m_sourcePort;
            uint16_t m_destPort;
            uint32_t m_sequenceNumber;
            uint32_t m_ackNumber;
            uint8_t m_dataOffset;
            uint8_t m_flags;
            uint16_t m_windowSize;
            uint16_t m_checksum;
            uint16_t m_urgentPointer = 0;
            uint32_t m_options;
        } __attribute__((packed));

        class TransmissionControlProtocolSocket;

        class TransmissionControlProtocolHandler
        {
        public:
            TransmissionControlProtocolHandler() = default;
            virtual void handle_TCP_message(TransmissionControlProtocolSocket* socket, uint8_t* data, uint16_t size) = 0;
        };

        struct TCPPacket
        {
        public:
            uint32_t sequenceNumber;
            uint32_t length;
            void* data;
            //uint64_t expiry;
        };

        class TransmissionControlProtocolSocket
        {
        friend class TransmissionControlProtocolManager;
        private:
            
            uint16_t m_remotePort = 0;
            InternetProtocolV4Address m_remoteIP = 0;
            uint16_t m_localPort = 0;
            InternetProtocolV4Address m_localIP = 0;
            TransmissionControlProtocolHandler* m_handler;
            
            Common::DoublyLinkedList<TCPPacket> m_retransmissionList;
            Common::DoublyLinkedList<TCPPacket> m_outOfOrderList;
            // These are variables declared by the TCP specification.
            uint32_t snd_una = 0xbeef0101;
            uint32_t snd_nxt = 0xbeef0101;
            uint32_t snd_wnd = 8192;
            uint32_t snd_up = 0;
            uint32_t snd_wl1 = 0;
            uint32_t snd_wl2 = 0;
            uint32_t iss = 0xbeef0101;
            uint32_t rcv_nxt = 0;
            uint32_t rcv_wnd = 8192;
            uint32_t rcv_up = 0;
            uint32_t irs = 0;

        public:
            TransmissionControlProtocolSocket() = default;
            void receive_TCP_message(InternetProtocolV4Address source, InternetProtocolV4Address dest, uint8_t* data, uint16_t size);
            enum State
            {
                LISTEN, SYN_SENT, SYN_RECEIVED, ESTABLISHED, FIN_WAIT1, FIN_WAIT2, CLOSE_WAIT, CLOSING, LAST_ACK, TIME_WAIT, CLOSED
            };
            State m_state = CLOSED;
            void send(uint8_t* data, uint16_t size);
        private:
            void receive_on_listen(TransmissionControlProtocolHeader* header);
            void receive_on_syn_sent(TransmissionControlProtocolHeader* header);
            void receive_ack(TransmissionControlProtocolHeader* header);
            void receive_data(uint8_t* data, uint16_t size);
            void receive_rst(TransmissionControlProtocolHeader* header);
            void receive_fin(TransmissionControlProtocolHeader* header);
            void send(uint32_t seq, uint8_t* data, uint16_t size, uint16_t flags = 0);
            void refresh_retransmission_queue();
            void add_to_received(const TCPPacket& packet);
            void process_received();
        };

        class TransmissionControlProtocolManager : InternetProtocolV4::InternetProtocolHandler
        {
        public:
            TransmissionControlProtocolManager();
            void on_receive_ip(InternetProtocolV4Address sourceIPAddressBigEndian, InternetProtocolV4Address destinationIPAddressBigEndian, uint8_t* payload, uint32_t size);
            // Connect to a remote endpoint, acting as a TCP client.
            TransmissionControlProtocolSocket* connect(InternetProtocolV4Address IPAddress, uint16_t port);
            // Listen for remote connections, acting as a TCP server
            TransmissionControlProtocolSocket* listen(uint16_t port);
            void disconnect(TransmissionControlProtocolSocket* socket);
            void send(TransmissionControlProtocolSocket* socket, uint32_t seq, uint8_t* data, uint16_t size, uint16_t flags = 0);
            void bind(TransmissionControlProtocolSocket* socket, TransmissionControlProtocolHandler* handler);
        private:
            Common::DoublyLinkedList<TransmissionControlProtocolSocket*> m_sockets;
            uint16_t m_freePort = 1024;
        };
    }
}

#endif
