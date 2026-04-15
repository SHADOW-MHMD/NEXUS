#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <openssl/evp.h>
#include <libpq-fe.h>
#include <microhttpd.h>
#include <curl/curl.h>
#include <ctype.h>
#include "db.h"
#include "api.h"
#include "auth.h"

static PGconn* global_db = NULL;
static int oauth_columns_checked = 0;
static int oauth_has_google_id = 0;
static int oauth_has_provider = 0;

void api_init(PGconn* db) {
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
    MHD_add_response_header(resp, "Cross-Origin-Opener-Policy", "same-origin-allow-popups");
    MHD_add_response_header(resp, "Cross-Origin-Embedder-Policy", "unsafe-none");
    
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
    static char buffer[8192];
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

static enum MHD_Result queue_json_response(struct MHD_Connection* connection,
                                           unsigned int status_code,
                                           char* response_body) {
    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(response_body), (void*)response_body, MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    {
        enum MHD_Result ret = MHD_queue_response(connection, status_code, resp);
        MHD_destroy_response(resp);
        return ret;
    }
}

static enum MHD_Result queue_redirect_response(struct MHD_Connection* connection,
                                               const char* location) {
    struct MHD_Response* resp = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(resp, "Location", location);
    {
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_FOUND, resp);
        MHD_destroy_response(resp);
        return ret;
    }
}

static enum MHD_Result queue_plain_json_response(struct MHD_Connection* connection,
                                                 unsigned int status_code,
                                                 const char* response_body) {
    struct MHD_Response* resp = MHD_create_response_from_buffer(
        strlen(response_body), (void*)response_body, MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    {
        enum MHD_Result ret = MHD_queue_response(connection, status_code, resp);
        MHD_destroy_response(resp);
        return ret;
    }
}

static const char* get_google_redirect_uri(void) {
    const char* redirect_uri = getenv("GOOGLE_REDIRECT_URI");
    return (redirect_uri && redirect_uri[0]) ? redirect_uri : "http://localhost:8080/api/auth/oauth/callback";
}

static const char* get_frontend_redirect_base(void) {
    const char* frontend_url = getenv("FRONTEND_BASE_URL");
    return (frontend_url && frontend_url[0]) ? frontend_url : "";
}

static long get_jwt_expiration_seconds(void) {
    const char* exp = getenv("JWT_EXPIRATION_SECONDS");
    long value = exp && exp[0] ? strtol(exp, NULL, 10) : 86400;
    return value > 0 ? value : 86400;
}

static int has_json_callback_response(struct MHD_Connection* connection) {
    const char* response_mode = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "response");
    const char* format = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "format");

    if ((response_mode && strcmp(response_mode, "json") == 0) ||
        (format && strcmp(format, "json") == 0)) {
        return 1;
    }

    return 0;
}

static int json_extract_string(const char* json, const char* key, char* out, size_t out_size) {
    const char* value = get_form_value(json, key);
    if (!value || !out || out_size == 0) return 0;
    strncpy(out, value, out_size - 1);
    out[out_size - 1] = '\0';
    return 1;
}

static int json_extract_long(const char* json, const char* key, long* out) {
    const char* value = get_form_value(json, key);
    if (!value || !out) return 0;
    *out = strtol(value, NULL, 10);
    return 1;
}

static int url_encode_value(const char* input, char* output, size_t output_size) {
    CURL* curl;
    char* escaped;

    if (!output || output_size == 0) return 0;
    output[0] = '\0';

    curl = curl_easy_init();
    if (!curl) return 0;

    escaped = curl_easy_escape(curl, input ? input : "", 0);
    if (!escaped) {
        curl_easy_cleanup(curl);
        return 0;
    }

    snprintf(output, output_size, "%s", escaped);
    curl_free(escaped);
    curl_easy_cleanup(curl);
    return 1;
}

