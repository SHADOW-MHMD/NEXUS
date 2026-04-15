#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <microhttpd.h>
#include <sqlite3.h>
#include <ctype.h>
#include "db.h"
#include "api.h"
#include "auth.h"

static sqlite3* global_db = NULL;

void api_init(sqlite3* db) {
    global_db = db;
}

// ============================================================================
// STATIC FILE SERVING
// ============================================================================

static enum MHD_Result serve_static_file(struct MHD_Connection* connection,
                                        const char* filename,
                                        const char* content_type) {
    FILE* f = fopen(filename, "rb");
    
    // If file not found, try with nexus_c prefix (in case server runs from parent dir)
    if (!f) {
        char alt_filename[512];
        snprintf(alt_filename, sizeof(alt_filename), "/home/Mishal/nexus_c/%s", filename);
        f = fopen(alt_filename, "rb");
    }
    
    if (!f) {
        const char* not_found = "{\"error\":\"File not found\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(
            strlen(not_found), (void*)not_found, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, resp);
        MHD_destroy_response(resp);
        return ret;
    }
    
    // Read entire file
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* content = malloc(size + 1);
    if (!content) {
        fclose(f);
        const char* error = "{\"error\":\"Memory error\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(
            strlen(error), (void*)error, MHD_RESPMEM_PERSISTENT);
        return MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
    }
    
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);
    
    struct MHD_Response* resp = MHD_create_response_from_buffer(
        size, (void*)content, MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(resp, "Content-Type", content_type);
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    
    return ret;
}

// ============================================================================
// JSON UTILITIES
// ============================================================================

char* json_error(const char* message) {
    char* result = malloc(512);
    snprintf(result, 512, "{\"error\":\"%s\"}", message);
    return result;
}

char* json_success(const char* message) {
    char* result = malloc(512);
    snprintf(result, 512, "{\"status\":\"success\",\"message\":\"%s\"}", message);
    return result;
}

/**
 * Simple URL decode
 */
static void url_decode(const char* src, char* dest, size_t dest_size) {
    if (!src || !dest) return;
    
    int i = 0, j = 0;
    while (src[i] && j < dest_size - 1) {
        if (src[i] == '%' && i + 2 < strlen(src)) {
            int val;
            sscanf(src + i + 1, "%2x", &val);
            dest[j++] = (char)val;
            i += 3;
        } else if (src[i] == '+') {
            dest[j++] = ' ';
            i++;
        } else {
            dest[j++] = src[i++];
        }
    }
    dest[j] = '\0';
}

/**
 * Parse query string parameters
 */
static void parse_query_params(const char* query_string, 
                               char* out_key, char* out_value, size_t size) {
    if (!query_string) return;
    
    char temp_key[256] = "", temp_value[512] = "";
    char* eq = strchr(query_string, '=');
    
    if (eq) {
        int key_len = eq - query_string;
        strncpy(temp_key, query_string, key_len);
        temp_key[key_len] = '\0';
        url_decode(eq + 1, temp_value, sizeof(temp_value));
    } else {
        strcpy(temp_key, query_string);
    }
    
    strncpy(out_key, temp_key, size - 1);
    strncpy(out_value, temp_value, size - 1);
}

// ============================================================================
// API ENDPOINTS - AUTHENTICATION
// ============================================================================

// Structure to hold request state
struct RequestContext {
    char post_data[512];
    size_t post_data_len;
};

