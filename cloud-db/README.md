# NEXUS — Inventory Management System

> A lightweight, AI-powered inventory manager with a C backend, Python AI service, and Docker deployment.

---

## ⚠️ AI System Status

The AI assistant is currently in **active development (experimental stage)**.

It is functional for inventory-related tasks such as:
* Stock level queries
* Damage and repair tracking
* Basic reporting and alerts

**Known limitations:**
* Occasional over-inclusion of inventory context in general queries
* Non-inventory conversations may be inconsistently handled
* Response behavior is still being tuned for production-grade reliability

> [!CAUTION]
> This module should not yet be considered production-ready.

---

## Architecture

```
┌─────────────┐    HTTP   ┌──────────────────┐    HTTP   ┌──────────────────┐
│   Browser   │ ◄──────► │  C Backend       │ ◄──────► │  Python AI Svc   │
│  (port 8080)│          │  (libmicrohttpd) │          │  (FastAPI :8000) │
└─────────────┘          └────────┬─────────┘          └────────┬─────────┘
                                  │                             │
                           ┌──────┴──────┐                ┌──────┴──────┐
                           │  SQLite DB  │                │ OpenRouter  │
                           │  (.db file) │                │     SDK     │
                           └─────────────┘                └─────────────┘
```

### Data Flow

1.  **Browser** → C Backend: REST API calls (auth, items, users, issues, backup)
2.  **C Backend** → Python AI Service: HTTP POST with inventory context
3.  **Python AI Service** → OpenRouter SDK: AI chat/analytics with `openrouter/free` model
4.  **Fallback**: If AI is unavailable, Python returns rule-based analysis

### Key Design Decisions

* **All AI calls go through Python** — the C backend never calls OpenRouter directly.
* **Service discovery via Docker DNS** — backend uses `http://ai-service:8000`, not localhost.
* **Rule-based fallback** — system works without an API key, just with limited AI features.

---

## Features

| Feature | Description |
| :--- | :--- |
| **Authentication** | Token-based login with HMAC-SHA256 password hashing |
| **Inventory CRUD** | Full item management with SKU, condition, quantity tracking |
| **User Management** | Role-based access (admin, manager, viewer) |
| **Issue Tracking** | Check items out/in with overdue detection |
| **AI Chat** | Natural language inventory queries via OpenRouter |
| **AI Analytics** | Automatic alerts for low stock, damaged items, overdue returns |
| **Backup/Restore** | Database snapshot creation and restoration |
| **Dashboard** | Real-time metrics with Chart.js visualizations |

---

## Quick Start

### Prerequisites

* Docker + Docker Compose v2
* An OpenRouter API key (free at [openrouter.ai](https://openrouter.ai))

### 1. Clone and Configure

```bash
git clone https://github.com/SHADOW-MHMD/NEXUS_C-Nano-Banana-Edition.git
cd NEXUS_C-Nano-Banana-Edition
cp .env.example .env
# Edit .env with your OPENROUTER_API_KEY
```

### 2. Start

```bash
docker compose up --build
```

On first run, the container prompts for admin credentials. Or pre-seed them in `.env`:

```env
APP_USERNAME=admin
APP_PASSWORD=admin123
```

### 3. Access

| Service | URL |
| :--- | :--- |
| **Main App** | http://localhost:8080 |
| **AI Service** | http://localhost:8000/docs |
| **Frontend (Nginx)** | http://localhost:3000 (optional) |

---

## Configuration

### Required Variables

| Variable | Description |
| :--- | :--- |
| `OPENROUTER_API_KEY` | OpenRouter API key for AI features |

### Optional Variables

| Variable | Default | Description |
| :--- | :--- | :--- |
| `APP_USERNAME` | *(none)* | Admin username for first-run seeding |
| `APP_PASSWORD` | *(none)* | Admin password for first-run seeding |
| `NEXUS_PORT` | `8080` | Backend listen port |
| `AI_SERVICE_URL` | `http://ai-service:8000` | Python AI service URL (set automatically in Docker) |

---

## Local Development (No Docker)

```bash
# Install dependencies
sudo apt-get install -y build-essential libsqlite3-dev libmicrohttpd-dev \
    libssl-dev libcurl4-openssl-dev python3 python3-pip
pip3 install -r ai_service/requirements.txt

# Build and run
make clean && make
python3 ai_service/python_helper.py &
./nexus_server nexus_data.db 8080
```

---

## API Endpoints

### Authentication
* `POST /api/auth/login` — Login (returns token)

### Items
* `GET /api/items` — List items
* `POST /api/items` — Create item
* `POST /api/items/{id}` — Update item
* `DELETE /api/items/{id}` — Soft-delete item

### Users
* `GET /api/users` — List users
* `POST /api/users` — Create user
* `POST /api/users/{id}` — Update user
* `DELETE /api/users/{id}` — Soft-delete user

### AI
* `POST /api/ai/chat` — AI chat (proxied to Python service)
* `GET /api/ai/alerts` — Inventory alerts
* `POST /api/reports/ai` — AI-generated report

---

## Troubleshooting

| Issue | Solution |
| :--- | :--- |
| `curl: not found` | Rebuild: `docker compose build` — curl is now included |
| `AI service unreachable` | Check `docker compose ps` — ai-service must be healthy |
| No admin user created | Set `APP_USERNAME`/`APP_PASSWORD` in `.env` before first run |
| Context Over-inclusion | Known experimental issue; check **AI System Status** section |

---

## License

No license file included yet. Add one before broader distribution.
