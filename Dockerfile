FROM python:3.13-slim-trixie AS simulator
RUN apt-get update \
 && apt-get install -y --no-install-recommends build-essential cmake ninja-build git ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src
COPY apps ./apps
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
 && cmake --build build --target netmind_cli \
 && strip build/netmind_cli

# Optional: build and run the C++ test suite in a clean Linux toolchain.
#   docker build --target cpp-test .
FROM simulator AS cpp-test
COPY config ./config
COPY tests ./tests
RUN cmake -S . -B build-test -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
 && cmake --build build-test \
 && ctest --test-dir build-test --output-on-failure

FROM node:22-slim AS viewer
WORKDIR /viewer
COPY viewer_v2/package.json viewer_v2/package-lock.json ./
RUN npm ci --no-audit --no-fund
COPY viewer_v2/ ./
RUN npm run build

FROM python:3.13-slim-trixie
ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    NETMIND_HOST=0.0.0.0 \
    NETMIND_PORT=8765 \
    NETMIND_CLI=/app/bin/netmind_cli \
    NETMIND_VIEWER_DIR=/app/viewer \
    NETMIND_DATA_DIR=/app/data \
    NETMIND_ML_ASSETS=/app/ml

WORKDIR /app/python
COPY python/requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

COPY --from=simulator /src/build/netmind_cli /app/bin/netmind_cli
COPY --from=viewer /viewer/dist /app/viewer
COPY config /app/config
COPY config /app/config-defaults
COPY data/dataset /app/data/dataset
COPY data/models /app/ml/models
COPY data/plots /app/ml/plots
COPY python/ ./

RUN useradd --create-home --uid 10001 netmind \
 && chown -R netmind:netmind /app/config /app/data
USER netmind

EXPOSE 8765
HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
  CMD python -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8765/api/health', timeout=4)"
CMD ["sh", "-c", "cp -r --update=none /app/config-defaults/. /app/config/ && exec python netmind_api.py"]
