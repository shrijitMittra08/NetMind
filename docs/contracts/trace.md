# NetMind Trace and Output Contract

This document defines the trace and metric-output formats shared by the
C++ simulator, metrics subsystem, Python modules, GraphRAG layer, and viewer.

## 1. Trace file

Each simulation run produces:

`data/runs/run_<id>_trace.csv`

For example:

`data/runs/run_001_trace.csv`

### 1.1 CSV header

The trace file must contain exactly this header:

## Conventions the spec leaves open
- `qlen` counts every packet in the channel FIFO **including the one currently being
  transmitted**. A channel with `queueCapacity=2` holds at most 2 packets in total;
  a third arrival is dropped with `queue_full`.
- Rows are ordered by `time_s`; rows with equal time are in causal order.
- `packet_id` and `flow_id` are `-` on rows that are not about a packet or flow.

```text
time_s,event_type,packet_id,flow_id,location,detail

