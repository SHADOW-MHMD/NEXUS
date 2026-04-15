#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sqlite3.h>
#include <unistd.h>
#include "db.h"
#include "auth.h"

static const char* SCHEMA_SQL = 
    "PRAGMA journal_mode=WAL;"
    "PRAGMA foreign_keys=ON;"
    
    "CREATE TABLE IF NOT EXISTS users ("
    "    id          INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    username    TEXT UNIQUE NOT NULL,"
    "    password_hash TEXT NOT NULL,"
    "    role        TEXT NOT NULL DEFAULT 'viewer',"
    "    email       TEXT,"
    "    full_name   TEXT,"
    "    is_active   INTEGER NOT NULL DEFAULT 1,"
    "    created_at  TEXT NOT NULL DEFAULT (datetime('now')),"
    "    last_login  TEXT"
    ");"
    
    "CREATE TABLE IF NOT EXISTS categories ("
    "    id          INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    name        TEXT UNIQUE NOT NULL,"
    "    description TEXT,"
    "    created_at  TEXT NOT NULL DEFAULT (datetime('now'))"
    ");"
    
    "CREATE TABLE IF NOT EXISTS items ("
    "    id              INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    sku             TEXT UNIQUE NOT NULL,"
    "    name            TEXT NOT NULL,"
    "    description     TEXT,"
    "    condition       TEXT NOT NULL DEFAULT 'Working',"
    "    category_id     INTEGER REFERENCES categories(id) ON DELETE SET NULL,"
    "    quantity        INTEGER NOT NULL DEFAULT 0,"
    "    min_quantity    INTEGER NOT NULL DEFAULT 0,"
    "    unit_price      REAL NOT NULL DEFAULT 0.0,"
    "    supplier        TEXT,"
    "    location        TEXT,"
    "    is_active       INTEGER NOT NULL DEFAULT 1,"
    "    created_at      TEXT NOT NULL DEFAULT (datetime('now')),"
    "    updated_at      TEXT NOT NULL DEFAULT (datetime('now'))"
    ");"
    
    "CREATE TABLE IF NOT EXISTS transactions ("
    "    id              INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    item_id         INTEGER NOT NULL REFERENCES items(id) ON DELETE CASCADE,"
    "    user_id         INTEGER REFERENCES users(id) ON DELETE SET NULL,"
    "    type            TEXT NOT NULL,"
    "    quantity        INTEGER NOT NULL,"
    "    unit_price      REAL,"
    "    notes           TEXT,"
    "    reference       TEXT,"
    "    created_at      TEXT NOT NULL DEFAULT (datetime('now'))"
    ");"
    
    "CREATE TABLE IF NOT EXISTS issue_records ("
    "    id                  INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    item_id             INTEGER NOT NULL REFERENCES items(id) ON DELETE CASCADE,"
    "    issued_by           INTEGER REFERENCES users(id) ON DELETE SET NULL,"
    "    returned_by         INTEGER REFERENCES users(id) ON DELETE SET NULL,"
    "    quantity            INTEGER NOT NULL,"
    "    quantity_returned   INTEGER NOT NULL DEFAULT 0,"
    "    assignee            TEXT NOT NULL,"
    "    issued_to           TEXT,"
    "    purpose             TEXT,"
    "    reference           TEXT,"
    "    issue_date          TEXT NOT NULL DEFAULT (datetime('now')),"
    "    return_date         TEXT,"
    "    expected_return_date TEXT,"
    "    actual_return_date  TEXT,"
    "    unit_price          REAL NOT NULL DEFAULT 0.0,"
    "    status              TEXT NOT NULL DEFAULT 'ISSUED',"
    "    return_condition    TEXT,"
    "    return_notes        TEXT,"
    "    created_at          TEXT NOT NULL DEFAULT (datetime('now'))"
    ");"
    
    "CREATE TABLE IF NOT EXISTS audit_log ("
    "    id          INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    user_id     INTEGER REFERENCES users(id),"
    "    action      TEXT NOT NULL,"
    "    target      TEXT,"
    "    details     TEXT,"
    "    created_at  TEXT NOT NULL DEFAULT (datetime('now'))"
    ");"
    
    "CREATE TABLE IF NOT EXISTS backup_history ("
    "    id          INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    created_at  TEXT NOT NULL DEFAULT (datetime('now')),"
    "    size_kb     REAL NOT NULL DEFAULT 0,"
    "    provider    TEXT NOT NULL DEFAULT 'local',"
    "    url         TEXT,"
    "    status      TEXT NOT NULL DEFAULT 'OK',"
    "    notes       TEXT,"
    "    created_by  INTEGER REFERENCES users(id),"
    "    is_local    INTEGER NOT NULL DEFAULT 1,"
    "    local_path  TEXT"
    ");"
    
    "CREATE TABLE IF NOT EXISTS app_settings ("
    "    key         TEXT PRIMARY KEY,"
    "    value       TEXT NOT NULL,"
    "    updated_at  TEXT NOT NULL DEFAULT (datetime('now'))"
    ");"
    
    
    "CREATE INDEX IF NOT EXISTS idx_items_sku ON items(sku);"
    "CREATE INDEX IF NOT EXISTS idx_items_category ON items(category_id);"
    "CREATE INDEX IF NOT EXISTS idx_transactions_item ON transactions(item_id);"
    "CREATE INDEX IF NOT EXISTS idx_transactions_date ON transactions(created_at);"
    "CREATE INDEX IF NOT EXISTS idx_audit_date ON audit_log(created_at);"
    "CREATE INDEX IF NOT EXISTS idx_issues_item ON issue_records(item_id);"
    "CREATE INDEX IF NOT EXISTS idx_issues_status ON issue_records(status);"
    "CREATE INDEX IF NOT EXISTS idx_issues_issue_date ON issue_records(issue_date);";

