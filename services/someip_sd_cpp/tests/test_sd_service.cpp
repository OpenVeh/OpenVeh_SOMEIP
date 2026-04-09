#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

#include "someip_sd/sd_api.hpp"
#include "someip_sd/sd_daemon.hpp"

int main() {
    someip_sd::SomeIpSdDaemon daemon("127.0.0.1", 30491);
    daemon.SetDiscoveryMulticast("239.255.0.1", 30491, "127.0.0.1");
    daemon.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    someip_sd::SomeIpSdApi api("127.0.0.1", 30491, 1000);
    api.SetDiscoveryMulticast("239.255.0.1", 30491, "127.0.0.1");

    someip_sd::ServiceDescriptor desc;
    desc.service_id = 0x3333;
    desc.instance_id = 0x0001;
    desc.major_version = 1;
    desc.minor_version = 2;
    desc.endpoint_host = "127.0.0.1";
    desc.endpoint_port = 41000;
    desc.transport = "udp";

    std::string error;
    assert(api.RegisterService(desc, 2, &error));

    auto found = api.DiscoverServices(desc.service_id, desc.instance_id, 600, &error);
    assert(error.empty());
    assert(!found.empty());
    assert(found.front().endpoint_port == desc.endpoint_port);

    assert(api.UnregisterService(desc.service_id, desc.instance_id, &error));
    found = api.DiscoverServices(desc.service_id, desc.instance_id, 600, &error);
    assert(found.empty());

    desc.service_id = 0x4444;
    desc.instance_id = 0x0002;
    desc.endpoint_port = 42000;
    assert(api.RegisterService(desc, 1, &error));
    std::this_thread::sleep_for(std::chrono::milliseconds(1300));
    found = api.DiscoverServices(desc.service_id, desc.instance_id, 600, &error);
    assert(found.empty());

#ifdef __linux__
    someip_sd::SomeIpSdDaemon daemon_uds("127.0.0.1", 30492);
    daemon_uds.SetDiscoveryMulticast("239.255.0.1", 30492, "127.0.0.1");
    daemon_uds.EnableUnixDomainControl("/tmp/openveh_someip_sd_test.sock");
    daemon_uds.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    someip_sd::SomeIpSdApi api_uds("127.0.0.1", 30492, 1000);
    api_uds.SetDiscoveryMulticast("239.255.0.1", 30492, "127.0.0.1");
    api_uds.EnableUnixDomainControl("/tmp/openveh_someip_sd_test.sock");

    desc.service_id = 0x5555;
    desc.instance_id = 0x0003;
    desc.endpoint_port = 43000;
    assert(api_uds.RegisterService(desc, 2, &error));
    found = api_uds.DiscoverServices(desc.service_id, desc.instance_id, 600, &error);
    assert(!found.empty());
    assert(api_uds.UnregisterService(desc.service_id, desc.instance_id, &error));

    daemon_uds.Stop();
#endif

    daemon.Stop();
    std::cout << "C++ SOME/IP-SD tests passed.\n";
    return 0;
}
