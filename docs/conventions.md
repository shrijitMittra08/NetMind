# NetMind conventions

## Naming
- Files: `PascalCase.h/.cpp` per class. Tests: `tests/unit/test_<module>.cpp`.
- Namespace `netmind`. Classes `PascalCase`, methods `camelCase`, private members end with `_`.
- Enum values `UPPER_CASE`.

## Headers
- `#pragma once`. Include order: own header, standard library, project headers.
- No `using namespace` in headers.

## Ownership and const
- `Graph` owns nodes and links via `unique_ptr`. Everyone else uses ids or non-owning raw pointers.
- No owning raw pointers, no `new`/`delete` outside smart-pointer factories.
- `const` everything that can be. `[[nodiscard]]` on queries.

## Errors
- Bad input (files, CLI arguments): throw `ConfigError` / `TopologyError`.
- Broken internal invariant (a bug): `NETMIND_CHECK(cond)`.

## Units and determinism
- Simulation time is `double` seconds. ms / Mbps / bytes appear only in config and
  output files. Conversion happens only in `common/Units.h`.
- All randomness goes through `Rng`. Never let `unordered_map` iteration order change
  behavior or output: sort keys first.

## Tests
- Every module gets a test file. A module is not done until its tests pass.
- Changing a frozen header needs a PR titled `[HEADER CHANGE]` approved by everyone.