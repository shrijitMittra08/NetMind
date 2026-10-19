#pragma once

#include <string>
#include <vector>
#include <cstddef>

namespace netmind {

class MetricsCollector;

struct ReportItem {
    std::string name;
    double metric{};
};

class Report {
public:
    explicit Report(const MetricsCollector& metrics);

    [[nodiscard]]
    std::vector<ReportItem> topLinksByUtilization(std::size_t k) const;

    [[nodiscard]]
    std::vector<ReportItem> topLinksByDrops(std::size_t k) const;

    [[nodiscard]]
    std::vector<ReportItem> flowsByLatency() const;

private:
    const MetricsCollector& metrics_;
};

}