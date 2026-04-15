#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
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
    "    purpose             TEXT,"
    "    reference           TEXT,"
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
    "CREATE INDEX IF NOT EXISTS idx_issues_status ON issue_records(status);";

/**
 * Initialize database connection and create schema.
 */
sqlite3* db_init(const char* db_path) {
    sqlite3* db;
    int rc = sqlite3_open(db_path, &db);
    
    if (rc) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
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
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    }
}

void db_seed_data(sqlite3* db) {
    // Create admin user with hashed password
    char* admin_hash = auth_hash_password("admin123");
    
    char seed_sql[4096];
    snprintf(seed_sql, sizeof(seed_sql),
        "INSERT OR IGNORE INTO users (username, password_hash, role, full_name, email) "
        "VALUES ('admin', '%s', 'admin', 'System Administrator', 'admin@nexus.local');",
        admin_hash);
    
    sqlite3_exec(db, seed_sql, NULL, NULL, NULL);
    free(admin_hash);
    
    // Insert default categories
    const char* categories_sql = 
        "INSERT OR IGNORE INTO categories (name, description) VALUES "
        "('Electronics', 'Electronic components and devices'), "
        "('Mechanical', 'Mechanical parts and hardware'), "
        "('Consumables', 'Consumable supplies'), "
        "('Tools', 'Tools and equipment'), "
        "('Software', 'Software licenses and media');";
    
    sqlite3_exec(db, categories_sql, NULL, NULL, NULL);
    
    // Insert sample items
    const char* items_sql = 
        "INSERT OR IGNORE INTO items (name, sku, category_id, quantity, min_quantity, unit_price, description, location) VALUES "
        "('Arduino Uno Microcontroller', 'ARDU-001', 1, 25, 5, 25.00, 'Arduino Uno rev3 development board', 'Shelf A1'), "
        "('USB Type-C Cable', 'USB-TC-001', 1, 150, 20, 3.50, '2m USB Type-C charging cable', 'Drawer B2'), "
        "('LED Strip RGB 5m', 'LED-RGB-001', 1, 8, 2, 45.00, 'Addressable RGB LED strip WS2812B', 'Shelf C3'), "
        "('Steel Bolts M6', 'BOLT-M6-100', 2, 500, 50, 0.25, 'Stainless steel bolts M6x30mm', 'Bin D1'), "
        "('Ball Bearing 6203', 'BEAR-6203', 2, 45, 10, 8.75, 'Deep groove ball bearing', 'Bin D2'), "
        "('Copper Tubing 1/2in', 'TUBE-CU-05', 2, 30, 5, 12.00, 'Copper tubing 1/2 inch diameter 10ft', 'Shelf E1'), "
        "('A4 Paper Ream 500 sheets', 'PAPER-A4-500', 3, 75, 10, 4.50, 'Standard white copy paper', 'Shelf F1'), "
        "('Ballpoint Pens (Box of 50)', 'PEN-BOX-50', 3, 120, 20, 7.99, 'Blue ballpoint pens', 'Drawer F2'), "
        "('Industrial Screwdriver Set', 'TOOL-SCREW-SET', 4, 12, 2, 49.99, '32-piece precision screwdriver set', 'Cabinet G1'), "
        "('Power Drill DeWalt 20V', 'DRY-DEWALT-20V', 4, 6, 1, 199.00, 'DeWalt compact drill driver', 'Cabinet G2'), "
        "('Microsoft Office 365 License', 'SW-O365-1YR', 5, 15, 3, 69.99, 'Office 365 Single User 1 Year', 'Digital'), "
        "('AutoCAD 2024 License', 'SW-ACAD-2024', 5, 4, 1, 680.00, 'AutoCAD 2024 Annual Subscription', 'Digital');";
    
    sqlite3_exec(db, items_sql, NULL, NULL, NULL);
    
    // Insert sample transactions
    const char* transactions_sql =
        "INSERT OR IGNORE INTO transactions (item_id, type, quantity, reference, notes) VALUES "
        "(1, 'IN', 25, 'PO-2026-001', 'Initial stock received from supplier'), "
        "(2, 'IN', 150, 'PO-2026-002', 'Bulk order USB cables'), "
        "(1, 'OUT', 3, 'REQ-2026-001', 'Issued to Engineering Lab'), "
        "(3, 'IN', 8, 'PO-2026-003', 'LED strips for prototype project'), "
        "(4, 'IN', 500, 'PO-2026-004', 'Bolt replenishment'), "
        "(4, 'OUT', 127, 'REQ-2026-002', 'Used in assembly line'), "
        "(5, 'IN', 45, 'PO-2026-005', 'Bearing stock'), "
        "(6, 'IN', 30, 'PO-2026-006', 'Copper tubing delivery'), "
        "(9, 'IN', 12, 'PO-2026-007', 'Tool purchase'), "
        "(10, 'IN', 6, 'PO-2026-008', 'Power drill purchase');";
    
    sqlite3_exec(db, transactions_sql, NULL, NULL, NULL);
    
    // Insert sample issues
    const char* issues_sql =
        "INSERT OR IGNORE INTO issue_records (item_id, assignee, quantity, status) VALUES "
        "(1, 'Engineering Team', 5, 'ISSUED'), "
        "(2, 'Service Dept', 10, 'PENDING'), "
        "(4, 'Manufacturing', 200, 'RETURNED'), "
        "(10, 'Maintenance', 1, 'ISSUED');";
    
    sqlite3_exec(db, issues_sql, NULL, NULL, NULL);
}