static enum MHD_Result handle_login(struct MHD_Connection* connection,
                                   struct RequestContext* ctx) {
    // Parse JSON POST data from accumulated context
    char username[128] = {0};
    char password[128] = {0};
    
    if (ctx && ctx->post_data_len > 0) {
        fprintf(stderr, "DEBUG: POST data: %s (len=%zu)\n", ctx->post_data, ctx->post_data_len);
        
        // Parse JSON: {"username":"admin","password":"admin123"}
        const char* data = ctx->post_data;
        
        // Find username value
        const char* u_key = "\"username\"";
        const char* u_pos = strstr(data, u_key);
        if (u_pos) {
            u_pos = strchr(u_pos, ':');
            if (u_pos) {
                u_pos = strchr(u_pos, '"');
                if (u_pos) {
                    u_pos++;
                    const char* u_end = strchr(u_pos, '"');
                    if (u_end && u_end - u_pos < (int)sizeof(username)) {
                        strncpy(username, u_pos, u_end - u_pos);
                    }
                }
            }
        }
        
        // Find password value
        const char* p_key = "\"password\"";
        const char* p_pos = strstr(data, p_key);
        if (p_pos) {
            p_pos = strchr(p_pos, ':');
            if (p_pos) {
                p_pos = strchr(p_pos, '"');
                if (p_pos) {
                    p_pos++;
                    const char* p_end = strchr(p_pos, '"');
                    if (p_end && p_end - p_pos < (int)sizeof(password)) {
                        strncpy(password, p_pos, p_end - p_pos);
                    }
                }
            }
        }
        
        fprintf(stderr, "DEBUG: Parsed username='%s', password='%s'\n", username, password);
    }
    
    User* user = db_authenticate(global_db, username, password);
    
    char* response = malloc(4096);
    
    if (user) {
        char* token = auth_create_token(user->id, user->username, user->role);
        
        // Escape all fields for JSON
        char escaped_username[256] = {0};
        char escaped_role[64] = {0};
        char escaped_token[2048] = {0};
        
        // Escape username
        int j = 0;
        for (int i = 0; user->username[i] && j < (int)sizeof(escaped_username) - 2; i++) {
            if (user->username[i] == '"' || user->username[i] == '\\') {
                escaped_username[j++] = '\\';
            }
            escaped_username[j++] = user->username[i];
        }
        escaped_username[j] = '\0';
        
        // Escape role
        j = 0;
        for (int i = 0; user->role[i] && j < (int)sizeof(escaped_role) - 2; i++) {
            if (user->role[i] == '"' || user->role[i] == '\\') {
                escaped_role[j++] = '\\';
            }
            escaped_role[j++] = user->role[i];
        }
        escaped_role[j] = '\0';
        
        // Escape token
        j = 0;
        for (int i = 0; token[i] && j < (int)sizeof(escaped_token) - 2; i++) {
            if (token[i] == '"' || token[i] == '\\') {
                escaped_token[j++] = '\\';
            }
            escaped_token[j++] = token[i];
        }
        escaped_token[j] = '\0';
        
        snprintf(response, 4096,
                "{\"status\":\"success\",\"token\":\"%s\",\"user_id\":%d,\"username\":\"%s\",\"role\":\"%s\"}",
                escaped_token, user->id, escaped_username, escaped_role);
        
        fprintf(stderr, "DEBUG: Login response: %s\n", response);
        free(token);
        free(user);
    } else {
        strcpy(response, "{\"error\":\"Invalid credentials\"}");
    }
    
    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(response), (void*)response, MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    
    return ret;
}

// ============================================================================
// API ENDPOINTS - ITEMS
// ============================================================================

static enum MHD_Result handle_get_items(struct MHD_Connection* connection,
                                       const char* query) {
    int count = 0;
    Item** items = db_get_items(global_db, "", 0, 0, &count);
    
    char* response = malloc(16384);
    strcpy(response, "[");
    
    for (int i = 0; i < count; i++) {
        char item_json[512];
        snprintf(item_json, sizeof(item_json),
                "{\"id\":%d,\"sku\":\"%s\",\"name\":\"%s\",\"quantity\":%d,\"unit_price\":%.2f%s",
                items[i]->id, items[i]->sku, items[i]->name, items[i]->quantity,
                items[i]->unit_price, (i < count - 1) ? "}," : "}");
        
        if (strlen(response) + strlen(item_json) < 16384) {
            strcat(response, item_json);
        }
    }
    strcat(response, "]");
    
    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(response), (void*)response, MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    
    free_items(items, count);
    free(response);
    
    return ret;
}