static char* base64url_decode_segment(const char* input) {
    size_t input_len = strlen(input);
    size_t padded_len = input_len;
    char* padded;
    unsigned char* output;
    int decoded_len;
    size_t i;

    while ((padded_len % 4) != 0) padded_len++;
    padded = malloc(padded_len + 1);
    if (!padded) return NULL;
    memset(padded, '=', padded_len);
    padded[padded_len] = '\0';

    for (i = 0; i < input_len; i++) {
        if (input[i] == '-') padded[i] = '+';
        else if (input[i] == '_') padded[i] = '/';
        else padded[i] = input[i];
    }

    output = malloc((padded_len / 4) * 3 + 1);
    if (!output) {
        free(padded);
        return NULL;
    }

    decoded_len = EVP_DecodeBlock(output, (const unsigned char*)padded, (int)padded_len);
    free(padded);
    if (decoded_len < 0) {
        free(output);
        return NULL;
    }

    if ((input_len % 4) == 2) decoded_len -= 2;
    else if ((input_len % 4) == 3) decoded_len -= 1;
    output[decoded_len] = '\0';
    return (char*)output;
}

static int parse_jwt_payload_json(const char* jwt, char** payload_json) {
    const char* first_dot;
    const char* second_dot;
    char* payload_segment;
    char* decoded_payload;

    if (!jwt || !payload_json) return 0;
    first_dot = strchr(jwt, '.');
    if (!first_dot) return 0;
    second_dot = strchr(first_dot + 1, '.');
    if (!second_dot) return 0;

    payload_segment = strndup(first_dot + 1, (size_t)(second_dot - first_dot - 1));
    if (!payload_segment) return 0;
    decoded_payload = base64url_decode_segment(payload_segment);
    free(payload_segment);
    if (!decoded_payload) return 0;
    *payload_json = decoded_payload;
    return 1;
}

static int ensure_oauth_schema_flags(void) {
    const char* columns[2] = {"google_id", "oauth_provider"};
    int* flags[2] = {&oauth_has_google_id, &oauth_has_provider};
    int i;

    if (oauth_columns_checked) return 1;

    for (i = 0; i < 2; i++) {
        const char* values[1] = {columns[i]};
        PGresult* result = PQexecParams(
            global_db,
            "SELECT 1 FROM information_schema.columns "
            "WHERE table_schema='public' AND table_name='users' AND column_name=$1",
            1, NULL, values, NULL, NULL, 0);
        if (!result || PQresultStatus(result) != PGRES_TUPLES_OK) {
            if (result) PQclear(result);
            return 0;
        }
        *flags[i] = PQntuples(result) > 0;
        PQclear(result);
    }

    oauth_columns_checked = 1;
    return 1;
}

static void derive_username_base(const char* email, const char* name, char* out, size_t out_size) {
    const char* source = NULL;
    size_t i, j = 0;

    if (email && email[0]) {
        source = email;
    } else if (name && name[0]) {
        source = name;
    } else {
        source = "viewer";
    }

    for (i = 0; source[i] && source[i] != '@' && j + 1 < out_size; i++) {
        unsigned char c = (unsigned char)source[i];
        if (isalnum(c)) out[j++] = (char)tolower(c);
        else if (c == '.' || c == '_' || c == '-') out[j++] = '_';
    }

    if (j == 0) {
        snprintf(out, out_size, "viewer");
    } else {
        out[j] = '\0';
    }
}

static void derive_unique_username(const char* email, const char* name, char* out, size_t out_size) {
    char base[128];
    int suffix = 0;

    derive_username_base(email, name, base, sizeof(base));
    snprintf(out, out_size, "%s", base);

    while (db_get_user_by_username(global_db, out) != NULL) {
        suffix++;
        snprintf(out, out_size, "%s%d", base, suffix);
    }
}