/**
 * Helper function to extract JSON string value
 */
static char* extract_json_string(const char* json, const char* key) {
    // Find the key - search for pattern "key":"value"
    char search_key[512];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);
    const char* key_pos = strstr(json, search_key);
    if (!key_pos) {
        return NULL;
    }
    
    // Move past the key and colon
    const char* value_start = key_pos + strlen(search_key);
    
    // Skip whitespace
    while (*value_start && (*value_start == ' ' || *value_start == '\t' || *value_start == '\n' || *value_start == '\r')) {
        value_start++;
    }
    
    // Check if it's a string (starts with quote)
    if (*value_start != '\"') {
        // Could be a number, object, or array - not a string  
        return NULL;
    }
    
    // Move to the actual string content
    value_start++;
    
    // First pass: count the length we need
    size_t length = 0;
    const char* temp = value_start;
    while (*temp && length < 2048) {
        if (*temp == '\\' && *(temp + 1)) {
            temp += 2;
        } else if (*temp == '\"') {
            break;
        } else {
            temp++;
        }
        length++;
    }
    
    // Allocate buffer for the result
    char* buffer = malloc(length + 1);
    if (!buffer) return NULL;
    
    // Second pass: copy with proper escape handling
    size_t i = 0;
    while (*value_start && i < length) {
        if (*value_start == '\\' && *(value_start + 1)) {
            // Handle escape sequences
            value_start++;
            if (*value_start == '\"') {
                buffer[i++] = '\"';
            } else if (*value_start == '\\') {
                buffer[i++] = '\\';
            } else if (*value_start == 'n') {
                buffer[i++] = '\n';
            } else if (*value_start == 't') {
                buffer[i++] = '\t';
            } else if (*value_start == 'r') {
                buffer[i++] = '\r';
            } else if (*value_start == '/') {
                buffer[i++] = '/';
            } else {
                buffer[i++] = *value_start;
            }
        } else if (*value_start == '\"') {
            break;
        } else {
            buffer[i++] = *value_start;
        }
        value_start++;
    }
    
    buffer[i] = 0;
    return buffer;
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
    const char* sql = "SELECT * FROM users ORDER BY id";
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
    char sql[1024] = "SELECT i.*, c.name as category_name FROM items i "
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
        item->id = sqlite3_column_int(stmt, 0);
        strncpy(item->sku, (const char*)sqlite3_column_text(stmt, 1), 127);
        strncpy(item->name, (const char*)sqlite3_column_text(stmt, 2), 255);
        strncpy(item->description, (const char*)sqlite3_column_text(stmt, 3) ?: "", 511);
        item->category_id = sqlite3_column_int(stmt, 4);
        item->quantity = sqlite3_column_int(stmt, 5);
        item->min_quantity = sqlite3_column_int(stmt, 6);
        item->unit_price = sqlite3_column_double(stmt, 7);
        strncpy(item->supplier, (const char*)sqlite3_column_text(stmt, 8) ?: "", 255);
        strncpy(item->location, (const char*)sqlite3_column_text(stmt, 9) ?: "", 255);
        item->is_active = sqlite3_column_int(stmt, 10);
        
        items[(*count)++] = item;
    }
    
    sqlite3_finalize(stmt);
    return items;
}

