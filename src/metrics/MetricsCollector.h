#include "netmind/metrics/MetricsCollector.h"

namespace netmind {

void MetricsCollector::onSent(SimTime, const Packet&)
{
}

void MetricsCollector::onEnqueue(
    SimTime,
    const Packet&,
    const ChannelId&,
    size_t)
{
}

void MetricsCollector::onTxStart(
    SimTime,
    const Packet&,
    const ChannelId&,
    SimTime)
{
}

void MetricsCollector::onArrive(
    SimTime,
    const Packet&,
    NodeId,
    NodeId)
{
}

void MetricsCollector::onForward(
    SimTime,
    const Packet&,
    NodeId,
    NodeId)
{
}

void MetricsCollector::onDelivered(SimTime, const Packet&)
{
}

void MetricsCollector::onDropped(
    SimTime,
    const Packet&,
    const std::string&,
    DropReason)
{
}

void MetricsCollector::onNetworkEvent(
    SimTime,
    const std::string&,
    const std::string&,
    const std::string&)
{
}

} // namespace netmind