static enum MHD_Result handle_create_item(struct MHD_Connection* connection,
                                         const char* upload_data, size_t* upload_size) {
    // Parse POST data for item creation
    char sku[128] = "", name[256] = "", description[512] = "";
    int quantity = 0;
    double unit_price = 0.0;
    
    if (upload_data && *upload_size > 0) {
        char buffer[2048];
        strncpy(buffer, upload_data, *upload_size < sizeof(buffer) ? *upload_size : sizeof(buffer) - 1);
        
        // Simple param extraction
        char* sku_start = strstr(buffer, "sku=");
        if (sku_start) {
            char* sku_end = strchr(sku_start + 4, '&');
            int sku_len = sku_end ? (sku_end - (sku_start + 4)) : strlen(sku_start + 4);
            strncpy(sku, sku_start + 4, sku_len < sizeof(sku) ? sku_len : sizeof(sku) - 1);
            url_decode(sku, sku, sizeof(sku));
        }
    }
    
    int success = db_create_item(global_db, sku, "Sample Item", "", 0, quantity, 0, unit_price, "", "");
    
    char* response = success ? json_success("Item created") : json_error("Failed to create item");
    
    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(response), (void*)response, MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    
    free(response);
    return ret;
}

// ============================================================================
// API ENDPOINTS - USERS
// ============================================================================
static enum MHD_Result handle_get_users(struct MHD_Connection* connection) {
    int count = 0;
    User** users = db_get_all_users(global_db, &count);
    
    fprintf(stderr, "DEBUG: handle_get_users called, count=%d\n", count);
    
    // Build response more safely
    char* response = malloc(16384);  // Larger buffer
    char* pos = response;
    int remaining = 16384;
    
    int written = snprintf(pos, remaining, "[");
    pos += written;
    remaining -= written;
    
    for (int i = 0; i < count; i++) {
        fprintf(stderr, "  User %d: %s (role=%s)\n", i, users[i]->username, users[i]->role);
        
        // Build JSON for this user
        if (i > 0) {
            written = snprintf(pos, remaining, ",");
            pos += written;
            remaining -= written;
        }
        
        written = snprintf(pos, remaining, 
                "{\"id\":%d,\"username\":\"%s\",\"full_name\":\"%s\",\"email\":\"%s\",\"role\":\"%s\"}",
                users[i]->id, users[i]->username, users[i]->full_name, users[i]->email, users[i]->role);
        pos += written;
        remaining -= written;
    }
    
    written = snprintf(pos, remaining, "]");
    pos += written;
    remaining -= written;
    
    fprintf(stderr, "DEBUG: Returning users response: %s\n", response);
    
    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(response), (void*)response, MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    
    free_users(users, count);
    
    return ret;
}

// ============================================================================
// API ENDPOINTS - ISSUES
// ============================================================================

static enum MHD_Result handle_get_issues(struct MHD_Connection* connection) {
    int count = 0;
    IssueRecord** issues = db_get_issues(global_db, "", "", &count);
    
    char* response = malloc(16384);
    strcpy(response, "[");
    
    for (int i = 0; i < count; i++) {
        char issue_json[512];
        snprintf(issue_json, sizeof(issue_json),
                "{\"id\":%d,\"item_id\":%d,\"assignee\":\"%s\",\"quantity\":%d,\"status\":\"%s\"%s",
                issues[i]->id, issues[i]->item_id, issues[i]->assignee, 
                issues[i]->quantity, issues[i]->status,
                (i < count - 1) ? "}," : "}");
        
        if (strlen(response) + strlen(issue_json) < 16384) {
            strcat(response, issue_json);
        }
    }
    strcat(response, "]");
    
    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(response), (void*)response, MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    
    free_issues(issues, count);
    free(response);
    
    return ret;
}

// ============================================================================// BACKUP OPERATIONS
// ============================================================================

static enum MHD_Result handle_backup_create(struct MHD_Connection* connection) {
    char backup_path[256];
    snprintf(backup_path, sizeof(backup_path), "nexus_backup_%ld.db", time(NULL));
    