static User* oauth_find_or_create_user(const char* google_id, const char* email, const char* full_name) {
    PGresult* result;
    User* user = NULL;
    char username[128];
    char* placeholder_hash;

    if (!ensure_oauth_schema_flags()) return NULL;

    if (oauth_has_google_id) {
        const char* values[2] = {email ? email : "", google_id ? google_id : ""};
        result = PQexecParams(
            global_db,
            "SELECT id FROM users WHERE email = $1 OR google_id = $2 ORDER BY id LIMIT 1",
            2, NULL, values, NULL, NULL, 0);
    } else {
        const char* values[1] = {email ? email : ""};
        result = PQexecParams(
            global_db,
            "SELECT id FROM users WHERE email = $1 ORDER BY id LIMIT 1",
            1, NULL, values, NULL, NULL, 0);
    }

    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK) {
        if (result) PQclear(result);
        return NULL;
    }

    if (PQntuples(result) > 0) {
        int user_id = atoi(PQgetvalue(result, 0, 0));
        PQclear(result);
        {
            char id_buf[16];
            const char* values[4];
            snprintf(id_buf, sizeof(id_buf), "%d", user_id);
            if (oauth_has_google_id && oauth_has_provider) {
                values[0] = email ? email : "";
                values[1] = full_name ? full_name : "";
                values[2] = google_id ? google_id : "";
                values[3] = id_buf;
                result = PQexecParams(
                    global_db,
                    "UPDATE users SET email = $1, full_name = $2, google_id = COALESCE(NULLIF($3, ''), google_id), "
                    "oauth_provider = 'google', last_login = NOW() WHERE id = $4::int",
                    4, NULL, values, NULL, NULL, 0);
            } else {
                values[0] = email ? email : "";
                values[1] = full_name ? full_name : "";
                values[2] = id_buf;
                result = PQexecParams(
                    global_db,
                    "UPDATE users SET email = $1, full_name = $2, last_login = NOW() WHERE id = $3::int",
                    3, NULL, values, NULL, NULL, 0);
            }
            if (result) PQclear(result);
        }
        return db_get_user(global_db, user_id);
    }
    PQclear(result);

    derive_unique_username(email, full_name, username, sizeof(username));
    placeholder_hash = auth_hash_password(google_id && google_id[0] ? google_id : (email && email[0] ? email : "oauth-user"));
    if (!placeholder_hash) return NULL;

    if (oauth_has_google_id && oauth_has_provider) {
        const char* values[7] = {
            username,
            placeholder_hash,
            "viewer",
            full_name ? full_name : username,
            email ? email : "",
            google_id ? google_id : "",
            "google"
        };
        result = PQexecParams(
            global_db,
            "INSERT INTO users (username, password_hash, role, full_name, email, google_id, oauth_provider, last_login) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, NOW()) RETURNING id",
            7, NULL, values, NULL, NULL, 0);
    } else {
        const char* values[5] = {
            username,
            placeholder_hash,
            "viewer",
            full_name ? full_name : username,
            email ? email : ""
        };
        result = PQexecParams(
            global_db,
            "INSERT INTO users (username, password_hash, role, full_name, email, last_login) "
            "VALUES ($1, $2, $3, $4, $5, NOW()) RETURNING id",
            5, NULL, values, NULL, NULL, 0);
    }

    free(placeholder_hash);

    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK || PQntuples(result) != 1) {
        if (result) PQclear(result);
        return NULL;
    }

    user = db_get_user(global_db, atoi(PQgetvalue(result, 0, 0)));
    PQclear(result);
    return user;
}

