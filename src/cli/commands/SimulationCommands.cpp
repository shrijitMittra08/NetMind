#include "netmind/cli/commands/SimulationCommands.h"

#include "netmind/cli/Command.h"
#include "netmind/cli/CommandLineInterface.h"
#include "netmind/cli/Session.h"
#include "netmind/common/Errors.h"
#include "netmind/metrics/Exporters.h"
#include "netmind/metrics/MetricsCollector.h"
#include "netmind/metrics/Report.h"
#include "netmind/metrics/TraceLogger.h"
#include "netmind/model/Flow.h"
#include "netmind/model/ForwardingTable.h"
#include "netmind/model/Link.h"
#include "netmind/model/Node.h"
#include "netmind/model/Router.h"
#include "netmind/model/TrafficPattern.h"
#include "netmind/routing/RoutingManager.h"
#include "netmind/simulation/ScenarioManager.h"
#include "netmind/simulation/SimulationEngine.h"
#include "netmind/simulation/events/CongestionEvent.h"
#include "netmind/simulation/events/LinkFailureEvent.h"
#include "netmind/simulation/events/RouterFailureEvent.h"
#include "netmind/simulation/events/RouterRestoreEvent.h"
#include "netmind/simulation/events/TrafficBurstEvent.h"
#include "netmind/simulation/events/LinkRestoreEvent"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace netmind::cli {
namespace {

using Args = std::vector<std::string>;

class FunctionCommand final : public Command {
public:
    using Handler = std::function<void(Session&, const Args&, std::ostream&)>;

    FunctionCommand(std::string name, std::string help, Handler handler)
        : name_(std::move(name)), help_(std::move(help)), handler_(std::move(handler)) {}

    std::string name() const override { return name_; }
    std::string help() const override { return help_; }

    void execute(Session& session, const Args& args, std::ostream& out) override {
        handler_(session, args, out);
    }

private:
    std::string name_;
    std::string help_;
    Handler handler_;
};

void requireArgCount(const Args& args, std::initializer_list<std::size_t> allowedCounts,
                     const std::string& usage) {
    for (std::size_t count : allowedCounts) {
        if (args.size() == count) {
            return;
        }
    }
    throw std::invalid_argument("Usage: " + usage);
}

double parseNumber(const std::string& text, const std::string& label) {
    std::size_t charactersUsed = 0;
    double value{};
    try {
        value = std::stod(text, &charactersUsed);
    } catch (...) {
        throw std::invalid_argument(label + " must be numeric");
    }
    if (charactersUsed != text.size() || !std::isfinite(value)) {
        throw std::invalid_argument(label + " must be a finite number");
    }
    return value;
}

std::uint64_t parseUnsigned(const std::string& text, const std::string& label) {
    if (text.empty() || text.front() == '-') {
        throw std::invalid_argument(label + " must be a non-negative integer");
    }
    std::size_t charactersUsed = 0;
    unsigned long long value{};
    try {
        value = std::stoull(text, &charactersUsed);
    } catch (...) {
        throw std::invalid_argument(label + " must be an integer");
    }
    if (charactersUsed != text.size()) {
        throw std::invalid_argument(label + " must be an integer");
    }
    return static_cast<std::uint64_t>(value);
}

std::uint64_t parseCount(const Args& args, std::size_t index, std::uint64_t defaultCount,
                         const std::string& label, std::uint64_t maximum,
                         const std::string& rangeMessage) {
    std::uint64_t count = defaultCount;
    if (args.size() > index) {
        count = parseUnsigned(args[index], label);
    }
    if (count == 0 || count > maximum) {
        throw std::invalid_argument(rangeMessage);
    }
    return count;
}

void requireWord(const Args& args, std::size_t index, const std::string& word,
                 const std::string& message) {
    if (args[index] != word) {
        throw std::invalid_argument(message);
    }
}

void requireNotInPast(const Session& session, SimTime time) {
    if (time < session.engine()->now()) {
        throw std::invalid_argument("Event time cannot be before current simulation time");
    }
}

const Node& requireNode(const Graph& graph, const std::string& name) {
    const Node* node = graph.findByName(name);
    if (node == nullptr) {
        throw TopologyError("Node '" + name + "' not found");
    }
    return *node;
}

NodeId requireHost(const Graph& graph, const std::string& name) {
    const Node& node = requireNode(graph, name);
    if (node.type() != NodeType::HOST) {
        throw TopologyError("Node '" + name + "' is not a host");
    }
    return node.getId();
}

Router& requireRouter(Graph& graph, const std::string& name) {
    Router* router = dynamic_cast<Router*>(graph.getNode(requireNode(graph, name).getId()));
    if (router == nullptr) {
        throw TopologyError("Node '" + name + "' is not a router");
    }
    return *router;
}

const Link& requireLink(const Graph& graph, const std::string& firstName,
                        const std::string& secondName) {
    const Node& first = requireNode(graph, firstName);
    const Node& second = requireNode(graph, secondName);
    const Link* link = graph.linkBetween(first.getId(), second.getId());
    if (link == nullptr) {
        throw TopologyError("No link connects '" + firstName + "' and '" + secondName + "'");
    }
    return *link;
}

std::string nodeName(const Graph& graph, NodeId id) {
    const Node* node = graph.getNode(id);
    return node == nullptr ? std::to_string(id) : node->name();
}

void finishMetrics(Session& session) {
    session.ensureEngine();
    if (session.metrics() == nullptr) {
        throw std::logic_error("Metrics observer is unavailable");
    }
    session.metrics()->finalize(session.engine()->now());
}

class PingRequestEvent final : public NetworkEvent {
public:
    explicit PingRequestEvent(Packet packet) : packet_(std::move(packet)) {}

