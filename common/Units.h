#pragma once

#include <cmath>
#include <stdexcept>

namespace netmind {

// Convert milliseconds to seconds.
inline double msToSec(double milliseconds) {
    if (!std::isfinite(milliseconds) || milliseconds < 0.0) {
        throw std::invalid_argument(
            "Milliseconds must be finite and non-negative");
    }

    return milliseconds / 1000.0;
}

// Calculate transmission time in seconds.
// bandwidthMbps is in megabits per second.
inline double txTimeSec(unsigned long long bytes, double bandwidthMbps) {
    if (!std::isfinite(bandwidthMbps) || bandwidthMbps <= 0.0) {
        throw std::invalid_argument(
            "Bandwidth must be finite and positive");
    }

    return (static_cast<double>(bytes) * 8.0) /
           (bandwidthMbps * 1'000'000.0);
}

// Calculate bandwidth in Mbps from bits transmitted over seconds.
inline double mbps(double bits, double seconds) {
    if (!std::isfinite(bits) || bits < 0.0) {
        throw std::invalid_argument(
            "Bits must be finite and non-negative");
    }

    if (!std::isfinite(seconds) || seconds <= 0.0) {
        throw std::invalid_argument(
            "Duration must be finite and positive");
    }

    return bits / seconds / 1'000'000.0;
}

}