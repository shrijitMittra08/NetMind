#pragma once

#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>

namespace netmind {

class Rng {
  public:
    explicit Rng(std::uint64_t seed) : engine_(seed) {}

    double uniform01() {
        return std::generate_canonical<double, 53>(engine_);
    }

    double exponential(double rate) {
        if (!std::isfinite(rate) || rate <= 0.0) {
            throw std::invalid_argument("Exponential rate must be finite and positive");
        }

        std::exponential_distribution<double> distribution(rate);
        return distribution(engine_);
    }

  private:
    std::mt19937_64 engine_;
};

}