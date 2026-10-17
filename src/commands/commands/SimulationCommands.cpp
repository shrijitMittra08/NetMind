#include "netmind/cli/commands/SimulationCommands.h"

#include "netmind/cli/Command.h"
#include "netmind/cli/CommandLineInterface.h"
#include "netmind/cli/Session.h"
#include "netmind/common/Errors.h"
#include "netmind/common/Units.h"
#include "netmind/metrics/Exporters.h"
#include "netmind/metrics/MetricsCollector.h"
#include "netmind/metrics/Report.h"
#include "netmind/metrics/TraceLogger.h"
#include "netmind/model/Flow.h"
#include "netmind/model/ForwardingTable.h"
#include "netmind/model/Host.h"
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
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
#include <chrono>

namespace netmind::cli {
namespace {

class FunctionCommand final : public Command {
public:
    using Handler = std::function<void(Session&, const std::vector<std::string>&,
                                       std::ostream&)>;
    FunctionCommand(std::string name, std::string help, Handler handler)
        : name_(std::move(name)), help_(std::move(help)),
          handler_(std::move(handler)) {}
    std::string name() const override { return name_; }
    std::string help() const override { return help_; }
    void execute(Session& session, const std::vector<std::string>& args,
                 std::ostream& out) override { handler_(session, args, out); }
private:
    std::string name_;
    std::string help_;
    Handler handler_;
};

void requireCount(const std::vector<std::string>& args,
                  std::initializer_list<std::size_t> counts,
                  const std::string& usage) {
    if (std::find(counts.begin(), counts.end(), args.size()) == counts.end()) {
        throw std::invalid_argument("Usage: " + usage);
    }
}

double number(const std::string& text, const std::string& label) {
    std::size_t used = 0;
    double value{};
    try { value = std::stod(text, &used); }
    catch (...) { throw std::invalid_argument(label + " must be numeric"); }
    if (used != text.size() || !std::isfinite(value))
        throw std::invalid_argument(label + " must be a finite number");
    return value;
}

std::uint64_t integer(const std::string& text, const std::string& label) {
    if (text.empty() || text.front() == '-')
        throw std::invalid_argument(label + " must be a non-negative integer");
    std::size_t used = 0;
    unsigned long long value{};
    try { value = std::stoull(text, &used); }
    catch (...) { throw std::invalid_argument(label + " must be an integer"); }
    if (used != text.size()) throw std::invalid_argument(label + " must be an integer");
    return static_cast<std::uint64_t>(value);
}

const Node& requireNode(const Graph& graph, const std::string& name) {
    const Node* node = graph.findByName(name);
    if (node == nullptr) throw TopologyError("Node '" + name + "' not found");
    return *node;
}

NodeId requireHost(const Graph& graph, const std::string& name) {
    const Node& node = requireNode(graph, name);
    if (node.type() != NodeType::HOST)
        throw TopologyError("Node '" + name + "' is not a host");
    return node.getId();
}

Router& requireRouter(Graph& graph, const std::string& name) {
    const Node& found = requireNode(graph, name);
    Node* node = graph.getNode(found.getId());
    auto* router = dynamic_cast<Router*>(node);
    if (router == nullptr) throw TopologyError("Node '" + name + "' is not a router");
    return *router;
}

const Link& requireLink(const Graph& graph, const std::string& a,
                        const std::string& b) {
    const Node& first = requireNode(graph, a);
    const Node& second = requireNode(graph, b);
    const Link* link = graph.linkBetween(first.getId(), second.getId());
    if (link == nullptr)
        throw TopologyError("No link connects '" + a + "' and '" + b + "'");
    return *link;
}

std::string nodeName(const Graph& graph, NodeId id) {
    const Node* node = graph.getNode(id);
    return node == nullptr ? std::to_string(id) : node->name();
}

void finishMetrics(Session& session) {
    session.ensureEngine();
    if (session.metrics() == nullptr)
        throw std::logic_error("Metrics observer is unavailable");
    session.metrics()->finalize(session.engine()->now());
}

class PingRequestEvent final : public NetworkEvent {
public:
    explicit PingRequestEvent(Packet packet) : packet_(std::move(packet)) {}
    void apply(SimulationEngine& engine) override {
        engine.originatePacket(std::move(packet_));
    }
    std::string describe() const override { return "CLI_PING_REQUEST"; }
private:
    Packet packet_;
};

class PingReplyObserver final : public SimObserver {
public:
    PingReplyObserver(
        NodeId source,
        NodeId destination,
        SimTime firstSendTime,
        std::vector<std::optional<double>>& rtts)
        : source_(source),
          destination_(destination),
          firstSendTime_(firstSendTime),
          rtts_(rtts) {}

