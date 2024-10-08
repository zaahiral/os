//@globals
uint32_t make_ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return ((uint32_t)d << 24) | ((uint32_t)c << 16) | ((uint32_t)b << 8) | (uint32_t)a;
}

void net_test()
{
    uint32_t gip_be = make_ip(10, 0, 2, 2);
    Drivers::AMDPCNetIIIDriver::instance()->send_data((uint8_t*)"Hello network!", 14);
    printf("Network test ready.\n");
    while (1) { Task::TaskManager::instance().sleep_for(1000); }
}
//@body
    auto task1 = Task::TaskManager::instance().create_new_kernel_task();
    task1.set_entry_point((void*)& net_test);

    Task::TaskManager::instance().add_task(task1);
    Task::TaskManager::instance().run();
