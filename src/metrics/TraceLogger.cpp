#include "netmind/metrics/TraceLogger.h"

namespace netmind {

TraceLogger::TraceLogger(const std::string& outputPath)
{
    (void)outputPath;
}

TraceLogger::~TraceLogger() = default;

void TraceLogger::onSent(SimTime, const Packet&)
{
}

void TraceLogger::onEnqueue(
    SimTime,
    const Packet&,
    const ChannelId&,
    size_t)
{
}

void TraceLogger::onTxStart(
    SimTime,
    const Packet&,
    const ChannelId&,
    SimTime)
{
}

void TraceLogger::onArrive(
    SimTime,
    const Packet&,
    NodeId,
    NodeId)
{
}

void TraceLogger::onForward(
    SimTime,
    const Packet&,
    NodeId,
    NodeId)
{
}

void TraceLogger::onDelivered(SimTime, const Packet&)
{
}

void TraceLogger::onDropped(
    SimTime,
    const Packet&,
    const std::string&,
    DropReason)
{
}

void TraceLogger::onNetworkEvent(
    SimTime,
    const std::string&,
    const std::string&,
    const std::string&)
{
}

} // namespace netmind