static int exchange_google_code(const char* code, char** id_token_out, char** access_token_out, char** error_out) {
    CURL* curl = curl_easy_init();
    struct curl_buffer buffer = {0};
    char* escaped_code = NULL;
    char* escaped_client_id = NULL;
    char* escaped_client_secret = NULL;
    char* escaped_redirect_uri = NULL;
    char* post_fields = NULL;
    const char* client_id = getenv("GOOGLE_CLIENT_ID");
    const char* client_secret = getenv("GOOGLE_CLIENT_SECRET");
    const char* redirect_uri = get_google_redirect_uri();
    const char* id_token_value;
    const char* access_token_value;
    CURLcode curl_rc;
    long http_status = 0;

    if (id_token_out) *id_token_out = NULL;
    if (access_token_out) *access_token_out = NULL;
    if (error_out) *error_out = NULL;

    if (!client_id || !client_id[0] || !client_secret || !client_secret[0]) {
        *error_out = strdup("Google OAuth credentials are not configured");
        if (curl) curl_easy_cleanup(curl);
        return 0;
    }

    if (!curl) {
        *error_out = strdup("Failed to initialize curl");
        return 0;
    }

    escaped_code = curl_easy_escape(curl, code, 0);
    escaped_client_id = curl_easy_escape(curl, client_id, 0);
    escaped_client_secret = curl_easy_escape(curl, client_secret, 0);
    escaped_redirect_uri = curl_easy_escape(curl, redirect_uri, 0);

    post_fields = malloc(strlen(escaped_code) + strlen(escaped_client_id) +
                         strlen(escaped_client_secret) + strlen(escaped_redirect_uri) + 128);
    if (!escaped_code || !escaped_client_id || !escaped_client_secret || !escaped_redirect_uri || !post_fields) {
        *error_out = strdup("Failed to build Google token exchange request");
        curl_free(escaped_code);
        curl_free(escaped_client_id);
        curl_free(escaped_client_secret);
        curl_free(escaped_redirect_uri);
        free(post_fields);
        curl_easy_cleanup(curl);
        return 0;
    }

    snprintf(post_fields, strlen(escaped_code) + strlen(escaped_client_id) +
             strlen(escaped_client_secret) + strlen(escaped_redirect_uri) + 128,
             "code=%s&client_id=%s&client_secret=%s&redirect_uri=%s&grant_type=authorization_code",
             escaped_code, escaped_client_id, escaped_client_secret, escaped_redirect_uri);

    buffer.capacity = 4096;
    buffer.data = calloc(1, buffer.capacity);
    if (!buffer.data) {
        *error_out = strdup("Failed to allocate OAuth response buffer");
        curl_free(escaped_code);
        curl_free(escaped_client_id);
        curl_free(escaped_client_secret);
        curl_free(escaped_redirect_uri);
        free(post_fields);
        curl_easy_cleanup(curl);
        return 0;
    }

    curl_easy_setopt(curl, CURLOPT_URL, "https://oauth2.googleapis.com/token");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "nexus-oauth/1.0");

    curl_rc = curl_easy_perform(curl);
    if (curl_rc != CURLE_OK) {
        *error_out = strdup(curl_easy_strerror(curl_rc));
        curl_free(escaped_code);
        curl_free(escaped_client_id);
        curl_free(escaped_client_secret);
        curl_free(escaped_redirect_uri);
        free(post_fields);
        curl_easy_cleanup(curl);
        free(buffer.data);
        return 0;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);

    id_token_value = get_form_value(buffer.data, "id_token");
    *id_token_out = id_token_value ? strdup(id_token_value) : NULL;
    access_token_value = get_form_value(buffer.data, "access_token");
    if (access_token_out) {
        *access_token_out = access_token_value ? strdup(access_token_value) : NULL;
    }

    if (!*id_token_out) {
        const char* error_value = get_form_value(buffer.data, "error");
        char message[512];
        snprintf(message, sizeof(message), "%s%s%ld",
                 error_value ? error_value : "Google token exchange failed",
                 http_status > 0 ? " (HTTP " : "",
                 http_status > 0 ? http_status : 0L);
        if (http_status > 0) {
            size_t len = strlen(message);
            if (len + 2 < sizeof(message)) {
                message[len] = ')';
                message[len + 1] = '\0';
            }
        }
        *error_out = strdup(message);
        curl_free(escaped_code);
        curl_free(escaped_client_id);
        curl_free(escaped_client_secret);
        curl_free(escaped_redirect_uri);
        free(post_fields);
        curl_easy_cleanup(curl);
        free(buffer.data);
        free(*id_token_out);
        *id_token_out = NULL;
        if (access_token_out && *access_token_out) {
            free(*access_token_out);
            *access_token_out = NULL;
        }
        return 0;
    }

    curl_free(escaped_code);
    curl_free(escaped_client_id);
    curl_free(escaped_client_secret);
    curl_free(escaped_redirect_uri);
    free(post_fields);
    curl_easy_cleanup(curl);
    free(buffer.data);
    return 1;
}

