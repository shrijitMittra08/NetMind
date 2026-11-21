#include "netmind/common/UnionFind.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <stdexcept>

TEST_CASE("UnionFind starts with one component per element") {
    netmind::UnionFind sets(5);

    REQUIRE(sets.size() == 5);
    REQUIRE(sets.componentCount() == 5);

    for (std::size_t i = 0; i < sets.size(); ++i) {
        REQUIRE(sets.find(i) == i);
    }
}

TEST_CASE("UnionFind joins and detects connected elements") {
    netmind::UnionFind sets(5);

    REQUIRE(sets.unite(0, 1));
    REQUIRE(sets.unite(1, 2));
    REQUIRE(sets.connected(0, 2));
    REQUIRE_FALSE(sets.connected(0, 3));
    REQUIRE(sets.componentCount() == 3);
}

TEST_CASE("UnionFind does not reduce the count for a repeated union") {
    netmind::UnionFind sets(3);

    REQUIRE(sets.unite(0, 1));
    REQUIRE_FALSE(sets.unite(0, 1));
    REQUIRE(sets.componentCount() == 2);
}

TEST_CASE("UnionFind rejects indices outside its range") {
    netmind::UnionFind sets(2);

    REQUIRE_THROWS_AS(sets.find(2), std::out_of_range);
    REQUIRE_THROWS_AS(sets.unite(0, 2), std::out_of_range);
    REQUIRE_THROWS_AS(sets.connected(2, 0), std::out_of_range);
}

TEST_CASE("UnionFind handles an empty collection") {
    netmind::UnionFind sets(0);

    REQUIRE(sets.size() == 0);
    REQUIRE(sets.componentCount() == 0);
    REQUIRE_THROWS_AS(sets.find(0), std::out_of_range);
}