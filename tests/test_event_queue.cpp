#include "netmind/common/Errors.h"
#include "netmind/simulation/EventQueue.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

class NamedEvent final : public netmind::NetworkEvent {
public:
    explicit NamedEvent(std::string name)
        : name_(std::move(name)) {}

    void apply(netmind::SimulationEngine&) override {}

    [[nodiscard]] std::string describe() const override {
        return name_;
    }

private:
    std::string name_;
};

}             

TEST_CASE("EventQueue pops events in increasing time order") {
    netmind::EventQueue queue;

    queue.schedule(
        3.0,
        std::make_shared<NamedEvent>("later"));
    queue.schedule(
        1.0,
        std::make_shared<NamedEvent>("first"));
    queue.schedule(
        2.0,
        std::make_shared<NamedEvent>("middle"));

    REQUIRE(queue.size() == 3);
    REQUIRE(queue.nextTime() == 1.0);

    const auto first = queue.pop();
    REQUIRE(first.time == 1.0);
    REQUIRE(first.event->describe() == "first");
    REQUIRE(queue.nextTime() == 2.0);

    const auto middle = queue.pop();
    REQUIRE(middle.time == 2.0);
    REQUIRE(middle.event->describe() == "middle");
    REQUIRE(queue.nextTime() == 3.0);

    const auto later = queue.pop();
    REQUIRE(later.time == 3.0);
    REQUIRE(later.event->describe() == "later");

    REQUIRE(queue.empty());
    REQUIRE_FALSE(queue.nextTime().has_value());
}

TEST_CASE("EventQueue preserves insertion order for equal times") {
    netmind::EventQueue queue;

    queue.schedule(
        5.0,
        std::make_shared<NamedEvent>("first scheduled"));
    queue.schedule(
        5.0,
        std::make_shared<NamedEvent>("second scheduled"));
    queue.schedule(
        5.0,
        std::make_shared<NamedEvent>("third scheduled"));

    REQUIRE(queue.pop().event->describe() == "first scheduled");
    REQUIRE(queue.pop().event->describe() == "second scheduled");
    REQUIRE(queue.pop().event->describe() == "third scheduled");
}

TEST_CASE("EventQueue rejects invalid times and null events") {
    netmind::EventQueue queue;
    const auto validEvent = std::make_shared<NamedEvent>("valid");

    REQUIRE_THROWS_AS(queue.schedule(-1.0, validEvent), netmind::ConfigError);
    REQUIRE_THROWS_AS(
        queue.schedule(
            std::numeric_limits<double>::infinity(),
            validEvent),
        netmind::ConfigError);
    REQUIRE_THROWS_AS(
        queue.schedule(
            std::numeric_limits<double>::quiet_NaN(),
            validEvent),
        netmind::ConfigError);
    REQUIRE_THROWS_AS(
        queue.schedule(1.0, nullptr),
        netmind::ConfigError);

    REQUIRE(queue.empty());
}

TEST_CASE("EventQueue rejects popping when empty") {
    netmind::EventQueue queue;

    REQUIRE_THROWS_AS(queue.pop(), std::logic_error);
}