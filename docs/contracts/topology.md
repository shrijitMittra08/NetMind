
# Topology JSON Contract

## Purpose

A topology file describes network nodes and undirected links. The graph owns
the node and link objects; other components refer to them by IDs.

## Structure

A topology JSON object contains:

- `nodes`: an array of network nodes.
- `links`: an array of undirected links.

Each node contains:

- `id`: unique node ID.
- `type`: `HOST`, `ROUTER`, or `SWITCH`.
- `name`: unique, non-empty name.
- `ip`: unique, valid IPv4 address.
- `pos`: optional two-element position array for visualization.

Each link contains:

- `a`: ID of the first endpoint.
- `b`: ID of the second endpoint.
- `bandwidthMbps`: positive bandwidth in Mbps.
- `latencyMs`: non-negative latency in milliseconds.
- `lossRate`: probability between 0 and 1, inclusive.
- `queueCapacity`: optional queue capacity in packets; defaults to 64.

## Example

    {
      "nodes": [
        {"id": 1, "type": "HOST", "name": "PC1", "ip": "10.0.0.1"},
        {"id": 2, "type": "ROUTER", "name": "R1", "ip": "10.0.0.254"}
      ],
      "links": [
        {
          "a": 1,
          "b": 2,
          "bandwidthMbps": 100,
          "latencyMs": 2,
          "lossRate": 0.0,
          "queueCapacity": 64
        }
      ]
    }

## Validation

- Node IDs, names, and IP addresses must be unique.
- Node types must be recognized.
- IP addresses must be valid dotted-quad IPv4 addresses.
- Link endpoints must refer to existing nodes.
- Self-links are invalid.
- Bandwidth must be greater than zero.
- Latency must be non-negative.
- Loss rate must be in the inclusive range [0, 1].
- A host must have exactly one link.
- Disconnected graphs produce a warning, not a validation error.

The loader should report invalid configuration through `TopologyError`.
