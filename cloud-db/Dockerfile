FROM debian:bookworm AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential libpq-dev libmicrohttpd-dev libssl-dev libcurl4-openssl-dev pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN make

# ---------- Python builder ----------
FROM debian:bookworm-slim AS python-deps

RUN apt-get update \
    && apt-get install -y --no-install-recommends python3 python3-pip \
    && rm -rf /var/lib/apt/lists/*

COPY ai_service/requirements.txt /tmp/requirements.txt
RUN pip3 install --no-cache-dir --break-system-packages -r /tmp/requirements.txt

# ---------- Runtime ----------
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends libpq5 libmicrohttpd12 libssl3 libcurl4 bash ca-certificates python3 postgresql-client \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# C binary
COPY --from=build /app/nexus_server /app/nexus_server
COPY --from=build /app/frontend /app/frontend

# Python AI service
COPY --from=python-deps /usr/local/lib/python3*/dist-packages /usr/local/lib/python3.11/dist-packages
COPY ai_service /app/ai_service

# Entrypoint
COPY docker-entrypoint.sh /app/docker-entrypoint.sh

RUN chmod +x /app/docker-entrypoint.sh

EXPOSE 8080

ENTRYPOINT ["/app/docker-entrypoint.sh"]
