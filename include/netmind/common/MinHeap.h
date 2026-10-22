#pragma once

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace netmind {

template <typename T, typename Compare = std::less<T>>
class MinHeap {
  public:
    MinHeap() = default;

    explicit MinHeap(Compare compare) : compare_(std::move(compare)) {}

    explicit MinHeap(std::vector<T> values, Compare compare = Compare{})
        : data_(std::move(values)), compare_(std::move(compare)) {
        buildHeap();
    }

    [[nodiscard]] bool empty() const noexcept {
        return data_.empty();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return data_.size();
    }

    [[nodiscard]] const T& top() const {
        if (empty()) {
            throw std::out_of_range("Cannot inspect an empty MinHeap");
        }

        return data_.front();
    }

    void push(const T& value) {
        data_.push_back(value);
        (void)siftUp(data_.size() - 1);
    }

    void push(T&& value) {
        data_.push_back(std::move(value));
        (void)siftUp(data_.size() - 1);
    }

    template <typename... Args>
    T& emplace(Args&&... args) {
        data_.emplace_back(std::forward<Args>(args)...);
        const std::size_t finalIndex = siftUp(data_.size() - 1);
        return data_[finalIndex];
    }

    void pop() {
        if (empty()) {
            throw std::out_of_range("Cannot remove from an empty MinHeap");
        }

        if (data_.size() == 1) {
            data_.pop_back();
            return;
        }

        data_.front() = std::move(data_.back());
        data_.pop_back();
        siftDown(0);
    }

    [[nodiscard]] T extractMin() {
        if (empty()) {
            throw std::out_of_range("Cannot extract from an empty MinHeap");
        }

        T result = std::move(data_.front());
        pop();
        return result;
    }

    void clear() noexcept {
        data_.clear();
    }

  private:
    std::vector<T> data_;
    Compare compare_{};

    void buildHeap() {
        for (std::size_t i = data_.size() / 2; i > 0; --i) {
            siftDown(i - 1);
        }
    }

    std::size_t siftUp(std::size_t index) {
        while (index > 0) {
            const std::size_t parent = (index - 1) / 2;

            if (!compare_(data_[index], data_[parent])) {
                break;
            }

            using std::swap;
            swap(data_[index], data_[parent]);
            index = parent;
        }

        return index;
    }

    void siftDown(std::size_t index) {
        const std::size_t count = data_.size();

        while (true) {
            const std::size_t left = index * 2 + 1;
            const std::size_t right = index * 2 + 2;
            std::size_t smallest = index;

            if (left < count && compare_(data_[left], data_[smallest])) {
                smallest = left;
            }

            if (right < count && compare_(data_[right], data_[smallest])) {
                smallest = right;
            }

            if (smallest == index) {
                break;
            }

            using std::swap;
            swap(data_[index], data_[smallest]);
            index = smallest;
        }
    }
};

}