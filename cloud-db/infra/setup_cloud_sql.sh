#!/usr/bin/env bash
set -euo pipefail

ENVIRONMENT="${1:-dev}"
PROJECT_ID="${PROJECT_ID:?Set PROJECT_ID before running this script}"
REGION="${REGION:-us-central1}"
ZONE="${ZONE:-us-central1-a}"
INSTANCE_NAME="${INSTANCE_NAME:-nexus-db-prod}"
DATABASE_NAME="${DATABASE_NAME:-nexus_db}"
DB_USER="${DB_USER:-nexus_app}"
DB_PASSWORD="${DB_PASSWORD:?Set DB_PASSWORD before running this script}"

if [[ "${ENVIRONMENT}" != "dev" && "${ENVIRONMENT}" != "prod" ]]; then
  echo "Usage: PROJECT_ID=... DB_PASSWORD=... $0 [dev|prod]" >&2
  exit 1
fi

if [[ "${ENVIRONMENT}" == "prod" ]]; then
  TIER="db-n1-standard-2"
  AVAILABILITY="REGIONAL"
else
  TIER="db-f1-micro"
  AVAILABILITY="ZONAL"
fi

echo "Project:      ${PROJECT_ID}"
echo "Environment:  ${ENVIRONMENT}"
echo "Instance:     ${INSTANCE_NAME}"
echo "Region:       ${REGION}"
echo "Tier:         ${TIER}"
echo "Availability: ${AVAILABILITY}"

gcloud config set project "${PROJECT_ID}" >/dev/null
gcloud services enable sqladmin.googleapis.com servicenetworking.googleapis.com

if ! gcloud sql instances describe "${INSTANCE_NAME}" >/dev/null 2>&1; then
  gcloud sql instances create "${INSTANCE_NAME}" \
    --database-version=POSTGRES_15 \
    --tier="${TIER}" \
    --region="${REGION}" \
    --availability-type="${AVAILABILITY}" \
    --storage-type=SSD \
    --storage-size=10 \
    --storage-auto-increase \
    --backup-start-time=02:00 \
    --retained-backups-count=7 \
    --enable-point-in-time-recovery \
    --maintenance-window-day=7 \
    --maintenance-window-hour=3 \
    --database-flags=cloudsql.iam_authentication=off \
    --labels=service=nexus-db,environment="${ENVIRONMENT}",managed-by=gcloud
fi

if ! gcloud sql databases describe "${DATABASE_NAME}" --instance="${INSTANCE_NAME}" >/dev/null 2>&1; then
  gcloud sql databases create "${DATABASE_NAME}" --instance="${INSTANCE_NAME}"
fi

if ! gcloud sql users describe "${DB_USER}" --instance="${INSTANCE_NAME}" >/dev/null 2>&1; then
  gcloud sql users create "${DB_USER}" --instance="${INSTANCE_NAME}" --password="${DB_PASSWORD}"
else
  gcloud sql users set-password "${DB_USER}" --instance="${INSTANCE_NAME}" --password="${DB_PASSWORD}"
fi

PRIMARY_IP="$(gcloud sql instances describe "${INSTANCE_NAME}" --format='value(ipAddresses[0].ipAddress)')"

echo
echo "Connection string:"
echo "postgresql://${DB_USER}:${DB_PASSWORD}@${PRIMARY_IP}:5432/${DATABASE_NAME}"
echo
echo "Cloud SQL Proxy / Auth Proxy format:"
echo "postgresql://${DB_USER}:${DB_PASSWORD}@127.0.0.1:5432/${DATABASE_NAME}?host=/cloudsql/${PROJECT_ID}:${REGION}:${INSTANCE_NAME}"
