#include "netmind/cli/Session.h"

#include "netmind/SimObserver.h"
#include "netmind/metrics/MetricsCollector.h"
#include "netmind/metrics/TraceLogger.h"
#include "netmind/model/Link.h"
#include "netmind/model/Node.h"
#include "netmind/simulation/Channel.h"
#include "netmind/simulation/SimulationEngine.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace netmind::cli {
namespace {

std::string nodeNameOrId(const Graph& graph, NodeId id) {
    const Node* node = graph.getNode(id);
    return node == nullptr ? std::to_string(id) : node->name();
}

std::string channelNameOrId(const Graph& graph, const ChannelId& channel) {
    const Link* link = graph.getLink(channel.linkId);
    if (link == nullptr) {
        return std::to_string(channel.linkId);
    }
    NodeId fromId = link->a();
    NodeId toId = link->b();
    if (channel.dir != Direction::AtoB) {
        std::swap(fromId, toId);
    }
    std::string fromName = std::to_string(link->a());
    if (const Node* from = graph.getNode(fromId)) {
        fromName = from->name();
    }
    std::string toName = std::to_string(link->b());
    if (const Node* to = graph.getNode(toId)) {
        toName = to->name();
    }
    return fromName + ">" + toName;
}

class CliEventObserver final : public SimObserver {
public:
    CliEventObserver(const Graph& graph, std::vector<std::string>& messages)
        : graph_(graph), messages_(messages) {}

    void onForward(SimTime time, const Packet& packet, NodeId at, NodeId next) override {
        messages_.push_back(prefix(time) + node(at) + " forward pkt " + std::to_string(packet.id) +
                            " -> " + node(next));
    }

    void onDelivered(SimTime time, const Packet& packet) override {
        messages_.push_back(prefix(time) + "packet " + std::to_string(packet.id) +
                            " delivered to " + node(packet.dstId));
    }

    void onDropped(SimTime time, const Packet& packet, const std::string& where,
                   DropReason reason) override {
        messages_.push_back(prefix(time) + "packet " + std::to_string(packet.id) + " dropped at " +
                            where + " (" + toString(reason) + ")");
    }

    void onNetworkEvent(SimTime time, const std::string& type, const std::string& target,
                        const std::string& detail) override {
        std::string message = prefix(time) + type + " " + target;
        if (!detail.empty()) {
            message += " " + detail;
        }
        messages_.push_back(message);
    }

private:
    [[nodiscard]] std::string node(NodeId id) const { return nodeNameOrId(graph_, id); }

    [[nodiscard]] static std::string prefix(SimTime time) {
        std::ostringstream out;
        out << "[t=" << std::fixed << std::setprecision(6) << time << "] ";
        return out.str();
    }

    const Graph& graph_;
    std::vector<std::string>& messages_;
};

}

Session::Session() = default;
Session::~Session() = default;

ScenarioManager& Session::scenarioManager() noexcept { return scenarioManager_; }
const ScenarioManager& Session::scenarioManager() const noexcept { return scenarioManager_; }
SimulationEngine* Session::engine() noexcept { return engine_.get(); }
const SimulationEngine* Session::engine() const noexcept { return engine_.get(); }

void Session::setEngine(std::unique_ptr<SimulationEngine> engine) {
    engine_ = std::move(engine);
    if (engine_ != nullptr) {
        for (const auto& observer : observers_) {
            engine_->addObserver(*observer);
        }
    }
}

void Session::clearRuntime() {
    engine_.reset();
    observers_.clear();
    metrics_ = nullptr;
    traceLogger_ = nullptr;
    tracePath_.clear();
    stepMessages_.clear();
}

void Session::ensureEngine() {
    if (engine_ != nullptr) {
        return;
    }
    const Graph& graph = scenarioManager_.graph();
    std::filesystem::create_directories(outputDirectory_);
    tracePath_ = outputDirectory_ / "netmind_live_trace.csv";
    engine_ = std::make_unique<SimulationEngine>(scenarioManager_.seed());

    auto metrics = std::make_unique<MetricsCollector>(graph);
    metrics_ = metrics.get();
    addObserver(std::move(metrics));

    auto trace = std::make_unique<TraceLogger>(
        tracePath_.string(), [&graph](NodeId id) { return nodeNameOrId(graph, id); },
        [&graph](const ChannelId& channel) { return channelNameOrId(graph, channel); });
    traceLogger_ = trace.get();
    addObserver(std::move(trace));

    addObserver(std::make_unique<CliEventObserver>(graph, stepMessages_));
    scenarioManager_.run(*engine_, 0.0);
}

void Session::reset() {
    clearRuntime();
    scenarioManager_.reset();
}

MetricsCollector* Session::metrics() noexcept { return metrics_; }
const MetricsCollector* Session::metrics() const noexcept { return metrics_; }
TraceLogger* Session::traceLogger() noexcept { return traceLogger_; }
const TraceLogger* Session::traceLogger() const noexcept { return traceLogger_; }
const std::filesystem::path& Session::tracePath() const noexcept { return tracePath_; }

FlowId Session::nextFlowId() const {
    return engine_ == nullptr ? 1 : engine_->allocateFlowId();
}

const std::vector<std::string>& Session::stepMessages() const noexcept { return stepMessages_; }
void Session::clearStepMessages() noexcept { stepMessages_.clear(); }

void Session::addObserver(std::unique_ptr<SimObserver> observer) {
    if (observer == nullptr) {
        throw std::invalid_argument("Cannot add a null simulation observer");
    }
    SimObserver& added = *observer;
    observers_.push_back(std::move(observer));
    if (engine_ != nullptr) {
        engine_->addObserver(added);
    }
}

const std::vector<std::unique_ptr<SimObserver>>& Session::observers() const noexcept {
    return observers_;
}

const std::filesystem::path& Session::outputDirectory() const noexcept { return outputDirectory_; }

void Session::setOutputDirectory(std::filesystem::path directory) {
    if (directory.empty()) {
        throw std::invalid_argument("Output directory cannot be empty");
    }
    outputDirectory_ = std::move(directory);
}

void Session::requestExit() noexcept { exitRequested_ = true; }
bool Session::exitRequested() const noexcept { return exitRequested_; }
void Session::clearExitRequest() noexcept { exitRequested_ = false; }

}
