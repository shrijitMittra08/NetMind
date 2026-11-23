import { describe, expect, it } from "vitest";
import { addLink, addNode, emptyModel, fromTopology, removeNode, toTopology, validateTopology } from "./topology";

const chain = () => {
  let m = emptyModel();
  m = addNode(m, "HOST", 0, 0);
  m = addNode(m, "ROUTER", 100, 0);
  m = addNode(m, "HOST", 200, 0);
  m = addLink(m, 1, 2);
  return addLink(m, 2, 3);
};

describe("topology builder model", () => {
  // Checks that new nodes get unique names and IP addresses
  it("names and addresses new nodes uniquely", () => {
    expect(chain().nodes.map((n) => [n.name, n.ip])).toEqual([
      ["PC1", "10.0.0.1"],
      ["R1", "10.0.0.2"],
      ["PC2", "10.0.0.3"],
    ]);
  });

  // Checks that a valid chain passes validation and duplicate or self links are ignored
  it("accepts a valid chain and ignores duplicate or self links", () => {
    const m = addLink(addLink(chain(), 2, 3), 1, 1);
    expect(m.links).toHaveLength(2);
    expect(validateTopology(m)).toEqual([]);
  });

  // Checks that a host with no link is reported as an error
  it("requires each host to have exactly one link", () => {
    const errors = validateTopology(removeNode(chain(), 2)).map((i) => i.message);
    expect(errors).toContain("Host PC1 must have exactly one link (it has 0).");
  });

  // Checks that duplicate names, invalid IPs and out-of-range loss rates are reported
  it("flags duplicate names, bad IPs and out-of-range link values", () => {
    const m = chain();
    m.nodes[2]!.name = "PC1";
    m.nodes[1]!.ip = "10.0.0.300";
    m.links[0]!.lossRate = 1.5;
    const messages = validateTopology(m).map((i) => i.message).join("\n");
    expect(messages).toMatch(/used by more than one node/);
    expect(messages).toMatch(/not a valid IPv4/);
    expect(messages).toMatch(/loss rate must be between 0 and 1/);
  });

  // Checks that a topology converted to JSON and back keeps the same data and positions
  it("round-trips through the topology JSON format with positions", () => {
    const json = toTopology(chain());
    expect(json.nodes[1]).toEqual({ id: 2, type: "ROUTER", name: "R1", ip: "10.0.0.2", pos: [100, 0] });
    expect(toTopology(fromTopology(json))).toEqual(json);
  });
});