static int column_exists(sqlite3* db, const char* table, const char* column) {
    char sql[128];
    sqlite3_stmt* stmt = NULL;
    int found = 0;

    snprintf(sql, sizeof(sql), "PRAGMA table_info(%s)", table);
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* existing = (const char*)sqlite3_column_text(stmt, 1);
        if (existing && strcmp(existing, column) == 0) {
            found = 1;
            break;
        }
    }

    sqlite3_finalize(stmt);
    return found;
}

static void ensure_column(sqlite3* db, const char* table, const char* column, const char* definition) {
    if (column_exists(db, table, column)) return;

    char sql[256];
    snprintf(sql, sizeof(sql), "ALTER TABLE %s ADD COLUMN %s %s", table, column, definition);
    sqlite3_exec(db, sql, NULL, NULL, NULL);
}

static void apply_schema_migrations(sqlite3* db) {
    // Keep older databases compatible by adding new columns in place.
    ensure_column(db, "items", "condition", "TEXT NOT NULL DEFAULT 'Working'");
    ensure_column(db, "issue_records", "issued_to", "TEXT");
    ensure_column(db, "issue_records", "issue_date", "TEXT");
    ensure_column(db, "issue_records", "return_date", "TEXT");

    sqlite3_exec(db,
        "UPDATE items SET condition='Working' WHERE condition IS NULL OR condition='';"
        "UPDATE issue_records SET issued_to=assignee WHERE issued_to IS NULL OR issued_to='';"
        "UPDATE issue_records SET issue_date=created_at WHERE issue_date IS NULL OR issue_date='';"
        "UPDATE issue_records SET return_date=actual_return_date WHERE return_date IS NULL AND actual_return_date IS NOT NULL;",
        NULL, NULL, NULL);
}

static const char* normalize_item_condition(const char* condition) {
    if (!condition || strlen(condition) == 0) return "Working";
    if (strcasecmp(condition, "Working") == 0) return "Working";
    if (strcasecmp(condition, "Damaged") == 0) return "Damaged";
    if (strcasecmp(condition, "Under Repair") == 0) return "Under Repair";
    return "Working";
}

static void load_item_from_stmt(sqlite3_stmt* stmt, Item* item) {
    memset(item, 0, sizeof(*item));
    item->id = sqlite3_column_int(stmt, 0);
    strncpy(item->sku, (const char*)sqlite3_column_text(stmt, 1) ?: "", sizeof(item->sku) - 1);
    strncpy(item->name, (const char*)sqlite3_column_text(stmt, 2) ?: "", sizeof(item->name) - 1);
    strncpy(item->description, (const char*)sqlite3_column_text(stmt, 3) ?: "", sizeof(item->description) - 1);
    strncpy(item->condition, (const char*)sqlite3_column_text(stmt, 4) ?: "Working", sizeof(item->condition) - 1);
    item->category_id = sqlite3_column_int(stmt, 5);
    item->quantity = sqlite3_column_int(stmt, 6);
    item->min_quantity = sqlite3_column_int(stmt, 7);
    item->unit_price = sqlite3_column_double(stmt, 8);
    strncpy(item->supplier, (const char*)sqlite3_column_text(stmt, 9) ?: "", sizeof(item->supplier) - 1);
    strncpy(item->location, (const char*)sqlite3_column_text(stmt, 10) ?: "", sizeof(item->location) - 1);
    item->is_active = sqlite3_column_int(stmt, 11);
    strncpy(item->created_at, (const char*)sqlite3_column_text(stmt, 12) ?: "", sizeof(item->created_at) - 1);
    strncpy(item->updated_at, (const char*)sqlite3_column_text(stmt, 13) ?: "", sizeof(item->updated_at) - 1);
    strncpy(item->category_name, (const char*)sqlite3_column_text(stmt, 14) ?: "", sizeof(item->category_name) - 1);
}

