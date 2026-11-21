#include "netmind/common/MinHeap.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <vector>

TEST_CASE("MinHeap returns values in ascending order") {
    netmind::MinHeap<int> heap;

    heap.push(5);
    heap.push(1);
    heap.push(4);
    heap.push(2);
    heap.push(3);

    REQUIRE(heap.size() == 5);
    REQUIRE(heap.top() == 1);

    REQUIRE(heap.extractMin() == 1);
    REQUIRE(heap.extractMin() == 2);
    REQUIRE(heap.extractMin() == 3);
    REQUIRE(heap.extractMin() == 4);
    REQUIRE(heap.extractMin() == 5);
    REQUIRE(heap.empty());
}

TEST_CASE("MinHeap builds correctly from an existing vector") {
    netmind::MinHeap<int> heap(std::vector<int>{7, 2, 9, 1, 4});

    REQUIRE(heap.extractMin() == 1);
    REQUIRE(heap.extractMin() == 2);
    REQUIRE(heap.extractMin() == 4);
    REQUIRE(heap.extractMin() == 7);
    REQUIRE(heap.extractMin() == 9);
}

TEST_CASE("MinHeap supports a custom comparator") {
    netmind::MinHeap<int, std::greater<int>> heap(
        std::greater<int>{});

    heap.push(2);
    heap.push(9);
    heap.push(4);

    REQUIRE(heap.extractMin() == 9);
    REQUIRE(heap.extractMin() == 4);
    REQUIRE(heap.extractMin() == 2);
}

TEST_CASE("MinHeap emplace returns the inserted element") {
    netmind::MinHeap<int> heap;

    int& inserted = heap.emplace(8);
    REQUIRE(inserted == 8);

    heap.emplace(3);
    REQUIRE(heap.top() == 3);
    REQUIRE(inserted == 8);
}

TEST_CASE("MinHeap clear empties the heap") {
    netmind::MinHeap<int> heap;
    heap.push(1);
    heap.push(2);

    heap.clear();

    REQUIRE(heap.empty());
    REQUIRE(heap.size() == 0);
}

TEST_CASE("MinHeap rejects top and removal on an empty heap") {
    netmind::MinHeap<int> heap;

    REQUIRE_THROWS_AS(heap.top(), std::out_of_range);
    REQUIRE_THROWS_AS(heap.pop(), std::out_of_range);
    REQUIRE_THROWS_AS(heap.extractMin(), std::out_of_range);
}