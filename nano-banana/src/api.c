#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <microhttpd.h>
#include <sqlite3.h>
#include <curl/curl.h>
#include <ctype.h>
#include "db.h"
#include "api.h"
#include "auth.h"

static sqlite3* global_db = NULL;

void api_init(sqlite3* db) {
    global_db = db;
    curl_global_init(CURL_GLOBAL_ALL);
}

// ============================================================================
// STATIC FILE SERVING
// ============================================================================

static enum MHD_Result serve_static_file(struct MHD_Connection* connection,
                                        const char* filename,
                                        const char* content_type) {
    FILE* f = fopen(filename, "rb");

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
 * Get value from form-urlencoded or JSON data
 */
static const char* get_form_value(const char* data, const char* key) {
    static char buffer[512];
    if (!data || !key) return NULL;

    // Try form-urlencoded first: key=value
    char search[128];
    snprintf(search, sizeof(search), "%s=", key);
    const char* start = strstr(data, search);
    if (start) {
        start += strlen(search);
        const char* end = strchr(start, '&');
        int len = end ? (end - start) : strlen(start);
        if (len >= (int)sizeof(buffer)) len = sizeof(buffer) - 1;
        strncpy(buffer, start, len);
        buffer[len] = '\0';
        
        // Simple URL decode for the value
        int i = 0, j = 0;
        char decoded[512];
        while (buffer[i] && j < (int)sizeof(decoded) - 1) {
            if (buffer[i] == '%' && buffer[i+1] && buffer[i+2]) {
                int val;
                sscanf(buffer + i + 1, "%2x", &val);
                decoded[j++] = (char)val;
                i += 3;
            } else if (buffer[i] == '+') {
                decoded[j++] = ' ';
                i++;
            } else {
                decoded[j++] = buffer[i++];
            }
        }
        decoded[j] = '\0';
        strcpy(buffer, decoded);
        return buffer;
    }

    // Try JSON: "key":"value" or "key":value
    snprintf(search, sizeof(search), "\"%s\":", key);
    start = strstr(data, search);
    if (start) {
        start += strlen(search);
        // Skip whitespace
        while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;
        
        if (*start == '\"') {
            // String value
            start++;
            const char* end = strchr(start, '\"');
            if (end) {
                int len = end - start;
                if (len >= (int)sizeof(buffer)) len = sizeof(buffer) - 1;
                strncpy(buffer, start, len);
                buffer[len] = '\0';
                return buffer;
            }
        } else {
            // Numeric value
            const char* end = start;
            while (*end && (isdigit(*end) || *end == '.' || *end == '-')) end++;
            int len = end - start;
            if (len > 0) {
                if (len >= (int)sizeof(buffer)) len = sizeof(buffer) - 1;
                strncpy(buffer, start, len);
                buffer[len] = '\0';
                return buffer;
            }
        }
    }

    return NULL;
}

/**
 * Get user ID from Authorization header
 */
static int get_user_id_from_token(const char* auth_header) {
    return auth_get_user_id_from_token(auth_header);
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

static void json_escape_string(const char* src, char* dest, size_t dest_size) {
    size_t j = 0;

    if (!src || !dest || dest_size == 0) return;

    for (size_t i = 0; src[i] && j + 2 < dest_size; i++) {
        unsigned char ch = (unsigned char)src[i];
        if (ch == '"' || ch == '\\') {
            dest[j++] = '\\';
            dest[j++] = (char)ch;
        } else if (ch == '\n') {
            dest[j++] = '\\';
            dest[j++] = 'n';
        } else if (ch == '\r') {
            dest[j++] = '\\';
            dest[j++] = 'r';
        } else if (ch == '\t') {
            dest[j++] = '\\';
            dest[j++] = 't';
        } else if (ch >= 32) {
            dest[j++] = (char)ch;
        }
    }

    dest[j] = '\0';
}

static void shell_escape_single_quotes(const char* src, char* dest, size_t dest_size) {
    size_t j = 0;

    if (!src || !dest || dest_size == 0) return;

    for (size_t i = 0; src[i] && j + 5 < dest_size; i++) {
        if (src[i] == '\'') {
            dest[j++] = '\'';
            dest[j++] = '\\';
            dest[j++] = '\'';
            dest[j++] = '\'';
        } else {
            dest[j++] = src[i];
        }
    }

    dest[j] = '\0';
}

/* ---------- libcurl write callback ---------- */
struct curl_buffer {
    char* data;
    size_t size;
    size_t capacity;
};

static size_t curl_write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    struct curl_buffer* buf = (struct curl_buffer*)userp;

    if (buf->size + realsize + 1 >= buf->capacity) {
        buf->capacity = (buf->capacity * 2) + realsize + 1024;
        char* grown = realloc(buf->data, buf->capacity);
        if (!grown) return 0;
        buf->data = grown;
    }

    memcpy(buf->data + buf->size, contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = '\0';
    return realsize;
}

/* ---------- call_ai_service: proxy to Python AI service ---------- */
static char* call_ai_service(const char* endpoint, const char* payload_json) {
    /* Resolve AI service URL:
     *   1. AI_SERVICE_URL env var (set by docker-compose)
     *   2. Docker service name (when running in same compose network)
     *   3. localhost fallback (for local dev outside Docker) */
    const char* ai_url = getenv("AI_SERVICE_URL");
    if (!ai_url || ai_url[0] == '\0') {
        /* Not set by env — try common Docker service names */
        ai_url = "http://ai-service:8000";
        fprintf(stderr, "[ai-service] AI_SERVICE_URL not set, using default: %s\n", ai_url);
    } else {
        fprintf(stderr, "[ai-service] Using AI_SERVICE_URL from env: %s\n", ai_url);
    }

    char full_url[512];
    snprintf(full_url, sizeof(full_url), "%s%s", ai_url, endpoint);
    fprintf(stderr, "[ai-service] Calling: %s\n", full_url);

    char escaped_payload_path[256];
    char payload_path[] = "/tmp/nexus_ai_proxy_XXXXXX";
    char command[2048];
    char* response = NULL;
    size_t total = 0;
    size_t capacity = 4096;
    int fd = mkstemp(payload_path);

    if (fd < 0) {
        fprintf(stderr, "[ai-service] Failed to create temp file\n");
        return NULL;
    }

    FILE* payload_file = fdopen(fd, "w");
    if (!payload_file) {
        close(fd);
        unlink(payload_path);
        fprintf(stderr, "[ai-service] Failed to open temp file for writing\n");
        return NULL;
    }

    fputs(payload_json ? payload_json : "{}", payload_file);
    fclose(payload_file);

    shell_escape_single_quotes(payload_path, escaped_payload_path, sizeof(escaped_payload_path));

    /* DO NOT redirect stderr to /dev/null — we need to see curl errors */
    snprintf(command, sizeof(command),
        "curl -sS -X POST '%s' -H 'Content-Type: application/json' --data-binary @'%s'",
        full_url, escaped_payload_path);

    fprintf(stderr, "[ai-service] curl command: %s\n", command);

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        unlink(payload_path);
        fprintf(stderr, "[ai-service] popen() failed\n");
        return NULL;
    }

    response = malloc(capacity);
    if (!response) {
        pclose(pipe);
        unlink(payload_path);
        fprintf(stderr, "[ai-service] malloc failed\n");
        return NULL;
    }

    while (!feof(pipe)) {
        if (total + 1024 >= capacity) {
            capacity *= 2;
            char* grown = realloc(response, capacity);
            if (!grown) {
                free(response);
                pclose(pipe);
                unlink(payload_path);
                fprintf(stderr, "[ai-service] realloc failed\n");
                return NULL;
            }
            response = grown;
        }

        size_t chunk = fread(response + total, 1, capacity - total - 1, pipe);
        total += chunk;
        if (chunk == 0) break;
    }

    response[total] = '\0';
    int exit_code = pclose(pipe);
    unlink(payload_path);

    if (total == 0) {
        free(response);
        fprintf(stderr, "[ai-service] Empty response from AI service (curl exit code: %d)\n", exit_code);
        return NULL;
    }

    /* Check if response looks like JSON */
    if (response[0] != '{' && response[0] != '[') {
        fprintf(stderr, "[ai-service] Non-JSON response (%zu bytes): %.*s\n",
            total, (int)(total > 200 ? 200 : total), response);
        free(response);
        return NULL;
    }

    fprintf(stderr, "[ai-service] Received %zu bytes from AI service\n", total);
    return response;
}

static char* build_ai_context_json(int user_id, const char* message) {
    int item_count = 0;
    int user_count = 0;
    int issue_count = 0;
    Item** items = db_get_items(global_db, "", 0, 0, &item_count);
    User** users = db_get_all_users(global_db, &user_count);
    IssueRecord** issues = db_get_issues(global_db, "", "", &issue_count);
    char* payload = malloc(131072);
    char escaped_message[2048];
    int offset;

    if (!payload) {
        free_items(items, item_count);
        free_users(users, user_count);
        free_issues(issues, issue_count);
        return NULL;
    }

    json_escape_string(message ? message : "", escaped_message, sizeof(escaped_message));
    offset = snprintf(payload, 131072,
        "{\"user_id\":%d,\"message\":\"%s\",\"counts\":{\"items\":%d,\"users\":%d,\"issues\":%d},\"items\":[",
        user_id, escaped_message, item_count, user_count, issue_count);

    for (int i = 0; i < item_count && offset < 126000; i++) {
        char sku[256], name[512], description[1024], condition[128], location[512];
        json_escape_string(items[i]->sku, sku, sizeof(sku));
        json_escape_string(items[i]->name, name, sizeof(name));
        json_escape_string(items[i]->description, description, sizeof(description));
        json_escape_string(items[i]->condition, condition, sizeof(condition));
        json_escape_string(items[i]->location, location, sizeof(location));
        offset += snprintf(payload + offset, 131072 - offset,
            "%s{\"id\":%d,\"sku\":\"%s\",\"name\":\"%s\",\"description\":\"%s\",\"condition\":\"%s\",\"quantity\":%d,\"min_quantity\":%d,\"unit_price\":%.2f,\"location\":\"%s\"}",
            i ? "," : "", items[i]->id, sku, name, description, condition,
            items[i]->quantity, items[i]->min_quantity, items[i]->unit_price, location);
    }

    offset += snprintf(payload + offset, 131072 - offset, "],\"users\":[");
    for (int i = 0; i < user_count && offset < 128000; i++) {
        char username[256], full_name[512], role[128];
        json_escape_string(users[i]->username, username, sizeof(username));
        json_escape_string(users[i]->full_name, full_name, sizeof(full_name));
        json_escape_string(users[i]->role, role, sizeof(role));
        offset += snprintf(payload + offset, 131072 - offset,
            "%s{\"id\":%d,\"username\":\"%s\",\"full_name\":\"%s\",\"role\":\"%s\"}",
            i ? "," : "", users[i]->id, username, full_name, role);
    }

    offset += snprintf(payload + offset, 131072 - offset, "],\"issues\":[");
    for (int i = 0; i < issue_count && offset < 130000; i++) {
        char issued_to[512], assignee[512], status[128], issue_date[128], expected_return_date[128];
        json_escape_string(issues[i]->issued_to, issued_to, sizeof(issued_to));
        json_escape_string(issues[i]->assignee, assignee, sizeof(assignee));
        json_escape_string(issues[i]->status, status, sizeof(status));
        json_escape_string(issues[i]->issue_date, issue_date, sizeof(issue_date));
        json_escape_string(issues[i]->expected_return_date, expected_return_date, sizeof(expected_return_date));
        offset += snprintf(payload + offset, 131072 - offset,
            "%s{\"id\":%d,\"item_id\":%d,\"issued_to\":\"%s\",\"assignee\":\"%s\",\"quantity\":%d,\"quantity_returned\":%d,\"status\":\"%s\",\"issue_date\":\"%s\",\"expected_return_date\":\"%s\"}",
            i ? "," : "", issues[i]->id, issues[i]->item_id, issued_to, assignee,
            issues[i]->quantity, issues[i]->quantity_returned, status, issue_date, expected_return_date);
    }

    snprintf(payload + offset, 131072 - offset, "]}");

    free_items(items, item_count);
    free_users(users, user_count);
    free_issues(issues, issue_count);
    return payload;
}

static enum MHD_Result queue_ai_proxy_response(struct MHD_Connection* connection,
                                               const char* service_path,
                                               const char* payload_json) {
    char* response = call_ai_service(service_path, payload_json);
    int status = response ? MHD_HTTP_OK : MHD_HTTP_BAD_GATEWAY;

    if (!response) {
        response = json_error("AI helper service unavailable");
    }

    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(response), (void*)response, MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(connection, status, resp);
    MHD_destroy_response(resp);
    return ret;
}

// ============================================================================
// API ENDPOINTS - AUTHENTICATION
// ============================================================================

// Structure to hold request state
struct RequestContext {
    char post_data[2097152];
    size_t post_data_len;
};

static enum MHD_Result handle_login(struct MHD_Connection* connection,
                                   struct RequestContext* ctx) {
    // Parse JSON POST data from accumulated context
    char username[128] = {0};
    char password[128] = {0};
    
    if (ctx && ctx->post_data_len > 0) {
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
        
        fprintf(stderr, "[auth] Login attempt: username='%s'\n", username);
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
    (void)query;
    int count = 0;
    Item** items = db_get_items(global_db, "", 0, 0, &count);
    
    char* response = malloc(16384);
    strcpy(response, "[");
    
    for (int i = 0; i < count; i++) {
        char item_json[1024];
        snprintf(item_json, sizeof(item_json),
                "{\"id\":%d,\"sku\":\"%s\",\"name\":\"%s\",\"description\":\"%s\","
                "\"condition\":\"%s\",\"quantity\":%d,\"min_quantity\":%d,\"unit_price\":%.2f,"
                "\"location\":\"%s\"%s",
                items[i]->id, items[i]->sku, items[i]->name, items[i]->description,
                items[i]->condition,
                items[i]->quantity, items[i]->min_quantity, items[i]->unit_price,
                items[i]->location, (i < count - 1) ? "}," : "}");
        
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
                                         struct RequestContext* ctx) {
    const char* auth = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Authorization");
    int user_id = get_user_id_from_token(auth);
    if (user_id <= 0) {
        const char* err = "{\"error\":\"Unauthorized\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(err), (void*)err, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        return MHD_queue_response(connection, MHD_HTTP_UNAUTHORIZED, resp);
    }

    // Parse POST data for item creation
    char sku[128] = "", name[256] = "", description[512] = "", condition[32] = "Working";
    int quantity = 0;
    
    const char* val;
    if ((val = get_form_value(ctx->post_data, "sku"))) strncpy(sku, val, 127);
    if ((val = get_form_value(ctx->post_data, "name"))) strncpy(name, val, 255);
    if ((val = get_form_value(ctx->post_data, "description"))) strncpy(description, val, 511);
    if ((val = get_form_value(ctx->post_data, "condition"))) strncpy(condition, val, 31);
    if ((val = get_form_value(ctx->post_data, "quantity"))) quantity = atoi(val);
    
    int item_id = db_create_item(global_db, sku, name, description, 0, quantity, 0, 0.0, "", "", condition);
    
    char response[256];
    if (item_id > 0) {
        snprintf(response, sizeof(response), "{\"status\":\"success\",\"message\":\"Item created\",\"id\":%d}", item_id);
    } else {
        strcpy(response, "{\"error\":\"Failed to create item\"}");
    }
    
    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(response), (void*)response, MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    return ret;
}

static enum MHD_Result handle_delete_item(struct MHD_Connection* connection, int item_id) {
    const char* auth = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Authorization");
    int user_id = get_user_id_from_token(auth);
    if (user_id <= 0) return MHD_NO;

    int success = db_delete_item(global_db, item_id);
    
    char* response = success ? json_success("Item deleted") : json_error("Failed to delete item");
    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(response), (void*)response, MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    free(response);
    return ret;
}

static enum MHD_Result handle_edit_item(struct MHD_Connection* connection, int item_id, struct RequestContext* ctx) {
    const char* auth = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Authorization");
    int user_id = get_user_id_from_token(auth);
    if (user_id <= 0) return MHD_NO;

    char sku[128] = "", name[256] = "", description[512] = "", condition[32] = "";
    int quantity = -1;
    
    const char* val;
    if ((val = get_form_value(ctx->post_data, "sku"))) strncpy(sku, val, 127);
    if ((val = get_form_value(ctx->post_data, "name"))) strncpy(name, val, 255);
    if ((val = get_form_value(ctx->post_data, "description"))) strncpy(description, val, 511);
    if ((val = get_form_value(ctx->post_data, "condition"))) strncpy(condition, val, 31);
    if ((val = get_form_value(ctx->post_data, "quantity"))) quantity = atoi(val);

    Item* existing = db_get_item(global_db, item_id);
    if (!existing) {
        char* response = json_error("Item not found");
        struct MHD_Response* resp = MHD_create_response_from_buffer(
            strlen(response), (void*)response, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, resp);
        MHD_destroy_response(resp);
        free(response);
        return ret;
    }

    if (strlen(name) == 0) strncpy(name, existing->name, sizeof(name) - 1);
    if (strlen(description) == 0) strncpy(description, existing->description, sizeof(description) - 1);
    if (strlen(condition) == 0) strncpy(condition, existing->condition, sizeof(condition) - 1);
    if (quantity < 0) quantity = existing->quantity;

    int success = db_update_item(global_db, item_id, name, description, existing->category_id,
                                 quantity, existing->min_quantity, existing->unit_price,
                                 existing->supplier, existing->location, condition);
    free(existing);
    
    char* response = success ? json_success("Item updated") : json_error("Failed to update item");
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

    // Build response more safely
    char* response = malloc(16384);  // Larger buffer
    char* pos = response;
    int remaining = 16384;
    
    int written = snprintf(pos, remaining, "[");
    pos += written;
    remaining -= written;
    
    for (int i = 0; i < count; i++) {
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

    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(response), (void*)response, MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    
    free_users(users, count);
    
    return ret;
}

static enum MHD_Result handle_create_user(struct MHD_Connection* connection, struct RequestContext* ctx) {
    const char* auth = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Authorization");
    int admin_id = get_user_id_from_token(auth);
    if (admin_id <= 0) return MHD_NO;

    char username[128] = "", password[128] = "", fullname[256] = "";
    char email[128] = "", role[32] = "viewer";
    
    const char* val;
    if ((val = get_form_value(ctx->post_data, "username"))) strncpy(username, val, 127);
    if ((val = get_form_value(ctx->post_data, "password"))) strncpy(password, val, 127);
    if ((val = get_form_value(ctx->post_data, "fullname"))) strncpy(fullname, val, 255);
    if ((val = get_form_value(ctx->post_data, "email"))) strncpy(email, val, 127);
    if ((val = get_form_value(ctx->post_data, "role"))) strncpy(role, val, 31);
    
    if (strlen(username) == 0 || strlen(password) == 0) {
        char* err = json_error("Username and password required");
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(err), (void*)err, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        return MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);
    }

    User* existing = db_get_user_by_username(global_db, username);
    if (existing) {
        free(existing);
        char* err = json_error("Username already exists");
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(err), (void*)err, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        return MHD_queue_response(connection, MHD_HTTP_CONFLICT, resp);
    }
    
    int user_id = db_create_user(global_db, username, password, role, fullname, email);
    
    char response[256];
    if (user_id > 0) {
        snprintf(response, sizeof(response), "{\"status\":\"success\",\"message\":\"User created\",\"user_id\":%d}", user_id);
    } else {
        strcpy(response, "{\"error\":\"Failed to create user\"}");
    }
    
    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(response), (void*)response, MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    return ret;
}

static enum MHD_Result handle_delete_user(struct MHD_Connection* connection, int user_id) {
    const char* auth = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Authorization");
    int admin_id = get_user_id_from_token(auth);
    if (admin_id <= 0) return MHD_NO;
    if (admin_id == user_id) {
        char* response = json_error("You cannot delete your own account");
        struct MHD_Response* resp = MHD_create_response_from_buffer(
            strlen(response), (void*)response, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);
        MHD_destroy_response(resp);
        free(response);
        return ret;
    }

    int success = db_delete_user(global_db, user_id);
    
    char* response = success ? json_success("User deleted") : json_error("Failed to delete user");
    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(response), (void*)response, MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    free(response);
    return ret;
}

static enum MHD_Result handle_edit_user(struct MHD_Connection* connection, int user_id, struct RequestContext* ctx) {
    const char* auth = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Authorization");
    int admin_id = get_user_id_from_token(auth);
    if (admin_id <= 0) return MHD_NO;

    User* existing = db_get_user(global_db, user_id);
    if (!existing || !existing->is_active) {
        if (existing) free(existing);
        char* response = json_error("User not found");
        struct MHD_Response* resp = MHD_create_response_from_buffer(
            strlen(response), (void*)response, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, resp);
        MHD_destroy_response(resp);
        free(response);
        return ret;
    }

    char role[32] = "";
    char fullname[256] = "";
    char email[128] = "";
    int is_active = existing->is_active;

    const char* val;
    if ((val = get_form_value(ctx->post_data, "role"))) strncpy(role, val, sizeof(role) - 1);
    if ((val = get_form_value(ctx->post_data, "fullname"))) strncpy(fullname, val, sizeof(fullname) - 1);
    if ((val = get_form_value(ctx->post_data, "email"))) strncpy(email, val, sizeof(email) - 1);

    if (strlen(role) == 0) strncpy(role, existing->role, sizeof(role) - 1);
    if (strlen(fullname) == 0) strncpy(fullname, existing->full_name, sizeof(fullname) - 1);
    if (strlen(email) == 0) strncpy(email, existing->email, sizeof(email) - 1);

    int success = db_update_user(global_db, user_id, role, fullname, email, is_active);
    free(existing);

    char* response = success ? json_success("User updated") : json_error("Failed to update user");
    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(response), (void*)response, MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    free(response);
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
        char issue_json[1024];
        snprintf(issue_json, sizeof(issue_json),
                "{\"id\":%d,\"item_id\":%d,\"assignee\":\"%s\",\"issued_to\":\"%s\","
                "\"quantity\":%d,\"quantity_returned\":%d,\"status\":\"%s\","
                "\"issue_date\":\"%s\",\"return_date\":\"%s\","
                "\"expected_return_date\":\"%s\",\"actual_return_date\":\"%s\"%s",
                issues[i]->id, issues[i]->item_id, issues[i]->assignee, issues[i]->issued_to,
                issues[i]->quantity, issues[i]->quantity_returned, issues[i]->status,
                issues[i]->issue_date, issues[i]->return_date,
                issues[i]->expected_return_date, issues[i]->actual_return_date,
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

static enum MHD_Result handle_get_overdue_issues(struct MHD_Connection* connection) {
    int count = 0;
    IssueRecord** issues = db_get_overdue_issues(global_db, &count);
    char* response = malloc(16384);
    strcpy(response, "[");

    for (int i = 0; i < count; i++) {
        char issue_json[1024];
        snprintf(issue_json, sizeof(issue_json),
                "{\"id\":%d,\"item_id\":%d,\"issued_to\":\"%s\",\"status\":\"OVERDUE\","
                "\"issue_date\":\"%s\",\"expected_return_date\":\"%s\"%s",
                issues[i]->id, issues[i]->item_id, issues[i]->issued_to,
                issues[i]->issue_date, issues[i]->expected_return_date,
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

static enum MHD_Result handle_post_issue(struct MHD_Connection* connection,
                                        struct RequestContext* ctx) {
    const char* auth = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Authorization");
    int user_id = get_user_id_from_token(auth);
    if (user_id <= 0) return MHD_NO;

    int item_id = 0;
    int quantity = 0;
    char assignee[128] = "";
    char purpose[256] = "";
    char issue_date[32] = "";
    char return_date[32] = "";

    const char* val;
    if ((val = get_form_value(ctx->post_data, "item_id"))) item_id = atoi(val);
    if ((val = get_form_value(ctx->post_data, "quantity"))) quantity = atoi(val);
    if ((val = get_form_value(ctx->post_data, "assignee"))) strncpy(assignee, val, 127);
    if ((val = get_form_value(ctx->post_data, "purpose"))) strncpy(purpose, val, 255);
    if ((val = get_form_value(ctx->post_data, "issue_date"))) strncpy(issue_date, val, 31);
    if ((val = get_form_value(ctx->post_data, "return_date"))) strncpy(return_date, val, 31);

    int issue_id = db_create_issue(global_db, item_id, user_id, quantity, 
                                 assignee, purpose, "NEXUS-UI", return_date, 0.0, issue_date);

    char response[256];
    if (issue_id > 0) {
        snprintf(response, sizeof(response), 
                "{\"status\":\"success\",\"message\":\"Issue created\",\"issue_id\":%d}", 
                issue_id);
    } else {
        strcpy(response, "{\"error\":\"Failed to create issue\"}");
    }

    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(response), (void*)response, MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    return ret;
}

static enum MHD_Result handle_return_issue(struct MHD_Connection* connection,
                                          int issue_id, struct RequestContext* ctx) {
    const char* auth = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Authorization");
    int user_id = get_user_id_from_token(auth);
    if (user_id <= 0) return MHD_NO;

    int quantity_returned = 0;
    char condition[64] = "Good";
    char notes[256] = "";

    const char* val;
    if ((val = get_form_value(ctx->post_data, "quantity_returned"))) quantity_returned = atoi(val);
    if ((val = get_form_value(ctx->post_data, "condition"))) strncpy(condition, val, 63);
    if ((val = get_form_value(ctx->post_data, "notes"))) strncpy(notes, val, 255);

    int result = db_return_issue(global_db, issue_id, user_id, quantity_returned, condition, notes);

    char response[256];
    if (result != 0) {
        strcpy(response, "{\"status\":\"success\",\"message\":\"Items returned\"}");
    } else {
        strcpy(response, "{\"error\":\"Failed to return items\"}");
    }

    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(response), (void*)response, MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    return ret;
}

/* ---------- Rule-based fallback helpers ---------- */

/* Build a minimal inventory context string for the AI prompt */
static char* build_inventory_context_json(void) {
    /* Build structured JSON inventory snapshot for the Python AI service.
     * The Python service expects:
     * {"low_stock":[...],"damaged":[...],"overdue":[...]} */
    int item_count = 0;
    Item** items = db_get_items(global_db, "", 0, 0, &item_count);
    int issue_count = 0;
    IssueRecord** issues = db_get_issues(global_db, "", "", &issue_count);

    static char buf[8192];
    int off = 0;

    off = snprintf(buf, sizeof(buf),
        "{\"low_stock\":[");

    /* Low stock items */
    int first = 1;
    for (int i = 0; i < item_count && off < (int)sizeof(buf) - 200; i++) {
        if (items[i]->quantity < 5) {
            char name[256], sku[128];
            json_escape_string(items[i]->name, name, sizeof(name));
            json_escape_string(items[i]->sku, sku, sizeof(sku));
            off += snprintf(buf + off, sizeof(buf) - off,
                "%s{\"name\":\"%s\",\"sku\":\"%s\",\"quantity\":%d,\"min_quantity\":%d}",
                first ? "" : ",", name, sku, items[i]->quantity, items[i]->min_quantity);
            first = 0;
        }
    }

    off += snprintf(buf + off, sizeof(buf) - off, "],\"damaged\":[");

    /* Damaged items */
    first = 1;
    for (int i = 0; i < item_count && off < (int)sizeof(buf) - 200; i++) {
        if (strcmp(items[i]->condition, "Damaged") == 0) {
            char name[256], sku[128], loc[256];
            json_escape_string(items[i]->name, name, sizeof(name));
            json_escape_string(items[i]->sku, sku, sizeof(sku));
            json_escape_string(items[i]->location, loc, sizeof(loc));
            off += snprintf(buf + off, sizeof(buf) - off,
                "%s{\"name\":\"%s\",\"sku\":\"%s\",\"location\":\"%s\"}",
                first ? "" : ",", name, sku, loc);
            first = 0;
        }
    }

    off += snprintf(buf + off, sizeof(buf) - off, "],\"overdue\":[");

    /* Overdue issues */
    first = 1;
    for (int i = 0; i < issue_count && off < (int)sizeof(buf) - 200; i++) {
        if (strcmp(issues[i]->status, "OVERDUE") == 0) {
            char issued_to[256], exp_date[128];
            json_escape_string(issues[i]->issued_to, issued_to, sizeof(issued_to));
            json_escape_string(issues[i]->expected_return_date, exp_date, sizeof(exp_date));
            off += snprintf(buf + off, sizeof(buf) - off,
                "%s{\"item_id\":%d,\"issued_to\":\"%s\",\"quantity\":%d,\"expected_return_date\":\"%s\"}",
                first ? "" : ",", issues[i]->item_id, issued_to, issues[i]->quantity, exp_date);
            first = 0;
        }
    }

    off += snprintf(buf + off, sizeof(buf) - off, "]}");

    free_items(items, item_count);
    free_issues(issues, issue_count);
    return buf;
}

static char* rule_based_chat_reply(const char* message) {
    /* Minimal fallback — only used when Python AI service is unreachable */
    char* ctx = build_inventory_context_json();

    static char reply[2048];
    snprintf(reply, sizeof(reply),
        "I can't reach the AI right now, but here's the current snapshot: %s. "
        "The AI service should be back soon for full details.",
        ctx);
    return reply;
}

/* ---------- AI endpoint handlers (OpenRouter via libcurl) ---------- */

static enum MHD_Result handle_ai_alerts(struct MHD_Connection* connection) {
    const char* auth = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Authorization");
    int user_id = get_user_id_from_token(auth);
    if (user_id <= 0) {
        const char* err = "{\"error\":\"Unauthorized\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(err), (void*)err, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_UNAUTHORIZED, resp);
        MHD_destroy_response(resp);
        return ret;
    }

    /* Build context and forward to Python AI service for alerts (rule-based or AI-enriched) */
    char* payload = build_ai_context_json(user_id, NULL);
    if (!payload) {
        char* response = json_error("Failed to prepare AI request");
        struct MHD_Response* resp = MHD_create_response_from_buffer(
            strlen(response), (void*)response, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
        MHD_destroy_response(resp);
        return ret;
    }

    enum MHD_Result ret = queue_ai_proxy_response(connection, "/ai/alerts", payload);
    free(payload);
    return ret;
}

static enum MHD_Result handle_ai_chat(struct MHD_Connection* connection, struct RequestContext* ctx) {
    const char* auth = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Authorization");
    int user_id = get_user_id_from_token(auth);
    if (user_id <= 0) {
        const char* err = "{\"error\":\"Unauthorized\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(err), (void*)err, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_UNAUTHORIZED, resp);
        MHD_destroy_response(resp);
        return ret;
    }

    const char* message = get_form_value(ctx->post_data, "message");
    const char* user_msg = message ? message : "";

    /* Build structured JSON inventory context */
    char* inv_ctx = build_inventory_context_json();

    /* Build JSON body for Python AI service: {message, context_json} */
    char escaped_msg[4096];
    char escaped_ctx[8192];
    json_escape_string(user_msg, escaped_msg, sizeof(escaped_msg));
    json_escape_string(inv_ctx, escaped_ctx, sizeof(escaped_ctx));

    char body[8192];
    snprintf(body, sizeof(body),
        "{\"message\":\"%s\",\"context\":\"%s\"}",
        escaped_msg, escaped_ctx);

    fprintf(stderr, "[ai-chat] Proxying request to Python AI service\n");

    /* Send to Python AI service */
    char* response = call_ai_service("/ai/chat", body);

    if (response) {
        fprintf(stderr, "[ai-chat] Received response from AI service\n");
        struct MHD_Response* resp = MHD_create_response_from_buffer(
            strlen(response), (void*)response, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
        MHD_destroy_response(resp);
        return ret;
    }

    /* Fallback: rule-based reply */
    fprintf(stderr, "[ai-chat] AI service unreachable, using fallback\n");
    char* fallback = rule_based_chat_reply(user_msg);
    char escaped_fb[2048];
    json_escape_string(fallback, escaped_fb, sizeof(escaped_fb));
    char* fb_response = malloc(4096);
    snprintf(fb_response, 4096, "{\"reply\":\"%s\",\"model\":\"rule-based-fallback\"}", escaped_fb);

    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(fb_response), (void*)fb_response, MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    return ret;
}

static enum MHD_Result handle_ai_report(struct MHD_Connection* connection) {
    const char* auth = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Authorization");
    int user_id = get_user_id_from_token(auth);
    if (user_id <= 0) {
        const char* err = "{\"error\":\"Unauthorized\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(err), (void*)err, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_UNAUTHORIZED, resp);
        MHD_destroy_response(resp);
        return ret;
    }

    char* payload = build_ai_context_json(user_id, NULL);
    if (!payload) {
        char* response = json_error("Failed to prepare AI request");
        struct MHD_Response* resp = MHD_create_response_from_buffer(
            strlen(response), (void*)response, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
        MHD_destroy_response(resp);
        return ret;
    }

    enum MHD_Result ret = queue_ai_proxy_response(connection, "/ai/report", payload);
    free(payload);
    return ret;
}

// ============================================================================
// BACKUP OPERATIONS
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
    
    // 1. Handle Static Files (GET only)
    if (strcmp(method, "GET") == 0) {
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
        
        // API GET Routes
        if (strcmp(url, "/api/items") == 0) return handle_get_items(connection, NULL);
        if (strcmp(url, "/api/users") == 0) return handle_get_users(connection);
        if (strcmp(url, "/api/issues/overdue") == 0) return handle_get_overdue_issues(connection);
        if (strcmp(url, "/api/issues") == 0) return handle_get_issues(connection);
        if (strcmp(url, "/api/ai/alerts") == 0) return handle_ai_alerts(connection);
        if (strncmp(url, "/api/backup/download/", 21) == 0) {
            return handle_backup_download(connection, url + 21);
        }
    }

    // 2. Handle DELETE routes
    if (strcmp(method, "DELETE") == 0) {
        if (strncmp(url, "/api/items/", 11) == 0) {
            return handle_delete_item(connection, atoi(url + 11));
        }
        if (strncmp(url, "/api/users/", 11) == 0) {
            return handle_delete_user(connection, atoi(url + 11));
        }
    }

    // 3. Handle POST/PUT routes (Data accumulation)
    if (strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0) {
        struct RequestContext* ctx = (struct RequestContext*)*con_cls;
        
        if (ctx == NULL) {
            ctx = calloc(1, sizeof(struct RequestContext));
            *con_cls = ctx;
            return MHD_YES;
        }
        
        if (upload_data && *upload_data_size > 0) {
            size_t remaining = sizeof(ctx->post_data) - ctx->post_data_len - 1;
            size_t to_copy = (*upload_data_size < remaining) ? *upload_data_size : remaining;
            memcpy(ctx->post_data + ctx->post_data_len, upload_data, to_copy);
            ctx->post_data_len += to_copy;
            ctx->post_data[ctx->post_data_len] = '\0';
            *upload_data_size = 0;
            return MHD_YES;
        }
        
        // All data received, route to handler
        enum MHD_Result ret = MHD_NO;
        
        if (strcmp(url, "/api/auth/login") == 0) ret = handle_login(connection, ctx);
        else if (strcmp(url, "/api/items") == 0 && strcmp(method, "POST") == 0) ret = handle_create_item(connection, ctx);
        else if (strncmp(url, "/api/items/", 11) == 0 && (strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0)) 
            ret = handle_edit_item(connection, atoi(url + 11), ctx);
        else if (strcmp(url, "/api/users") == 0 && strcmp(method, "POST") == 0) ret = handle_create_user(connection, ctx);
        else if (strncmp(url, "/api/users/", 11) == 0 && (strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0))
            ret = handle_edit_user(connection, atoi(url + 11), ctx);
        else if (strcmp(url, "/api/issues") == 0 && strcmp(method, "POST") == 0) ret = handle_post_issue(connection, ctx);
        else if (strncmp(url, "/api/issues/", 12) == 0 && strstr(url, "/return")) 
            ret = handle_return_issue(connection, atoi(url + 12), ctx);
        else if (strcmp(url, "/api/ai/chat") == 0 && strcmp(method, "POST") == 0)
            ret = handle_ai_chat(connection, ctx);
        else if (strcmp(url, "/api/reports/ai") == 0 && strcmp(method, "POST") == 0)
            ret = handle_ai_report(connection);
        else if (strcmp(url, "/api/backup/create") == 0) ret = handle_backup_create(connection);
        else if (strcmp(url, "/api/backup/restore") == 0) ret = handle_backup_restore(connection, ctx);
        
        free(ctx);
        *con_cls = NULL;
        return (ret == 0) ? MHD_NO : ret;
    }

    // Default 404
    const char* not_found = "{\"error\":\"Endpoint not found\"}";
    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(not_found), (void*)not_found, MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    return MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, resp);
}
