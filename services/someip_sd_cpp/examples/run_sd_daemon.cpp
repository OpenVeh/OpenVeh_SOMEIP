#include <csignal>
#include <chrono>
#include <iostream>
#include <thread>

#include "someip_sd/sd_daemon.hpp"

namespace {
volatile std::sig_atomic_t g_stop = 0;

void OnSignal(int) { g_stop = 1; }
}  // namespace

int main() {
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    someip_sd::SomeIpSdDaemon daemon("127.0.0.1", 30490);
    daemon.SetDiscoveryMulticast("239.255.0.1", 30490, "127.0.0.1");
#ifdef __linux__
    daemon.EnableUnixDomainControl("/tmp/openveh_someip_sd.sock");
#endif
    daemon.Start();
    std::cout << "SOME/IP-SD daemon started on 127.0.0.1:30490\n";

    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    daemon.Stop();
    std::cout << "SOME/IP-SD daemon stopped\n";
    return 0;
}
