#!/usr/bin/env bash
set -euo pipefail

POSTGRES_CONNECTION_STRING="${POSTGRES_CONNECTION_STRING:-postgresql://nexus_app:postgres@postgres:5432/nexus_db}"
PORT="${NEXUS_PORT:-8080}"

# ---------------------------------------------------------------------------
# Start Python AI service (FastAPI on port 8000)
# ---------------------------------------------------------------------------
if [ -f /app/ai_service/python_helper.py ]; then
    # Load .env if present
    if [ -f /app/ai_service/.env ]; then
        set -a
        # shellcheck disable=SC1091
        . /app/ai_service/.env
        set +a
    fi

    echo "[entrypoint] Starting AI service on :8000 ..."
    python3 /app/ai_service/python_helper.py &
    AI_PID=$!
    sleep 2  # give it a moment to bind
    echo "[entrypoint] AI service PID=$AI_PID"
fi

if [ -n "${APP_USERNAME:-}" ]; then
    export NEXUS_INIT_USERNAME="${NEXUS_INIT_USERNAME:-$APP_USERNAME}"
fi
if [ -n "${APP_PASSWORD:-}" ]; then
    export NEXUS_INIT_PASSWORD="${NEXUS_INIT_PASSWORD:-$APP_PASSWORD}"
fi
export NEXUS_INIT_FULLNAME="${NEXUS_INIT_FULLNAME:-${NEXUS_INIT_USERNAME:-admin}}"
export NEXUS_INIT_EMAIL="${NEXUS_INIT_EMAIL:-}"

# ---------------------------------------------------------------------------
# Start the C server (foreground)
# ---------------------------------------------------------------------------
echo "[entrypoint] Starting NEXUS server on :${PORT} ..."
exec /app/nexus_server "$POSTGRES_CONNECTION_STRING" "$PORT"
