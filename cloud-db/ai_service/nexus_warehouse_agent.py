"""
NEXUS Warehouse Agent — FastAPI Backend with Google OAuth 2.0 and AI Integration.
"""
from __future__ import annotations

import json
import os
import time
from datetime import datetime, timezone, timedelta
from pathlib import Path
from typing import Any, Optional

import httpx
import psycopg
from dotenv import load_dotenv
from fastapi import FastAPI, HTTPException, Request, Body
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import HTMLResponse, JSONResponse
from google.oauth2 import id_token
from google.auth.transport import requests
from jose import jwt
from pydantic import BaseModel

# ============================================================================
# CONFIGURATION
# ============================================================================

load_dotenv()
load_dotenv("/app/.env")

def get_clean_env(name, default=""):
    val = os.getenv(name, default)
    if val:
        return val.strip(' "\'')
    return default

# Google OAuth
GOOGLE_CLIENT_ID = get_clean_env("GOOGLE_CLIENT_ID")
JWT_SECRET = get_clean_env("JWT_SECRET", "change-this-secret-now-or-else")
JWT_ALGORITHM = "HS256"
JWT_EXPIRATION_SECONDS = int(os.getenv("JWT_EXPIRATION_SECONDS", "86400"))

# Database (Postgres)
# Use environment variables from docker-compose or .env
DB_USER = os.getenv("POSTGRES_USER", "nexus_app")
DB_PASS = os.getenv("POSTGRES_PASSWORD", "postgres")
DB_NAME = os.getenv("POSTGRES_DB", "nexus_db")
DB_HOST = os.getenv("POSTGRES_HOST", "postgres") # Service name in docker-compose
DB_PORT = os.getenv("POSTGRES_PORT", "5432")

# Construct connection string if not provided
DB_DSN = os.getenv("POSTGRES_CONNECTION_STRING")
if not DB_DSN:
    DB_DSN = f"postgresql://{DB_USER}:{DB_PASS}@{DB_HOST}:{DB_PORT}/{DB_NAME}"

# OpenRouter AI
OPENROUTER_API_KEY = os.getenv("OPENROUTER_API_KEY", "")
OPENROUTER_API_URL = "https://openrouter.ai/api/v1/chat/completions"
MODEL_NAME = "qwen/qwen-3-coder-32b-instruct"

# System prompt for context injection
SYSTEM_PROMPT = (
    "You are the NEXUS Warehouse Agent. Access the inventory database to answer "
    "questions about stock, SKUs (like NX-001), and warehouse capacity."
)

# Template folder path
TEMPLATE_DIR = Path(os.getenv(
    "NEXUS_TEMPLATE_DIR",
    str(Path(__file__).parent.parent / "new template")
))

# ============================================================================
# FASTAPI APP
# ============================================================================

app = FastAPI(title="NEXUS Warehouse Agent", version="1.1.0")

# ============================================================================
# MIDDLEWARE (CORS)
# ============================================================================
app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://localhost:8080", "http://127.0.0.1:8080", "*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

@app.get("/api/ping")
async def ping():
    return {"status": "pong"}

# ============================================================================
# DATABASE UTILITIES
# ============================================================================

def get_db_connection():
    try:
        conn = psycopg.connect(DB_DSN)
        return conn
    except Exception as e:
        print(f"[db-error] Failed to connect to database: {e}")
        return None

def find_or_create_user(email: str, name: str, google_id: str):
    conn = get_db_connection()
    if not conn:
        return None
    
    try:
        with conn.cursor() as cur:
            # Check if user exists by email or google_id
            cur.execute(
                "SELECT id, username, role, full_name, email FROM users WHERE email = %s OR google_id = %s",
                (email, google_id)
            )
            user = cur.fetchone()
            
            if user:
                # Update existing user with google_id if missing
                cur.execute(
                    "UPDATE users SET google_id = %s, last_login = NOW() WHERE id = %s",
                    (google_id, user[0])
                )
                conn.commit()
                return {
                    "id": user[0],
                    "username": user[1],
                    "role": user[2],
                    "full_name": user[3],
                    "email": user[4]
                }
            else:
                # Create new user
                # Derive username from email
                username = email.split('@')[0]
                # Check for username collision
                cur.execute("SELECT id FROM users WHERE username = %s", (username,))
                if cur.fetchone():
                    username = f"{username}_{int(time.time()) % 1000}"
                
                cur.execute(
                    """INSERT INTO users (username, password_hash, role, full_name, email, google_id, oauth_provider, last_login)
                       VALUES (%s, 'oauth_placeholder', 'viewer', %s, %s, %s, 'google', NOW())
                       RETURNING id, username, role, full_name, email""",
                    (username, name, email, google_id)
                )
                new_user = cur.fetchone()
                conn.commit()
                return {
                    "id": new_user[0],
                    "username": new_user[1],
                    "role": new_user[2],
                    "full_name": new_user[3],
                    "email": new_user[4]
                }
    except Exception as e:
        print(f"[db-error] Error in find_or_create_user: {e}")
        conn.rollback()
        return None
    finally:
        conn.close()

