# ──────────────────────────────────────────────────────────
# NEXUS C Backend — Multi-stage build
# Stage 1: Compile the C server
# ──────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        libpq-dev \
        libmicrohttpd-dev \
        libssl-dev \
        libcurl4-openssl-dev \
        pkg-config \
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
        libpq5 \
        libmicrohttpd12 \
        libssl3 \
        libcurl4 \
        postgresql-client \
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

EXPOSE 8080

ENV POSTGRES_CONNECTION_STRING=postgresql://nexus_app:postgres@postgres:5432/nexus_db
ENV NEXUS_PORT=8080
ENV AI_SERVICE_URL=http://ai-service:8000

ENTRYPOINT ["/app/docker-entrypoint.sh"]
CMD ["/app/nexus_server", "postgresql://nexus_app:postgres@postgres:5432/nexus_db", "8080"]
