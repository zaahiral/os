#ifndef AMDPCNETIII_H
#define AMDPCNETIII_H

#include "cpu/io/io.h"
#include "cpu/io/pci.h"
#include "cpu/interrupts/interrupts.h"
#include "drivers/network/networkdriver.h"
#include "common/doublyLinkedList.h"
#include "event/dispatcher.h"
#include "event/sender.h"

namespace Drivers
{
    #define AMDPCNETIII_VENDOR_ID 0x1022
    #define AMDPCNETIII_DEVICE_ID 0x2000
    
    #define APROM0 0x00
    #define APROM2 0x02
    #define APROM4 0x04
    #define RDP 0x10
    #define RAP 0x12
    #define RESET 0x14
    #define BDP 0x16

    #define CSR0 0
    #define CSR1 1
    #define CSR2 2
    #define CSR3 3
    #define CSR4 4
    #define BCR20 20

    #define BCR20_SSIZE32 0x100
    #define BCR20_PCNET_PCI_32 0x2

    #define DESCRIPTOR32_SIZE 0x10
    #define TRANSMIT_BUFFER_COUNT 8
    #define RECEIVE_BUFFER_COUNT 8

    #define OWN_BUFFER 0x80000000
    #define BUFFER_LEN 0x07FF
    #define ONES 0xF000
    #define STP 0x2000000
    #define ENP 0x1000000

    #define BUFFER_SIZE 2048

    #define CSR0_INIT 0x1
    #define CSR0_STRT 0x2
    #define CSR0_TDMD 0x8
    #define CSR0_IENA 0x40

    struct NetworkDataReceived
    {
        uint8_t* buf;
        uint32_t size;
    };

    struct InitialisationBlock32
    {
        uint16_t mode;
        unsigned resv1  : 4;
        unsigned rlen   : 4;
        unsigned resv2  : 4;
        unsigned tlen   : 4;
        uint64_t padr   : 48;
        uint16_t resv3;
        uint64_t ladrf;
        uint32_t rdra;
        uint32_t tdra;
    } __attribute__((packed));

    struct TransmitDescriptor32
    {
        uint32_t TMD0;
        uint32_t TMD1;
        uint32_t TMD2;
        uint32_t TMD3;
    } __attribute__((packed));

    struct ReceiveDescriptor32
    {
        uint32_t RMD0;
        uint32_t RMD1;
        uint32_t RMD2;
        uint32_t RMD3;
    } __attribute__((packed));

    class AMDPCNetIIIDriver : CPU::InterruptHandler, public EthernetNetworkDriver, Events::EventSender
    {
    public:
        AMDPCNetIIIDriver();
        ~AMDPCNetIIIDriver() = default;
        static AMDPCNetIIIDriver* instance();
        void irq_handler();
        void send_data(uint8_t* buf, uint64_t size);
        uint64_t get_MAC_address();
        uint32_t get_IP_address();
        void set_IP_address(uint32_t IPAddress);
        void register_event_listener(uint64_t ped, uint64_t tid);
        void block_event_listen(uint64_t ped, uint64_t tid);
    private:
        static AMDPCNetIIIDriver* m_self;

        InitialisationBlock32 m_initBlock;
        PeripheralComponentInterconnect::PCIDevice* m_PCIDevice;

        uint8_t m_currentTransmitBufferDescriptor;
        uint8_t m_currentReceiveBufferDescriptor;
        // The descriptors describe a transmit or receive buffer.
        uint8_t m_transmitBufferDescriptors[DESCRIPTOR32_SIZE*TRANSMIT_BUFFER_COUNT+15];
        uint8_t m_receiveBufferDescriptors[DESCRIPTOR32_SIZE*RECEIVE_BUFFER_COUNT+15];
        // The descriptors must be stored at a word aligned position; these are pointers to the start of the descriptor ring.
        TransmitDescriptor32* m_transmitBufferDescriptorRingBase;
        ReceiveDescriptor32* m_receiveBufferDescriptorRingBase;
        uint8_t* m_transmitBuffersBase;
        uint8_t* m_receiveBuffersBase;
        uint8_t m_transmitBuffers[BUFFER_SIZE*TRANSMIT_BUFFER_COUNT+15];
        uint8_t m_receiveBuffers[BUFFER_SIZE*RECEIVE_BUFFER_COUNT+15];

        uint16_t m_rdpPort;
        uint16_t m_rapPort;
        uint16_t m_resetPort;
        uint16_t m_bdpPort;

        uint64_t m_MACAddress = 0;
        uint32_t m_IPAddress = 0;
        Common::DoublyLinkedList<Events::EventDescriptorItem> m_listenqueue;
        Common::DoublyLinkedList<Events::EventDescriptorItem> m_waitqueue;
        void receive_data();
    };
}

#endif
