#pragma once

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace netmind {

class UnionFind {
  public:
    explicit UnionFind(std::size_t size) : parent_(size), rank_(size, 0), componentCount_(size) {
        for (std::size_t element = 0; element < size; ++element) {
            parent_[element] = element;
        }
    }

    [[nodiscard]] std::size_t find(std::size_t element) {
        if (element >= parent_.size()) {
            throw std::out_of_range("UnionFind element index is out of range");
        }
        if (parent_[element] != element) {
            parent_[element] = find(parent_[element]);
        }
        return parent_[element];
    }

    bool unite(std::size_t a, std::size_t b) {
        const std::size_t rootA = find(a);
        const std::size_t rootB = find(b);
        if (rootA == rootB) {
            return false;
        }
        if (rank_[rootA] < rank_[rootB]) {
            parent_[rootA] = rootB;
        } else if (rank_[rootA] > rank_[rootB]) {
            parent_[rootB] = rootA;
        } else {
            parent_[rootB] = rootA;
            ++rank_[rootA];
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
};

}