static void compute_issue_status(IssueRecord* issue) {
    if (strcmp(issue->status, "RETURNED") == 0) return;

    sqlite3* mem_db = NULL;
    sqlite3_stmt* stmt = NULL;
    int overdue = 0;

    if (sqlite3_open(":memory:", &mem_db) == SQLITE_OK &&
        sqlite3_prepare_v2(mem_db,
            "SELECT CASE WHEN julianday('now') - julianday(?) > 14 THEN 1 ELSE 0 END",
            -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, issue->issue_date, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            overdue = sqlite3_column_int(stmt, 0);
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(mem_db);

    if ((issue->return_date[0] == '\0') && overdue) {
        strncpy(issue->status, "OVERDUE", sizeof(issue->status) - 1);
    }
}

static void load_issue_from_stmt(sqlite3_stmt* stmt, IssueRecord* issue) {
    memset(issue, 0, sizeof(*issue));
    issue->id = sqlite3_column_int(stmt, 0);
    issue->item_id = sqlite3_column_int(stmt, 1);
    issue->issued_by = sqlite3_column_int(stmt, 2);
    issue->returned_by = sqlite3_column_int(stmt, 3);
    issue->quantity = sqlite3_column_int(stmt, 4);
    issue->quantity_returned = sqlite3_column_int(stmt, 5);
    strncpy(issue->assignee, (const char*)sqlite3_column_text(stmt, 6) ?: "", sizeof(issue->assignee) - 1);
    strncpy(issue->issued_to, (const char*)sqlite3_column_text(stmt, 7) ?: issue->assignee, sizeof(issue->issued_to) - 1);
    strncpy(issue->purpose, (const char*)sqlite3_column_text(stmt, 8) ?: "", sizeof(issue->purpose) - 1);
    strncpy(issue->reference, (const char*)sqlite3_column_text(stmt, 9) ?: "", sizeof(issue->reference) - 1);
    strncpy(issue->issue_date, (const char*)sqlite3_column_text(stmt, 10) ?: "", sizeof(issue->issue_date) - 1);
    strncpy(issue->return_date, (const char*)sqlite3_column_text(stmt, 11) ?: "", sizeof(issue->return_date) - 1);
    strncpy(issue->expected_return_date, (const char*)sqlite3_column_text(stmt, 12) ?: "", sizeof(issue->expected_return_date) - 1);
    strncpy(issue->actual_return_date, (const char*)sqlite3_column_text(stmt, 13) ?: "", sizeof(issue->actual_return_date) - 1);
    issue->unit_price = sqlite3_column_double(stmt, 14);
    strncpy(issue->status, (const char*)sqlite3_column_text(stmt, 15) ?: "ISSUED", sizeof(issue->status) - 1);
    strncpy(issue->return_condition, (const char*)sqlite3_column_text(stmt, 16) ?: "", sizeof(issue->return_condition) - 1);
    strncpy(issue->return_notes, (const char*)sqlite3_column_text(stmt, 17) ?: "", sizeof(issue->return_notes) - 1);
    strncpy(issue->created_at, (const char*)sqlite3_column_text(stmt, 18) ?: "", sizeof(issue->created_at) - 1);
    compute_issue_status(issue);
}

/**
 * Initialize database connection and create schema.
 */
sqlite3* db_init(const char* db_path) {
    sqlite3* db;
    int rc = sqlite3_open(db_path, &db);
    
    if (rc) {
        fprintf(stderr, "[db] Cannot open database: %s\n", sqlite3_errmsg(db));
        return NULL;
    }
    
    // Enable foreign keys
    sqlite3_exec(db, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL);
    
    return db;
}

void db_close(sqlite3* db) {
    if (db) {
        sqlite3_close(db);
    }
}

void db_create_schema(sqlite3* db) {
    char* err_msg = NULL;
    int rc = sqlite3_exec(db, SCHEMA_SQL, NULL, NULL, &err_msg);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[db] SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    }

    apply_schema_migrations(db);
}

void db_seed_data(sqlite3* db) {
    /* Read admin credentials from environment.
     * Primary: NEXUS_INIT_* (Docker/legacy)
     * Fallback: APP_USERNAME / APP_PASSWORD (.env convention) */
    const char* admin_username = getenv("NEXUS_INIT_USERNAME");
    const char* admin_password  = getenv("NEXUS_INIT_PASSWORD");
    const char* admin_full_name = getenv("NEXUS_INIT_FULLNAME");
    const char* admin_email     = getenv("NEXUS_INIT_EMAIL");

    /* Fallback to APP_USERNAME / APP_PASSWORD if NEXUS_INIT_ vars are not set */
    if (!admin_username || strlen(admin_username) == 0) {
        admin_username = getenv("APP_USERNAME");
    }
    if (!admin_password || strlen(admin_password) == 0) {
        admin_password = getenv("APP_PASSWORD");
    }

    sqlite3_stmt* stmt = NULL;
    int item_count = 0;

    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM items", -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            item_count = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);

    if (item_count == 0) {
        sqlite3_exec(db,
            "INSERT INTO items (sku, name, description, condition, quantity, min_quantity, unit_price, supplier, location) VALUES "
            "('NB-001', 'Raspberry Pi 4', 'Primary control board for edge deployments', 'Working', 8, 3, 75.00, 'PiShop', 'Shelf A1'),"
            "('NB-002', 'USB Barcode Scanner', 'Handheld scanner for stock intake', 'Damaged', 4, 2, 28.50, 'ScanTech', 'Drawer B2'),"
            "('NB-003', 'Ethernet Cable 5m', 'Cat6 cable for workstation and router links', 'Working', 15, 5, 6.99, 'CableHub', 'Bin C3'),"
            "('NB-004', 'Thermal Label Roll', 'Consumable labels for item tagging', 'Under Repair', 12, 4, 9.25, 'LabelWorks', 'Shelf D1'),"
            "('NB-005', 'Power Adapter 12V', 'Replacement adapter for field devices', 'Working', 6, 2, 18.75, 'VoltSupply', 'Rack E4');",
            NULL, NULL, NULL);
    }

    if (!admin_username || strlen(admin_username) == 0 ||
        !admin_password || strlen(admin_password) == 0) {
        fprintf(stderr,
            "[auth] No admin credentials set in environment.\n"
            "         Please configure APP_USERNAME/APP_PASSWORD in .env or\n"
            "         set NEXUS_INIT_USERNAME/NEXUS_INIT_PASSWORD.\n"
            "         No default admin user will be created.\n");
        return;
    }

    fprintf(stderr, "[auth] Seeding admin user: %s\n", admin_username);

    if (!admin_full_name || strlen(admin_full_name) == 0) admin_full_name = admin_username;
    if (!admin_email || strlen(admin_email) == 0) admin_email = "";

    // Create admin user with hashed password
    char* admin_hash = auth_hash_password(admin_password);
    
    char seed_sql[4096];
    snprintf(seed_sql, sizeof(seed_sql),
        "INSERT OR IGNORE INTO users (username, password_hash, role, full_name, email) "
        "VALUES ('%s', '%s', 'admin', '%s', '%s');",
        admin_username, admin_hash, admin_full_name, admin_email);
    
    sqlite3_exec(db, seed_sql, NULL, NULL, NULL);
    free(admin_hash);
}

// ============================================================================
// USER OPERATIONS
// ============================================================================


User* db_authenticate(sqlite3* db, const char* username, const char* password) {
    const char* sql = "SELECT * FROM users WHERE username=? AND is_active=1";
    sqlite3_stmt* stmt;
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return NULL;
    
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    
    User* user = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user = malloc(sizeof(User));
        user->id = sqlite3_column_int(stmt, 0);
        strncpy(user->username, (const char*)sqlite3_column_text(stmt, 1), 127);
        
        const char* stored_hash = (const char*)sqlite3_column_text(stmt, 2);
        
        // Verify password
        if (auth_verify_password(password, stored_hash)) {
            strncpy(user->role, (const char*)sqlite3_column_text(stmt, 3), 31);
            strncpy(user->email, (const char*)sqlite3_column_text(stmt, 4) ?: "", 255);
            strncpy(user->full_name, (const char*)sqlite3_column_text(stmt, 5) ?: "", 255);
            user->is_active = sqlite3_column_int(stmt, 6);
            strncpy(user->created_at, (const char*)sqlite3_column_text(stmt, 7), 31);
            strncpy(user->last_login, (const char*)sqlite3_column_text(stmt, 8) ?: "", 31);
            
            // Update last_login
            const char* update_sql = "UPDATE users SET last_login=datetime('now') WHERE id=?";
            sqlite3_stmt* update_stmt;
            sqlite3_prepare_v2(db, update_sql, -1, &update_stmt, NULL);
            sqlite3_bind_int(update_stmt, 1, user->id);
            sqlite3_step(update_stmt);
            sqlite3_finalize(update_stmt);
        } else {
            free(user);
            user = NULL;
        }
    }
    
    sqlite3_finalize(stmt);
    return user;
}

