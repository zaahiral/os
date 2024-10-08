#include "event/dispatcher.h"
#include "process/taskManager.h"

namespace Events {

    void EventDispatcher::register_event_sender(char* event, EventSender* sender)
    {
        auto str = FixedLenStringHashContainer();
        strcpy(str.str, event);
        m_eventSenderHashmap.insert(str, sender);
    }

    void EventDispatcher::add_process_to_event_list(EventDescriptorItem item, Common::DoublyLinkedList<EventDescriptorItem>* list)
    {
        list->append(item);
    } 

    void EventDispatcher::add_process_to_listener_event_list(uint64_t ped, uint64_t tid, Common::DoublyLinkedList<EventDescriptorItem>* list)
    {
        add_process_to_event_list({ped, tid}, list);
        Task::TaskManager::instance().get_ed(ped, tid)->last.listenerListElements.append(list);

    } 

    void EventDispatcher::add_process_to_wait_event_list(uint64_t ped, uint64_t tid, Common::DoublyLinkedList<EventDescriptorItem>* list)
    {
        add_process_to_event_list({ped, tid}, list);
        Task::TaskManager::instance().get_ed(ped, tid)->last.waitListElements.append(list);

    }

    void EventDispatcher::remove_process_from_listener_lists(uint64_t ped, uint64_t tid)
    {
        Task::TaskManager::instance().lock();
        auto descriptor = Task::TaskManager::instance().get_ed(ped, tid);
        for (auto it = descriptor->last.listenerListElements.first(); !it.is_end(); ++it)
        {
            for (auto listit = (*it)->first(); !listit.is_end(); ++listit)
            {
                if (listit->tid == tid) { (*it)->remove(listit); break; }
            }
        }
        Task::TaskManager::instance().unlock();
    }

    void EventDispatcher::remove_process_from_wait_lists(uint64_t ped, uint64_t tid)
    {
        Task::TaskManager::instance().lock();
        auto descriptor = Task::TaskManager::instance().get_ed(ped, tid);
        for (auto it = descriptor->last.waitListElements.first(); !it.is_end(); ++it)
        {
            for (auto listit = (*it)->first(); !listit.is_end(); ++listit)
            {
                if (listit->tid == tid) { (*it)->remove(listit); break; }
            }
        }
        // A task can wait again on this descriptor, so discard the records of its previous wait.
        descriptor->last.waitListElements.clear();
        Task::TaskManager::instance().unlock();
    }

    void EventDispatcher::wake_processes(Common::DoublyLinkedList<EventDescriptorItem>* list)
    {
        // We need to make a copy of the list as we are removing items from the original.
        auto listCopy = *list;
        for (auto it = listCopy.first(); !it.is_end(); ++it)
        {
            remove_process_from_wait_lists((*it).processEventDescriptor, (*it).tid);
            Task::TaskManager::instance().unblock_task((*it).tid);
        }
    }

    uint64_t EventDispatcher::register_event_listener(uint64_t tid, char* event, void* param)
    { 
        auto str = FixedLenStringHashContainer();
        strcpy(str.str, event);
        
        auto eventSender = EventDispatcher::instance().m_eventSenderHashmap.find(str);
        if (!(eventSender->first == str)) { return -1; }
        auto ed = Task::TaskManager::instance().add_ed((void*)eventSender->last, param, tid);
        eventSender->last->register_event_listener(ed, tid); 
        return ed;
    }

    void EventDispatcher::dispatch_event(EventMessage event, EventDescriptorItem processed)
    {
        Task::TaskManager::instance().lock();
        auto ped = Task::TaskManager::instance().get_ed(processed.processEventDescriptor, processed.tid);
        ped->last.queue.append(event);
        Task::TaskManager::instance().unlock();
    }

    void EventDispatcher::dispatch_event(EventMessage event, Common::DoublyLinkedList<EventDescriptorItem>* processeds)
    {
        for (auto it=processeds->first(); !it.is_end(); ++it)
        {
            dispatch_event(event, *it);
        }
    }

    void EventDispatcher::block_event_listen(uint64_t tid, uint64_t ed)
    {
        Task::TaskManager::instance().lock();
        auto ped = Task::TaskManager::instance().get_ed(ed, tid);
        auto sender = (EventSender*)(ped->last.eventSender);
        if (ped->last.queue.size() != 0) {
            Task::TaskManager::instance().unlock();
            return;
        }
        sender->block_event_listen(ed, tid);
        Task::TaskManager::instance().unlock();
        Task::TaskManager::instance().block_task();
    }

    uint64_t EventDispatcher::deregister_event_listener(uint64_t tid, uint64_t ed)
    {
        EventDispatcher::instance().remove_process_from_listener_lists(ed, tid);
        EventDispatcher::instance().remove_process_from_wait_lists(ed, tid);
        Task::TaskManager::instance().remove_ed(ed, tid);
        return 1;
    }

    uint64_t EventDispatcher::read_from_event_queue(uint64_t tid, uint64_t ed)
    {
        Task::TaskManager::instance().lock();
        auto ped = Task::TaskManager::instance().get_ed(ed, tid);
        if (ped->last.queue.first().is_end())
        { 
            Task::TaskManager::instance().unlock();
            return 0;
        }
        auto ret = *(ped->last.queue.first());
        ped->last.queue.remove(ped->last.queue.first());
        Task::TaskManager::instance().unlock();
        return (uint64_t)ret.message;
    }
}
