#include "netmind/common/UnionFind.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <stdexcept>

// Checks that every element starts in its own separate group
TEST_CASE("UnionFind starts with one component per element") {
    netmind::UnionFind sets(5);

    REQUIRE(sets.size() == 5);
    REQUIRE(sets.componentCount() == 5);

    for (std::size_t i = 0; i < sets.size(); ++i) {
        REQUIRE(sets.find(i) == i);
    }
}

// Checks that joining elements connects them and lowers the group count
TEST_CASE("UnionFind joins and detects connected elements") {
    netmind::UnionFind sets(5);

    REQUIRE(sets.unite(0, 1));
    REQUIRE(sets.unite(1, 2));
    REQUIRE(sets.connected(0, 2));
    REQUIRE_FALSE(sets.connected(0, 3));
    REQUIRE(sets.componentCount() == 3);
}

// Checks that joining two already-joined elements returns false and keeps the count the same
TEST_CASE("UnionFind does not reduce the count for a repeated union") {
    netmind::UnionFind sets(3);

    REQUIRE(sets.unite(0, 1));
    REQUIRE_FALSE(sets.unite(0, 1));
    REQUIRE(sets.componentCount() == 2);
}

// Checks that using an index that is too large throws an out of range error
TEST_CASE("UnionFind rejects indices outside its range") {
    netmind::UnionFind sets(2);

    REQUIRE_THROWS_AS(sets.find(2), std::out_of_range);
    REQUIRE_THROWS_AS(sets.unite(0, 2), std::out_of_range);
    REQUIRE_THROWS_AS(sets.connected(2, 0), std::out_of_range);
}

// Checks that an empty union-find has size zero and rejects any lookup
TEST_CASE("UnionFind handles an empty collection") {
    netmind::UnionFind sets(0);

    REQUIRE(sets.size() == 0);
    REQUIRE(sets.componentCount() == 0);
    REQUIRE_THROWS_AS(sets.find(0), std::out_of_range);
}