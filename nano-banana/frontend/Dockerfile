# ──────────────────────────────────────────────────────────
# NEXUS Frontend — Nginx serving static HTML/CSS/JS
# (Optional: the C backend already serves these files.
#  This service is useful if you want the frontend
#  on a separate port or behind a reverse proxy.)
# ──────────────────────────────────────────────────────────
FROM nginx:alpine

# Remove default nginx config
RUN rm /etc/nginx/conf.d/default.conf

COPY frontend/nginx.conf /etc/nginx/conf.d/default.conf

# Copy static files
COPY frontend/ /usr/share/nginx/html/

EXPOSE 3000
