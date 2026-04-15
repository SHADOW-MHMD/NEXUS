#!/usr/bin/env bash
# Startup script for NEXUS Warehouse Agent
# Loads .env and starts uvicorn on port 8000

set -e

# Load environment variables
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Try multiple .env locations
if [ -f "$PROJECT_ROOT/.env" ]; then
    set -a
    source "$PROJECT_ROOT/.env"
    set +a
    echo "[warehouse-agent] Loaded .env from $PROJECT_ROOT"
elif [ -f "$SCRIPT_DIR/.env" ]; then
    set -a
    source "$SCRIPT_DIR/.env"
    set +a
    echo "[warehouse-agent] Loaded .env from $SCRIPT_DIR"
else
    echo "[warehouse-agent] WARNING: No .env file found"
fi

# Check for required environment variables
if [ -z "$OPENROUTER_API_KEY" ]; then
    echo "[warehouse-agent] WARNING: OPENROUTER_API_KEY not set — AI features will not work"
fi

# Start the warehouse agent
echo "[warehouse-agent] Starting NEXUS Warehouse Agent on port 8000..."
echo "[warehouse-agent] Model: qwen/qwen-3-coder-32b-instruct"

cd "$SCRIPT_DIR"
uvicorn nexus_warehouse_agent:app \
    --host 0.0.0.0 \
    --port 8000 \
    --reload \
    --log-level info