    void apply(SimulationEngine& engine) override { engine.originatePacket(std::move(packet_)); }

    std::string describe() const override { return "CLI_PING_REQUEST"; }

private:
    Packet packet_;
};

std::shared_ptr<PingRequestEvent> makeEchoRequest(PacketId id, NodeId source,
                                                  NodeId destination) {
    Packet request;
    request.id = id;
    request.srcId = source;
    request.dstId = destination;
    request.sizeBytes = 64;
    request.type = PacketType::ECHO_REQUEST;
    return std::make_shared<PingRequestEvent>(std::move(request));
}

class PingReplyObserver final : public SimObserver {
public:
    PingReplyObserver(NodeId source, NodeId destination, SimTime firstSendTime,
                      std::vector<std::optional<double>>& rttsMs)
        : source_(source), destination_(destination), firstSendTime_(firstSendTime),
          rttsMs_(rttsMs) {}

    void onDelivered(SimTime time, const Packet& packet) override {
        if (packet.type != PacketType::ECHO_REPLY || packet.srcId != destination_ ||
            packet.dstId != source_) {
            return;
        }
        const double offset = packet.originTime - firstSendTime_;
        const auto requestIndex = static_cast<long long>(std::llround(offset));
        if (requestIndex < 0 || static_cast<std::size_t>(requestIndex) >= rttsMs_.size() ||
            std::abs(offset - static_cast<double>(requestIndex)) > 1e-9) {
            return;
        }
        rttsMs_[static_cast<std::size_t>(requestIndex)] =
            std::max(0.0, (time - packet.originTime) * 1000.0);
    }

private:
    NodeId source_;
    NodeId destination_;
    SimTime firstSendTime_;
    std::vector<std::optional<double>>& rttsMs_;
};

struct HopArrival {
    NodeId nodeId{};
    SimTime time{};
};

class TracerouteObserver final : public SimObserver {
public:
    TracerouteObserver(PacketId probeId, NodeId destination, std::vector<HopArrival>& arrivals,
                       bool& reachedDestination)
        : probeId_(probeId), destination_(destination), arrivals_(arrivals),
          reachedDestination_(reachedDestination) {}

