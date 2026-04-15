#!/usr/bin/env python3
import argparse
import sqlite3
import sys

try:
    import psycopg  # type: ignore

    DRIVER = "psycopg"
except ImportError:
    try:
        import psycopg2 as psycopg  # type: ignore

        DRIVER = "psycopg2"
    except ImportError:
        print("Install psycopg or psycopg2 to run this migration script.", file=sys.stderr)
        sys.exit(2)


TABLE_ORDER = [
    "users",
    "categories",
    "items",
    "transactions",
    "issue_records",
    "audit_log",
    "app_settings",
    "backup_history",
]


def connect_postgres(dsn: str):
    conn = psycopg.connect(dsn)
    conn.autocommit = False
    return conn


def fetch_sqlite_rows(sqlite_conn: sqlite3.Connection, table: str):
    cursor = sqlite_conn.execute(f"SELECT * FROM {table}")
    columns = [description[0] for description in cursor.description]
    rows = cursor.fetchall()
    return columns, rows


def truncate_postgres(pg_conn):
    with pg_conn.cursor() as cur:
        cur.execute(
            "TRUNCATE TABLE backup_history, app_settings, audit_log, issue_records, "
            "transactions, items, categories, users RESTART IDENTITY CASCADE"
        )


def insert_rows(pg_conn, table: str, columns, rows):
    if not rows:
        return

    placeholders = ", ".join(["%s"] * len(columns))
    column_list = ", ".join(columns)
    sql = f"INSERT INTO {table} ({column_list}) VALUES ({placeholders})"

    with pg_conn.cursor() as cur:
        if DRIVER == "psycopg":
            cur.executemany(sql, rows)
        else:
            cur.executemany(sql, rows)


def reset_sequences(pg_conn):
    tables = ["users", "categories", "items", "transactions", "issue_records", "audit_log", "backup_history"]
    with pg_conn.cursor() as cur:
        for table in tables:
            cur.execute(
                "SELECT setval(pg_get_serial_sequence(%s, 'id'), "
                "COALESCE((SELECT MAX(id) FROM " + table + "), 1), "
                "COALESCE((SELECT MAX(id) IS NOT NULL FROM " + table + "), false))",
                (table,),
            )


def validate_counts(sqlite_conn: sqlite3.Connection, pg_conn):
    mismatches = []
    with pg_conn.cursor() as cur:
        for table in TABLE_ORDER:
            sqlite_count = sqlite_conn.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]
            cur.execute(f"SELECT COUNT(*) FROM {table}")
            pg_count = cur.fetchone()[0]
            if sqlite_count != pg_count:
                mismatches.append((table, sqlite_count, pg_count))
            print(f"{table}: sqlite={sqlite_count} postgres={pg_count}")
    return mismatches


def main():
    parser = argparse.ArgumentParser(description="Migrate NEXUS SQLite data into PostgreSQL")
    parser.add_argument("--sqlite", required=True, help="Path to nexus_data.db")
    parser.add_argument("--postgres", required=True, help="PostgreSQL connection string")
    args = parser.parse_args()

    sqlite_conn = sqlite3.connect(args.sqlite)
    sqlite_conn.row_factory = sqlite3.Row
    pg_conn = connect_postgres(args.postgres)

    try:
        truncate_postgres(pg_conn)
        for table in TABLE_ORDER:
            columns, rows = fetch_sqlite_rows(sqlite_conn, table)
            insert_rows(pg_conn, table, columns, rows)
        reset_sequences(pg_conn)
        mismatches = validate_counts(sqlite_conn, pg_conn)
        if mismatches:
            pg_conn.rollback()
            for table, sqlite_count, pg_count in mismatches:
                print(f"Count mismatch for {table}: sqlite={sqlite_count} postgres={pg_count}", file=sys.stderr)
            sys.exit(1)
        pg_conn.commit()
        print("Migration completed successfully.")
    except Exception:
        pg_conn.rollback()
        raise
    finally:
        pg_conn.close()
        sqlite_conn.close()


if __name__ == "__main__":
    main()
