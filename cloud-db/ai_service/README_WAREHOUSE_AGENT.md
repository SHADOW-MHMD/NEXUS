# NEXUS Warehouse Agent

FastAPI backend service that proxies AI requests to OpenRouter using the `qwen/qwen-3-coder-32b-instruct` model for the NEXUS inventory management system.

## Features

- **AI Command Proxying**: Receives JSON commands from the frontend and sends them to OpenRouter
- **Context Injection**: Prepends system prompt for inventory-aware AI responses
- **Status Code Detection**: Automatically detects [OK], [WARNING], or [CRITICAL] status from AI responses
- **Dashboard Serving**: Serves the HTML dashboard from the `new template` folder

## Quick Start

### 1. Install Dependencies

```bash
pip install -r requirements_warehouse_agent.txt
```

### 2. Configure Environment

Make sure your `.env` file (in project root) has:

```env
OPENROUTER_API_KEY=sk-or-v1-your-api-key-here
```

### 3. Run the Service

```bash
# Using the startup script
./start_warehouse.sh

# Or directly with uvicorn
uvicorn nexus_warehouse_agent:app --host 0.0.0.0 --port 8000 --reload
```

## API Endpoints

### GET `/`
Serves the NEXUS dashboard HTML from the `new template` folder.

### POST `/ai/command`
Primary AI command endpoint.

**Request Body:**
```json
{
  "command": "What items are low in stock?",
  "context": {
    "items": [
      {"name": "Neural Core v4", "sku": "NX-001", "quantity": 124},
      {"name": "Haptic Sensor Hub", "sku": "NX-00812", "quantity": 12}
    ]
  }
}
```

**Response:**
```json
{
  "status_code": "[WARNING]",
  "response": "Low stock detected for Haptic Sensor Hub (NX-00812) with only 12 units remaining...",
  "timestamp": "2026-04-14T10:30:00+00:00"
}
```

### GET `/health`
Health check endpoint.

**Response:**
```json
{
  "status": "healthy",
  "model": "qwen/qwen-3-coder-32b-instruct",
  "api_configured": true,
  "timestamp": "2026-04-14T10:30:00+00:00"
}
```

## Status Code Detection

The service automatically analyzes AI responses and assigns status codes:

- **`[OK]`**: Normal operations, no issues detected
- **`[WARNING]`**: Minor issues (low stock, items needing attention, monitoring required)
- **`[CRITICAL]`**: Severe issues (out of stock, urgent action required, emergencies)

## System Prompt

Every AI request includes this system prompt for context injection:

> "You are the NEXUS Warehouse Agent. Access the inventory database to answer questions about stock, SKUs (like NX-001), and warehouse capacity."

## Architecture

```
Frontend (HTML/JS)
    ↓ POST /ai/command
NEXUS Warehouse Agent (FastAPI)
    ↓ System Prompt + Command
OpenRouter API (qwen/qwen-3-coder-32b-instruct)
    ↓ AI Response with Status Code
Frontend (Displays response with [OK]/[WARNING]/[CRITICAL])
```

## Integration with Frontend

Example JavaScript to call the AI endpoint:

```javascript
async function sendAICommand(command, context = null) {
  const response = await fetch('http://localhost:8000/ai/command', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ command, context })
  });
  
  const data = await response.json();
  console.log(`Status: ${data.status_code}`);
  console.log(`Response: ${data.response}`);
  return data;
}

// Usage
sendAICommand('What is the current warehouse capacity?', {
  capacity: '74.2%',
  total_skus: 1204
});
```

## Files

- `nexus_warehouse_agent.py` — Main FastAPI application
- `requirements_warehouse_agent.txt` — Python dependencies
- `start_warehouse.sh` — Startup script with .env loading
- `README_WAREHOUSE_AGENT.md` — This file
