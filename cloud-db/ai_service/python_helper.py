"""
NEXUS AI Service — Unified FastAPI microservice for AI and Google OAuth.
"""
from __future__ import annotations

import json
import os
import time
from datetime import datetime, timezone, timedelta
from typing import Any, Optional

import httpx
import psycopg
from dotenv import load_dotenv
from fastapi import FastAPI, HTTPException, Request, Body
from fastapi.middleware.cors import CORSMiddleware
from google.oauth2 import id_token
from google.auth.transport import requests
from jose import jwt
from openai import OpenAI
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
DB_USER = os.getenv("POSTGRES_USER", "nexus_app")
DB_PASS = os.getenv("POSTGRES_PASSWORD", "postgres")
DB_NAME = os.getenv("POSTGRES_DB", "nexus_db")
DB_HOST = os.getenv("POSTGRES_HOST", "postgres")
DB_PORT = os.getenv("POSTGRES_PORT", "5432")

DB_DSN = os.getenv("POSTGRES_CONNECTION_STRING")
if not DB_DSN:
    DB_DSN = f"postgresql://{DB_USER}:{DB_PASS}@{DB_HOST}:{DB_PORT}/{DB_NAME}"

# OpenRouter AI
OPENROUTER_API_KEY = os.getenv("OPENROUTER_API_KEY", "")
MODEL_NAME = "openrouter/free"

# Thresholds
LOW_STOCK_THRESHOLD = 5
OVERDUE_DAYS = 14

# Initialize OpenRouter SDK client
or_client = None
if OPENROUTER_API_KEY:
    or_client = OpenAI(
        api_key=OPENROUTER_API_KEY,
        base_url="https://openrouter.ai/api/v1",
    )
    print("[ai-service] OpenRouter SDK initialized")

app = FastAPI(title="NEXUS AI Service", version="4.1.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

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
            cur.execute(
                "SELECT id, username, role, full_name, email FROM users WHERE email = %s OR google_id = %s",
                (email, google_id)
            )
            user = cur.fetchone()
            
            if user:
                cur.execute(
                    "UPDATE users SET google_id = %s, last_login = NOW() WHERE id = %s",
                    (google_id, user[0])
                )
                conn.commit()
                return {
                    "id": user[0], "username": user[1], "role": user[2], "full_name": user[3], "email": user[4]
                }
            else:
                username = email.split('@')[0]
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
                    "id": new_user[0], "username": new_user[1], "role": new_user[2], "full_name": new_user[3], "email": new_user[4]
                }
    except Exception as e:
        print(f"[db-error] Error in find_or_create_user: {e}")
        conn.rollback()
        return None
    finally:
        conn.close()

# ============================================================================
# AUTHENTICATION ENDPOINT
# ============================================================================

class GoogleVerifyRequest(BaseModel):
    token: str

@app.post("/api/auth/google/verify")
async def verify_google_token(request_data: GoogleVerifyRequest):
    if not GOOGLE_CLIENT_ID:
        raise HTTPException(status_code=500, detail="Google Client ID not configured")

    try:
        idinfo = id_token.verify_oauth2_token(request_data.token, requests.Request(), GOOGLE_CLIENT_ID)
        google_id, email, name = idinfo['sub'], idinfo['email'], idinfo.get('name', idinfo['email'].split('@')[0])

        user = find_or_create_user(email, name, google_id)
        if not user:
            raise HTTPException(status_code=500, detail="Failed to sync user to database")

        expires = datetime.now(timezone.utc) + timedelta(seconds=JWT_EXPIRATION_SECONDS)
        payload = {"sub": str(user["id"]), "username": user["username"], "role": user["role"], "exp": expires}
        token = jwt.encode(payload, JWT_SECRET, algorithm=JWT_ALGORITHM)

        return {"status": "success", "token": token, "user": user}
    except Exception as e:
        raise HTTPException(status_code=401, detail=f"Authentication error: {str(e)}")

@app.get("/api/ping")
async def ping():
    return {"status": "pong"}

# ============================================================================
# AI REQUEST / RESPONSE SCHEMAS
# ============================================================================

class AlertItem(BaseModel):
    type: str
    icon: str
    message: str
    priority: str = "MEDIUM"

class LegacyChatRequest(BaseModel):
    message: str = ""
    items: list[dict[str, Any]] = []
    issues: list[dict[str, Any]] = []
    counts: dict[str, Any] = {}
    users: list[dict[str, Any]] = []

class LegacyChatResponse(BaseModel):
    reply: str

class ChatWithContextRequest(BaseModel):
    message: str = ""
    context: str = ""

class LegacyReportRequest(BaseModel):
    items: list[dict[str, Any]] = []
    issues: list[dict[str, Any]] = []
    counts: dict[str, Any] = {}
    users: list[dict[str, Any]] = []

class LegacyReportResponse(BaseModel):
    status: str
    model: str
    generated_at: str
    report: str

# ============================================================================
# DATA PROCESSING LAYER
# ============================================================================

def _detect_low_stock(items: list[dict]) -> list[dict[str, Any]]:
    results = []
    for item in items:
        q, mq = int(item.get("quantity", 0) or 0), int(item.get("min_quantity", 0) or 0)
        if q < LOW_STOCK_THRESHOLD or (mq > 0 and q <= mq):
            results.append({"name": item.get("name", "?"), "sku": item.get("sku", "?"), "quantity": q, "min_quantity": mq})
    return results

def _detect_damaged(items: list[dict]) -> list[dict[str, Any]]:
    return [{"name": i.get("name", "?"), "sku": i.get("sku", "?"), "location": i.get("location", "Unknown")} 
            for i in items if (i.get("condition") or "").strip() == "Damaged"]

def _detect_overdue(issues: list[dict]) -> list[dict[str, Any]]:
    results = []
    now = datetime.now(timezone.utc)
    for issue in issues:
        is_overdue = (issue.get("status") or "").upper() == "OVERDUE"
        if is_overdue:
            results.append({"issued_to": issue.get("issued_to") or "Unknown", "item_id": issue.get("item_id", "?")})
    return results

# ============================================================================
# AI ENDPOINTS
# ============================================================================

@app.post("/ai/chat", response_model=LegacyChatResponse)
def ai_chat(req: ChatWithContextRequest):
    system_prompt = "You are a friendly inventory management assistant. Use provided data only."
    user_content = f"Context: {req.context}\nUser: {req.message}"
    
    if not or_client:
        return LegacyChatResponse(reply="AI Service unavailable. Fallback: " + req.context[:100])
        
    try:
        response = or_client.chat.completions.create(
            model=MODEL_NAME,
            messages=[{"role": "system", "content": system_prompt}, {"role": "user", "content": user_content}],
            temperature=0.3,
            max_tokens=512,
        )
        return LegacyChatResponse(reply=response.choices[0].message.content.strip())
    except Exception as e:
        return LegacyChatResponse(reply=f"AI Error: {str(e)}")

@app.post("/ai/alerts", response_model=list[AlertItem])
def ai_alerts_legacy(req: LegacyChatRequest):
    low, dmg, ovr = _detect_low_stock(req.items), _detect_damaged(req.items), _detect_overdue(req.issues)
    alerts = []
    for it in low: alerts.append(AlertItem(type="low_stock", icon="inventory_2", message=f"Low stock: {it['name']}"))
    for it in dmg: alerts.append(AlertItem(type="damaged", icon="build", message=f"Damaged: {it['name']}"))
    if not alerts: alerts.append(AlertItem(type="info", icon="info", message="Nominal."))
    return alerts

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
