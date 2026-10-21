#pragma once

#include "netmind/common/Types.h"
#include "netmind/graph/Graph.h"

#include <cstdint>
#include <memory>
#include <string>

namespace netmind {

class SimulationEngine;

class ScenarioManager {
public:
    ScenarioManager() = default;

    void loadScenario(const std::string& path);
    void loadTopology(const std::string& path);
    void setGraph(std::unique_ptr<Graph> graph);

    [[nodiscard]] const Graph& graph() const;
    [[nodiscard]] Graph& mutableGraph();
    [[nodiscard]] SimTime durationSeconds() const noexcept;
    [[nodiscard]] bool hasScenario() const noexcept;
    [[nodiscard]] const std::string& topologyPath() const noexcept;

    void setRoutingAlgorithm(const std::string& name);
    void setRoutingWeight(const std::string& name);
    [[nodiscard]] const std::string& routingAlgorithm() const noexcept;
    [[nodiscard]] const std::string& routingWeight() const noexcept;

    void setSeed(std::uint64_t seed);
    [[nodiscard]] std::uint64_t seed() const;

    [[nodiscard]] ScenarioManager cloneWith(
        const std::string& algorithmName) const;

    void run(SimulationEngine& engine, SimTime endTime);
    void reset();

private:
    struct ScenarioData;

    std::unique_ptr<Graph> graph_;
    std::shared_ptr<ScenarioData> scenario_;
    std::uint64_t seed_{0};
    std::string topologyPath_;
    std::string routingAlgorithm_{"dijkstra"};
    std::string routingWeight_{"latency"};
    double reconvergenceDelaySeconds_{0.0};
    SimTime durationSeconds_{0.0};
};

}