User* db_get_user(sqlite3* db, int user_id) {
    const char* sql = "SELECT * FROM users WHERE id=?";
    sqlite3_stmt* stmt;
    
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, user_id);
    
    User* user = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user = malloc(sizeof(User));
        user->id = sqlite3_column_int(stmt, 0);
        strncpy(user->username, (const char*)sqlite3_column_text(stmt, 1), 127);
        strncpy(user->role, (const char*)sqlite3_column_text(stmt, 3), 31);
        strncpy(user->email, (const char*)sqlite3_column_text(stmt, 4) ?: "", 255);
        strncpy(user->full_name, (const char*)sqlite3_column_text(stmt, 5) ?: "", 255);
        user->is_active = sqlite3_column_int(stmt, 6);
        strncpy(user->created_at, (const char*)sqlite3_column_text(stmt, 7), 31);
        strncpy(user->last_login, (const char*)sqlite3_column_text(stmt, 8) ?: "", 31);
    }
    
    sqlite3_finalize(stmt);
    return user;
}

User* db_get_user_by_username(sqlite3* db, const char* username) {
    const char* sql = "SELECT * FROM users WHERE username=?";
    sqlite3_stmt* stmt;
    
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    
    User* user = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user = malloc(sizeof(User));
        user->id = sqlite3_column_int(stmt, 0);
        strncpy(user->username, (const char*)sqlite3_column_text(stmt, 1), 127);
        strncpy(user->role, (const char*)sqlite3_column_text(stmt, 3), 31);
        strncpy(user->email, (const char*)sqlite3_column_text(stmt, 4) ?: "", 255);
        strncpy(user->full_name, (const char*)sqlite3_column_text(stmt, 5) ?: "", 255);
        user->is_active = sqlite3_column_int(stmt, 6);
    }
    
    sqlite3_finalize(stmt);
    return user;
}

User** db_get_all_users(sqlite3* db, int* count) {
    const char* sql = "SELECT * FROM users WHERE is_active=1 ORDER BY id";
    sqlite3_stmt* stmt;
    
    *count = 0;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    // Count rows first
    int estimated = 10;
    User** users = malloc(estimated * sizeof(User*));
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (*count >= estimated) {
            estimated *= 2;
            users = realloc(users, estimated * sizeof(User*));
        }
        
        User* user = malloc(sizeof(User));
        user->id = sqlite3_column_int(stmt, 0);
        strncpy(user->username, (const char*)sqlite3_column_text(stmt, 1), 127);
        strncpy(user->role, (const char*)sqlite3_column_text(stmt, 3), 31);
        strncpy(user->email, (const char*)sqlite3_column_text(stmt, 4) ?: "", 255);
        strncpy(user->full_name, (const char*)sqlite3_column_text(stmt, 5) ?: "", 255);
        user->is_active = sqlite3_column_int(stmt, 6);
        
        users[(*count)++] = user;
    }
    
    sqlite3_finalize(stmt);
    return users;
}

int db_create_user(sqlite3* db, const char* username, const char* password,
                   const char* role, const char* full_name, const char* email) {
    char* hash = auth_hash_password(password);
    const char* sql = "INSERT INTO users (username, password_hash, role, full_name, email) "
                      "VALUES (?,?,?,?,?)";
    
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, role, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, full_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, email, -1, SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    free(hash);
    
    return rc == SQLITE_DONE ? 1 : 0;
}

int db_update_user(sqlite3* db, int user_id, const char* role,
                   const char* full_name, const char* email, int is_active) {
    const char* sql = "UPDATE users SET role=?, full_name=?, email=?, is_active=? WHERE id=?";
    sqlite3_stmt* stmt;
    
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, role, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, full_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, email, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, is_active);
    sqlite3_bind_int(stmt, 5, user_id);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE ? 1 : 0;
}