Item* db_get_item(sqlite3* db, int item_id) {
    const char* sql = "SELECT * FROM items WHERE id=?";
    sqlite3_stmt* stmt;
    
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, item_id);
    
    Item* item = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        item = malloc(sizeof(Item));
        item->id = sqlite3_column_int(stmt, 0);
        strncpy(item->sku, (const char*)sqlite3_column_text(stmt, 1), 127);
        strncpy(item->name, (const char*)sqlite3_column_text(stmt, 2), 255);
        strncpy(item->description, (const char*)sqlite3_column_text(stmt, 3) ?: "", 511);
        item->category_id = sqlite3_column_int(stmt, 4);
        item->quantity = sqlite3_column_int(stmt, 5);
        item->min_quantity = sqlite3_column_int(stmt, 6);
        item->unit_price = sqlite3_column_double(stmt, 7);
        strncpy(item->supplier, (const char*)sqlite3_column_text(stmt, 8) ?: "", 255);
        strncpy(item->location, (const char*)sqlite3_column_text(stmt, 9) ?: "", 255);
        item->is_active = sqlite3_column_int(stmt, 10);
    }
    
    sqlite3_finalize(stmt);
    return item;
}

int db_create_item(sqlite3* db, const char* sku, const char* name, const char* description,
                   int category_id, int quantity, int min_quantity, double unit_price,
                   const char* supplier, const char* location) {
    const char* sql = "INSERT INTO items (sku, name, description, category_id, quantity, "
                      "min_quantity, unit_price, supplier, location) VALUES (?,?,?,?,?,?,?,?,?)";
    
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, sku, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, description, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, category_id);
    sqlite3_bind_int(stmt, 5, quantity);
    sqlite3_bind_int(stmt, 6, min_quantity);
    sqlite3_bind_double(stmt, 7, unit_price);
    sqlite3_bind_text(stmt, 8, supplier, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, location, -1, SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE ? 1 : 0;
}

