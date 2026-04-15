#ifndef DB_H
#define DB_H

#include <libpq-fe.h>
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
    char condition[32];
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
    char issued_to[256];
    char purpose[512];
    char reference[128];
    char issue_date[32];
    char return_date[32];
    char expected_return_date[32];
    char actual_return_date[32];
    double unit_price;
    char status[32];       // ISSUED, PARTIAL, RETURNED, OVERDUE
    char return_condition[64];
    char return_notes[512];
    char created_at[32];
} IssueRecord;



// Database initialization
PGconn* db_init(const char* connection_string);
void db_close(PGconn* conn);
void db_create_schema(PGconn* conn);
void db_seed_data(PGconn* conn);

// User operations
User* db_authenticate(PGconn* conn, const char* username, const char* password);
User* db_get_user(PGconn* conn, int user_id);
User* db_get_user_by_username(PGconn* conn, const char* username);
User** db_get_all_users(PGconn* conn, int* count);
int db_create_user(PGconn* conn, const char* username, const char* password,
                   const char* role, const char* full_name, const char* email);
int db_update_user(PGconn* conn, int user_id, const char* role,
                   const char* full_name, const char* email, int is_active);
int db_change_password(PGconn* conn, int user_id, const char* new_password);
int db_delete_user(PGconn* conn, int user_id);

// Category operations
Category** db_get_categories(PGconn* conn, int* count);
int db_create_category(PGconn* conn, const char* name, const char* description);

// Item operations
Item** db_get_items(PGconn* conn, const char* search, int category_id, int low_stock, int* count);
Item* db_get_item(PGconn* conn, int item_id);
int db_create_item(PGconn* conn, const char* sku, const char* name, const char* description,
                   int category_id, int quantity, int min_quantity, double unit_price,
                   const char* supplier, const char* location, const char* condition);
int db_update_item(PGconn* conn, int item_id, const char* name, const char* description,
                   int category_id, int quantity, int min_quantity, double unit_price,
                   const char* supplier, const char* location, const char* condition);
int db_delete_item(PGconn* conn, int item_id);

// Transaction operations
Transaction** db_get_transactions(PGconn* conn, const char* search, int* count);
int db_adjust_stock(PGconn* conn, int item_id, int delta, int user_id,
                    const char* tx_type, double price, const char* notes, const char* ref);

// Issue/Return operations
IssueRecord** db_get_issues(PGconn* conn, const char* search, const char* status, int* count);
IssueRecord** db_get_overdue_issues(PGconn* conn, int* count);
int db_create_issue(PGconn* conn, int item_id, int user_id, int quantity,
                    const char* assignee, const char* purpose, const char* reference,
                    const char* expected_return, double unit_price, const char* issue_date);
int db_return_issue(PGconn* conn, int issue_id, int user_id, int quantity_returned,
                    const char* condition, const char* notes);

// Backup operations
char* db_backup_to_file(PGconn* conn, const char* backup_path);
int db_restore_from_file(PGconn* conn, const char* backup_path);

// Settings
void db_save_setting(PGconn* conn, const char* key, const char* value);
char* db_get_setting(PGconn* conn, const char* key);

// Google Drive Credentials


// Backup History
int db_save_backup_record(PGconn* conn, double size_kb, const char* provider,
                         const char* url, const char* status, const char* notes,
                         int created_by, int is_local, const char* local_path);

// Utility
void free_users(User** users, int count);
void free_items(Item** items, int count);void free_categories(Category** categories, int count);
void free_transactions(Transaction** txs, int count);
void free_issues(IssueRecord** issues, int count);

#endif
