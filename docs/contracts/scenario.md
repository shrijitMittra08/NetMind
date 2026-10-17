
# Scenario JSON Contract

## Purpose

A scenario configures a simulation run: its topology, random seed, duration,
routing policy, traffic flows, and scheduled network events.

## Fields

- `name`: scenario name.
- `topology`: path to a topology JSON file.
- `seed`: deterministic random seed.
- `duration_s`: simulation duration in seconds.
- `routing.algorithm`: routing algorithm name.
- `routing.weight`: routing weight policy.
- `routing.reconvergence_delay_ms`: optional reconvergence delay in milliseconds.
- `flows`: array of traffic-flow configurations.
- `events`: array of scheduled network events.

Each flow contains:

- `id`: flow ID.
- `src` and `dst`: source and destination host names.
- `pattern`: traffic pattern name.
- `rate_mbps`: traffic rate in Mbps.
- `packet_bytes`: packet size in bytes.
- `start_s` and `stop_s`: start and stop times in seconds.

Supported event types:

- `LINK_FAILURE`
- `LINK_RESTORE`
- `ROUTER_FAILURE`
- `ROUTER_RESTORE`
- `CONGESTION`
- `TRAFFIC_BURST`

Event times use seconds.

## Example

    {
      "name": "link_failure_demo",
      "topology": "config/topologies/ring_backup.json",
      "seed": 42,
      "duration_s": 60,
      "routing": {
        "algorithm": "dijkstra",
        "weight": "latency",
        "reconvergence_delay_ms": 0
      },
      "flows": [
        {
          "id": 1,
          "src": "PC1",
          "dst": "PC2",
          "pattern": "constant",
          "rate_mbps": 5,
          "packet_bytes": 1000,
          "start_s": 0,
          "stop_s": 60
        }
      ],
      "events": [
        {
          "type": "LINK_FAILURE",
          "a": "R1",
          "b": "R2",
          "time_s": 20
        },
        {
          "type": "LINK_RESTORE",
          "a": "R1",
          "b": "R2",
          "time_s": 40
        }
      ]
    }

## Validation expectations

The scenario loader should reject invalid field types, unknown references,
invalid time ranges, unsupported event types, and invalid routing settings
using the project's configuration-error conventions.