int db_update_item(sqlite3* db, int item_id, const char* name, const char* description,
                   int category_id, int quantity, int min_quantity, double unit_price,
                   const char* supplier, const char* location) {
    const char* sql = "UPDATE items SET name=?, description=?, category_id=?, quantity=?, "
                      "min_quantity=?, unit_price=?, supplier=?, location=?, "
                      "updated_at=datetime('now') WHERE id=?";
    
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, description, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, category_id);
    sqlite3_bind_int(stmt, 4, quantity);
    sqlite3_bind_int(stmt, 5, min_quantity);
    sqlite3_bind_double(stmt, 6, unit_price);
    sqlite3_bind_text(stmt, 7, supplier, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, location, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 9, item_id);
    
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
    const char* sql = "SELECT * FROM issue_records ORDER BY created_at DESC";
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
        issue->id = sqlite3_column_int(stmt, 0);
        issue->item_id = sqlite3_column_int(stmt, 1);
        issue->issued_by = sqlite3_column_int(stmt, 2);
        issue->returned_by = sqlite3_column_int(stmt, 3);
        issue->quantity = sqlite3_column_int(stmt, 4);
        issue->quantity_returned = sqlite3_column_int(stmt, 5);
        strncpy(issue->assignee, (const char*)sqlite3_column_text(stmt, 6), 255);
        strncpy(issue->purpose, (const char*)sqlite3_column_text(stmt, 7) ?: "", 511);
        strncpy(issue->reference, (const char*)sqlite3_column_text(stmt, 8) ?: "", 127);
        strncpy(issue->expected_return_date, (const char*)sqlite3_column_text(stmt, 9) ?: "", 31);
        strncpy(issue->actual_return_date, (const char*)sqlite3_column_text(stmt, 10) ?: "", 31);
        issue->unit_price = sqlite3_column_double(stmt, 11);
        strncpy(issue->status, (const char*)sqlite3_column_text(stmt, 12), 31);
        
        issues[(*count)++] = issue;
    }
    
    sqlite3_finalize(stmt);
    return issues;
}

int db_create_issue(sqlite3* db, int item_id, int user_id, int quantity,
                    const char* assignee, const char* purpose, const char* reference,
                    const char* expected_return, double unit_price) {
    const char* check_sql = "SELECT quantity FROM items WHERE id=? AND is_active=1";
    sqlite3_stmt* stmt;
    
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
    const char* tx_sql = "INSERT INTO transactions (item_id, user_id, type, quantity, unit_price, notes) "
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
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    // Create issue record
    const char* issue_sql = "INSERT INTO issue_records (item_id, issued_by, quantity, assignee, "
                           "purpose, reference, expected_return_date, unit_price, status) "
                           "VALUES (?,?,?,?,?,?,?,'ISSUED')";
    sqlite3_prepare_v2(db, issue_sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, item_id);
    sqlite3_bind_int(stmt, 2, user_id);
    sqlite3_bind_int(stmt, 3, quantity);
    sqlite3_bind_text(stmt, 4, assignee, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, purpose, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, reference, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, expected_return, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 8, unit_price);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return 1;
}

int db_return_issue(sqlite3* db, int issue_id, int user_id, int quantity_returned,
                    const char* condition, const char* notes) {
    const char* check_sql = "SELECT quantity, quantity_returned, status FROM issue_records WHERE id=?";
    sqlite3_stmt* stmt;
    
    sqlite3_prepare_v2(db, check_sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, issue_id);
    
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return 0;
    }
    
    int original_qty = sqlite3_column_int(stmt, 0);
    int returned_qty = sqlite3_column_int(stmt, 1);
    const char* status = (const char*)sqlite3_column_text(stmt, 2);
    
    sqlite3_finalize(stmt);
    
    int outstanding = original_qty - returned_qty;
    if (quantity_returned > outstanding) return 0;
    
    int new_returned = returned_qty + quantity_returned;
    const char* new_status = (new_returned >= original_qty) ? "RETURNED" : "PARTIAL";
    
    // Update issue record
    const char* update_sql = "UPDATE issue_records SET quantity_returned=?, status=?, "
                            "returned_by=?, actual_return_date=datetime('now'), "
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
        fprintf(stderr, "Failed to open backup file: %s\n", sqlite3_errmsg(backup_db));
        sqlite3_close(backup_db);
        return 0;
    }
    
    // Create backup object (restore direction: backup_db -> db)
    sqlite3_backup* backup = sqlite3_backup_init(db, "main", backup_db, "main");
    if (!backup) {
        fprintf(stderr, "Backup init failed: %s\n", sqlite3_errmsg(db));
        sqlite3_close(backup_db);
        return 0;
    }
    
    // Perform the restore
    sqlite3_backup_step(backup, -1);
    sqlite3_backup_finish(backup);
    
    int final_rc = sqlite3_errcode(db);
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