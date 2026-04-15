#!/bin/bash
set -e

# Load .env if present (compose env vars take precedence)
for f in /app/.env /app/ai_service/.env; do
    if [ -f "$f" ]; then
        set -a
        . "$f"
        set +a
        break
    fi
done

if [ -n "${OPENROUTER_API_KEY:-}" ]; then
    echo "[ai-service] OPENROUTER_API_KEY configured"
else
    echo "[ai-service] WARNING: OPENROUTER_API_KEY not set — using fallback mode"
fi

exec uvicorn python_helper:app --host 0.0.0.0 --port 8000