    char* msg = db_backup_to_file(global_db, backup_path);
    
    char response[512];
    snprintf(response, sizeof(response), 
             "{\"status\":\"success\",\"message\":\"%s\",\"filename\":\"%s\"}", 
             msg, backup_path);
    
    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(response), (void*)response, MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    free(msg);
    
    return ret;
}

static enum MHD_Result handle_backup_download(struct MHD_Connection* connection,
                                             const char* filename) {
    // Sanitize filename to prevent directory traversal
    if (!filename || strchr(filename, '/') || strchr(filename, '\\')) {
        const char* error = "{\"error\":\"Invalid filename\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(
            strlen(error), (void*)error, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);
        MHD_destroy_response(resp);
        return ret;
    }
    
    FILE* f = fopen(filename, "rb");
    if (!f) {
        const char* error = "{\"error\":\"Backup file not found\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(
            strlen(error), (void*)error, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, resp);
        MHD_destroy_response(resp);
        return ret;
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // Read file
    char* buffer = malloc(size);
    fread(buffer, 1, size, f);
    fclose(f);
    
    // Send as attachment
    struct MHD_Response* resp = MHD_create_response_from_buffer(size, buffer, MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(resp, "Content-Type", "application/octet-stream");
    MHD_add_response_header(resp, "Content-Disposition", 
                           "attachment; filename=\"nexus_backup.db\"");
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    
    return ret;
}

static enum MHD_Result handle_backup_restore(struct MHD_Connection* connection,
                                            struct RequestContext* ctx) {
    if (!ctx || ctx->post_data_len == 0) {
        const char* error = "{\"error\":\"No file provided\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(
            strlen(error), (void*)error, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);
        MHD_destroy_response(resp);
        return ret;
    }
    
    // Save the uploaded file temporarily
    char temp_path[256];
    snprintf(temp_path, sizeof(temp_path), "/tmp/nexus_restore_%ld.db", time(NULL));
    
    FILE* temp_file = fopen(temp_path, "wb");
    if (!temp_file) {
        const char* error = "{\"error\":\"Failed to save backup file\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(
            strlen(error), (void*)error, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
        MHD_destroy_response(resp);
        return ret;
    }
    
    fwrite(ctx->post_data, 1, ctx->post_data_len, temp_file);
    fclose(temp_file);
    
    // Restore from the temporary file
    int success = db_restore_from_file(global_db, temp_path);
    
    // Clean up temporary file
    remove(temp_path);
    
    char response[256];
    if (success) {
        snprintf(response, sizeof(response), "{\"status\":\"success\",\"message\":\"Backup restored successfully\"}");
    } else {
        snprintf(response, sizeof(response), "{\"error\":\"Failed to restore backup\"}");
    }
    
    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(response), (void*)response, MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    
    enum MHD_Result ret = MHD_queue_response(connection, success ? MHD_HTTP_OK : MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
    MHD_destroy_response(resp);
    
    return ret;
}

// ============================================================================
// MAIN REQUEST HANDLER
// ============================================================================