static const char* get_clean_env(const char* name) {
    static char clean_buf[1024];
    const char* val = getenv(name);
    if (!val || !val[0]) return NULL;

    // Skip leading whitespace and quotes
    while (*val == ' ' || *val == '"' || *val == '\'') val++;
    
    strncpy(clean_buf, val, sizeof(clean_buf) - 1);
    clean_buf[sizeof(clean_buf) - 1] = '\0';
    
    // Trim trailing whitespace and quotes
    int len = strlen(clean_buf);
    while (len > 0 && (clean_buf[len-1] == ' ' || clean_buf[len-1] == '"' || clean_buf[len-1] == '\'')) {
        clean_buf[--len] = '\0';
    }
    return clean_buf;
}

static int build_google_auth_url(const char* state, char* auth_url, size_t auth_url_size, char** error_out) {
    const char* client_id = get_clean_env("GOOGLE_CLIENT_ID");
    const char* redirect_uri = get_google_redirect_uri();
    char encoded_client_id[1024];
    char encoded_redirect_uri[1024];
    char encoded_state[1024];

    if (error_out) *error_out = NULL;
    if (!client_id || !client_id[0]) {
        if (error_out) *error_out = strdup("GOOGLE_CLIENT_ID is not configured");
        return 0;
    }

    if (!url_encode_value(client_id, encoded_client_id, sizeof(encoded_client_id)) ||
        !url_encode_value(redirect_uri, encoded_redirect_uri, sizeof(encoded_redirect_uri)) ||
        !url_encode_value(state, encoded_state, sizeof(encoded_state))) {
        if (error_out) *error_out = strdup("Failed to build Google OAuth URL");
        return 0;
    }

    snprintf(auth_url, auth_url_size,
             "https://accounts.google.com/o/oauth2/v2/auth"
             "?client_id=%s"
             "&redirect_uri=%s"
             "&response_type=code"
             "&scope=openid%%20email%%20profile"
             "&state=%s"
             "&access_type=online"
             "&include_granted_scopes=true"
             "&prompt=select_account",
             encoded_client_id, encoded_redirect_uri, encoded_state);
    return 1;
}

static int build_frontend_redirect_location(const char* token,
                                            const User* user,
                                            char* redirect_url,
                                            size_t redirect_url_size) {
    char encoded_token[4096];
    char encoded_username[512];
    char encoded_role[128];
    const char* frontend_base = get_frontend_redirect_base();

    if (!token || !user || !redirect_url || redirect_url_size == 0) return 0;

    if (!url_encode_value(token, encoded_token, sizeof(encoded_token)) ||
        !url_encode_value(user->username, encoded_username, sizeof(encoded_username)) ||
        !url_encode_value(user->role, encoded_role, sizeof(encoded_role))) {
        return 0;
    }

    if (frontend_base[0]) {
        snprintf(redirect_url, redirect_url_size,
                 "%s/login?token=%s&user_id=%d&username=%s&role=%s",
                 frontend_base, encoded_token, user->id, encoded_username, encoded_role);
    } else {
        snprintf(redirect_url, redirect_url_size,
                 "/login?token=%s&user_id=%d&username=%s&role=%s",
                 encoded_token, user->id, encoded_username, encoded_role);
    }

    return 1;
}

