# ──────────────────────────────────────────────────────────
# NEXUS AI Service — FastAPI on Python 3.11
# ──────────────────────────────────────────────────────────
FROM python:3.11-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

WORKDIR /app

# Install dependencies first (Docker layer cache)
COPY ai_service/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy application code
COPY ai_service/ ./

# Copy startup script
COPY ai_service/start.sh /app/start.sh
RUN chmod +x /app/start.sh

EXPOSE 8000

ENTRYPOINT ["/app/start.sh"]
