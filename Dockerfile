# ============================================================
# Chronos Blockchain Node - Multi-stage Docker Build
# ============================================================
# Stage 1: Build all dependencies + compile the node
# Stage 2: Minimal runtime image
# ============================================================

# ---- Stage 1: Builder ----
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ninja-build \
    pkg-config \
    libssl-dev \
    libsnappy-dev \
    wget \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /deps

# ---- Build Protobuf 3.21.12 from source (system version is too old) ----
RUN wget -q https://github.com/protocolbuffers/protobuf/releases/download/v21.12/protobuf-cpp-3.21.12.tar.gz \
    && tar xzf protobuf-cpp-3.21.12.tar.gz \
    && cd protobuf-3.21.12 \
    && mkdir build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
                -Dprotobuf_BUILD_TESTS=OFF \
                -Dprotobuf_BUILD_SHARED_LIBS=OFF \
                -DCMAKE_INSTALL_PREFIX=/usr/local \
    && make -j$(nproc) && make install \
    && ldconfig

# ---- Build LevelDB from source ----
RUN git clone --depth=1 --branch 1.23 https://github.com/google/leveldb.git leveldb \
    && cd leveldb \
    && mkdir build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
                -DLEVELDB_BUILD_TESTS=OFF \
                -DLEVELDB_BUILD_BENCHMARKS=OFF \
                -DCMAKE_INSTALL_PREFIX=/usr/local \
    && make -j$(nproc) && make install \
    && ldconfig

# ---- Build liboqs (post-quantum crypto) ----
RUN git clone --depth=1 --branch 0.10.1 https://github.com/open-quantum-safe/liboqs.git liboqs \
    && cd liboqs \
    && mkdir build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
                -DOQS_BUILD_ONLY_LIB=ON \
                -DOQS_DIST_BUILD=ON \
                -DBUILD_SHARED_LIBS=OFF \
                -DCMAKE_INSTALL_PREFIX=/usr/local \
    && make -j$(nproc) && make install \
    && ldconfig

# ---- Build Chronos node ----
COPY . /chronos
WORKDIR /chronos

RUN mkdir build && cd build \
    && cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DPROTOBUF_LOCAL_ROOT="" \
        -DLEVELDB_LOCAL_ROOT="" \
        -DOQS_ROOT=/usr/local \
        -DCHRONOS_USE_OQS=ON \
    && make -j$(nproc) chronos_node wallet_cli genesis_tool

# ---- Stage 2: Runtime ----
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libsnappy1v5 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Copy compiled binaries
COPY --from=builder /chronos/build/chronos_node /usr/local/bin/chronos_node
COPY --from=builder /chronos/build/wallet_cli    /usr/local/bin/wallet_cli
COPY --from=builder /chronos/build/genesis_tool  /usr/local/bin/genesis_tool
COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh

# Create chronos user (never run as root)
RUN useradd -m -s /bin/bash chronos

# Data directory (mounted as volume in production)
RUN mkdir -p /data/chronos && chown chronos:chronos /data/chronos

# Key storage
RUN mkdir -p /home/chronos/.chronos/keys && chown -R chronos:chronos /home/chronos/.chronos

USER chronos
WORKDIR /home/chronos

# Ports:
#   8645 — P2P (peers connect here)
#   8080 — JSON-RPC (wallets / API)
EXPOSE 8645 8080

# Entrypoint dispatches: wallet_cli / genesis_tool / chronos_node (default)
ENTRYPOINT ["docker-entrypoint.sh"]
CMD ["--config", "/data/chronos/config.toml"]
