#pragma once

#include <filesystem>
#include <string>

namespace netmind {

    class MetricsCollector;

    class Exporters {

    public:
        static void exportFlowsCsv(
            const std::filesystem::path& outputPath,
            const MetricsCollector& metrics
        );
        static void exportLinksCsv(
            const std::filesystem::path& outputPath,
            const MetricsCollector& metrics
        );
        static void exportWindowsCsv(
            const std::filesystem::path& outputPath,
            const MetricsCollector& metrics
        );
        static void exportNetworkCsv(
            const std::filesystem::path& outputPath,
            const MetricsCollector& metrics
        );
        static void exportManifest(
            const std::filesystem::path& outputPath,
            const std::string& manifestJson
        );
        static void exportAll(
            const std::filesystem::path& outputDir,
            const std::string& runId,
            const MetricsCollector& metrics,
            const std::string& manifestJson
        );

    };

}