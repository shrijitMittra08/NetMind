#include "netmind/simulation/ScenarioManager.h"

#include "netmind/common/Errors.h"
#include "netmind/common/Units.h"
#include "netmind/graph/TopologyLoader.h"
#include "netmind/model/Flow.h"
#include "netmind/model/TrafficPattern.h"
#include "netmind/routing/RoutingManager.h"
#include "netmind/simulation/EventFactory.h"
#include "netmind/simulation/SimulationEngine.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

namespace netmind {

struct ScenarioManager::ScenarioData {
    nlohmann::json document;
};

namespace {

const nlohmann::json& requiredField(const nlohmann::json& object, const char* name) {
    if (!object.is_object() || !object.contains(name)) {
        throw ConfigError(std::string("Scenario is missing required field: ") + name);
    }
    return object.at(name);
}

std::string requiredString(const nlohmann::json& object, const char* name) {
    const nlohmann::json& value = requiredField(object, name);
    if (!value.is_string()) {
        throw ConfigError(std::string("Scenario field must be a string: ") + name);
    }
    return value.get<std::string>();
}

double requiredNumber(const nlohmann::json& object, const char* name) {
    const nlohmann::json& value = requiredField(object, name);
    if (!value.is_number()) {
        throw ConfigError(std::string("Scenario field must be numeric: ") + name);
    }
    const double number = value.get<double>();
    if (!std::isfinite(number)) {
        throw ConfigError(std::string("Scenario field must be finite: ") + name);
    }
    return number;
}

const nlohmann::json& requiredInteger(const nlohmann::json& object, const char* name) {
    const nlohmann::json& value = requiredField(object, name);
    if (!value.is_number_integer()) {
        throw ConfigError(
            std::string("Scenario field must be a non-negative integer: ") + name);
    }
    return value;
}

std::uint32_t requiredUint32(const nlohmann::json& object, const char* name) {
    const nlohmann::json& value = requiredInteger(object, name);
    const std::uint64_t maxValue = std::numeric_limits<std::uint32_t>::max();
    if (value.is_number_unsigned()) {
        const std::uint64_t number = value.get<std::uint64_t>();
        if (number > maxValue) {
            throw ConfigError(std::string("Scenario integer is too large: ") + name);
        }
        return static_cast<std::uint32_t>(number);
    }
    const std::int64_t number = value.get<std::int64_t>();
    if (number < 0 || static_cast<std::uint64_t>(number) > maxValue) {
        throw ConfigError(
            std::string("Scenario integer is outside the valid range: ") + name);
    }
    return static_cast<std::uint32_t>(number);
}

std::uint64_t requiredUint64(const nlohmann::json& object, const char* name) {
    const nlohmann::json& value = requiredInteger(object, name);
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    const std::int64_t number = value.get<std::int64_t>();
    if (number < 0) {
        throw ConfigError(std::string("Scenario field must be non-negative: ") + name);
    }
    return static_cast<std::uint64_t>(number);
}

const nlohmann::json& requiredArray(
    const nlohmann::json& document, const char* name, const char* errorMessage) {
    if (!document.contains(name) || !document.at(name).is_array()) {
        throw ConfigError(errorMessage);
    }
    return document.at(name);
}

NodeId requireHostId(const Graph& graph, const std::string& name) {
    const Node* node = graph.findByName(name);
    if (node == nullptr) {
        throw ConfigError("Scenario refers to unknown host: " + name);
    }
    if (node->type() != NodeType::HOST) {
        throw ConfigError("Scenario flow endpoint is not a host: " + name);
    }
    return node->getId();
}

std::unique_ptr<TrafficPattern> makePattern(
    const nlohmann::json& flow, double rateMbps, std::uint32_t packetBytes) {
    const std::string pattern = requiredString(flow, "pattern");
    if (pattern == "constant") {
        return std::make_unique<ConstantRate>(rateMbps, packetBytes);
    }
    if (pattern == "bursty") {
        return std::make_unique<BurstyPattern>(
            requiredNumber(flow, "on_duration_s"),
            requiredNumber(flow, "off_duration_s"),
            rateMbps,
            packetBytes);
    }
    if (pattern == "poisson") {
        const double packetsPerSecond =
            rateMbps * 1'000'000.0 / (static_cast<double>(packetBytes) * 8.0);
        return std::make_unique<PoissonPattern>(packetsPerSecond, packetBytes);
    }
    throw ConfigError("Unsupported traffic pattern: " + pattern);
}

Flow readFlow(const nlohmann::json& flow, const Graph& graph, double duration) {
    const FlowId id = requiredUint32(flow, "id");
    const NodeId src = requireHostId(graph, requiredString(flow, "src"));
    const NodeId dst = requireHostId(graph, requiredString(flow, "dst"));
    const double rateMbps = requiredNumber(flow, "rate_mbps");
    const std::uint32_t packetBytes = requiredUint32(flow, "packet_bytes");
    const double start = requiredNumber(flow, "start_s");
    const double stop = requiredNumber(flow, "stop_s");
    if (start < 0.0 || stop <= start || stop > duration) {
        throw ConfigError("Flow times must satisfy 0 <= start_s < stop_s <= duration_s");
    }
    return Flow(id, src, dst, start, stop, makePattern(flow, rateMbps, packetBytes));
}

void validateRoutingChoice(
    const std::string& algorithm, const std::string& weight, double reconvergenceDelaySeconds) {
    RoutingManager validator;
    validator.setAlgorithm(algorithm);
    validator.setPolicy(weight);
    validator.setReconvergenceDelay(reconvergenceDelaySeconds);
}

nlohmann::json readScenarioDocument(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw ConfigError("Unable to open scenario file: " + path);
    }
    nlohmann::json document;
    try {
        input >> document;
    } catch (const nlohmann::json::exception& error) {
        throw ConfigError(std::string("Invalid scenario JSON: ") + error.what());
    }
    if (!document.is_object()) {
        throw ConfigError("Scenario root must be a JSON object");
    }
    return document;
}

double readReconvergenceDelaySeconds(const nlohmann::json& routing) {
    if (!routing.contains("reconvergence_delay_ms")) {
        return 0.0;
    }
    const nlohmann::json& delayValue = routing.at("reconvergence_delay_ms");
    if (!delayValue.is_number()) {
        throw ConfigError("reconvergence_delay_ms must be numeric");
    }
    const double delayMs = delayValue.get<double>();
    if (!std::isfinite(delayMs) || delayMs < 0.0) {
        throw ConfigError("reconvergence_delay_ms must be finite and non-negative");
    }
    return msToSec(delayMs);
}

std::filesystem::path resolveTopologyPath(
    const std::string& topology, const std::string& scenarioPath) {
    std::filesystem::path resolved(topology);
    if (resolved.is_relative()) {
        resolved = std::filesystem::absolute(scenarioPath).parent_path() / resolved;
    }
    return std::filesystem::absolute(resolved);
}

void validateFlows(const nlohmann::json& flows, const Graph& graph, double duration) {
    std::unordered_set<FlowId> flowIds;
    for (const nlohmann::json& flow : flows) {
        if (!flow.is_object()) {
            throw ConfigError("Each scenario flow must be an object");
        }
        if (!flowIds.insert(requiredUint32(flow, "id")).second) {
            throw ConfigError("Scenario flow IDs must be unique");
        }
        readFlow(flow, graph, duration);
    }
}

void validateEvents(const nlohmann::json& events, const Graph& graph, double duration) {
    for (const nlohmann::json& event : events) {
        if (!event.is_object()) {
            throw ConfigError("Each scenario event must be an object");
        }
        const double time = requiredNumber(event, "time_s");
        if (time < 0.0 || time > duration) {
            throw ConfigError("Event time must be between zero and scenario duration");
        }
        (void)EventFactory::create(event, graph);
    }
}

}

