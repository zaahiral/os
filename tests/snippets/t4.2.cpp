//@globals
void task_one()
{
    printf("Task one\n");
    while (1)
        Task::TaskManager::instance().sleep_for(1000);
}
void task_two()
{
    printf("Task two\n");
    while (1)
        Task::TaskManager::instance().sleep_for(1000);
}
void task_three()
{
    printf("Task three\n");
    while (1)
        Task::TaskManager::instance().sleep_for(1000);
}
void task_four()
{
    printf("Task four\n");
    while (1)
        Task::TaskManager::instance().sleep_for(1000);
}
//@body
    auto task1 = Task::TaskManager::instance().create_new_kernel_task(600);
    task1.set_entry_point((void*)& task_one);
    auto task2 = Task::TaskManager::instance().create_new_kernel_task(270);
    task2.set_entry_point((void*)& task_two);
    auto task3 = Task::TaskManager::instance().create_new_kernel_task(23);
    task3.set_entry_point((void*)& task_three);
    auto task4 = Task::TaskManager::instance().create_new_kernel_task(512);
    task4.set_entry_point((void*)& task_four);
    Task::TaskManager::instance().add_task(task1);
    Task::TaskManager::instance().add_task(task2);
    Task::TaskManager::instance().add_task(task3);
    Task::TaskManager::instance().add_task(task4);
    Task::TaskManager::instance().run();