    void onArrive(SimTime time, const Packet& packet, NodeId node, NodeId) override {
        if (packet.id != probeId_ || packet.type != PacketType::ECHO_REQUEST) {
            return;
        }
        arrivals_.push_back({node, time});
        if (node == destination_) {
            reachedDestination_ = true;
        }
    }

private:
    PacketId probeId_;
    NodeId destination_;
    std::vector<HopArrival>& arrivals_;
    bool& reachedDestination_;
};

template <typename DonePredicate>
void runUntilDeadline(SimulationEngine& engine, SimTime deadline, DonePredicate isDone) {
    while (!isDone()) {
        const auto nextTime = engine.nextEventTime();
        if (!nextTime || *nextTime > deadline) {
            break;
        }
        engine.step();
    }
    if (!isDone() && engine.now() < deadline) {
        engine.runUntil(deadline);
    }
}

void showRoute(Session& session, const Args& args, std::ostream& out) {
    requireArgCount(args, {1}, "show route <router>");
    session.ensureEngine();
    Router& router = requireRouter(session.engine()->mutableGraph(), args[0]);
    const Graph& graph = session.engine()->graph();

    out << "Destination  Next hop       Cost       Source\n";
    for (const auto& [destination, entry] : router.forwardingTable().toSortedVector()) {
        const char sourceLetter = entry.source == ForwardingTable::Source::STATIC ? 'S' : 'D';
        out << std::left << std::setw(13) << nodeName(graph, destination)
            << std::setw(15) << nodeName(graph, entry.nextHop)
            << std::right << std::fixed << std::setprecision(4)
            << std::setw(11) << entry.cost << "       " << sourceLetter << '\n';
    }
}

std::pair<std::string, std::string> parseLinkEndpoints(const Args& args) {
    if (args.size() == 2) {
        return {args[0], args[1]};
    }
    const std::string& text = args[0];
    const std::size_t dash = text.find('-');
    if (dash == std::string::npos || dash == 0 || dash + 1 == text.size()) {
        throw std::invalid_argument("Link must be written as A-B");
    }
    return {text.substr(0, dash), text.substr(dash + 1)};
}

void showQueue(Session& session, const Args& args, std::ostream& out) {
    requireArgCount(args, {1, 2}, "show queue <A>-<B> (or show queue <A> <B>)");
    const auto [firstName, secondName] = parseLinkEndpoints(args);
    session.ensureEngine();
    const Graph& graph = session.engine()->graph();
    const Link& link = requireLink(graph, firstName, secondName);
    finishMetrics(session);

    out << "Direction     Queued  Max seen  Queue drops  Loss drops  Down drops\n";
    for (Direction direction : {Direction::AtoB, Direction::BtoA}) {
        NodeId from = link.a();
        NodeId to = link.b();
        if (direction == Direction::BtoA) {
            std::swap(from, to);
        }

        std::size_t queued = 0;
        for (const ChannelSnapshot& snapshot : session.engine()->channelSnapshots(link.getId())) {
            if (snapshot.id.dir == direction) {
                queued = snapshot.queuedPackets;
            }
        }
        ChannelMetrics counters{};
        for (const ChannelMetrics& metric : session.metrics()->channels()) {
            if (metric.channel.linkId == link.getId() && metric.channel.dir == direction) {
                counters = metric;
            }
        }

        out << std::left << std::setw(14) << (nodeName(graph, from) + ">" + nodeName(graph, to))
            << std::right << std::setw(7) << queued << std::setw(9) << counters.maxQueue
            << std::setw(13) << counters.dropsQueue << std::setw(12) << counters.dropsLoss
            << std::setw(12) << counters.dropsDown << '\n';
    }
}

void showMetrics(Session& session, const Args& args, std::ostream& out) {
    requireArgCount(args, {0, 1}, "show metrics [flow]");
    finishMetrics(session);
    const MetricsCollector& metrics = *session.metrics();
    const NetworkMetrics& network = metrics.network();

    out << "Network: reliability=" << std::fixed << std::setprecision(2)
        << network.reliabilityPct << "% throughput=" << network.avgThroughputMbps
        << " Mbps latency=" << network.avgLatencyMs << " ms drops=" << network.totalDrops
        << " events=" << network.events << '\n';

    std::optional<FlowId> selectedFlow;
    if (!args.empty()) {
        selectedFlow = static_cast<FlowId>(parseUnsigned(args[0], "flow ID"));
    }

    out << "Flow  Sent  Delivered  Dropped  Loss%  Avg latency ms  Throughput Mbps\n";
    for (const FlowMetrics& flow : metrics.flows()) {
        if (selectedFlow && flow.flowId != *selectedFlow) {
            continue;
        }
        out << std::setw(4) << flow.flowId << std::setw(6) << flow.sent
            << std::setw(11) << flow.delivered << std::setw(9) << flow.dropped
            << std::setw(7) << std::setprecision(2) << flow.lossPct
            << std::setw(16) << flow.avgLatencyMs << std::setw(18) << flow.throughputMbps << '\n';
    }

    if (!selectedFlow) {
        out << "Most utilized channels:\n";
        for (const ReportItem& item : Report(metrics).topLinksByUtilization(3)) {
            out << "  " << item.name << " " << item.metric << "%\n";
        }
    }
}

void recomputeRoutesIfRunning(Session& session, const std::string& algorithm,
                              const std::string& weight) {
    if (!session.engine()) {
        return;
    }
    RoutingManager& routing = session.engine()->routingManager();
    routing.setAlgorithm(algorithm);
    routing.setPolicy(weight);
    (void)routing.recomputeAll(session.engine()->mutableGraph());
}

void setAlgorithm(Session& session, const Args& args, std::ostream& out) {
    requireArgCount(args, {1}, "set algo <bfs|dfs|dijkstra|bellman-ford>");
    session.scenarioManager().setRoutingAlgorithm(args[0]);
    recomputeRoutesIfRunning(session, args[0], session.scenarioManager().routingWeight());
    out << "Routing algorithm set to " << args[0] << '\n';
}

void setWeight(Session& session, const Args& args, std::ostream& out) {
    requireArgCount(args, {1}, "set weight <hops|latency>");
    session.scenarioManager().setRoutingWeight(args[0]);
    recomputeRoutesIfRunning(session, session.scenarioManager().routingAlgorithm(), args[0]);
    out << "Routing weight set to " << args[0] << '\n';
}

void setSeed(Session& session, const Args& args, std::ostream& out) {
    requireArgCount(args, {1}, "set seed <n>");
    const std::uint64_t seed = parseUnsigned(args[0], "seed");
    session.scenarioManager().setSeed(seed);
    session.reset();
    out << "Seed set to " << seed << "; run reset\n";
}

void addStaticRoute(Session& session, const Args& args, std::ostream& out) {
    const std::string usage = "route add <router> <dst> via <next>";
    requireArgCount(args, {4}, usage);
    requireWord(args, 2, "via", "Usage: " + usage);

    session.ensureEngine();
    Graph& graph = session.engine()->mutableGraph();
    Router& router = requireRouter(graph, args[0]);
    const Node& destination = requireNode(graph, args[1]);
    const Node& nextHop = requireNode(graph, args[3]);
    if (graph.linkBetween(router.getId(), nextHop.getId()) == nullptr) {
        throw TopologyError("Next hop '" + args[3] + "' is not directly connected to router '" +
                            args[0] + "'");
    }

    session.engine()->routingManager().addStaticRoute(graph, router.getId(), destination.getId(),
                                                      nextHop.getId());
    out << "Static route added on " << args[0] << ": " << args[1] << " via " << args[3] << '\n';
}

void addFlow(Session& session, const Args& args, std::ostream& out) {
    requireArgCount(args, {4, 6, 7}, "add flow <src> <dst> <mbps> <bytes> [start stop] [bursty]");
    session.ensureEngine();
    SimulationEngine& engine = *session.engine();
    const Graph& graph = engine.graph();

    const NodeId source = requireHost(graph, args[0]);
    const NodeId destination = requireHost(graph, args[1]);
    const double rateMbps = parseNumber(args[2], "rate");
    const std::uint64_t packetBytes = parseUnsigned(args[3], "packet size");
    if (packetBytes == 0 || packetBytes > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("Packet size must be in 1..4294967295 bytes");
    }

    SimTime start = engine.now();
    SimTime stop = start + 10.0;
    if (args.size() >= 6) {
        start = parseNumber(args[4], "start time");
        stop = parseNumber(args[5], "stop time");
    }
    if (start < engine.now() || stop <= start) {
        throw std::invalid_argument("Flow times must satisfy now <= start < stop");
    }

    const bool bursty = args.size() == 7;
    if (bursty) {
        requireWord(args, 6, "bursty", "The optional traffic pattern must be 'bursty'");
    }

    const auto bytes = static_cast<std::uint32_t>(packetBytes);
    std::unique_ptr<TrafficPattern> pattern;
    if (bursty) {
        pattern = std::make_unique<BurstyPattern>(0.5, 0.5, rateMbps, bytes);
    } else {
        pattern = std::make_unique<ConstantRate>(rateMbps, bytes);
    }

    const FlowId id = engine.allocateFlowId();
    engine.addFlow(Flow(id, source, destination, start, stop, std::move(pattern)));
    out << "Flow " << id << " added (" << args[0] << " -> " << args[1] << ", " << rateMbps
        << " Mbps, " << bytes << " bytes)\n";
}

void ping(Session& session, const Args& args, std::ostream& out) {
    requireArgCount(args, {2, 3}, "ping <A> <B> [count]");
    session.ensureEngine();
    SimulationEngine& engine = *session.engine();
    const NodeId source = requireHost(engine.graph(), args[0]);
    const NodeId destination = requireHost(engine.graph(), args[1]);
    const std::uint64_t count =
        parseCount(args, 2, 4, "count", 1000, "Ping count must be between 1 and 1000");

    const SimTime firstSendTime = engine.now();
    std::vector<std::optional<double>> rttsMs(static_cast<std::size_t>(count));
    PingReplyObserver observer(source, destination, firstSendTime, rttsMs);
    engine.addObserver(observer);
    for (std::uint64_t i = 0; i < count; ++i) {
        engine.scheduleEvent(firstSendTime + static_cast<double>(i),
                             makeEchoRequest(engine.allocatePacketId(), source, destination));
    }

    const SimTime deadline = firstSendTime + static_cast<double>(count - 1) + 2.0;
    runUntilDeadline(engine, deadline, [&rttsMs]() {
        return std::all_of(rttsMs.begin(), rttsMs.end(),
                           [](const std::optional<double>& rtt) { return rtt.has_value(); });
    });
    engine.removeObserver(observer);

    std::size_t received = 0;
    double sumMs = 0.0;
    double minimumMs = std::numeric_limits<double>::infinity();
    double maximumMs = 0.0;
    for (const std::optional<double>& rtt : rttsMs) {
        if (!rtt) {
            out << "Request timed out\n";
            continue;
        }
        ++received;
        sumMs += *rtt;
        minimumMs = std::min(minimumMs, *rtt);
        maximumMs = std::max(maximumMs, *rtt);
        out << "Reply from " << args[1] << ": time=" << std::fixed << std::setprecision(3)
            << *rtt << " ms\n";
    }

    const double lossPct =
        100.0 * static_cast<double>(count - received) / static_cast<double>(count);
    out << "Ping statistics: sent=" << count << ", received=" << received
        << ", loss=" << std::fixed << std::setprecision(1) << lossPct << "%";
    if (received > 0) {
        out << ", min/avg/max=" << std::fixed << std::setprecision(3) << minimumMs << "/"
            << sumMs / static_cast<double>(received) << "/" << maximumMs << " ms";
    }
    out << '\n';
}

void traceroute(Session& session, const Args& args, std::ostream& out) {
    requireArgCount(args, {2}, "traceroute <A> <B>");
    session.ensureEngine();
    SimulationEngine& engine = *session.engine();
    const Graph& graph = engine.graph();
    const NodeId source = requireHost(graph, args[0]);
    const NodeId destination = requireHost(graph, args[1]);

    const SimTime start = engine.now();
    const PacketId probeId = engine.allocatePacketId();
    std::vector<HopArrival> arrivals;
    bool reachedDestination = false;
    TracerouteObserver observer(probeId, destination, arrivals, reachedDestination);
    engine.addObserver(observer);
    engine.scheduleEvent(start, makeEchoRequest(probeId, source, destination));
    runUntilDeadline(engine, start + 2.0, [&reachedDestination]() { return reachedDestination; });
    engine.removeObserver(observer);

    out << "Tracing route from " << args[0] << " to " << args[1] << '\n';
    std::size_t hopNumber = 1;
    for (const HopArrival& arrival : arrivals) {
        out << std::setw(2) << hopNumber << "  " << nodeName(graph, arrival.nodeId) << "  "
            << std::fixed << std::setprecision(3) << (arrival.time - start) * 1000.0 << " ms\n";
        ++hopNumber;
    }
    if (!reachedDestination) {
        out << std::setw(2) << hopNumber << "  Request timed out\n";
    }
}

void scheduleLinkEvent(Session& session, const Args& args, std::ostream& out, bool restore) {
    requireArgCount(args, {4}, std::string(restore ? "restore" : "fail") + " link <A> <B> at <t>");
    requireWord(args, 2, "at", "Expected 'at <time>'");

    session.ensureEngine();
    const Link& link = requireLink(session.engine()->graph(), args[0], args[1]);
    const SimTime time = parseNumber(args[3], "event time");
    requireNotInPast(session, time);

    std::shared_ptr<NetworkEvent> event;
    if (restore) {
        event = std::make_shared<LinkRestoreEvent>(link.getId());
    } else {
        event = std::make_shared<LinkFailureEvent>(link.getId());
    }
    session.engine()->scheduleEvent(time, std::move(event));
    out << (restore ? "Link restore" : "Link failure") << " scheduled for " << args[0] << "-"
        << args[1] << " at " << time << " s\n";
}

void scheduleRouterEvent(Session& session, const Args& args, std::ostream& out, bool restore) {
    requireArgCount(args, {3}, std::string(restore ? "restore" : "fail") + " router <R> at <t>");
    requireWord(args, 1, "at", "Expected 'at <time>'");

    session.ensureEngine();
    const Node& node = requireNode(session.engine()->graph(), args[0]);
    if (node.type() != NodeType::ROUTER) {
        throw TopologyError("Node '" + args[0] + "' is not a router");
    }
    const SimTime time = parseNumber(args[2], "event time");
    requireNotInPast(session, time);

    std::shared_ptr<NetworkEvent> event;
    if (restore) {
        event = std::make_shared<RouterRestoreEvent>(node.getId());
    } else {
        event = std::make_shared<RouterFailureEvent>(node.getId());
    }
    session.engine()->scheduleEvent(time, std::move(event));
    out << (restore ? "Router restore" : "Router failure") << " scheduled for " << args[0]
        << " at " << time << " s\n";
}

void congestLink(Session& session, const Args& args, std::ostream& out) {
    const std::string usage = "congest link <A> <B> factor <f> at <t> for <d>";
    requireArgCount(args, {8}, usage);
    requireWord(args, 2, "factor", "Usage: " + usage);
    requireWord(args, 4, "at", "Usage: " + usage);
    requireWord(args, 6, "for", "Usage: " + usage);

    session.ensureEngine();
    const Link& link = requireLink(session.engine()->graph(), args[0], args[1]);
    const double factor = parseNumber(args[3], "bandwidth factor");
    const SimTime time = parseNumber(args[5], "event time");
    const SimTime duration = parseNumber(args[7], "duration");
    requireNotInPast(session, time);

    session.engine()->scheduleEvent(
        time, std::make_shared<CongestionEvent>(link.getId(), factor, 0.0, duration));
    out << "Congestion scheduled for " << args[0] << "-" << args[1] << " at " << time
        << " s (bandwidth factor " << factor << ", duration " << duration << " s)\n";
}

void scheduleBurst(Session& session, const Args& args, std::ostream& out) {
    const std::string usage = "burst <src> <dst> <mbps> at <t> for <d>";
    requireArgCount(args, {7}, usage);
    requireWord(args, 3, "at", "Usage: " + usage);
    requireWord(args, 5, "for", "Usage: " + usage);

    session.ensureEngine();
    const Graph& graph = session.engine()->graph();
    const NodeId source = requireHost(graph, args[0]);
    const NodeId destination = requireHost(graph, args[1]);
    const double rateMbps = parseNumber(args[2], "rate");
    const SimTime time = parseNumber(args[4], "event time");
    const SimTime duration = parseNumber(args[6], "duration");
    requireNotInPast(session, time);
    if (rateMbps <= 0.0 || duration <= 0.0) {
        throw std::invalid_argument("Rate and duration must be positive");
    }

    const FlowId id = session.engine()->allocateFlowId();
    session.engine()->scheduleEvent(time, std::make_shared<TrafficBurstEvent>(
                                              id, source, destination, duration, rateMbps, 1000));
    out << "Burst scheduled for " << args[0] << " -> " << args[1] << " at " << time << " s for "
        << duration << " s\n";
}

void runFor(Session& session, const Args& args, std::ostream& out) {
    requireArgCount(args, {1}, "run <seconds>");
    const SimTime duration = parseNumber(args[0], "duration");
    if (duration < 0.0) {
        throw std::invalid_argument("Run duration must be non-negative");
    }

    session.ensureEngine();
    const SimTime end = session.engine()->now() + duration;
    if (session.scenarioManager().hasScenario() &&
        end > session.scenarioManager().durationSeconds()) {
        throw ConfigError("Run would exceed the scenario duration");
    }
    session.engine()->runUntil(end);
    out << "Simulation time: " << std::fixed << std::setprecision(6) << session.engine()->now()
        << " s\n";
}

void stepEvents(Session& session, const Args& args, std::ostream& out) {
    requireArgCount(args, {0, 1}, "step [n]");
    const std::uint64_t count =
        parseCount(args, 0, 1, "step count", 100000, "Step count must be between 1 and 100000");

    session.ensureEngine();
    for (std::uint64_t i = 0; i < count; ++i) {
        if (session.engine()->finished()) {
            out << "No pending events\n";
            break;
        }
        session.clearStepMessages();
        session.engine()->step();
        if (session.stepMessages().empty()) {
            out << "[t=" << std::fixed << std::setprecision(6) << session.engine()->now()
                << "] event processed\n";
        }
        for (const std::string& message : session.stepMessages()) {
            out << message << '\n';
        }
    }
}

struct AlgorithmResult {
    std::size_t hops = 0;
    double avgLatencyMs = 0.0;
    double avgLossPct = 0.0;
    double avgThroughputMbps = 0.0;
    double recoveryTimeS = 0.0;
    std::size_t routersProcessed = 0;
    double computeMicroseconds = 0.0;
};

AlgorithmResult evaluateAlgorithm(Session& session, const std::string& algorithm, NodeId source,
                                  NodeId destination) {
    AlgorithmResult result;
    ScenarioManager clone = session.scenarioManager().cloneWith(algorithm);

    RoutingManager routing;
    routing.setAlgorithm(algorithm);
    routing.setPolicy(session.scenarioManager().routingWeight());
    const auto computeStart = std::chrono::steady_clock::now();
    result.routersProcessed = routing.recomputeAll(clone.mutableGraph()).routersProcessed;
    const auto computeEnd = std::chrono::steady_clock::now();
    result.computeMicroseconds =
        std::chrono::duration<double, std::micro>(computeEnd - computeStart).count();

    const std::vector<NodeId> route = routing.explainRoute(clone.graph(), source, destination);
    if (!route.empty()) {
        result.hops = route.size() - 1;
    }

    MetricsCollector metrics(clone.graph());
    SimulationEngine engine(clone.seed());
    engine.addObserver(metrics);
    clone.run(engine, clone.durationSeconds());
    metrics.finalize(engine.now());

    const std::vector<FlowMetrics>& flows = metrics.flows();
    for (const FlowMetrics& flow : flows) {
        result.avgLatencyMs += flow.avgLatencyMs;
        result.avgLossPct += flow.lossPct;
        result.avgThroughputMbps += flow.throughputMbps;
    }
    if (!flows.empty()) {
        const auto flowCount = static_cast<double>(flows.size());
        result.avgLatencyMs /= flowCount;
        result.avgLossPct /= flowCount;
        result.avgThroughputMbps /= flowCount;
    }
    result.recoveryTimeS = metrics.network().recoveryTimeS;
    return result;
}

void compareAlgorithms(Session& session, const Args& args, std::ostream& out) {
    requireArgCount(args, {2}, "compare <source-host> <destination-host>");
    if (!session.scenarioManager().hasScenario()) {
        throw ConfigError("compare requires a loaded scenario file");
    }

    const Graph& graph =
        session.engine() ? session.engine()->graph() : session.scenarioManager().graph();
    const NodeId source = requireHost(graph, args[0]);
    const NodeId destination = requireHost(graph, args[1]);

    out << std::left << std::setw(16) << "Algorithm" << std::setw(8) << "Hops"
        << std::setw(16) << "Avg latency" << std::setw(12) << "Loss %"
        << std::setw(15) << "Throughput" << std::setw(15) << "Recovery(s)"
        << std::setw(18) << "Route computes" << "Compute us\n";
    for (const std::string algorithm : {"bfs", "dfs", "dijkstra", "bellman-ford"}) {
        const AlgorithmResult result = evaluateAlgorithm(session, algorithm, source, destination);
        out << std::left << std::setw(16) << algorithm << std::setw(8) << result.hops
            << std::fixed << std::setprecision(3) << std::setw(16) << result.avgLatencyMs << " ms"
            << std::setprecision(2) << std::setw(12) << result.avgLossPct
            << std::setprecision(3) << std::setw(15) << result.avgThroughputMbps << " Mbps"
            << std::setw(15) << result.recoveryTimeS << std::setw(18) << result.routersProcessed
            << std::setprecision(2) << result.computeMicroseconds << '\n';
    }
}

std::string nextFreeRunId(const std::filesystem::path& directory) {
    for (unsigned index = 1;; ++index) {
        std::ostringstream name;
        name << "run_" << std::setw(3) << std::setfill('0') << index;
        if (!std::filesystem::exists(directory / (name.str() + ".json"))) {
            return name.str();
        }
    }
}

void exportRun(Session& session, const Args& args, std::ostream& out) {
    requireArgCount(args, {0, 1}, "export [dir]");
    if (!args.empty()) {
        session.setOutputDirectory(args[0]);
    }
    finishMetrics(session);
    session.traceLogger()->flush();

    const std::filesystem::path directory = session.outputDirectory();
    std::filesystem::create_directories(directory);
    const std::string runId = nextFreeRunId(directory);

    const nlohmann::json manifest{
        {"seed", session.scenarioManager().seed()},
        {"algorithm", session.engine()->routingManager().algorithmName()},
        {"weight", session.engine()->routingManager().policyName()},
        {"topology", session.scenarioManager().topologyPath()},
        {"simulation_time_s", session.engine()->now()}};
    Exporters::exportAll(directory, runId, *session.metrics(), manifest.dump(2));
    std::filesystem::copy_file(session.tracePath(), directory / (runId + "_trace.csv"),
                               std::filesystem::copy_options::overwrite_existing);
    out << "Exported run files to " << directory.string() << " (" << runId << ")\n";
}

void resetRun(Session& session, const Args& args, std::ostream& out) {
    requireArgCount(args, {0}, "reset");
    session.reset();
    out << "Run reset; loaded topology/scenario retained\n";
}

void registerCommand(CommandLineInterface& cli, std::string name, std::string help,
                     FunctionCommand::Handler handler) {
    cli.addCommand(
        std::make_unique<FunctionCommand>(std::move(name), std::move(help), std::move(handler)));
}

}

