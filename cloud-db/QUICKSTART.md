# Quick Start

Get NEXUS running in under 2 minutes.

---

## Prerequisites

- [Docker](https://docs.docker.com/get-docker/) installed
- [Docker Compose](https://docs.docker.com/compose/install/) v2+

---

## Step 1: Clone

```bash
git clone https://github.com/SHADOW-MHMD/NEXUS_C-Nano-Banana-Edition.git
cd NEXUS_C-Nano-Banana-Edition
```

## Step 2: Configure

```bash
cp .env.example .env
```

Edit `.env` and set at minimum:

```env
OPENROUTER_API_KEY=sk-or-v1-your-key-here
```

Get a free key at [openrouter.ai](https://openrouter.ai/keys).  
The system works without it (rule-based fallback), but AI features need a key.

## Step 3: Start

```bash
docker compose up --build
```

This builds and starts 3 services:

| Service | Port | Purpose |
|---|---|---|
| `ai-service` | 8000 | Python FastAPI — OpenRouter AI |
| `backend` | 8080 | C server — API + web UI |
| `frontend` | 3000 | Nginx — static files (optional) |

## Step 4: Create Admin

On first run, the terminal prompts:

```
============================================
  NEXUS — First Run Setup
============================================
Admin username: admin
Admin password: ********
Confirm password: ********
```

Or skip this by setting in `.env`:

```env
APP_USERNAME=admin
APP_PASSWORD=admin123
```

## Step 5: Access

- **Main App**: http://localhost:8080
- **AI Docs**: http://localhost:8000/docs

---

## Verify AI Works

```bash
curl -s -X POST http://localhost:8000/ai/chat \
  -H "Content-Type: application/json" \
  -d '{"message":"hello","context":"INVENTORY SNAPSHOT: All clear — no alerts."}'
```

Expected: `{"reply":"Hey there! 👋 ...","reply_model":"openrouter/free"}`

## Stop

```bash
docker compose down
```

Destroy all data: `docker compose down -v`

---

## Local Build (No Docker)

```bash
sudo apt-get install -y build-essential libsqlite3-dev libmicrohttpd-dev \
    libssl-dev libcurl4-openssl-dev python3 python3-pip
pip3 install -r ai_service/requirements.txt
make clean && make
python3 ai_service/python_helper.py &
./nexus_server nexus_data.db 8080
```

Open http://localhost:8080.

---

## Next Steps

- [README.md](./README.md) — Full documentation
- [SETUP.md](./SETUP.md) — Detailed setup and troubleshooting
- [API.md](./API.md) — Complete API reference
