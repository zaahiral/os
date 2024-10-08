//@globals
void keyboard_task()
{
    auto eventDescriptor = Events::EventDispatcher::instance().register_event_listener(Task::TaskManager::instance().get_current_task(), "HID/Keyboard", 0);
    printf("Keyboard listener ready.\n");
    while (1)
    {
        Events::EventDispatcher::instance().block_event_listen(Task::TaskManager::instance().get_current_task(), eventDescriptor);
        auto scanCode = (uint8_t)Events::EventDispatcher::instance().read_from_event_queue(Task::TaskManager::instance().get_current_task(), eventDescriptor);
        printf("Scan code received: ");
        printf(scanCode);
        printf("\n");
    }
}
//@body
    auto task1 = Task::TaskManager::instance().create_new_kernel_task();
    task1.set_entry_point((void*)& keyboard_task);
    Task::TaskManager::instance().add_task(task1);
    Task::TaskManager::instance().run();
