#!/usr/bin/env bash
set -euo pipefail

DB_PATH="${NEXUS_DB_PATH:-/app/data/nexus_data.db}"
PORT="${NEXUS_PORT:-8080}"

mkdir -p "$(dirname "$DB_PATH")"

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

# ---------------------------------------------------------------------------
# First-run DB setup (interactive only if stdin is a TTY)
# ---------------------------------------------------------------------------
needs_init="false"
if [ ! -f "$DB_PATH" ] || [ ! -s "$DB_PATH" ]; then
    needs_init="true"
fi

if [ "$needs_init" = "true" ] && [ -t 0 ]; then
    echo "First run setup"

    while true; do
        read -r -p "New username: " init_username
        if [ -n "$init_username" ]; then
            break
        fi
        echo "Username cannot be empty."
    done

    while true; do
        read -r -s -p "New password: " init_password
        echo
        read -r -s -p "Confirm password: " init_password_confirm
        echo

        if [ -z "$init_password" ]; then
            echo "Password cannot be empty."
            continue
        fi

        if [ "$init_password" != "$init_password_confirm" ]; then
            echo "Passwords do not match."
            continue
        fi

        break
    done

    export NEXUS_INIT_USERNAME="$init_username"
    export NEXUS_INIT_PASSWORD="$init_password"
    export NEXUS_INIT_FULLNAME="$init_username"
    export NEXUS_INIT_EMAIL=""

    read -r -p "Google API key for AI reports (leave blank to skip): " google_api_key
    if [ -n "${google_api_key:-}" ]; then
        export NEXUS_GOOGLE_API_KEY="$google_api_key"
    fi
fi

# ---------------------------------------------------------------------------
# Start the C server (foreground)
# ---------------------------------------------------------------------------
echo "[entrypoint] Starting NEXUS server on :${PORT} ..."
exec /app/nexus_server "$DB_PATH" "$PORT"