void ScenarioManager::loadScenario(const std::string& path) {
    nlohmann::json document = readScenarioDocument(path);

    const std::string name = requiredString(document, "name");
    if (name.empty()) {
        throw ConfigError("Scenario name cannot be empty");
    }
    const std::string topology = requiredString(document, "topology");
    const std::uint64_t scenarioSeed = requiredUint64(document, "seed");
    const double duration = requiredNumber(document, "duration_s");
    if (duration <= 0.0) {
        throw ConfigError("Scenario duration must be positive");
    }

    const nlohmann::json& routing = requiredField(document, "routing");
    if (!routing.is_object()) {
        throw ConfigError("Scenario routing field must be an object");
    }
    const std::string algorithm = requiredString(routing, "algorithm");
    const std::string weight = requiredString(routing, "weight");
    const double reconvergenceDelaySeconds = readReconvergenceDelaySeconds(routing);
    validateRoutingChoice(algorithm, weight, reconvergenceDelaySeconds);

    const std::filesystem::path resolvedTopologyPath = resolveTopologyPath(topology, path);
    auto loadedGraph =
        std::make_unique<Graph>(TopologyLoader::loadFromFile(resolvedTopologyPath.string()));
    const nlohmann::json& flows =
        requiredArray(document, "flows", "Scenario flows field must be an array");
    const nlohmann::json& events =
        requiredArray(document, "events", "Scenario events field must be an array");
    validateFlows(flows, *loadedGraph, duration);
    validateEvents(events, *loadedGraph, duration);

    auto data = std::make_shared<ScenarioData>();
    data->document = std::move(document);
    scenario_ = std::move(data);
    graph_ = std::move(loadedGraph);
    seed_ = scenarioSeed;
    topologyPath_ = resolvedTopologyPath.string();
    routingAlgorithm_ = algorithm;
    routingWeight_ = weight;
    reconvergenceDelaySeconds_ = reconvergenceDelaySeconds;
    durationSeconds_ = duration;
}

