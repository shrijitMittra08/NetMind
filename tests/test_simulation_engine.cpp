#include "netmind/SimObserver.h"
#include "netmind/common/Errors.h"
#include "netmind/simulation/SimulationEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class RecordingEvent final : public netmind::NetworkEvent {
public:
    RecordingEvent(
        std::string name,
        std::vector<std::pair<std::string, netmind::SimTime>>& log
    )
        : name_(std::move(name)),
          log_(log) {}

    void apply(netmind::SimulationEngine& engine) override {
        log_.emplace_back(name_, engine.now());
    }

    [[nodiscard]] std::string describe() const override {
        return name_;
    }

private:
    std::string name_;
    std::vector<std::pair<std::string, netmind::SimTime>>& log_;
};

std::shared_ptr<RecordingEvent> makeEvent(
    const std::string& name,
    std::vector<std::pair<std::string, netmind::SimTime>>& log
) {
    return std::make_shared<RecordingEvent>(name, log);
}

}             

TEST_CASE("SimulationEngine step executes exactly one event") {
    netmind::SimulationEngine engine(1234);
    std::vector<std::pair<std::string, netmind::SimTime>> log;

    engine.scheduleEvent(3.0, makeEvent("later", log));
    engine.scheduleEvent(1.0, makeEvent("first", log));
    engine.scheduleEvent(1.0, makeEvent("second", log));

    REQUIRE(engine.now() == 0.0);
    REQUIRE(engine.pendingEvents() == 3);

    engine.step();
    REQUIRE(engine.now() == 1.0);
    REQUIRE(log.size() == 1);
    REQUIRE(log[0].first == "first");

    engine.step();
    REQUIRE(engine.now() == 1.0);
    REQUIRE(log.size() == 2);
    REQUIRE(log[1].first == "second");

    engine.step();
    REQUIRE(engine.now() == 3.0);
    REQUIRE(log.size() == 3);
    REQUIRE(log[2].first == "later");
    REQUIRE(engine.finished());
}

TEST_CASE("SimulationEngine runUntil processes events through end time") {
    netmind::SimulationEngine engine(1234);
    std::vector<std::pair<std::string, netmind::SimTime>> log;

    engine.scheduleEvent(2.0, makeEvent("inside", log));
    engine.scheduleEvent(5.0, makeEvent("after", log));

    engine.runUntil(3.0);

    REQUIRE(engine.now() == 3.0);
    REQUIRE(log.size() == 1);
    REQUIRE(log[0].first == "inside");
    REQUIRE(engine.pendingEvents() == 1);
    REQUIRE_FALSE(engine.finished());

    engine.runUntil(5.0);

    REQUIRE(engine.now() == 5.0);
    REQUIRE(log.size() == 2);
    REQUIRE(log[1].first == "after");
    REQUIRE(engine.finished());
}

TEST_CASE("SimulationEngine run processes every pending event") {
    netmind::SimulationEngine engine(1234);
    std::vector<std::pair<std::string, netmind::SimTime>> log;

    engine.scheduleEvent(2.0, makeEvent("first", log));
    engine.scheduleEvent(4.0, makeEvent("second", log));

    engine.run();

    REQUIRE(engine.now() == 4.0);
    REQUIRE(log.size() == 2);
    REQUIRE(engine.finished());
}

TEST_CASE("SimulationEngine rejects invalid event times") {
    netmind::SimulationEngine engine(1234);
    std::vector<std::pair<std::string, netmind::SimTime>> log;

    REQUIRE_THROWS_AS(
        engine.scheduleEvent(-1.0, makeEvent("past", log)),
        netmind::ConfigError);

    REQUIRE_THROWS_AS(
        engine.scheduleEvent(
            std::numeric_limits<double>::infinity(),
            makeEvent("infinite", log)),
        netmind::ConfigError);

    REQUIRE_THROWS_AS(
        engine.scheduleEvent(
            std::numeric_limits<double>::quiet_NaN(),
            makeEvent("not-a-time", log)),
        netmind::ConfigError);

    engine.scheduleEvent(2.0, makeEvent("valid", log));
    engine.step();

    REQUIRE_THROWS_AS(
        engine.scheduleEvent(1.0, makeEvent("too-early", log)),
        netmind::ConfigError);

    REQUIRE_THROWS_AS(engine.runUntil(1.0), netmind::ConfigError);
}

TEST_CASE("SimulationEngine allocates distinct packet IDs") {
    netmind::SimulationEngine first(1);
    netmind::SimulationEngine second(2);

    REQUIRE(first.allocatePacketId() == 1);
    REQUIRE(first.allocatePacketId() == 2);
    REQUIRE(second.allocatePacketId() == 1);
}

TEST_CASE("SimulationEngine observer registration avoids duplicates") {
    class DeliveryObserver final : public netmind::SimObserver {
    public:
        void onDelivered(netmind::SimTime, const netmind::Packet&) override {
            ++deliveries;
        }

        int deliveries{};
    };

    netmind::SimulationEngine engine(1234);
    DeliveryObserver observer;
    netmind::Packet packet;

    engine.addObserver(observer);
    engine.addObserver(observer);
    engine.deliver(packet, 1);

    REQUIRE(observer.deliveries == 1);

    engine.removeObserver(observer);
    engine.deliver(packet, 1);

    REQUIRE(observer.deliveries == 1);
}