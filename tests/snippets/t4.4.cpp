//@globals
void elf_task()
{
    Task::TaskManager::instance().lock();
    auto currentTask = Task::TaskManager::instance().get_current_task_pointer();
    Loader::prepare_elf("initrd/bin/hello", currentTask);
    auto entryPoint = currentTask->get_entry_point();
    Task::TaskManager::instance().unlock();
    // prepare_elf maps the user stack, and the stack grows down from its top.
    start_userspace_task_sysret((uint64_t)entryPoint, Loader::USER_STACK_BASE + Loader::USER_STACK_SIZE);
}
//@body
    auto task1 = Task::TaskManager::instance().create_new_kernel_task();
    task1.set_entry_point((void*)& elf_task);
    Task::TaskManager::instance().add_task(task1);
    Task::TaskManager::instance().run();
