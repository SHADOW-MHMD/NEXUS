# NEXUS SQLite to Cloud SQL PostgreSQL Migration Guide

## 1. Back Up the Existing SQLite Database

Create a point-in-time copy of the current SQLite database file before any migration work:

```bash
cp /app/data/nexus_data.db /app/data/nexus_data.db.bak.$(date +%Y%m%d%H%M%S)
```

If WAL mode is still active, copy the sidecar files as well:

```bash
cp /app/data/nexus_data.db-wal /app/data/nexus_data.db-wal.bak.$(date +%Y%m%d%H%M%S) 2>/dev/null || true
cp /app/data/nexus_data.db-shm /app/data/nexus_data.db-shm.bak.$(date +%Y%m%d%H%M%S) 2>/dev/null || true
```

## 2. Create the Cloud SQL Instance

### gcloud CLI

```bash
export PROJECT_ID="your-gcp-project"
export DB_PASSWORD="replace-with-strong-password"
./infra/setup_cloud_sql.sh dev
./infra/setup_cloud_sql.sh prod
```

The script uses:

- Instance name: `nexus-db-prod`
- PostgreSQL version: `POSTGRES_15`
- Dev tier: `db-f1-micro`
- Prod tier: `db-n1-standard-2`
- Automated backups retained: `7`
- HA: `REGIONAL` in prod, `ZONAL` in dev

### Terraform

```bash
cd infra
terraform init
terraform apply \
  -var="project_id=your-gcp-project" \
  -var="environment=prod" \
  -var="db_password=replace-with-strong-password"
```

Terraform outputs the application connection string in this format:

```text
postgresql://nexus_app:password@HOST:5432/nexus_db
```

## 3. Run the Data Migration

Install a PostgreSQL Python driver locally first:

```bash
pip install psycopg[binary]
```

Then run the migration:

```bash
python3 scripts/migrate_sqlite_to_postgres.py \
  --sqlite /app/data/nexus_data.db \
  --postgres "postgresql://nexus_app:password@HOST:5432/nexus_db"
```

The script:

- Reads all 8 tables from SQLite
- Inserts them into PostgreSQL in foreign-key-safe order
- Preserves IDs and timestamps
- Resets PostgreSQL sequences after insert
- Validates row counts and aborts on mismatch

## 4. Update the Application Configuration

Set the backend connection string:

```bash
export POSTGRES_CONNECTION_STRING="postgresql://nexus_app:password@HOST:5432/nexus_db"
```

Docker Compose already uses this format:

```text
postgresql://user:password@host:5432/nexus_db
```

Examples:

- Dev: `postgresql://nexus_app:postgres@postgres:5432/nexus_db`
- Staging: `postgresql://nexus_app:staging-password@10.20.0.15:5432/nexus_db`
- Prod direct: `postgresql://nexus_app:prod-password@34.72.10.25:5432/nexus_db`
- Prod via Cloud SQL Auth Proxy: `postgresql://nexus_app:prod-password@127.0.0.1:5432/nexus_db?host=/cloudsql/project:us-central1:nexus-db-prod`

## 5. Rollback Procedure

If validation fails or the application misbehaves after cutover:

1. Stop the backend deployment that points at PostgreSQL.
2. Restore the previous application configuration so the backend points back to SQLite.
3. Put the saved SQLite backup file back in place:

```bash
cp /app/data/nexus_data.db.bak.TIMESTAMP /app/data/nexus_data.db
```

4. If WAL sidecar backups were taken, restore those too.
5. Restart the original SQLite-backed application.

## 6. Verification Checklist

Run these checks after cutover:

```bash
make clean && make
docker compose up --build
python3 scripts/migrate_sqlite_to_postgres.py --sqlite /app/data/nexus_data.db --postgres "$POSTGRES_CONNECTION_STRING"
```

Confirm:

- `users`, `categories`, `items`, `transactions`, `issue_records`, `audit_log`, `backup_history`, and `app_settings` have matching row counts
- Admin seeding does not duplicate `username`
- CRUD requests against each API area still succeed
