
#pragma once

#include <cstddef>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace netmind {

class UnionFind {
  public:
    explicit UnionFind(std::size_t size) : parent_(size), rank_(size, 0), componentCount_(size) {
        std::iota(parent_.begin(), parent_.end(), std::size_t{0});
    }

    [[nodiscard]] std::size_t find(std::size_t element) {
        checkElement(element);

        if (parent_[element] != element) {
            parent_[element] = find(parent_[element]);
        }

        return parent_[element];
    }

    bool unite(std::size_t a, std::size_t b) {
        a = find(a);
        b = find(b);

        if (a == b) {
            return false;
        }

        if (rank_[a] < rank_[b]) {
            parent_[a] = b;
        } else if (rank_[a] > rank_[b]) {
            parent_[b] = a;
        } else {
            parent_[b] = a;
            ++rank_[a];
        }

        --componentCount_;
        return true;
    }

    [[nodiscard]] bool connected(std::size_t a, std::size_t b) {
        return find(a) == find(b);
    }

    [[nodiscard]] std::size_t componentCount() const noexcept {
        return componentCount_;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return parent_.size();
    }

  private:
    std::vector<std::size_t> parent_;
    std::vector<std::size_t> rank_;
    std::size_t componentCount_;

    void checkElement(std::size_t element) const {
        if (element >= parent_.size()) {
            throw std::out_of_range("UnionFind element index is out of range");
        }
    }
};

}