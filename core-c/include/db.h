#ifndef DB_H
#define DB_H

#include <sqlite3.h>
#include <time.h>

typedef struct {
    int id;
    char username[128];
    char role[32];         // admin, manager, viewer
    char email[256];
    char full_name[256];
    int is_active;
    char created_at[32];
    char last_login[32];
} User;

typedef struct {
    int id;
    char name[256];
    char description[512];
    char created_at[32];
} Category;

typedef struct {
    int id;
    char sku[128];
    char name[256];
    char description[512];
    int category_id;
    char category_name[256];
    int quantity;
    int min_quantity;
    double unit_price;
    char supplier[256];
    char location[256];
    int is_active;
    char created_at[32];
    char updated_at[32];
} Item;

typedef struct {
    int id;
    int item_id;
    int user_id;
    char type[32];         // IN, OUT, ADJUST, ISSUE, RETURN
    int quantity;
    double unit_price;
    char notes[512];
    char reference[128];
    char created_at[32];
} Transaction;

typedef struct {
    int id;
    int item_id;
    int issued_by;
    int returned_by;
    int quantity;
    int quantity_returned;
    char assignee[256];
    char purpose[512];
    char reference[128];
    char expected_return_date[32];
    char actual_return_date[32];
    double unit_price;
    char status[32];       // ISSUED, PARTIAL, RETURNED
    char return_condition[64];
    char return_notes[512];
    char created_at[32];
} IssueRecord;



// Database initialization
sqlite3* db_init(const char* db_path);
void db_close(sqlite3* db);
void db_create_schema(sqlite3* db);
void db_seed_data(sqlite3* db);

// User operations
User* db_authenticate(sqlite3* db, const char* username, const char* password);
User* db_get_user(sqlite3* db, int user_id);
User* db_get_user_by_username(sqlite3* db, const char* username);
User** db_get_all_users(sqlite3* db, int* count);
int db_create_user(sqlite3* db, const char* username, const char* password,
                   const char* role, const char* full_name, const char* email);
int db_update_user(sqlite3* db, int user_id, const char* role,
                   const char* full_name, const char* email, int is_active);
int db_change_password(sqlite3* db, int user_id, const char* new_password);
int db_delete_user(sqlite3* db, int user_id);

// Category operations
Category** db_get_categories(sqlite3* db, int* count);
int db_create_category(sqlite3* db, const char* name, const char* description);

// Item operations
Item** db_get_items(sqlite3* db, const char* search, int category_id, int low_stock, int* count);
Item* db_get_item(sqlite3* db, int item_id);
int db_create_item(sqlite3* db, const char* sku, const char* name, const char* description,
                   int category_id, int quantity, int min_quantity, double unit_price,
                   const char* supplier, const char* location);
int db_update_item(sqlite3* db, int item_id, const char* name, const char* description,
                   int category_id, int quantity, int min_quantity, double unit_price,
                   const char* supplier, const char* location);
int db_delete_item(sqlite3* db, int item_id);

// Transaction operations
Transaction** db_get_transactions(sqlite3* db, const char* search, int* count);
int db_adjust_stock(sqlite3* db, int item_id, int delta, int user_id,
                    const char* tx_type, double price, const char* notes, const char* ref);

// Issue/Return operations
IssueRecord** db_get_issues(sqlite3* db, const char* search, const char* status, int* count);
int db_create_issue(sqlite3* db, int item_id, int user_id, int quantity,
                    const char* assignee, const char* purpose, const char* reference,
                    const char* expected_return, double unit_price);
int db_return_issue(sqlite3* db, int issue_id, int user_id, int quantity_returned,
                    const char* condition, const char* notes);

// Backup operations
char* db_backup_to_file(sqlite3* db, const char* backup_path);
int db_restore_from_file(sqlite3* db, const char* backup_path);

// Settings
void db_save_setting(sqlite3* db, const char* key, const char* value);
char* db_get_setting(sqlite3* db, const char* key);

// Google Drive Credentials


// Backup History
int db_save_backup_record(sqlite3* db, double size_kb, const char* provider,
                         const char* url, const char* status, const char* notes,
                         int created_by, int is_local, const char* local_path);

// Utility
void free_users(User** users, int count);
void free_items(Item** items, int count);void free_categories(Category** categories, int count);
void free_transactions(Transaction** txs, int count);
void free_issues(IssueRecord** issues, int count);

#endif