int db_change_password(sqlite3* db, int user_id, const char* new_password) {
    char* hash = auth_hash_password(new_password);
    const char* sql = "UPDATE users SET password_hash=? WHERE id=?";
    
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, user_id);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    free(hash);
    
    return rc == SQLITE_DONE ? 1 : 0;
}

int db_delete_user(sqlite3* db, int user_id) {
    const char* sql = "UPDATE users SET is_active=0 WHERE id=?";
    sqlite3_stmt* stmt;
    
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, user_id);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE ? 1 : 0;
}

// ============================================================================
// CATEGORY OPERATIONS
// ============================================================================

Category** db_get_categories(sqlite3* db, int* count) {
    const char* sql = "SELECT * FROM categories ORDER BY name";
    sqlite3_stmt* stmt;
    
    *count = 0;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    int estimated = 10;
    Category** categories = malloc(estimated * sizeof(Category*));
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (*count >= estimated) {
            estimated *= 2;
            categories = realloc(categories, estimated * sizeof(Category*));
        }
        
        Category* cat = malloc(sizeof(Category));
        cat->id = sqlite3_column_int(stmt, 0);
        strncpy(cat->name, (const char*)sqlite3_column_text(stmt, 1), 255);
        strncpy(cat->description, (const char*)sqlite3_column_text(stmt, 2) ?: "", 511);
        strncpy(cat->created_at, (const char*)sqlite3_column_text(stmt, 3), 31);
        
        categories[(*count)++] = cat;
    }
    
    sqlite3_finalize(stmt);
    return categories;
}

int db_create_category(sqlite3* db, const char* name, const char* description) {
    const char* sql = "INSERT INTO categories (name, description) VALUES (?,?)";
    sqlite3_stmt* stmt;
    
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, description, -1, SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE ? 1 : 0;
}

// ============================================================================
// ITEM OPERATIONS
// ============================================================================

Item** db_get_items(sqlite3* db, const char* search, int category_id, int low_stock, int* count) {
    char sql[1024] = "SELECT i.id, i.sku, i.name, i.description, i.condition, i.category_id, i.quantity, "
                     "i.min_quantity, i.unit_price, i.supplier, i.location, i.is_active, i.created_at, "
                     "i.updated_at, c.name as category_name FROM items i "
                     "LEFT JOIN categories c ON i.category_id = c.id WHERE i.is_active=1";
    
    if (search && strlen(search) > 0) {
        strcat(sql, " AND (i.name LIKE '%");
        strcat(sql, search);
        strcat(sql, "%' OR i.sku LIKE '%");
        strcat(sql, search);
        strcat(sql, "%')");
    }
    
    if (category_id > 0) {
        char cat_clause[64];
        snprintf(cat_clause, sizeof(cat_clause), " AND i.category_id=%d", category_id);
        strcat(sql, cat_clause);
    }
    
    if (low_stock) {
        strcat(sql, " AND i.quantity <= i.min_quantity");
    }
    
    strcat(sql, " ORDER BY i.name");
    
    sqlite3_stmt* stmt;
    *count = 0;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    int estimated = 20;
    Item** items = malloc(estimated * sizeof(Item*));
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (*count >= estimated) {
            estimated *= 2;
            items = realloc(items, estimated * sizeof(Item*));
        }
        
        Item* item = malloc(sizeof(Item));
        load_item_from_stmt(stmt, item);
        items[(*count)++] = item;
    }
    
    sqlite3_finalize(stmt);
    return items;
}

Item* db_get_item(sqlite3* db, int item_id) {
    const char* sql = "SELECT i.id, i.sku, i.name, i.description, i.condition, i.category_id, i.quantity, "
                      "i.min_quantity, i.unit_price, i.supplier, i.location, i.is_active, i.created_at, "
                      "i.updated_at, c.name as category_name FROM items i "
                      "LEFT JOIN categories c ON i.category_id = c.id WHERE i.id=?";
    sqlite3_stmt* stmt;
    
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, item_id);
    
    Item* item = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        item = malloc(sizeof(Item));
        load_item_from_stmt(stmt, item);
    }
    
    sqlite3_finalize(stmt);
    return item;
}

int db_create_item(sqlite3* db, const char* sku, const char* name, const char* description,
                   int category_id, int quantity, int min_quantity, double unit_price,
                   const char* supplier, const char* location, const char* condition) {
    const char* sql = "INSERT INTO items (sku, name, description, condition, category_id, quantity, "
                      "min_quantity, unit_price, supplier, location) VALUES (?,?,?,?,?,?,?,?,?,?)";
    
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, sku, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, description, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, normalize_item_condition(condition), -1, SQLITE_TRANSIENT);
    if (category_id > 0) {
        sqlite3_bind_int(stmt, 5, category_id);
    } else {
        sqlite3_bind_null(stmt, 5);
    }
    sqlite3_bind_int(stmt, 6, quantity);
    sqlite3_bind_int(stmt, 7, min_quantity);
    sqlite3_bind_double(stmt, 8, unit_price);
    sqlite3_bind_text(stmt, 9, supplier, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, location, -1, SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE ? 1 : 0;
}

int db_update_item(sqlite3* db, int item_id, const char* name, const char* description,
                   int category_id, int quantity, int min_quantity, double unit_price,
                   const char* supplier, const char* location, const char* condition) {
    const char* sql = "UPDATE items SET name=?, description=?, condition=?, category_id=?, quantity=?, "
                      "min_quantity=?, unit_price=?, supplier=?, location=?, "
                      "updated_at=datetime('now') WHERE id=?";
    
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, description, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, normalize_item_condition(condition), -1, SQLITE_TRANSIENT);
    if (category_id > 0) {
        sqlite3_bind_int(stmt, 4, category_id);
    } else {
        sqlite3_bind_null(stmt, 4);
    }
    sqlite3_bind_int(stmt, 5, quantity);
    sqlite3_bind_int(stmt, 6, min_quantity);
    sqlite3_bind_double(stmt, 7, unit_price);
    sqlite3_bind_text(stmt, 8, supplier, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, location, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 10, item_id);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE ? 1 : 0;
}

