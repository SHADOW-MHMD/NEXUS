# Setup & Troubleshooting Guide

---

## Architecture

```
Browser (port 8080)
  │
  ▼
┌─────────────────────────────────────────────┐
│  C Backend (libmicrohttpd)                  │
│  - Serves frontend static files             │
│  - Handles REST API (items, users, issues)  │
│  - Proxies AI requests to Python service    │
│  - SQLite database access                   │
└──────────────┬──────────────────────────────┘
               │ HTTP POST
               │ http://ai-service:8000/ai/*
               ▼
┌─────────────────────────────────────────────┐
│  Python AI Service (FastAPI :8000)          │
│  - Preprocesses inventory data (rules)      │
│  - Calls OpenRouter SDK (openrouter/free)   │
│  - Returns AI analysis or rule-based result │
└──────────────┬──────────────────────────────┘
               │ HTTPS
               ▼
┌─────────────────────────────────────────────┐
│  OpenRouter API                             │
│  Model: openrouter/free                     │
└─────────────────────────────────────────────┘
```

**Critical**: The C backend does NOT call OpenRouter directly. All AI logic flows through the Python service.

---

## Docker Deployment

### Standard Start

```bash
docker compose up --build
```

### With Optional Frontend

```bash
docker compose --profile frontend up --build
```

### Preseed Admin Credentials

```env
# .env
APP_USERNAME=admin
APP_PASSWORD=admin123
NEXUS_INIT_FULLNAME=Administrator
```

### Persistent Data

Database is stored in a Docker named volume (`nexus-data`). To access it:

```bash
docker volume ls | grep nexus
docker run --rm -v nexus_clean_app_nexus-data:/data alpine ls /data
```

---

## Environment Variables

### Required for AI Features

| Variable | Example | Where It's Used |
|---|---|---|
| `OPENROUTER_API_KEY` | `sk-or-v1-...` | Python AI service (OpenRouter SDK) |

### Optional

| Variable | Default | Description |
|---|---|---|
| `APP_USERNAME` | _(none)_ | Admin user for first-run DB seeding |
| `APP_PASSWORD` | _(none)_ | Admin password for first-run DB seeding |
| `NEXUS_PORT` | `8080` | Backend listen port |
| `NEXUS_DB_PATH` | `/app/data/nexus_data.db` | Database file path |
| `AI_SERVICE_URL` | `http://ai-service:8000` | Python service URL (auto-set in Docker) |
| `NEXUS_INIT_FULLNAME` | _(username)_ | Admin full name |
| `NEXUS_INIT_EMAIL` | _(empty)_ | Admin email |

---

## Troubleshooting

### `sh: 1: curl: not found`

**Cause**: Backend container missing curl binary.  
**Fix**: Rebuild — the Dockerfile now includes curl.

```bash
docker compose build --no-cache backend
docker compose up -d
```

### `AI service unreachable`

**Cause**: Backend cannot reach Python service.  
**Diagnose**:

```bash
# Check service health
docker compose ps

# Test connectivity from backend container
docker exec nexus-backend curl -s http://ai-service:8000/docs | head -5
```

**Expected**: Returns HTML. If it returns "Connection refused", the ai-service is not running.

### OpenRouter errors in Python logs

**Check**:

```bash
docker compose logs ai-service | grep -i error
```

Common causes:
- **Invalid API key** → `OpenRouter error: 401` → Check `.env`
- **Model unavailable** → `OpenRouter error: 404` → Model `openrouter/free` may be temporarily down
- **Rate limited** → `OpenRouter error: 429` → Wait and retry

The system falls back to rule-based analysis automatically.

### No admin user on first run

**Cause**: `APP_USERNAME`/`APP_PASSWORD` not set and no TTY for interactive setup.  
**Fix**: Add to `.env` and restart:

```env
APP_USERNAME=admin
APP_PASSWORD=admin123
```

### Port already in use

```bash
lsof -i :8080  # Find what's using the port
```

Change port in `.env`:

```env
NEXUS_PORT=9090
```

Then update compose port mapping:

```yaml
ports:
  - "${NEXUS_PORT:-9090}:8080"
```

### Database corruption

**Recover from backup**:

```bash
# List backups
docker exec nexus-backend ls /app/data/

# Restore (stops server, replaces DB, restarts)
docker compose down
docker run --rm -v nexus_clean_app_nexus-data:/data \
  -v $(pwd):/backup alpine \
  cp /backup/nexus_backup.db /data/nexus_data.db
docker compose up -d
```

---

## Logging

### View All Logs

```bash
docker compose logs -f
```

### Backend Only

```bash
docker compose logs -f backend
```

### AI Service Only

```bash
docker compose logs -f ai-service
```

### Log Format

All logs use `[component] message` format:

```
[entrypoint] Loading /app/.env
[entrypoint] APP_USERNAME configured
[entrypoint] OPENROUTER_API_KEY configured
[entrypoint] Admin user 'admin' will be created
[entrypoint] Starting NEXUS server on :8080
[ai-chat] Proxying request to Python AI service
[ai-service] Received 256 bytes from AI service
[ai-service] calling OpenRouter (model=openrouter/free)
[ai-service] OpenRouter response: 127 chars
```

---

## Security Notes

- **API keys are never exposed to the frontend** — all AI calls are server-side
- **`.env` is in `.gitignore`** — never commit it
- **Password hashing**: HMAC-SHA256 with per-user random salt
- **Tokens are simplified** — not proper JWT; sufficient for trusted internal use
- **For production**: Add proper JWT signing, HTTPS, and input validation
