#include "drivers/hid/keyboard.h"
#include "process/taskManager.h"

namespace Drivers
{
    KeyboardDriver* KeyboardDriver::m_self;
    KeyboardDriver* KeyboardDriver::instance() { return m_self; }

    // PS/2 keyboard interrupts are IRQ 33.
    KeyboardDriver::KeyboardDriver() : InterruptHandler(33)
    {
        m_self = this;
        Events::EventDispatcher::instance().register_event_sender("HID/Keyboard", this);
    }

    void KeyboardDriver::block_event_listen(uint64_t ped, uint64_t tid)
    {
        Events::EventDispatcher::instance().add_process_to_wait_event_list(ped, tid, &m_waitqueue);
    }

    void KeyboardDriver::register_event_listener(uint64_t ped, uint64_t tid)
    {
        Events::EventDispatcher::instance().add_process_to_listener_event_list(ped, tid, &m_listenqueue);
    }

    void KeyboardDriver::register_listener(KeyboardListener* listener)
    {
        m_listeners.append(listener);
    }

    void KeyboardDriver::deregister_listener(KeyboardListener* listener)
    {
        for (auto it = m_listeners.first(); !it.is_end(); ++it)
        {
            if (*it == listener) { m_listeners.remove(it); break; }
        }
    }

    void KeyboardDriver::irq_handler()
    {
        auto eventMessage = Events::EventMessage();
        // Read in scan code from keyboard.
        uint8_t scanCode = IO::in_8(0x60);
        if (m_listeners.size())
        {
            auto& manager = Task::TaskManager::instance();
            manager.lock();
            for (auto it = m_listeners.first(); !it.is_end(); ++it) { (*it)->on_scan_code(scanCode); }
            manager.unlock();
        }
        eventMessage.message = (void*)scanCode;
        Events::EventDispatcher::instance().dispatch_event(eventMessage, &m_self->m_listenqueue);
        Events::EventDispatcher::instance().wake_processes(&m_self->m_waitqueue);
    }
}
