#include "network/udp.h"

namespace Networking
{
    namespace UserDatagramProtocol
    {
        UserDatagramProtocolManager::UserDatagramProtocolManager() : InternetProtocolV4::InternetProtocolHandler(InternetProtocolV4::InternetProtocolManager::instance(), 0x11) {}

        void UserDatagramProtocolSocket::receive_UDP_message(uint8_t* data, uint16_t size)
        {
            if (m_handler) { m_handler->handle_UDP_message(this, data, size); }
            char* print = " ";
                for(int i = 0; i < size; i++)
                {
                    print[0] = ((uint8_t*)data)[i];
                    printf(print);
                }
        }

        void UserDatagramProtocolManager::on_receive_ip(InternetProtocolV4Address sourceIPAddressBigEndian, InternetProtocolV4Address destinationIPAddressBigEndian, uint8_t* payload, uint32_t size)
        {
            if (size < sizeof(UserDatagramProtocolHeader)) { return; }
            auto message = (UserDatagramProtocolHeader*)payload;
            uint16_t localPort = message->get_dest_port();
            uint16_t remotePort = message->get_source_port();

            // Search for a socket that listens for the connection
            for (auto socketIt = m_sockets.first(); !socketIt.is_end(); ++socketIt)
            {
                // We have a listening socket.
                if ((*socketIt)->m_localPort == message->get_dest_port()
                && (*socketIt)->m_localIP == destinationIPAddressBigEndian
                && (*socketIt)->m_listening)
                {
                    // Set the socket's remote endpoint to the sender of the message.
                    (*socketIt)->m_listening = false;
                    (*socketIt)->m_remotePort = message->get_source_port();
                    (*socketIt)->m_remoteIP = sourceIPAddressBigEndian;
                }
                else if (!((*socketIt)->m_localPort == message->get_dest_port()
                    && (*socketIt)->m_localIP == destinationIPAddressBigEndian
                    && (*socketIt)->m_remotePort == message->get_source_port()
                    && (*socketIt)->m_remoteIP == sourceIPAddressBigEndian))
                {
                    // The socket is already connected to another endpoint.
                    break;
                }
                (*socketIt)->receive_UDP_message(payload + sizeof(UserDatagramProtocolHeader), size - sizeof(UserDatagramProtocolHeader));
            }
        }

        UserDatagramProtocolSocket* UserDatagramProtocolManager::connect(InternetProtocolV4Address IPAddress, uint16_t port)
        {
            UserDatagramProtocolSocket* socket = new UserDatagramProtocolSocket();
            socket->m_remotePort = port;
            socket->m_remoteIP = IPAddress;
            socket->m_localPort = m_freePort++;
            socket->m_localIP = Ethernet::EthernetLayerManager::instance()->get_IP_address();
            socket->m_remotePort = ((socket->m_remotePort & 0xFF00)>>8) | ((socket->m_remotePort & 0x00FF) << 8);
            socket->m_localPort = ((socket->m_localPort & 0xFF00)>>8) | ((socket->m_localPort & 0x00FF) << 8);
            auto it = m_sockets.append(socket);
            return *it;
        }

        UserDatagramProtocolSocket* UserDatagramProtocolManager::listen(uint16_t port)
        {
            UserDatagramProtocolSocket* socket = new UserDatagramProtocolSocket();
            socket->m_listening = true;
            socket->m_localPort = port;
            socket->m_localIP = Ethernet::EthernetLayerManager::instance()->get_IP_address();
            socket->m_localPort = ((socket->m_localPort & 0xFF00)>>8) | ((socket->m_localPort & 0x00FF) << 8);
            auto it = m_sockets.append(socket);
            return *it;
        }

        void UserDatagramProtocolManager::disconnect(UserDatagramProtocolSocket* socket)
        {
            for (auto it = m_sockets.first(); !it.is_end(); ++it)
            {
                if (*it == socket)
                {
                    m_sockets.remove(it);
                    delete socket;
                    break;
                }
            }
        }

        void UserDatagramProtocolManager::send(UserDatagramProtocolSocket* socket, uint8_t* data, uint16_t size)
        {
            uint16_t length = size + sizeof(UserDatagramProtocolHeader);
            auto buffer = (uint8_t*)kmalloc(length, 0);
            
            auto message = (UserDatagramProtocolHeader*)buffer;
            message->set_source_port(socket->m_localPort);
            message->set_dest_port(socket->m_remotePort);
            message->set_length(((length & 0x00FF) << 8) | ((length & 0xFF00) >> 8));
            
            memcpy(buffer + sizeof(UserDatagramProtocolHeader), data, size);
            message->set_checksum(0);
            InternetProtocolV4::InternetProtocolManager::instance()->send_data(socket->m_remoteIP, 0x11, buffer, length);
            kfree(buffer);
        }

        void UserDatagramProtocolManager::bind(UserDatagramProtocolSocket* socket, UserDatagramProtocolHandler* handler)
        {
            socket->m_handler = handler;
        }
    }
}