int db_delete_item(sqlite3* db, int item_id) {
    const char* sql = "UPDATE items SET is_active=0 WHERE id=?";
    sqlite3_stmt* stmt;
    
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, item_id);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE ? 1 : 0;
}

int db_adjust_stock(sqlite3* db, int item_id, int delta, int user_id,
                    const char* tx_type, double price, const char* notes, const char* ref) {
    const char* check_sql = "SELECT quantity FROM items WHERE id=?";
    sqlite3_stmt* stmt;
    
    sqlite3_prepare_v2(db, check_sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, item_id);
    
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return 0;
    }
    
    int current_qty = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    
    int new_qty = current_qty + delta;
    if (new_qty < 0) return 0;  // Insufficient stock
    
    const char* update_sql = "UPDATE items SET quantity=?, updated_at=datetime('now') WHERE id=?";
    sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, new_qty);
    sqlite3_bind_int(stmt, 2, item_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    const char* tx_sql = "INSERT INTO transactions (item_id, user_id, type, quantity, "
                        "unit_price, notes, reference) VALUES (?,?,?,?,?,?,?)";
    sqlite3_prepare_v2(db, tx_sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, item_id);
    sqlite3_bind_int(stmt, 2, user_id);
    sqlite3_bind_text(stmt, 3, tx_type, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, delta);
    sqlite3_bind_double(stmt, 5, price);
    sqlite3_bind_text(stmt, 6, notes, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, ref, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return 1;
}

// ============================================================================
// TRANSACTION OPERATIONS
// ============================================================================

Transaction** db_get_transactions(sqlite3* db, const char* search, int* count) {
    const char* sql = "SELECT * FROM transactions ORDER BY created_at DESC";
    sqlite3_stmt* stmt;
    
    *count = 0;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    int estimated = 20;
    Transaction** txs = malloc(estimated * sizeof(Transaction*));
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (*count >= estimated) {
            estimated *= 2;
            txs = realloc(txs, estimated * sizeof(Transaction*));
        }
        
        Transaction* tx = malloc(sizeof(Transaction));
        tx->id = sqlite3_column_int(stmt, 0);
        tx->item_id = sqlite3_column_int(stmt, 1);
        tx->user_id = sqlite3_column_int(stmt, 2);
        strncpy(tx->type, (const char*)sqlite3_column_text(stmt, 3), 31);
        tx->quantity = sqlite3_column_int(stmt, 4);
        tx->unit_price = sqlite3_column_double(stmt, 5);
        strncpy(tx->notes, (const char*)sqlite3_column_text(stmt, 6) ?: "", 511);
        strncpy(tx->reference, (const char*)sqlite3_column_text(stmt, 7) ?: "", 127);
        strncpy(tx->created_at, (const char*)sqlite3_column_text(stmt, 8), 31);
        
        txs[(*count)++] = tx;
    }
    
    sqlite3_finalize(stmt);
    return txs;
}

// ============================================================================
// ISSUE/RETURN OPERATIONS
// ============================================================================

IssueRecord** db_get_issues(sqlite3* db, const char* search, const char* status, int* count) {
    (void)search;
    (void)status;
    const char* sql = "SELECT id, item_id, issued_by, returned_by, quantity, quantity_returned, "
                      "assignee, issued_to, purpose, reference, issue_date, return_date, "
                      "expected_return_date, actual_return_date, unit_price, status, "
                      "return_condition, return_notes, created_at "
                      "FROM issue_records ORDER BY issue_date DESC";
    sqlite3_stmt* stmt;
    
    *count = 0;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    int estimated = 20;
    IssueRecord** issues = malloc(estimated * sizeof(IssueRecord*));
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (*count >= estimated) {
            estimated *= 2;
            issues = realloc(issues, estimated * sizeof(IssueRecord*));
        }
        
        IssueRecord* issue = malloc(sizeof(IssueRecord));
        load_issue_from_stmt(stmt, issue);
        issues[(*count)++] = issue;
    }
    
    sqlite3_finalize(stmt);
    return issues;
}

IssueRecord** db_get_overdue_issues(sqlite3* db, int* count) {
    const char* sql = "SELECT id, item_id, issued_by, returned_by, quantity, quantity_returned, "
                      "assignee, issued_to, purpose, reference, issue_date, return_date, "
                      "expected_return_date, actual_return_date, unit_price, status, "
                      "return_condition, return_notes, created_at "
                      "FROM issue_records "
                      "WHERE return_date IS NULL AND julianday('now') - julianday(issue_date) > 14 "
                      "ORDER BY issue_date DESC";
    sqlite3_stmt* stmt;

    *count = 0;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    int estimated = 10;
    IssueRecord** issues = malloc(estimated * sizeof(IssueRecord*));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (*count >= estimated) {
            estimated *= 2;
            issues = realloc(issues, estimated * sizeof(IssueRecord*));
        }

        IssueRecord* issue = malloc(sizeof(IssueRecord));
        load_issue_from_stmt(stmt, issue);
        strncpy(issue->status, "OVERDUE", sizeof(issue->status) - 1);
        issues[(*count)++] = issue;
    }

    sqlite3_finalize(stmt);
    return issues;
}

