#include "netmind/metrics/Report.h"

#include <algorithm>
#include <string>
#include <vector>

#include "netmind/metrics/MetricsCollector.h"

namespace netmind {
namespace {

bool rankedBefore(const ReportItem& left, const ReportItem& right) {
    if (left.metric != right.metric) {
        return left.metric > right.metric;
    }
    return left.name < right.name;
}

std::vector<ReportItem> keepTop(std::vector<ReportItem> items, std::size_t k) {
    const std::size_t count = std::min(k, items.size());
    std::partial_sort(items.begin(), items.begin() + count, items.end(), rankedBefore);
    items.resize(count);
    return items;
}

}

Report::Report(const MetricsCollector& metrics) : metrics_(metrics) {}

std::vector<ReportItem> Report::topLinksByUtilization(std::size_t k) const {
    std::vector<ReportItem> items;
    for (const ChannelMetrics& channel : metrics_.channels()) {
        items.push_back({channel.name, channel.utilizationPct});
    }
    return keepTop(items, k);
}

std::vector<ReportItem> Report::topLinksByDrops(std::size_t k) const {
    std::vector<ReportItem> items;
    for (const ChannelMetrics& channel : metrics_.channels()) {
        const std::size_t drops = channel.dropsQueue + channel.dropsLoss + channel.dropsDown;
        items.push_back({channel.name, static_cast<double>(drops)});
    }
    return keepTop(items, k);
}

std::vector<ReportItem> Report::flowsByLatency() const {
    std::vector<ReportItem> items;
    for (const FlowMetrics& flow : metrics_.flows()) {
        if (flow.delivered > 0) {
            items.push_back({"Flow " + std::to_string(flow.flowId), flow.avgLatencyMs});
        }
    }
    std::stable_sort(items.begin(), items.end(), rankedBefore);
    return items;
}

}
