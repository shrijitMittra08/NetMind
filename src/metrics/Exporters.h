#include "netmind/metrics/Exporters.h"

namespace netmind {

void Exporters::exportFlowsCsv(
    const std::filesystem::path&,
    const MetricsCollector&)
{
}

void Exporters::exportLinksCsv(
    const std::filesystem::path&,
    const MetricsCollector&)
{
}

void Exporters::exportWindowsCsv(
    const std::filesystem::path&,
    const MetricsCollector&)
{
}

void Exporters::exportNetworkCsv(
    const std::filesystem::path&,
    const MetricsCollector&)
{
}

void Exporters::exportManifest(
    const std::filesystem::path&,
    const std::string&)
{
}

void Exporters::exportAll(
    const std::filesystem::path&,
    const std::string&,
    const MetricsCollector&,
    const std::string&)
{
}

} // namespace netmind