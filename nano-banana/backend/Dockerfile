# ──────────────────────────────────────────────────────────
# NEXUS C Backend — Multi-stage build
# Stage 1: Compile the C server
# ──────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        libsqlite3-dev \
        libmicrohttpd-dev \
        libssl-dev \
        libcurl4-openssl-dev \
        make && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY src/ ./src/
COPY include/ ./include/
COPY Makefile .
RUN make

# ──────────────────────────────────────────────────────────
# Stage 2: Minimal runtime image
# ──────────────────────────────────────────────────────────
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        libsqlite3-0 \
        libmicrohttpd12 \
        libssl3 \
        libcurl4 \
        curl \
        ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy compiled binary
COPY --from=builder /build/nexus_server /app/nexus_server

# Copy frontend static files (served by the C server)
COPY frontend/ /app/frontend/

# Copy entrypoint
COPY docker-entrypoint-backend.sh /app/docker-entrypoint.sh
RUN chmod +x /app/docker-entrypoint.sh

# Database volume
VOLUME ["/app/data"]

EXPOSE 8080

ENV NEXUS_DB_PATH=/app/data/nexus_data.db
ENV NEXUS_PORT=8080
ENV AI_SERVICE_URL=http://ai-service:8000

ENTRYPOINT ["/app/docker-entrypoint.sh"]
CMD ["/app/nexus_server", "/app/data/nexus_data.db", "8080"]