    void onDelivered(SimTime time, const Packet& packet) override {
        if (packet.type != PacketType::ECHO_REPLY ||
            packet.srcId != destination_ ||
            packet.dstId != source_) {
            return;
        }

        const double offset = packet.originTime - firstSendTime_;
        const auto requestIndex = static_cast<long long>(std::llround(offset));

        if (requestIndex < 0 ||
            static_cast<std::size_t>(requestIndex) >= rtts_.size() ||
            std::abs(offset - static_cast<double>(requestIndex)) > 1e-9) {
            return;
        }

        rtts_[static_cast<std::size_t>(requestIndex)] =
            std::max(0.0, (time - packet.originTime) * 1000.0);
    }

private:
    NodeId source_;
    NodeId destination_;
    SimTime firstSendTime_;
    std::vector<std::optional<double>>& rtts_;
};

class TracerouteObserver final : public SimObserver {
public:
    struct HopArrival {
        NodeId nodeId{};
        SimTime time{};
    };

    TracerouteObserver(
        PacketId probeId,
        NodeId destination,
        std::vector<HopArrival>& arrivals,
        bool& reachedDestination)
        : probeId_(probeId),
          destination_(destination),
          arrivals_(arrivals),
          reachedDestination_(reachedDestination) {}

    void onArrive(
        SimTime time,
        const Packet& packet,
        NodeId node,
        NodeId) override {
        if (packet.id != probeId_ ||
            packet.type != PacketType::ECHO_REQUEST) {
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
void runUntilDeadline(
    SimulationEngine& engine,
    SimTime deadline,
    DonePredicate isDone) {
    while (!isDone()) {
        const auto next = engine.nextEventTime();
        if (!next || *next > deadline) {
            break;
        }
        engine.step();
    }

    if (!isDone() && engine.now() < deadline) {
        engine.runUntil(deadline);
    }
}

void registerCommand(CommandLineInterface& cli, std::string name,
                     std::string help, FunctionCommand::Handler handler) {
    cli.addCommand(std::make_unique<FunctionCommand>(
        std::move(name), std::move(help), std::move(handler)));
}

} // namespace

void registerSimulationCommands(CommandLineInterface& cli) {
    registerCommand(cli, "show route", "Show a router forwarding table",
        [](Session& s, const auto& a, auto& out) {
            requireCount(a, {1}, "show route <router>");
            s.ensureEngine();
            auto& router = requireRouter(s.engine()->mutableGraph(), a[0]);
            out << "Destination  Next hop       Cost       Source\n";
            for (const auto& item : router.forwardingTable().toSortedVector()) {
                out << std::left << std::setw(13)
                    << nodeName(s.engine()->graph(), item.first)
                    << std::setw(15)
                    << nodeName(s.engine()->graph(), item.second.nextHop)
                    << std::right << std::fixed << std::setprecision(4)
                    << std::setw(11) << item.second.cost << "       "
                    << (item.second.source == ForwardingTable::Source::STATIC ? 'S' : 'D')
                    << '\n';
            }
        });

    registerCommand(cli, "show queue", "Show directional queues for a link",
        [](Session& s, const auto& a, auto& out) {
            requireCount(a, {1, 2}, "show queue <A>-<B> (or show queue <A> <B>)");
            std::string first, second;
            if (a.size() == 2) { first = a[0]; second = a[1]; }
            else {
                const auto split = a[0].find('-');
                if (split == std::string::npos || split == 0 || split + 1 == a[0].size())
                    throw std::invalid_argument("Link must be written as A-B");
                first = a[0].substr(0, split); second = a[0].substr(split + 1);
            }
            s.ensureEngine();
            const Graph& graph = s.engine()->graph();
            const Link& link = requireLink(graph, first, second);
            finishMetrics(s);
            out << "Direction     Queued  Max seen  Queue drops  Loss drops  Down drops\n";
            for (Direction direction : {Direction::AtoB, Direction::BtoA}) {
                const NodeId from = direction == Direction::AtoB ? link.a() : link.b();
                const NodeId to = direction == Direction::AtoB ? link.b() : link.a();
                const ChannelId id{link.getId(), direction};
                std::size_t queued = 0;
                for (const auto& snapshot : s.engine()->channelSnapshots(link.getId()))
                    if (snapshot.id.dir == direction) queued = snapshot.queuedPackets;
                std::size_t maxSeen = 0, qdrop = 0, ldrop = 0, ddrop = 0;
                for (const auto& metric : s.metrics()->channels()) if (
                    metric.channel.linkId == id.linkId && metric.channel.dir == id.dir) {
                    maxSeen = metric.maxQueue; qdrop = metric.dropsQueue;
                    ldrop = metric.dropsLoss; ddrop = metric.dropsDown;
                }
                out << std::left << std::setw(14)
                    << (nodeName(graph, from) + ">" + nodeName(graph, to))
                    << std::right << std::setw(7) << queued
                    << std::setw(9) << maxSeen
                    << std::setw(13) << qdrop << std::setw(12) << ldrop
                    << std::setw(12) << ddrop << '\n';
            }
        });

    registerCommand(cli, "show metrics", "Show run aggregates, optionally for one flow",
        [](Session& s, const auto& a, auto& out) {
            requireCount(a, {0, 1}, "show metrics [flow]");
            finishMetrics(s);
            const auto& metrics = *s.metrics();
            out << "Network: reliability=" << std::fixed << std::setprecision(2)
                << metrics.network().reliabilityPct << "% throughput="
                << metrics.network().avgThroughputMbps << " Mbps latency="
                << metrics.network().avgLatencyMs << " ms drops="
                << metrics.network().totalDrops << " events="
                << metrics.network().events << '\n';
            const std::optional<FlowId> selected = a.empty() ? std::nullopt
                : std::optional<FlowId>(static_cast<FlowId>(integer(a[0], "flow ID")));
            out << "Flow  Sent  Delivered  Dropped  Loss%  Avg latency ms  Throughput Mbps\n";
            for (const auto& flow : metrics.flows()) {
                if (selected && flow.flowId != *selected) continue;
                out << std::setw(4) << flow.flowId << std::setw(6) << flow.sent
                    << std::setw(11) << flow.delivered << std::setw(9) << flow.dropped
                    << std::setw(7) << std::setprecision(2) << flow.lossPct
                    << std::setw(16) << flow.avgLatencyMs
                    << std::setw(18) << flow.throughputMbps << '\n';
            }
            if (!selected) {
                Report report(metrics);
                out << "Most utilized channels:\n";
                for (const auto& item : report.topLinksByUtilization(3))
                    out << "  " << item.name << " " << item.metric << "%\n";
            }
        });

    registerCommand(cli, "set algo", "Select bfs, dfs, dijkstra, or bellman-ford",
        [](Session& s, const auto& a, auto& out) {
            requireCount(a, {1}, "set algo <bfs|dfs|dijkstra|bellman-ford>");
            s.scenarioManager().setRoutingAlgorithm(a[0]);
            if (s.engine()) {
                auto& rm = s.engine()->routingManager();
                rm.setAlgorithm(a[0]); rm.setPolicy(s.scenarioManager().routingWeight());
                (void)rm.recomputeAll(s.engine()->mutableGraph());
            }
            out << "Routing algorithm set to " << a[0] << '\n';
        });

    registerCommand(cli, "set weight", "Select hops or latency routing weights",
        [](Session& s, const auto& a, auto& out) {
            requireCount(a, {1}, "set weight <hops|latency>");
            s.scenarioManager().setRoutingWeight(a[0]);
            if (s.engine()) {
                auto& rm = s.engine()->routingManager();
                rm.setAlgorithm(s.scenarioManager().routingAlgorithm());
                rm.setPolicy(a[0]); (void)rm.recomputeAll(s.engine()->mutableGraph());
            }
            out << "Routing weight set to " << a[0] << '\n';
        });

    registerCommand(cli, "set seed", "Set the random seed and reset the run",
        [](Session& s, const auto& a, auto& out) {
            requireCount(a, {1}, "set seed <n>");
            const auto seed = integer(a[0], "seed");
            s.scenarioManager().setSeed(seed);
            s.reset();
            out << "Seed set to " << seed << "; run reset\n";
        });

    registerCommand(cli, "route add", "Install a static route on a router",
        [](Session& s, const auto& a, auto& out) {
            requireCount(a, {4}, "route add <router> <dst> via <next>");
            if (a[2] != "via") throw std::invalid_argument("Usage: route add <router> <dst> via <next>");
            s.ensureEngine(); Graph& graph = s.engine()->mutableGraph();
            Router& router = requireRouter(graph, a[0]);
            const Node& dst = requireNode(graph, a[1]);
            const Node& next = requireNode(graph, a[3]);
            if (graph.linkBetween(router.getId(), next.getId()) == nullptr)
                throw TopologyError("Next hop '" + a[3] + "' is not directly connected to router '" + a[0] + "'");
            s.engine()->routingManager().addStaticRoute(
                graph, router.getId(), dst.getId(), next.getId());
            out << "Static route added on " << a[0] << ": " << a[1]
                << " via " << a[3] << '\n';
        });

    registerCommand(cli, "add flow", "Add a constant or bursty traffic flow",
        [](Session& s, const auto& a, auto& out) {
            requireCount(a, {4, 6, 7}, "add flow <src> <dst> <mbps> <bytes> [start stop] [bursty]");
            s.ensureEngine();
            const Graph& graph = s.engine()->graph();
            const NodeId src = requireHost(graph, a[0]);
            const NodeId dst = requireHost(graph, a[1]);
            const double rate = number(a[2], "rate");
            const auto bytes64 = integer(a[3], "packet size");
            if (bytes64 == 0 || bytes64 > std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("Packet size must be in 1..4294967295 bytes");
            const SimTime start = a.size() >= 6 ? number(a[4], "start time") : s.engine()->now();
            const SimTime stop = a.size() >= 6 ? number(a[5], "stop time") : start + 10.0;
            if (start < s.engine()->now() || stop <= start)
                throw std::invalid_argument("Flow times must satisfy now <= start < stop");
            if (a.size() == 7 && a[6] != "bursty")
                throw std::invalid_argument("The optional traffic pattern must be 'bursty'");
            std::unique_ptr<TrafficPattern> pattern;
            if (a.size() == 7)
                pattern = std::make_unique<BurstyPattern>(0.5, 0.5, rate, static_cast<std::uint32_t>(bytes64));
            else
                pattern = std::make_unique<ConstantRate>(rate, static_cast<std::uint32_t>(bytes64));
            const FlowId id = s.engine()->allocateFlowId();
            s.engine()->addFlow(Flow(id, src, dst, start, stop, std::move(pattern)));
            out << "Flow " << id << " added (" << a[0] << " -> " << a[1]
                << ", " << rate << " Mbps, " << bytes64 << " bytes)\n";
        });

    registerCommand(cli, "ping", "Send echo requests between two hosts",
    [](Session& s, const auto& a, auto& out) {
        requireCount(a, {2, 3}, "ping <A> <B> [count]");
        s.ensureEngine();

        const Graph& graph = s.engine()->graph();
        const NodeId src = requireHost(graph, a[0]);
        const NodeId dst = requireHost(graph, a[1]);

        const std::uint64_t count =
            a.size() == 3 ? integer(a[2], "count") : 4;
        if (count == 0 || count > 1000) {
            throw std::invalid_argument(
                "Ping count must be between 1 and 1000");
        }

        SimulationEngine& engine = *s.engine();
        const SimTime firstSendTime = engine.now();
        std::vector<std::optional<double>> rtts(
            static_cast<std::size_t>(count));

        PingReplyObserver observer(
            src, dst, firstSendTime, rtts);
        engine.addObserver(observer);

        for (std::uint64_t i = 0; i < count; ++i) {
            Packet request;
            request.id = engine.allocatePacketId();
            request.srcId = src;
            request.dstId = dst;
            request.sizeBytes = 64;
            request.type = PacketType::ECHO_REQUEST;

            engine.scheduleEvent(
                firstSendTime + static_cast<double>(i),
                std::make_shared<PingRequestEvent>(std::move(request)));
        }

        // The last request is sent at firstSendTime + count - 1 and gets
        // its own two-second reply window.
        const SimTime deadline =
            firstSendTime + static_cast<double>(count - 1) + 2.0;

        const auto allRepliesReceived = [&rtts]() {
            return std::all_of(
                rtts.begin(),
                rtts.end(),
                [](const auto& rtt) { return rtt.has_value(); });
        };

        runUntilDeadline(engine, deadline, allRepliesReceived);
        engine.removeObserver(observer);

        std::size_t received = 0;
        double sum = 0.0;
        double minimum = std::numeric_limits<double>::infinity();
        double maximum = 0.0;

        for (std::size_t i = 0; i < rtts.size(); ++i) {
            if (!rtts[i]) {
                out << "Request timed out\n";
                continue;
            }

            ++received;
            sum += *rtts[i];
            minimum = std::min(minimum, *rtts[i]);
            maximum = std::max(maximum, *rtts[i]);

            out << "Reply from " << a[1]
                << ": time=" << std::fixed << std::setprecision(3)
                << *rtts[i] << " ms\n";
        }

        const double lossPct =
            100.0 * static_cast<double>(count - received) /
            static_cast<double>(count);

        out << "Ping statistics: sent=" << count
            << ", received=" << received
            << ", loss=" << std::fixed << std::setprecision(1)
            << lossPct << "%";

        if (received > 0) {
            out << ", min/avg/max="
                << std::fixed << std::setprecision(3)
                << minimum << "/"
                << sum / static_cast<double>(received) << "/"
                << maximum << " ms";
        }
        out << '\n';
    });

    registerCommand(cli, "traceroute", "Trace a simulated packet's route",
    [](Session& s, const auto& a, auto& out) {
        requireCount(a, {2}, "traceroute <A> <B>");
        s.ensureEngine();

        SimulationEngine& engine = *s.engine();
        const Graph& graph = engine.graph();
        const NodeId src = requireHost(graph, a[0]);
        const NodeId dst = requireHost(graph, a[1]);

        const SimTime start = engine.now();
        const PacketId probeId = engine.allocatePacketId();
        std::vector<TracerouteObserver::HopArrival> arrivals;
        bool reachedDestination = false;

        TracerouteObserver observer(
            probeId, dst, arrivals, reachedDestination);
        engine.addObserver(observer);

        Packet probe;
        probe.id = probeId;
        probe.srcId = src;
        probe.dstId = dst;
        probe.sizeBytes = 64;
        probe.type = PacketType::ECHO_REQUEST;

        engine.scheduleEvent(
            start,
            std::make_shared<PingRequestEvent>(std::move(probe)));

        runUntilDeadline(
            engine,
            start + 2.0,
            [&reachedDestination]() { return reachedDestination; });

        engine.removeObserver(observer);

        out << "Tracing route from " << a[0] << " to " << a[1] << '\n';

        std::size_t hopNumber = 1;
        for (const auto& arrival : arrivals) {
            out << std::setw(2) << hopNumber++ << "  "
                << nodeName(graph, arrival.nodeId) << "  "
                << std::fixed << std::setprecision(3)
                << (arrival.time - start) * 1000.0 << " ms\n";
        }

        if (!reachedDestination) {
            out << std::setw(2) << hopNumber
                << "  Request timed out\n";
        }
    });

    auto scheduleLinkCommand = [&cli](const std::string& command, bool restore) {
        registerCommand(cli, command, restore ? "Schedule link restoration" : "Schedule link failure",
            [restore](Session& s, const auto& a, auto& out) {
                requireCount(a, {4}, restore ? "restore link <A> <B> at <t>" : "fail link <A> <B> at <t>");
                if (a[2] != "at") throw std::invalid_argument("Expected 'at <time>'");
                s.ensureEngine(); const Link& link = requireLink(s.engine()->graph(), a[0], a[1]);
                const SimTime time = number(a[3], "event time");
                if (time < s.engine()->now()) throw std::invalid_argument("Event time cannot be before current simulation time");
                std::shared_ptr<NetworkEvent> event;
                if (restore) event = std::make_shared<LinkRestoreEvent>(link.getId());
                else event = std::make_shared<LinkFailureEvent>(link.getId());
                s.engine()->scheduleEvent(time, std::move(event));
                out << (restore ? "Link restore" : "Link failure") << " scheduled for "
                    << a[0] << "-" << a[1] << " at " << time << " s\n";
            });
    };
    scheduleLinkCommand("fail link", false);
    scheduleLinkCommand("restore link", true);

    auto scheduleRouterCommand = [&cli](const std::string& command, bool restore) {
        registerCommand(cli, command, restore ? "Schedule router restoration" : "Schedule router failure",
            [restore](Session& s, const auto& a, auto& out) {
                requireCount(a, {3}, restore ? "restore router <R> at <t>" : "fail router <R> at <t>");
                if (a[1] != "at") throw std::invalid_argument("Expected 'at <time>'");
                s.ensureEngine(); const Node& node = requireNode(s.engine()->graph(), a[0]);
                if (node.type() != NodeType::ROUTER) throw TopologyError("Node '" + a[0] + "' is not a router");
                const SimTime time = number(a[2], "event time");
                if (time < s.engine()->now()) throw std::invalid_argument("Event time cannot be before current simulation time");
                std::shared_ptr<NetworkEvent> event;
                if (restore) event = std::make_shared<RouterRestoreEvent>(node.getId());
                else event = std::make_shared<RouterFailureEvent>(node.getId());
                s.engine()->scheduleEvent(time, std::move(event));
                out << (restore ? "Router restore" : "Router failure") << " scheduled for "
                    << a[0] << " at " << time << " s\n";
            });
    };
    scheduleRouterCommand("fail router", false);
    scheduleRouterCommand("restore router", true);

    registerCommand(cli, "congest link", "Schedule temporary link degradation",
        [](Session& s, const auto& a, auto& out) {
            requireCount(a, {8}, "congest link <A> <B> factor <f> at <t> for <d>");
            if (a[2] != "factor" || a[4] != "at" || a[6] != "for")
                throw std::invalid_argument("Usage: congest link <A> <B> factor <f> at <t> for <d>");
            s.ensureEngine(); const Link& link = requireLink(s.engine()->graph(), a[0], a[1]);
            const double factor = number(a[3], "bandwidth factor");
            const SimTime time = number(a[5], "event time");
            const SimTime duration = number(a[7], "duration");
            if (time < s.engine()->now()) throw std::invalid_argument("Event time cannot be before current simulation time");
            s.engine()->scheduleEvent(time, std::make_shared<CongestionEvent>(link.getId(), factor, 0.0, duration));
            out << "Congestion scheduled for " << a[0] << "-" << a[1]
                << " at " << time << " s (bandwidth factor " << factor
                << ", duration " << duration << " s)\n";
        });

    registerCommand(cli, "burst", "Schedule a temporary burst flow",
        [](Session& s, const auto& a, auto& out) {
            requireCount(a, {7}, "burst <src> <dst> <mbps> at <t> for <d>");
            if (a[3] != "at" || a[5] != "for") throw std::invalid_argument("Usage: burst <src> <dst> <mbps> at <t> for <d>");
            s.ensureEngine(); const Graph& graph = s.engine()->graph();
            const NodeId src = requireHost(graph, a[0]); const NodeId dst = requireHost(graph, a[1]);
            const double rate = number(a[2], "rate"); const SimTime time = number(a[4], "event time"); const SimTime duration = number(a[6], "duration");
            if (time < s.engine()->now()) throw std::invalid_argument("Event time cannot be before current simulation time");
            if (rate <= 0.0 || duration <= 0.0) throw std::invalid_argument("Rate and duration must be positive");
            const FlowId id = s.engine()->allocateFlowId();
            s.engine()->scheduleEvent(time, std::make_shared<TrafficBurstEvent>(id, src, dst, duration, rate, 1000));
            out << "Burst scheduled for " << a[0] << " -> " << a[1] << " at " << time << " s for " << duration << " s\n";
        });

    registerCommand(cli, "run", "Advance the simulation by a number of seconds",
        [](Session& s, const auto& a, auto& out) {
            requireCount(a, {1}, "run <seconds>"); const SimTime duration = number(a[0], "duration");
            if (duration < 0.0) throw std::invalid_argument("Run duration must be non-negative");
            s.ensureEngine(); const SimTime end = s.engine()->now() + duration;
            if (s.scenarioManager().hasScenario() && end > s.scenarioManager().durationSeconds())
                throw ConfigError("Run would exceed the scenario duration");
            s.engine()->runUntil(end); out << "Simulation time: " << std::fixed << std::setprecision(6) << s.engine()->now() << " s\n";
        });

    registerCommand(cli, "step", "Execute the next event or the next n events",
        [](Session& s, const auto& a, auto& out) {
            requireCount(a, {0, 1}, "step [n]"); const std::uint64_t count = a.empty() ? 1 : integer(a[0], "step count");
            if (count == 0 || count > 100000) throw std::invalid_argument("Step count must be between 1 and 100000");
            s.ensureEngine();
            for (std::uint64_t i = 0; i < count; ++i) {
                if (s.engine()->finished()) { out << "No pending events\n"; break; }
                s.clearStepMessages(); s.engine()->step();
                if (s.stepMessages().empty()) out << "[t=" << std::fixed << std::setprecision(6) << s.engine()->now() << "] event processed\n";
                else for (const auto& message : s.stepMessages()) out << message << '\n';
            }
        });

        registerCommand(cli, "compare", "Compare routing algorithms on the loaded scenario",
        [](Session& s, const auto& a, auto& out) {
            requireCount(a, {2}, "compare <source-host> <destination-host>");

            if (!s.scenarioManager().hasScenario()) {
                throw ConfigError(
                    "compare requires a loaded scenario file");
            }

            const Graph& active =
                s.engine() ? s.engine()->graph()
                           : s.scenarioManager().graph();

            const NodeId src = requireHost(active, a[0]);
            const NodeId dst = requireHost(active, a[1]);

            const std::vector<std::string> algorithms{
                "bfs",
                "dfs",
                "dijkstra",
                "bellman-ford"
            };

            out << std::left
                << std::setw(16) << "Algorithm"
                << std::setw(8) << "Hops"
                << std::setw(16) << "Avg latency"
                << std::setw(12) << "Loss %"
                << std::setw(15) << "Throughput"
                << std::setw(15) << "Recovery(s)"
                << std::setw(18) << "Route computes"
                << "Compute us\n";

            for (const auto& algorithm : algorithms) {
                auto clone = s.scenarioManager().cloneWith(algorithm);

                RoutingManager router;
                router.setAlgorithm(algorithm);
                router.setPolicy(s.scenarioManager().routingWeight());

                const auto computeStart =
                    std::chrono::steady_clock::now();

                const RoutingChangeSummary routingSummary =
                    router.recomputeAll(clone.mutableGraph());

                const auto computeEnd =
                    std::chrono::steady_clock::now();

                const double computeMicroseconds =
                    std::chrono::duration<double, std::micro>(
                        computeEnd - computeStart)
                        .count();

                const auto route =
                    router.explainRoute(clone.graph(), src, dst);

                const std::size_t hops =
                    route.empty() ? 0 : route.size() - 1;

                MetricsCollector metrics(clone.graph());
                SimulationEngine engine(clone.seed());
                engine.addObserver(metrics);

                const SimTime duration = clone.durationSeconds();
                clone.run(engine, duration);
                metrics.finalize(engine.now());

                double latency = 0.0;
                double loss = 0.0;
                double throughput = 0.0;
                std::size_t flowCount = 0;

                for (const auto& flow : metrics.flows()) {
                    latency += flow.avgLatencyMs;
                    loss += flow.lossPct;
                    throughput += flow.throughputMbps;
                    ++flowCount;
                }

                if (flowCount > 0) {
                    latency /= static_cast<double>(flowCount);
                    loss /= static_cast<double>(flowCount);
                    throughput /= static_cast<double>(flowCount);
                }

                out << std::left
                    << std::setw(16) << algorithm
                    << std::setw(8) << hops
                    << std::fixed << std::setprecision(3)
                    << std::setw(16) << latency << " ms"
                    << std::setprecision(2)
                    << std::setw(12) << loss
                    << std::setprecision(3)
                    << std::setw(15) << throughput << " Mbps"
                    << std::setprecision(3)
                    << std::setw(15)
                    << metrics.network().recoveryTimeS
                    << std::setw(18)
                    << routingSummary.routersProcessed
                    << std::setprecision(2)
                    << computeMicroseconds << '\n';
            }
        });

    registerCommand(cli, "export", "Write the trace, metrics, and manifest files",
        [](Session& s, const auto& a, auto& out) {
            requireCount(a, {0, 1}, "export [dir]");
            if (!a.empty()) s.setOutputDirectory(a[0]);
            finishMetrics(s); s.traceLogger()->flush();
            const auto dir = s.outputDirectory(); std::filesystem::create_directories(dir);
            unsigned index = 1; std::string runId;
            do { std::ostringstream name; name << "run_" << std::setw(3) << std::setfill('0') << index++; runId = name.str(); }
            while (std::filesystem::exists(dir / (runId + ".json")));
            nlohmann::json manifest{
                {"seed", s.scenarioManager().seed()},
                {"algorithm", s.engine()->routingManager().algorithmName()},
                {"weight", s.engine()->routingManager().policyName()},
                {"topology", "loaded topology"},
                {"simulation_time_s", s.engine()->now()}};
            Exporters::exportAll(dir, runId, *s.metrics(), manifest.dump(2));
            const auto traceOut = dir / (runId + "_trace.csv");
            std::filesystem::copy_file(s.tracePath(), traceOut, std::filesystem::copy_options::overwrite_existing);
            out << "Exported run files to " << dir.string() << " (" << runId << ")\n";
        });

    registerCommand(cli, "reset", "Rebuild the engine and restore the loaded topology/scenario",
        [](Session& s, const auto& a, auto& out) {
            requireCount(a, {0}, "reset"); s.reset(); out << "Run reset; loaded topology/scenario retained\n";
        });
}

} // namespace netmind::cli
