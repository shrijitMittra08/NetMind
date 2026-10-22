#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "netmind/simulation/ScenarioManager.h"

namespace netmind {

class SimObserver;
class SimulationEngine;
class MetricsCollector;
class TraceLogger;

namespace cli {

class Session {
  public:
    Session();
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    [[nodiscard]] ScenarioManager& scenarioManager() noexcept;
    [[nodiscard]] const ScenarioManager& scenarioManager() const noexcept;

    [[nodiscard]] SimulationEngine* engine() noexcept;
    [[nodiscard]] const SimulationEngine* engine() const noexcept;

    void setEngine(std::unique_ptr<SimulationEngine> engine);
    void addObserver(std::unique_ptr<SimObserver> observer);
    void clearRuntime();
    void ensureEngine();
    void reset();

    [[nodiscard]] MetricsCollector* metrics() noexcept;
    [[nodiscard]] const MetricsCollector* metrics() const noexcept;
    [[nodiscard]] TraceLogger* traceLogger() noexcept;
    [[nodiscard]] const TraceLogger* traceLogger() const noexcept;
    [[nodiscard]] const std::filesystem::path& tracePath() const noexcept;
    [[nodiscard]] FlowId nextFlowId() const;
    [[nodiscard]] const std::vector<std::string>& stepMessages() const noexcept;
    void clearStepMessages() noexcept;

    [[nodiscard]] const std::vector<std::unique_ptr<SimObserver>>& observers() const noexcept;

    [[nodiscard]] const std::filesystem::path& outputDirectory() const noexcept;
    void setOutputDirectory(std::filesystem::path directory);

    void requestExit() noexcept;
    [[nodiscard]] bool exitRequested() const noexcept;
    void clearExitRequest() noexcept;

  private:
    ScenarioManager scenarioManager_;

    std::vector<std::unique_ptr<SimObserver>> observers_;
    std::unique_ptr<SimulationEngine> engine_;
    MetricsCollector* metrics_{};
    TraceLogger* traceLogger_{};
    SimObserver* consoleObserver_{};
    std::filesystem::path tracePath_;
    std::vector<std::string> stepMessages_;

    std::filesystem::path outputDirectory_{"data"};
    bool exitRequested_{false};
};

}
}