void ScenarioManager::loadTopology(const std::string& path) {
    graph_ = std::make_unique<Graph>(TopologyLoader::loadFromFile(path));
    scenario_.reset();
    topologyPath_ = std::filesystem::absolute(path).string();
    resetRoutingAndDuration();
}

void ScenarioManager::setGraph(std::unique_ptr<Graph> graph) {
    if (graph == nullptr) {
        throw ConfigError("Scenario graph cannot be null");
    }
    graph_ = std::move(graph);
    scenario_.reset();
    topologyPath_.clear();
    resetRoutingAndDuration();
}

void ScenarioManager::resetRoutingAndDuration() {
    routingAlgorithm_ = "dijkstra";
    routingWeight_ = "latency";
    reconvergenceDelaySeconds_ = 0.0;
    durationSeconds_ = 0.0;
}

const Graph& ScenarioManager::graph() const {
    if (graph_ == nullptr) {
        throw ConfigError("No topology has been loaded");
    }
    return *graph_;
}

Graph& ScenarioManager::mutableGraph() {
    if (graph_ == nullptr) {
        throw ConfigError("No topology has been loaded");
    }
    return *graph_;
}

void ScenarioManager::setSeed(std::uint64_t seed) {
    seed_ = seed;
}

std::uint64_t ScenarioManager::seed() const {
    return seed_;
}

SimTime ScenarioManager::durationSeconds() const noexcept {
    return durationSeconds_;
}

bool ScenarioManager::hasScenario() const noexcept {
    return scenario_ != nullptr;
}

const std::string& ScenarioManager::topologyPath() const noexcept {
    return topologyPath_;
}

void ScenarioManager::setRoutingAlgorithm(const std::string& name) {
    validateRoutingChoice(name, routingWeight_, reconvergenceDelaySeconds_);
    routingAlgorithm_ = name;
}

void ScenarioManager::setRoutingWeight(const std::string& name) {
    validateRoutingChoice(routingAlgorithm_, name, reconvergenceDelaySeconds_);
    routingWeight_ = name;
}

const std::string& ScenarioManager::routingAlgorithm() const noexcept {
    return routingAlgorithm_;
}

const std::string& ScenarioManager::routingWeight() const noexcept {
    return routingWeight_;
}

ScenarioManager ScenarioManager::cloneWith(const std::string& algorithmName) const {
    if (scenario_ == nullptr || topologyPath_.empty()) {
        throw ConfigError("cloneWith requires a scenario loaded from a scenario file");
    }
    validateRoutingChoice(algorithmName, routingWeight_, reconvergenceDelaySeconds_);

    ScenarioManager clone;
    clone.graph_ = std::make_unique<Graph>(TopologyLoader::loadFromFile(topologyPath_));
    clone.scenario_ = scenario_;
    clone.seed_ = seed_;
    clone.topologyPath_ = topologyPath_;
    clone.routingAlgorithm_ = algorithmName;
    clone.routingWeight_ = routingWeight_;
    clone.reconvergenceDelaySeconds_ = reconvergenceDelaySeconds_;
    clone.durationSeconds_ = durationSeconds_;
    return clone;
}

void ScenarioManager::run(SimulationEngine& engine, SimTime endTime) {
    if (graph_ == nullptr) {
        throw ConfigError("Load a topology or scenario before running");
    }
    if (!std::isfinite(endTime) || endTime < 0.0) {
        throw ConfigError("Scenario run end time must be finite and non-negative");
    }
    if (scenario_ != nullptr && endTime > durationSeconds_) {
        throw ConfigError("Run end time cannot exceed the scenario duration");
    }

    engine.setSeed(seed_);
    engine.routingManager().setAlgorithm(routingAlgorithm_);
    engine.routingManager().setPolicy(routingWeight_);
    engine.routingManager().setReconvergenceDelay(reconvergenceDelaySeconds_);
    engine.setGraph(std::move(graph_));

    if (scenario_ != nullptr) {
        const nlohmann::json& document = scenario_->document;
        for (const nlohmann::json& flow : document.at("flows")) {
            engine.addFlow(readFlow(flow, engine.graph(), durationSeconds_));
        }
        for (const nlohmann::json& event : document.at("events")) {
            engine.scheduleEvent(
                requiredNumber(event, "time_s"), EventFactory::create(event, engine.graph()));
        }
    }
    engine.runUntil(endTime);
}

void ScenarioManager::reset() {
    if (!topologyPath_.empty()) {
        graph_ = std::make_unique<Graph>(TopologyLoader::loadFromFile(topologyPath_));
        return;
    }
    graph_.reset();
    scenario_.reset();
    resetRoutingAndDuration();
}

}
