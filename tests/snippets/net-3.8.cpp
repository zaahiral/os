//@globals
uint32_t make_ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return ((uint32_t)d << 24) | ((uint32_t)c << 16) | ((uint32_t)b << 8) | (uint32_t)a;
}

void net_test()
{
    uint32_t gip_be = make_ip(10, 0, 2, 2);
    auto tcpManager = new Networking::TransmissionControlProtocol::TransmissionControlProtocolManager();
    auto socket = tcpManager->connect(gip_be, 3939);
    while (socket->m_state != Networking::TransmissionControlProtocol::TransmissionControlProtocolSocket::State::ESTABLISHED) {}
    socket->send((uint8_t*)"POST /d493159e-39a2-4015-a5b7-cfcc05a9d739 HTTP/1.1\nHost: localhost\nAccept: application/json\nContent-Type: application/json\nContent-Length: 15\n\n{\"test\":\"data\"}", 159);
    printf("Network test ready.\n");
    while (1) { Task::TaskManager::instance().sleep_for(1000); }
}
//@body
    auto task1 = Task::TaskManager::instance().create_new_kernel_task();
    task1.set_entry_point((void*)& net_test);

    Task::TaskManager::instance().add_task(task1);
    Task::TaskManager::instance().run();