void registerSimulationCommands(CommandLineInterface& cli) {
    registerCommand(cli, "show route", "Show a router forwarding table", showRoute);
    registerCommand(cli, "show queue", "Show directional queues for a link", showQueue);
    registerCommand(cli, "show metrics", "Show run aggregates, optionally for one flow",
                    showMetrics);
    registerCommand(cli, "set algo", "Select bfs, dfs, dijkstra, or bellman-ford", setAlgorithm);
    registerCommand(cli, "set weight", "Select hops or latency routing weights", setWeight);
    registerCommand(cli, "set seed", "Set the random seed and reset the run", setSeed);
    registerCommand(cli, "route add", "Install a static route on a router", addStaticRoute);
    registerCommand(cli, "add flow", "Add a constant or bursty traffic flow", addFlow);
    registerCommand(cli, "ping", "Send echo requests between two hosts", ping);
    registerCommand(cli, "traceroute", "Trace a simulated packet's route", traceroute);
    registerCommand(cli, "fail link", "Schedule link failure",
                    [](Session& session, const Args& args, std::ostream& out) {
                        scheduleLinkEvent(session, args, out, false);
                    });
    registerCommand(cli, "restore link", "Schedule link restoration",
                    [](Session& session, const Args& args, std::ostream& out) {
                        scheduleLinkEvent(session, args, out, true);
                    });
    registerCommand(cli, "fail router", "Schedule router failure",
                    [](Session& session, const Args& args, std::ostream& out) {
                        scheduleRouterEvent(session, args, out, false);
                    });
    registerCommand(cli, "restore router", "Schedule router restoration",
                    [](Session& session, const Args& args, std::ostream& out) {
                        scheduleRouterEvent(session, args, out, true);
                    });
    registerCommand(cli, "congest link", "Schedule temporary link degradation", congestLink);
    registerCommand(cli, "burst", "Schedule a temporary burst flow", scheduleBurst);
    registerCommand(cli, "run", "Advance the simulation by a number of seconds", runFor);
    registerCommand(cli, "step", "Execute the next event or the next n events", stepEvents);
    registerCommand(cli, "compare", "Compare routing algorithms on the loaded scenario",
                    compareAlgorithms);
    registerCommand(cli, "export", "Write the trace, metrics, and manifest files", exportRun);
    registerCommand(cli, "reset", "Rebuild the engine and restore the loaded topology/scenario",
                    resetRun);
}

}