int db_create_issue(sqlite3* db, int item_id, int user_id, int quantity,
                    const char* assignee, const char* purpose, const char* reference,
                    const char* expected_return, double unit_price, const char* issue_date) {
    const char* check_sql = "SELECT quantity FROM items WHERE id=? AND is_active=1";
    sqlite3_stmt* stmt;
    int has_issue_date = issue_date && strlen(issue_date) > 0;
    
    sqlite3_prepare_v2(db, check_sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, item_id);
    
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return 0;
    }
    
    int available = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    
    if (available < quantity) return 0;  // Insufficient stock
    
    // Reduce stock
    const char* update_sql = "UPDATE items SET quantity=quantity-? WHERE id=?";
    sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, quantity);
    sqlite3_bind_int(stmt, 2, item_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    // Create transaction record
    const char* tx_sql = has_issue_date
        ? "INSERT INTO transactions (item_id, user_id, type, quantity, unit_price, notes, created_at) "
          "VALUES (?,?,?,?,?,?,?)"
        : "INSERT INTO transactions (item_id, user_id, type, quantity, unit_price, notes) "
          "VALUES (?,?,?,?,?,?)";
    sqlite3_prepare_v2(db, tx_sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, item_id);
    sqlite3_bind_int(stmt, 2, user_id);
    sqlite3_bind_text(stmt, 3, "ISSUE", -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, -quantity);
    sqlite3_bind_double(stmt, 5, unit_price);
    
    char notes[512];
    snprintf(notes, sizeof(notes), "Issued to: %s | %s", assignee, purpose);
    sqlite3_bind_text(stmt, 6, notes, -1, SQLITE_TRANSIENT);
    if (has_issue_date) {
        char issue_timestamp[32];
        snprintf(issue_timestamp, sizeof(issue_timestamp), "%s 00:00:00", issue_date);
        sqlite3_bind_text(stmt, 7, issue_timestamp, -1, SQLITE_TRANSIENT);
    }
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    // Create issue record
    const char* issue_sql = has_issue_date
        ? "INSERT INTO issue_records (item_id, issued_by, quantity, assignee, issued_to, "
          "purpose, reference, issue_date, expected_return_date, unit_price, status, created_at) "
          "VALUES (?,?,?,?,?,?,?,?,?,?,'ISSUED',?)"
        : "INSERT INTO issue_records (item_id, issued_by, quantity, assignee, issued_to, "
          "purpose, reference, expected_return_date, unit_price, status) "
          "VALUES (?,?,?,?,?,?,?,?,?,'ISSUED')";
    sqlite3_prepare_v2(db, issue_sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, item_id);
    sqlite3_bind_int(stmt, 2, user_id);
    sqlite3_bind_int(stmt, 3, quantity);
    sqlite3_bind_text(stmt, 4, assignee, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, assignee, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, purpose, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, reference, -1, SQLITE_TRANSIENT);
    if (has_issue_date) {
        char issue_timestamp[32];
        snprintf(issue_timestamp, sizeof(issue_timestamp), "%s 00:00:00", issue_date);
        sqlite3_bind_text(stmt, 8, issue_timestamp, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, expected_return, -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 10, unit_price);
        sqlite3_bind_text(stmt, 11, issue_timestamp, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_text(stmt, 8, expected_return, -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 9, unit_price);
    }
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return 1;
}

int db_return_issue(sqlite3* db, int issue_id, int user_id, int quantity_returned,
                    const char* condition, const char* notes) {
    const char* check_sql = "SELECT quantity, quantity_returned FROM issue_records WHERE id=?";
    sqlite3_stmt* stmt;
    
    sqlite3_prepare_v2(db, check_sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, issue_id);
    
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return 0;
    }
    
    int original_qty = sqlite3_column_int(stmt, 0);
    int returned_qty = sqlite3_column_int(stmt, 1);
    sqlite3_finalize(stmt);
    
    int outstanding = original_qty - returned_qty;
    if (quantity_returned > outstanding) return 0;
    
    int new_returned = returned_qty + quantity_returned;
    const char* new_status = (new_returned >= original_qty) ? "RETURNED" : "PARTIAL";
    
    // Update issue record
    const char* update_sql = (new_returned >= original_qty)
        ? "UPDATE issue_records SET quantity_returned=?, status=?, returned_by=?, "
          "actual_return_date=datetime('now'), return_date=datetime('now'), "
          "return_condition=?, return_notes=? WHERE id=?"
        : "UPDATE issue_records SET quantity_returned=?, status=?, returned_by=?, "
          "actual_return_date=NULL, return_date=NULL, "
          "return_condition=?, return_notes=? WHERE id=?";
    
    sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, new_returned);
    sqlite3_bind_text(stmt, 2, new_status, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, user_id);
    sqlite3_bind_text(stmt, 4, condition, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, notes, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, issue_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    // Restore stock
    const char* restore_sql = "UPDATE items SET quantity=quantity+? WHERE id="
                             "(SELECT item_id FROM issue_records WHERE id=?)";
    sqlite3_prepare_v2(db, restore_sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, quantity_returned);
    sqlite3_bind_int(stmt, 2, issue_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return 1;
}

// ============================================================================
// BACKUP & RESTORE
// ============================================================================

char* db_backup_to_file(sqlite3* db, const char* backup_path) {
    /**
     * Create a backup of the database file.
     * Returns a status message (allocated, must be freed).
     */
    if (!db || !backup_path) {
        char* msg = malloc(128);
        strcpy(msg, "Invalid parameters");
        return msg;
    }
    
    // Open the backup database
    sqlite3* backup_db;
    int rc = sqlite3_open(backup_path, &backup_db);
    if (rc) {
        char* msg = malloc(256);
        snprintf(msg, 256, "Failed to open backup file: %s", sqlite3_errmsg(backup_db));
        sqlite3_close(backup_db);
        return msg;
    }
    
    // Create backup
    sqlite3_backup* backup = sqlite3_backup_init(backup_db, "main", db, "main");
    if (!backup) {
        char* msg = malloc(256);
        snprintf(msg, 256, "Backup init failed: %s", sqlite3_errmsg(backup_db));
        sqlite3_close(backup_db);
        return msg;
    }
    
    // Perform the backup
    sqlite3_backup_step(backup, -1);
    sqlite3_backup_finish(backup);
    
    int final_rc = sqlite3_errcode(backup_db);
    sqlite3_close(backup_db);
    
    if (final_rc == SQLITE_OK) {
        char* msg = malloc(256);
        snprintf(msg, 256, "Backup created successfully at %s", backup_path);
        return msg;
    } else {
        char* msg = malloc(256);
        snprintf(msg, 256, "Backup failed with code %d", final_rc);
        return msg;
    }
}

int db_restore_from_file(sqlite3* db, const char* backup_path) {
    /**
     * Restore database from a backup file.
     * Returns 1 on success, 0 on failure.
     */
    if (!db || !backup_path) return 0;
    
    // Open the backup database for reading
    sqlite3* backup_db;
    int rc = sqlite3_open(backup_path, &backup_db);
    if (rc) {
        fprintf(stderr, "[db] Failed to open backup file: %s\n", sqlite3_errmsg(backup_db));
        sqlite3_close(backup_db);
        return 0;
    }

    sqlite3_exec(db, "PRAGMA wal_checkpoint(TRUNCATE);", NULL, NULL, NULL);

    // Create backup object (restore direction: backup_db -> db)
    sqlite3_backup* backup = sqlite3_backup_init(db, "main", backup_db, "main");
    if (!backup) {
        fprintf(stderr, "[db] Backup init failed: %s\n", sqlite3_errmsg(db));
        sqlite3_close(backup_db);
        return 0;
    }
    
    // Perform the restore, retrying if the database is temporarily busy.
    int step_rc;
    do {
        step_rc = sqlite3_backup_step(backup, 64);
        if (step_rc == SQLITE_BUSY || step_rc == SQLITE_LOCKED) {
            sqlite3_sleep(50);
        }
    } while (step_rc == SQLITE_OK || step_rc == SQLITE_BUSY || step_rc == SQLITE_LOCKED);
    
    int finish_rc = sqlite3_backup_finish(backup);
    
    int final_rc = (step_rc == SQLITE_DONE && finish_rc == SQLITE_OK) ? SQLITE_OK : sqlite3_errcode(db);
    sqlite3_close(backup_db);
    
    return (final_rc == SQLITE_OK) ? 1 : 0;
}

// ============================================================================
// MEMORY MANAGEMENT
// ============================================================================

void free_users(User** users, int count) {
    if (users) {
        for (int i = 0; i < count; i++) {
            free(users[i]);
        }
        free(users);
    }
}

void free_items(Item** items, int count) {
    if (items) {
        for (int i = 0; i < count; i++) {
            free(items[i]);
        }
        free(items);
    }
}

void free_categories(Category** categories, int count) {
    if (categories) {
        for (int i = 0; i < count; i++) {
            free(categories[i]);
        }
        free(categories);
    }
}

void free_transactions(Transaction** txs, int count) {
    if (txs) {
        for (int i = 0; i < count; i++) {
            free(txs[i]);
        }
        free(txs);
    }
}

void free_issues(IssueRecord** issues, int count) {
    if (issues) {
        for (int i = 0; i < count; i++) {
            free(issues[i]);
        }
        free(issues);
    }
}
// ============================================================================
// SETTINGS
// ============================================================================

void db_save_setting(sqlite3* db, const char* key, const char* value) {
    if (!db || !key || !value) return;
    
    const char* sql = 
        "INSERT INTO app_settings (key, value, updated_at) "
        "VALUES (?, ?, datetime('now')) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value, "
        "updated_at=excluded.updated_at";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return;
    }
    
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

char* db_get_setting(sqlite3* db, const char* key) {
    if (!db || !key) return "";
    
    const char* sql = "SELECT value FROM app_settings WHERE key = ?";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return "";
    }
    
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* value = (const char*)sqlite3_column_text(stmt, 0);
        char* result = malloc(256);
        strncpy(result, value ? value : "", 255);
        sqlite3_finalize(stmt);
        return result;
    }
    
    sqlite3_finalize(stmt);
    char* empty = malloc(1);
    empty[0] = '\0';
    return empty;
}

// ============================================================================
// BACKUP HISTORY
// ============================================================================

int db_save_backup_record(sqlite3* db, double size_kb, const char* provider,
                         const char* url, const char* status, const char* notes,
                         int created_by, int is_local, const char* local_path) {
    if (!db) return -1;
    
    const char* sql = 
        "INSERT INTO backup_history "
        "(size_kb, provider, url, status, notes, created_by, is_local, local_path) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_double(stmt, 1, size_kb);
    sqlite3_bind_text(stmt, 2, provider, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, url ? url : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, status ? status : "OK", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, notes ? notes : "", -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 6, created_by);
    sqlite3_bind_int(stmt, 7, is_local);
    sqlite3_bind_text(stmt, 8, local_path ? local_path : "", -1, SQLITE_STATIC);
    
    int result = sqlite3_step(stmt);
    int last_id = (result == SQLITE_DONE) ? sqlite3_last_insert_rowid(db) : -1;
    sqlite3_finalize(stmt);
    
    return last_id;
}
