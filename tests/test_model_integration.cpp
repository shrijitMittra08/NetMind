#include "netmind/model/Host.h"
#include "netmind/model/Link.h"
#include "netmind/model/Router.h"

#include <catch2/catch_test_macros.hpp>

#include <iostream>

TEST_CASE("network model entities can be constructed together") {
    netmind::Host hostA(1, "HostA", "192.168.1.1");
    netmind::Router router(2, "Router1", "192.168.1.254");
    netmind::Host hostB(3, "HostB", "192.168.2.1");

    netmind::Link linkA(101, 1, 2, 100.0, 2.0, 0.0, 10);
    netmind::Link linkB(102, 2, 3, 100.0, 3.0, 0.0, 10);

    REQUIRE(hostA.getId() == 1);
    REQUIRE(router.getId() == 2);
    REQUIRE(hostB.getId() == 3);

    REQUIRE(linkA.connects(hostA.getId(), router.getId()));
    REQUIRE(linkB.connects(router.getId(), hostB.getId()));

    std::cout
        << "NetMind model integration check\n"
        << "Host: " << hostA.name() << " (" << hostA.ip() << ")\n"
        << "Router: " << router.name() << " (" << router.ip() << ")\n"
        << "Host: " << hostB.name() << " (" << hostB.ip() << ")\n"
        << "Link " << linkA.getId() << ": "
        << linkA.a() << " <-> " << linkA.b() << "\n"
        << "Link " << linkB.getId() << ": "
        << linkB.a() << " <-> " << linkB.b() << '\n';
}