static int parse_google_id_token(const char* id_token,
                                 char* google_id, size_t google_id_size,
                                 char* email, size_t email_size,
                                 char* full_name, size_t full_name_size,
                                 char* username_hint, size_t username_hint_size,
                                 char** error_out) {
    char* payload_json = NULL;
    char issuer[128] = {0};
    char audience[512] = {0};
    long exp = 0;
    const char* client_id = getenv("GOOGLE_CLIENT_ID");

    if (!parse_jwt_payload_json(id_token, &payload_json)) {
        *error_out = strdup("Failed to decode Google ID token");
        return 0;
    }

    if (!json_extract_string(payload_json, "sub", google_id, google_id_size) ||
        !json_extract_string(payload_json, "email", email, email_size)) {
        free(payload_json);
        *error_out = strdup("Google ID token is missing required claims");
        return 0;
    }

    json_extract_string(payload_json, "name", full_name, full_name_size);
    json_extract_string(payload_json, "given_name", username_hint, username_hint_size);
    json_extract_string(payload_json, "iss", issuer, sizeof(issuer));
    json_extract_string(payload_json, "aud", audience, sizeof(audience));
    json_extract_long(payload_json, "exp", &exp);
    free(payload_json);

    if (exp < (long)time(NULL)) {
        *error_out = strdup("Google ID token has expired");
        return 0;
    }
    if (strcmp(issuer, "accounts.google.com") != 0 &&
        strcmp(issuer, "https://accounts.google.com") != 0) {
        *error_out = strdup("Google ID token issuer is invalid");
        return 0;
    }
    if (!client_id || strcmp(audience, client_id) != 0) {
        *error_out = strdup("Google ID token audience mismatch");
        return 0;
    }
    return 1;
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

static enum MHD_Result handle_oauth_start(struct MHD_Connection* connection) {
    char auth_url[4096];
    char* state = auth_create_oauth_state(600);
    char* error = NULL;
    char* response;

    if (!state) {
        return queue_plain_json_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                                         "{\"error\":\"Failed to generate OAuth state\"}");
    }

    if (!build_google_auth_url(state, auth_url, sizeof(auth_url), &error)) {
        free(state);
        if (!error) error = strdup("Failed to generate Google auth URL");
        {
            char* json = json_error(error);
            free(error);
            return queue_json_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, json);
        }
    }

    response = malloc(strlen(auth_url) + strlen(state) + 128);
    if (!response) {
        free(state);
        return queue_plain_json_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                                         "{\"error\":\"Out of memory\"}");
    }

    snprintf(response, strlen(auth_url) + strlen(state) + 128,
             "{\"auth_url\":\"%s\",\"state\":\"%s\"}", auth_url, state);
    free(state);
    return queue_json_response(connection, MHD_HTTP_OK, response);
}

static enum MHD_Result handle_oauth_callback(struct MHD_Connection* connection) {
    const char* code = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "code");
    const char* state = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "state");
    const char* oauth_error = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "error");
    char* id_token = NULL;
    char* access_token = NULL;
    char* error = NULL;
    char google_id[256] = {0};
    char email[256] = {0};
    char full_name[256] = {0};
    char username_hint[128] = {0};
    User* user = NULL;
    char* token = NULL;

    if (oauth_error && oauth_error[0]) {
        char* response = json_error(oauth_error);
        return queue_json_response(connection, MHD_HTTP_BAD_REQUEST, response);
    }

    if (!code || !state) {
        return queue_plain_json_response(connection, MHD_HTTP_BAD_REQUEST,
                                         "{\"error\":\"Missing OAuth callback parameters\"}");
    }

    if (!auth_verify_oauth_state(state)) {
        return queue_plain_json_response(connection, MHD_HTTP_UNAUTHORIZED,
                                         "{\"error\":\"Invalid or expired OAuth state\"}");
    }

    if (!exchange_google_code(code, &id_token, &access_token, &error)) {
        if (!error) error = strdup("Failed to exchange Google auth code");
        {
            char* response = json_error(error);
            free(error);
            return queue_json_response(connection, MHD_HTTP_BAD_GATEWAY, response);
        }
    }

    if (!parse_google_id_token(id_token, google_id, sizeof(google_id),
                               email, sizeof(email), full_name, sizeof(full_name),
                               username_hint, sizeof(username_hint), &error)) {
        free(id_token);
        free(access_token);
        if (!error) error = strdup("Failed to parse Google ID token");
        {
            char* response = json_error(error);
            free(error);
            return queue_json_response(connection, MHD_HTTP_UNAUTHORIZED, response);
        }
    }

    user = oauth_find_or_create_user(google_id, email, full_name[0] ? full_name : username_hint);
    free(id_token);
    free(access_token);
    if (!user) {
        return queue_plain_json_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                                         "{\"error\":\"Failed to find or create user\"}");
    }

    token = auth_create_jwt(user->id, user->username, user->role, get_jwt_expiration_seconds());
    if (!token) {
        free(user);
        return queue_plain_json_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                                         "{\"error\":\"Failed to create JWT\"}");
    }

    if (has_json_callback_response(connection)) {
        char* response = malloc(strlen(token) + strlen(user->username) + strlen(user->role) + strlen(user->email) + 256);
        if (!response) {
            free(token);
            free(user);
            return queue_plain_json_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                                             "{\"error\":\"Out of memory\"}");
        }

        snprintf(response, strlen(token) + strlen(user->username) + strlen(user->role) + strlen(user->email) + 256,
                 "{\"status\":\"success\",\"token\":\"%s\",\"user_id\":%d,\"username\":\"%s\",\"role\":\"%s\",\"email\":\"%s\"}",
                 token, user->id, user->username, user->role, user->email);
        free(token);
        free(user);
        return queue_json_response(connection, MHD_HTTP_OK, response);
    }

    {
        char redirect_url[8192];
        if (!build_frontend_redirect_location(token, user, redirect_url, sizeof(redirect_url))) {
            free(token);
            free(user);
            return queue_plain_json_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                                             "{\"error\":\"Failed to build frontend redirect\"}");
        }
        free(token);
        free(user);
        return queue_redirect_response(connection, redirect_url);
    }
}

