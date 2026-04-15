#!/usr/bin/env bash
set -euo pipefail

POSTGRES_CONNECTION_STRING="${POSTGRES_CONNECTION_STRING:-postgresql://nexus_app:postgres@postgres:5432/nexus_db}"
PORT="${NEXUS_PORT:-8080}"

# ── Load .env if present ──
for env_file in /app/.env /app/ai_service/.env; do
    if [ -f "$env_file" ]; then
        echo "[entrypoint] Loading $env_file"
        set -a
        # shellcheck disable=SC1091
        . "$env_file"
        set +a
        break
    fi
done

# ── Validate environment ──
if [ -n "${APP_USERNAME:-}" ]; then
    echo "[entrypoint] APP_USERNAME configured"
else
    echo "[entrypoint] WARNING: APP_USERNAME not set — no admin user will be created"
fi

if [ -n "${OPENROUTER_API_KEY:-}" ]; then
    echo "[entrypoint] OPENROUTER_API_KEY configured"
else
    echo "[entrypoint] WARNING: OPENROUTER_API_KEY not set — AI will use fallback mode"
fi

if [ -n "${APP_USERNAME:-}" ]; then
    export NEXUS_INIT_USERNAME="${NEXUS_INIT_USERNAME:-$APP_USERNAME}"
fi
if [ -n "${APP_PASSWORD:-}" ]; then
    export NEXUS_INIT_PASSWORD="${NEXUS_INIT_PASSWORD:-$APP_PASSWORD}"
fi
export NEXUS_INIT_FULLNAME="${NEXUS_INIT_FULLNAME:-${NEXUS_INIT_USERNAME:-admin}}"
export NEXUS_INIT_EMAIL="${NEXUS_INIT_EMAIL:-}"

echo "[entrypoint] Starting NEXUS server on :${PORT}"
echo "[entrypoint] PostgreSQL connection string configured"
exec /app/nexus_server "$POSTGRES_CONNECTION_STRING" "$PORT"