enum MHD_Result handle_request(void* cls, struct MHD_Connection* connection,
                              const char* url, const char* method,
                              const char* version, const char* upload_data,
                              size_t* upload_data_size, void** con_cls) {
    (void)cls;
    (void)version;
    
    // Serve static files
    if (strcmp(url, "/") == 0 || strcmp(url, "/index.html") == 0) {
        return serve_static_file(connection, "frontend/index.html", "text/html");
    }
    else if (strncmp(url, "/css/", 5) == 0) {
        char filepath[256] = "frontend";
        strncat(filepath, url, sizeof(filepath) - strlen(filepath) - 1);
        return serve_static_file(connection, filepath, "text/css");
    }
    else if (strncmp(url, "/js/", 4) == 0) {
        char filepath[256] = "frontend";
        strncat(filepath, url, sizeof(filepath) - strlen(filepath) - 1);
        return serve_static_file(connection, filepath, "application/javascript");
    }
    
    // API routes - Process POST data for login
    else if (strcmp(url, "/api/auth/login") == 0 && strcmp(method, "POST") == 0) {
        struct RequestContext* ctx = (struct RequestContext*)*con_cls;
        
        // Initialize context on first call
        if (ctx == NULL) {
            ctx = malloc(sizeof(struct RequestContext));
            memset(ctx, 0, sizeof(struct RequestContext));
            *con_cls = ctx;
            return MHD_YES;  // Continue to get data
        }
        
        // Accumulate POST data
        if (upload_data && *upload_data_size > 0) {
            size_t remaining = sizeof(ctx->post_data) - ctx->post_data_len;
            size_t to_copy = (*upload_data_size < remaining) ? *upload_data_size : remaining;
            memcpy(ctx->post_data + ctx->post_data_len, upload_data, to_copy);
            ctx->post_data_len += to_copy;
            *upload_data_size = 0;  // Mark as consumed
            return MHD_YES;  // Continue accumulating
        }
        
        // All data received - process login
        enum MHD_Result ret = handle_login(connection, ctx);
        free(ctx);
        *con_cls = NULL;
        return ret;
    }
    else if (strcmp(url, "/api/items") == 0 && strcmp(method, "GET") == 0) {
        return handle_get_items(connection, NULL);
    }
    else if (strcmp(url, "/api/items") == 0 && strcmp(method, "POST") == 0) {
        return handle_create_item(connection, upload_data, upload_data_size);
    }
    else if (strcmp(url, "/api/users") == 0 && strcmp(method, "GET") == 0) {
        return handle_get_users(connection);
    }
    else if (strcmp(url, "/api/users") == 0 && strcmp(method, "POST") == 0) {
        struct RequestContext* ctx = (struct RequestContext*)*con_cls;
        
        // Initialize context on first call
        if (ctx == NULL) {
            ctx = malloc(sizeof(struct RequestContext));
            memset(ctx, 0, sizeof(struct RequestContext));
            *con_cls = ctx;
            return MHD_YES;  // Continue to get data
        }
        
        // Accumulate POST data
        if (upload_data && *upload_data_size > 0) {
            size_t remaining = sizeof(ctx->post_data) - ctx->post_data_len;
            size_t to_copy = (*upload_data_size < remaining) ? *upload_data_size : remaining;
            memcpy(ctx->post_data + ctx->post_data_len, upload_data, to_copy);
            ctx->post_data_len += to_copy;
            *upload_data_size = 0;  // Mark as consumed
            return MHD_YES;  // Continue accumulating
        }
        
        // Parse the accumulated data
        char username[128] = "", password[128] = "", fullname[256] = "";
        char email[128] = "", role[32] = "user";
        
        if (ctx && ctx->post_data_len > 0) {
            char buffer[512];
            strncpy(buffer, ctx->post_data, ctx->post_data_len < sizeof(buffer) ? ctx->post_data_len : sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            
            // Parse username=value&password=value&fullname=value&email=value&role=value
            char* username_start = strstr(buffer, "username=");
            if (username_start) {
                username_start += 9;
                char* username_end = strchr(username_start, '&');
                int username_len = username_end ? (username_end - username_start) : strlen(username_start);
                strncpy(username, username_start, username_len < sizeof(username) ? username_len : sizeof(username) - 1);
                url_decode(username, username, sizeof(username));
            }
            
            char* password_start = strstr(buffer, "password=");
            if (password_start) {
                password_start += 9;
                char* password_end = strchr(password_start, '&');
                int password_len = password_end ? (password_end - password_start) : strlen(password_start);
                strncpy(password, password_start, password_len < sizeof(password) ? password_len : sizeof(password) - 1);
                url_decode(password, password, sizeof(password));
            }
            
            char* fullname_start = strstr(buffer, "fullname=");
            if (fullname_start) {
                fullname_start += 9;
                char* fullname_end = strchr(fullname_start, '&');
                int fullname_len = fullname_end ? (fullname_end - fullname_start) : strlen(fullname_start);
                strncpy(fullname, fullname_start, fullname_len < sizeof(fullname) ? fullname_len : sizeof(fullname) - 1);
                url_decode(fullname, fullname, sizeof(fullname));
            }
            
            char* email_start = strstr(buffer, "email=");
            if (email_start) {
                email_start += 6;
                char* email_end = strchr(email_start, '&');
                int email_len = email_end ? (email_end - email_start) : strlen(email_start);
                strncpy(email, email_start, email_len < sizeof(email) ? email_len : sizeof(email) - 1);
                url_decode(email, email, sizeof(email));
            }
            
            char* role_start = strstr(buffer, "role=");
            if (role_start) {
                role_start += 5;
                char* role_end = strchr(role_start, '&');
                int role_len = role_end ? (role_end - role_start) : strlen(role_start);
                strncpy(role, role_start, role_len < sizeof(role) ? role_len : sizeof(role) - 1);
                url_decode(role, role, sizeof(role));
            }
        }
        
        // Check if username already exists
        User* existing = db_get_user_by_username(global_db, username);
        if (existing) {
            free(existing);
            const char* response = "{\"error\":\"Username already exists\"}";
            struct MHD_Response* resp = MHD_create_response_from_buffer(
                strlen(response), (void*)response, MHD_RESPMEM_PERSISTENT);
            MHD_add_response_header(resp, "Content-Type", "application/json");
            enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);
            MHD_destroy_response(resp);
            free(ctx);
            *con_cls = NULL;
            return ret;
        }
        
        // Create user
        int user_id = db_create_user(global_db, username, password, role, fullname, email);
        
        char response_buf[512];
        if (user_id > 0) {
            snprintf(response_buf, sizeof(response_buf),
                    "{\"status\":\"success\",\"message\":\"User created\",\"user_id\":%d}",
                    user_id);
        } else {
            strcpy(response_buf, "{\"error\":\"Failed to create user\"}");
        }
        
        struct MHD_Response* resp = MHD_create_response_from_buffer(
            strlen(response_buf), (void*)response_buf, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
        MHD_destroy_response(resp);
        free(ctx);
        *con_cls = NULL;
        return ret;
    }
    else if (strcmp(url, "/api/issues") == 0 && strcmp(method, "GET") == 0) {
        return handle_get_issues(connection);
    }
    else if (strcmp(url, "/api/backup/create") == 0 && strcmp(method, "POST") == 0) {
        return handle_backup_create(connection);
    }
    else if (strncmp(url, "/api/backup/download/", 21) == 0 && strcmp(method, "GET") == 0) {
        const char* filename = url + 21;  // Skip "/api/backup/download/"
        return handle_backup_download(connection, filename);
    }
    else if (strcmp(url, "/api/backup/restore") == 0 && strcmp(method, "POST") == 0) {
        struct RequestContext* ctx = (struct RequestContext*)*con_cls;
        
        // First time - initialize context
        if (!ctx) {
            ctx = malloc(sizeof(struct RequestContext));
            ctx->post_data_len = 0;
            *con_cls = ctx;
            return MHD_YES;  // Continue to get data
        }
        
        // Accumulate POST data
        if (upload_data && *upload_data_size > 0) {
            size_t remaining = sizeof(ctx->post_data) - ctx->post_data_len;
            size_t to_copy = (*upload_data_size < remaining) ? *upload_data_size : remaining;
            memcpy(ctx->post_data + ctx->post_data_len, upload_data, to_copy);
            ctx->post_data_len += to_copy;
            *upload_data_size = 0;  // Mark as consumed
            return MHD_YES;  // Continue accumulating
        }
        
        // All data received - process restore
        enum MHD_Result ret = handle_backup_restore(connection, ctx);
        free(ctx);
        *con_cls = NULL;
        return ret;
    }

    else {
        // Default 404
        const char* not_found = "{\"error\":\"Endpoint not found\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(
            strlen(not_found), (void*)not_found, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, resp);
        MHD_destroy_response(resp);
        
        return ret;
    }
}
