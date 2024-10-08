//@globals
//@body
    auto task1 = Task::TaskManager::instance().create_new_kernel_task();
    task1.set_entry_point((void*)& task_one);

    Task::TaskManager::instance().add_task(task1);
    Task::TaskManager::instance().run();
