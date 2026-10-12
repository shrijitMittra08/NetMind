#include "netmind/metrics/Report.h"

namespace netmind {

Report::Report(const MetricsCollector& metrics)
    : metrics_(metrics)
{
}

std::vector<ReportItem> Report::topLinksByUtilization(
    std::size_t) const
{
    return {};
}

std::vector<ReportItem> Report::topLinksByDrops(
    std::size_t) const
{
    return {};
}

std::vector<ReportItem> Report::flowsByLatency() const
{
    return {};
}

} // namespace netmind