# ============================================================================
# AUTHENTICATION
# ============================================================================

class GoogleVerifyRequest(BaseModel):
    token: str

@app.post("/api/auth/google/verify")
async def verify_google_token(request_data: GoogleVerifyRequest):
    """
    Verifies the Google ID token sent from the frontend.
    If valid, creates/updates the user in the database and returns a JWT.
    """
    if not GOOGLE_CLIENT_ID:
        raise HTTPException(status_code=500, detail="Google Client ID not configured")

    try:
        # Verify the ID token
        idinfo = id_token.verify_oauth2_token(
            request_data.token, 
            requests.Request(), 
            GOOGLE_CLIENT_ID
        )

        # Token is valid, extract info
        google_id = idinfo['sub']
        email = idinfo['email']
        name = idinfo.get('name', email.split('@')[0])

        # Find or create user in DB
        user = find_or_create_user(email, name, google_id)
        if not user:
            raise HTTPException(status_code=500, detail="Failed to sync user to database")

        # Create JWT for frontend session
        expires = datetime.now(timezone.utc) + timedelta(seconds=JWT_EXPIRATION_SECONDS)
        payload = {
            "sub": str(user["id"]),
            "username": user["username"],
            "role": user["role"],
            "exp": expires
        }
        token = jwt.encode(payload, JWT_SECRET, algorithm=JWT_ALGORITHM)

        return {
            "status": "success",
            "token": token,
            "user": {
                "id": user["id"],
                "username": user["username"],
                "role": user["role"],
                "full_name": user["full_name"],
                "email": user["email"]
            }
        }

    except ValueError as e:
        # Invalid token
        raise HTTPException(status_code=401, detail=f"Invalid Google token: {str(e)}")
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Authentication error: {str(e)}")

# ============================================================================
# AI REQUEST/RESPONSE SCHEMAS
# ============================================================================

class AICommandRequest(BaseModel):
    command: str
    context: dict[str, Any] | None = None


class AICommandResponse(BaseModel):
    status_code: str
    response: str
    timestamp: str


# ============================================================================
# AI CORE LOGIC
# ============================================================================

def _detect_status_code(response_text: str) -> str:
    response_lower = response_text.lower()
    critical_keywords = ["out of stock", "critical", "urgent", "emergency", "zero stock"]
    warning_keywords = ["low stock", "warning", "attention", "monitor", "running low"]

    for keyword in critical_keywords:
        if keyword in response_lower: return "[CRITICAL]"
    for keyword in warning_keywords:
        if keyword in response_lower: return "[WARNING]"
    return "[OK]"

async def call_openrouter(command: str, context: dict[str, Any] | None = None) -> str:
    if not OPENROUTER_API_KEY:
        raise HTTPException(status_code=503, detail="OpenRouter API key not configured")

    user_content = command
    if context:
        user_content = f"Inventory Context:\n{json.dumps(context, indent=2)}\n\nQuery: {command}"

    headers = {
        "Authorization": f"Bearer {OPENROUTER_API_KEY}",
        "Content-Type": "application/json",
        "HTTP-Referer": "http://localhost:8000",
        "X-Title": "NEXUS Warehouse Agent"
    }

    payload = {
        "model": MODEL_NAME,
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": user_content}
        ],
        "temperature": 0.3,
        "max_tokens": 1024
    }

    async with httpx.AsyncClient(timeout=60.0) as client:
        try:
            response = await client.post(OPENROUTER_API_URL, headers=headers, json=payload)
            if response.status_code != 200:
                raise HTTPException(status_code=response.status_code, detail=f"AI Error: {response.text}")
            
            data = response.json()
            return data.get("choices", [{}])[0].get("message", {}).get("content", "").strip()
        except Exception as e:
            raise HTTPException(status_code=503, detail=f"AI Service unavailable: {str(e)}")

# ============================================================================
# ROUTES
# ============================================================================

@app.get("/", response_class=HTMLResponse)
async def serve_dashboard():
    possible_files = ["code.html", "index.html", "dashboard.html"]
    for filename in possible_files:
        html_path = TEMPLATE_DIR / filename
        if html_path.exists():
            return html_path.read_text(encoding="utf-8")
    raise HTTPException(status_code=404, detail="Dashboard template not found")

@app.post("/ai/command", response_model=AICommandResponse)
async def handle_ai_command(request: AICommandRequest):
    ai_response = await call_openrouter(command=request.command, context=request.context)
    return AICommandResponse(
        status_code=_detect_status_code(ai_response),
        response=ai_response,
        timestamp=datetime.now(timezone.utc).isoformat()
    )

@app.get("/health")
async def health_check():
    return {"status": "healthy", "timestamp": datetime.now(timezone.utc).isoformat()}

# ============================================================================
# ENTRY POINT
# ============================================================================

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
