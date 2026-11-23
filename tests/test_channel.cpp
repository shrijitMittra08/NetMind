#include "netmind/simulation/Channel.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

// Checks that a channel refuses new packets once its queue is full
TEST_CASE("Channel enforces its queue capacity") {
    netmind::Channel channel(
        {7, netmind::Direction::AtoB},
        1,
        2);

    netmind::Packet first;
    first.id = 1;

    netmind::Packet second;
    second.id = 2;

    REQUIRE(channel.tryEnqueue(first, 1));
    REQUIRE_FALSE(channel.tryEnqueue(second, 1));
    REQUIRE(channel.queueSize() == 1);
    REQUIRE(channel.front().id == 1);
}

// Checks that finishing a transmission only works for the current one, using its generation number
TEST_CASE("Channel completes only the current transmission") {
    netmind::Channel channel(
        {7, netmind::Direction::AtoB},
        1,
        2);

    netmind::Packet first;
    first.id = 1;

    netmind::Packet second;
    second.id = 2;

    REQUIRE(channel.tryEnqueue(first, 2));
    REQUIRE(channel.tryEnqueue(second, 2));

    const std::uint64_t firstGeneration =
        channel.beginTransmission();

    REQUIRE(channel.busy());
    REQUIRE(channel.front().id == 1);

    const auto completed =
        channel.completeTransmission(firstGeneration);

    REQUIRE(completed.has_value());
    REQUIRE(completed->id == 1);
    REQUIRE_FALSE(channel.busy());
    REQUIRE(channel.queueSize() == 1);
    REQUIRE(channel.front().id == 2);

    const std::uint64_t secondGeneration =
        channel.beginTransmission();

    REQUIRE_FALSE(
        channel.completeTransmission(firstGeneration).has_value());

    const auto secondCompleted =
        channel.completeTransmission(secondGeneration);

    REQUIRE(secondCompleted.has_value());
    REQUIRE(secondCompleted->id == 2);
    REQUIRE(channel.empty());
}

// Checks that flushing a channel returns its queued packets and cancels the pending transmission
TEST_CASE("Channel flush removes queued packets and invalidates events") {
    netmind::Channel channel(
        {7, netmind::Direction::AtoB},
        1,
        2);

    netmind::Packet packet;
    packet.id = 42;

    REQUIRE(channel.tryEnqueue(packet, 2));

    const std::uint64_t generation =
        channel.beginTransmission();

    const std::vector<netmind::Packet> flushed = channel.flush();

    REQUIRE(flushed.size() == 1);
    REQUIRE(flushed.front().id == 42);
    REQUIRE(channel.empty());
    REQUIRE_FALSE(channel.busy());
    REQUIRE_FALSE(
        channel.completeTransmission(generation).has_value());
}