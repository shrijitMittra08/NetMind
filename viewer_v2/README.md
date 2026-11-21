# NetMind Workbench (viewer v2)

React + TypeScript rewrite of `viewer/`. Same features (topology builder and import,
interactive and CLI-script setup, multi-algorithm runs, ML baseline, replay, GraphRAG),
with a native replay view instead of the legacy iframe.

## Run

The API must be running on port 8765 (the dev server proxies `/api` to it):

```bash
cd python
NETMIND_CLI=../build/release/netmind_cli.exe .venv/Scripts/python.exe -m uvicorn netmind_api:app --port 8765
```

Then, in `viewer_v2/`:

```bash
npm install
npm run dev        # http://localhost:5174
npm test           # domain unit tests (Vitest)
npm run build      # typecheck + production bundle in dist/
npm run lint
```

## Layout

```
src/
  lib/         API client, shared types (mirror python/netmind_api.py), formatting
  domain/      pure logic with tests: topology validation, setup checks and payloads,
               CLI script parse/generate, CSV and the replay model
  state/       WorkbenchProvider: topology, settings, script, runs and the run action;
               drafts are kept in localStorage
  components/  one folder per page: topology, setup, results, replay, insights
  styles/      theme tokens (light/dark), base primitives, page styles
```

The domain modules hold every rule the simulator enforces (host link counts, unique
names/IPs, valid event targets), so the UI blocks a run instead of the simulator failing.
