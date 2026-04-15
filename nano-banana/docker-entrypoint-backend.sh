#!/usr/bin/env bash
set -euo pipefail

DB_PATH="${NEXUS_DB_PATH:-/app/data/nexus_data.db}"
PORT="${NEXUS_PORT:-8080}"

mkdir -p "$(dirname "$DB_PATH")"

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

# ── Seed admin user on first run ──
needs_init="false"
if [ ! -f "$DB_PATH" ] || [ ! -s "$DB_PATH" ]; then
    needs_init="true"
fi

if [ "$needs_init" = "true" ]; then
    if [ -n "${APP_USERNAME:-}" ]; then
        export NEXUS_INIT_USERNAME="$APP_USERNAME"
    fi
    if [ -n "${APP_PASSWORD:-}" ]; then
        export NEXUS_INIT_PASSWORD="$APP_PASSWORD"
    fi

    if [ -z "${NEXUS_INIT_USERNAME:-}" ] || [ -z "${NEXUS_INIT_PASSWORD:-}" ]; then
        if [ -t 0 ]; then
            echo "============================================"
            echo "  NEXUS — First Run Setup"
            echo "============================================"

            while true; do
                read -r -p "Admin username: " NEXUS_INIT_USERNAME
                if [ -n "$NEXUS_INIT_USERNAME" ]; then break; fi
                echo "Username cannot be empty."
            done

            while true; do
                read -r -s -p "Admin password: " NEXUS_INIT_PASSWORD; echo
                read -r -s -p "Confirm password: " NEXUS_INIT_CONFIRM; echo
                if [ -n "$NEXUS_INIT_PASSWORD" ] && [ "$NEXUS_INIT_PASSWORD" = "$NEXUS_INIT_CONFIRM" ]; then break; fi
                [ -z "$NEXUS_INIT_PASSWORD" ] && echo "Password cannot be empty." || echo "Passwords do not match."
            done

            export NEXUS_INIT_USERNAME NEXUS_INIT_PASSWORD
        else
            echo "[entrypoint] No credentials configured — no admin user will be created"
            echo "[entrypoint] Set APP_USERNAME and APP_PASSWORD in .env, then restart"
        fi
    fi

    if [ -n "${NEXUS_INIT_USERNAME:-}" ] && [ -n "${NEXUS_INIT_PASSWORD:-}" ]; then
        echo "[entrypoint] Admin user '${NEXUS_INIT_USERNAME}' will be created"
    fi

    export NEXUS_INIT_FULLNAME="${NEXUS_INIT_FULLNAME:-${NEXUS_INIT_USERNAME:-admin}}"
    export NEXUS_INIT_EMAIL="${NEXUS_INIT_EMAIL:-}"
else
    echo "[entrypoint] Database exists at ${DB_PATH}"
fi

echo "[entrypoint] Starting NEXUS server on :${PORT}"
exec "$@"
