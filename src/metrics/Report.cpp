#include "netmind/metrics/Report.h"

#include "netmind/metrics/MetricsCollector.h"

#include <algorithm>
#include <string>
#include <vector>

namespace netmind {

Report::Report(const MetricsCollector& metrics)
    : metrics_(metrics) {}

std::vector<ReportItem> Report::topLinksByUtilization(
    std::size_t k) const {
    std::vector<ReportItem> items;
    items.reserve(metrics_.channels().size());

    for (const auto& channel : metrics_.channels()) {
        items.push_back({channel.name, channel.utilizationPct});
    }

    const auto count = std::min(k, items.size());
    const auto compare = [](const ReportItem& left, const ReportItem& right) {
        if (left.metric != right.metric) {
            return left.metric > right.metric;
        }
        return left.name < right.name;
    };

    std::partial_sort(items.begin(), items.begin() + count, items.end(), compare);
    items.resize(count);
    return items;
}

std::vector<ReportItem> Report::topLinksByDrops(std::size_t k) const {
    std::vector<ReportItem> items;
    items.reserve(metrics_.channels().size());

    for (const auto& channel : metrics_.channels()) {
        const auto drops =
            channel.dropsQueue + channel.dropsLoss + channel.dropsDown;
        items.push_back({channel.name, static_cast<double>(drops)});
    }

    const auto count = std::min(k, items.size());
    const auto compare = [](const ReportItem& left, const ReportItem& right) {
        if (left.metric != right.metric) {
            return left.metric > right.metric;
        }
        return left.name < right.name;
    };

    std::partial_sort(items.begin(), items.begin() + count, items.end(), compare);
    items.resize(count);
    return items;
}

std::vector<ReportItem> Report::flowsByLatency() const {
    std::vector<ReportItem> items;

    for (const auto& flow : metrics_.flows()) {
        if (flow.delivered == 0) {
            continue;
        }

        items.push_back({
            "Flow " + std::to_string(flow.flowId),
            flow.avgLatencyMs
        });
    }

    std::stable_sort(
        items.begin(),
        items.end(),
        [](const ReportItem& left, const ReportItem& right) {
            if (left.metric != right.metric) {
                return left.metric > right.metric;
            }
            return left.name < right.name;
        });

    return items;
}

}