static enum MHD_Result handle_logout(struct MHD_Connection* connection) {
    const char* auth = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Authorization");
    int user_id = get_user_id_from_token(auth);

    if (user_id <= 0) {
        return queue_plain_json_response(connection, MHD_HTTP_UNAUTHORIZED,
                                         "{\"error\":\"Unauthorized\"}");
    }

    return queue_plain_json_response(connection, MHD_HTTP_OK,
                                     "{\"status\":\"success\",\"message\":\"Logged out\"}");
}

static enum MHD_Result handle_refresh_token(struct MHD_Connection* connection) {
    const char* auth = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Authorization");
    int user_id = get_user_id_from_token(auth);
    User* user;
    char* token;
    char* response;

    if (user_id <= 0) {
        return queue_plain_json_response(connection, MHD_HTTP_UNAUTHORIZED,
                                         "{\"error\":\"Unauthorized\"}");
    }

    user = db_get_user(global_db, user_id);
    if (!user || !user->is_active) {
        if (user) free(user);
        return queue_plain_json_response(connection, MHD_HTTP_UNAUTHORIZED,
                                         "{\"error\":\"User not found or inactive\"}");
    }

    token = auth_create_jwt(user->id, user->username, user->role, get_jwt_expiration_seconds());
    if (!token) {
        free(user);
        return queue_plain_json_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                                         "{\"error\":\"Failed to refresh token\"}");
    }

    response = malloc(strlen(token) + strlen(user->username) + strlen(user->role) + 128);
    if (!response) {
        free(token);
        free(user);
        return queue_plain_json_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                                         "{\"error\":\"Out of memory\"}");
    }

    snprintf(response, strlen(token) + strlen(user->username) + strlen(user->role) + 128,
             "{\"status\":\"success\",\"token\":\"%s\",\"user_id\":%d,\"username\":\"%s\",\"role\":\"%s\"}",
             token, user->id, user->username, user->role);
    free(token);
    free(user);
    return queue_json_response(connection, MHD_HTTP_OK, response);
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
        if (strcmp(url, "/") == 0 || strcmp(url, "/index.html") == 0 || strcmp(url, "/login") == 0) {
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
        if (strcmp(url, "/api/auth/oauth/callback") == 0) return handle_oauth_callback(connection);
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
        else if (strcmp(url, "/api/auth/oauth/start") == 0) ret = handle_oauth_start(connection);
        else if (strcmp(url, "/api/auth/logout") == 0) ret = handle_logout(connection);
        else if (strcmp(url, "/api/auth/refresh") == 0) ret = handle_refresh_token(connection);
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
