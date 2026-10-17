#include <iostream>
#include "common/Ip.h"
#include "model/Host.h"
#include "model/Router.h"
#include "model/Packet.h"
using namespace netmind;
int main() {
    Host hostA(1, "HostA", "10.0.0.1", 2);
    Router router(2, "R1", "10.0.0.2");
    Host hostB(3, "HostB", "10.0.0.3", 2);
    Packet packet(
        1,                
        hostA.getId(),    
        hostB.getId(),      
        1,                
        512,                
        PacketType::DATA,
        64,               
        0.0 
    );
    std::cout << "=== NetMind Demo ===\n\n";
    std::cout << "Network:\n";
    std::cout << hostA.describe() << "\n";
    std::cout << "    |\n";
    std::cout << "    |\n";
    std::cout << router.describe() << "\n";
    std::cout << "    |\n";
    std::cout << "    |\n";
    std::cout << hostB.describe() << "\n\n";
    std::cout << "Packet:\n";
    std::cout << packet << "\n";
    std::cout << "\nPacket path:\n";
    packet.pathTaken.push_back(hostA.getId());
    packet.pathTaken.push_back(router.getId());
    packet.pathTaken.push_back(hostB.getId());
    std::cout << "HostA -> R1 -> HostB\n";
    std::cout << "Hop count: " << packet.hopCount() << "\n";
    packet.markDelivered(0.025);
    std::cout << "Status: DELIVERED\n";
    std::cout << "Latency: " << packet.latencySec() << " sec\n";
    return 0;
}