#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "auth.h"

#define SALT_LEN 32
#define JWT_SIG_LEN SHA256_DIGEST_LENGTH

static const char* DEV_FALLBACK_SECRET = "nexus-dev-insecure-jwt-secret";

static void bytes_to_hex(const unsigned char* bytes, size_t len, char* out) {
    size_t i;
    for (i = 0; i < len; i++) {
        sprintf(out + (i * 2), "%02x", bytes[i]);
    }
    out[len * 2] = '\0';
}

static const char* auth_get_jwt_secret(void) {
    static int initialized = 0;
    static char derived_secret[(SHA256_DIGEST_LENGTH * 2) + 1];
    const char* jwt_secret;
    const char* openrouter_key;
    unsigned char digest[SHA256_DIGEST_LENGTH];

    if (initialized) {
        return derived_secret;
    }

    jwt_secret = getenv("JWT_SECRET");
    if (jwt_secret && jwt_secret[0] != '\0') {
        snprintf(derived_secret, sizeof(derived_secret), "%s", jwt_secret);
        initialized = 1;
        return derived_secret;
    }

    openrouter_key = getenv("OPENROUTER_API_KEY");
    if (openrouter_key && openrouter_key[0] != '\0') {
        SHA256((const unsigned char*)openrouter_key, strlen(openrouter_key), digest);
        bytes_to_hex(digest, sizeof(digest), derived_secret);
        initialized = 1;
        return derived_secret;
    }

    SHA256((const unsigned char*)DEV_FALLBACK_SECRET, strlen(DEV_FALLBACK_SECRET), digest);
    bytes_to_hex(digest, sizeof(digest), derived_secret);
    fprintf(stderr, "[auth] WARNING: JWT_SECRET and OPENROUTER_API_KEY are unset. Using a development fallback secret.\n");
    initialized = 1;
    return derived_secret;
}

static char* base64url_encode_bytes(const unsigned char* input, size_t input_len) {
    size_t encoded_len;
    unsigned char* base64;
    char* output;
    size_t i;
    size_t out_len = 0;

    if (!input) return NULL;

    encoded_len = 4 * ((input_len + 2) / 3);
    base64 = malloc(encoded_len + 1);
    if (!base64) return NULL;

    EVP_EncodeBlock(base64, input, (int)input_len);
    base64[encoded_len] = '\0';

    for (i = 0; i < encoded_len; i++) {
        if (base64[i] == '=') break;
        out_len++;
    }

    output = malloc(out_len + 1);
    if (!output) {
        free(base64);
        return NULL;
    }

    for (i = 0; i < out_len; i++) {
        if (base64[i] == '+') output[i] = '-';
        else if (base64[i] == '/') output[i] = '_';
        else output[i] = (char)base64[i];
    }
    output[out_len] = '\0';

    free(base64);
    return output;
}

static unsigned char* base64url_decode_bytes(const char* input, size_t* output_len) {
    size_t input_len;
    size_t padded_len;
    char* padded;
    unsigned char* output;
    int decoded_len;
    size_t i;

    if (!input || !output_len) return NULL;

    input_len = strlen(input);
    padded_len = input_len;
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

    while (input_len > 0 && input[input_len - 1] == '=') input_len--;
    if ((strlen(input) % 4) == 2) decoded_len -= 2;
    else if ((strlen(input) % 4) == 3) decoded_len -= 1;

    output[decoded_len] = '\0';
    *output_len = (size_t)decoded_len;
    return output;
}

static char* jwt_sign_compact(const char* header_json, const char* payload_json) {
    char* header_b64;
    char* payload_b64;
    char* signing_input;
    unsigned char digest[JWT_SIG_LEN];
    unsigned int digest_len = JWT_SIG_LEN;
    char* signature_b64;
    char* token;
    size_t signing_len;
    const char* secret = auth_get_jwt_secret();

    header_b64 = base64url_encode_bytes((const unsigned char*)header_json, strlen(header_json));
    payload_b64 = base64url_encode_bytes((const unsigned char*)payload_json, strlen(payload_json));
    if (!header_b64 || !payload_b64) {
        free(header_b64);
        free(payload_b64);
        return NULL;
    }

    signing_len = strlen(header_b64) + strlen(payload_b64) + 2;
    signing_input = malloc(signing_len);
    if (!signing_input) {
        free(header_b64);
        free(payload_b64);
        return NULL;
    }
    snprintf(signing_input, signing_len, "%s.%s", header_b64, payload_b64);

    HMAC(EVP_sha256(),
         secret, (int)strlen(secret),
         (const unsigned char*)signing_input, strlen(signing_input),
         digest, &digest_len);

    signature_b64 = base64url_encode_bytes(digest, digest_len);
    if (!signature_b64) {
        free(header_b64);
        free(payload_b64);
        free(signing_input);
        return NULL;
    }

    token = malloc(strlen(signing_input) + strlen(signature_b64) + 2);
    if (!token) {
        free(header_b64);
        free(payload_b64);
        free(signing_input);
        free(signature_b64);
        return NULL;
    }

    snprintf(token, strlen(signing_input) + strlen(signature_b64) + 2,
             "%s.%s", signing_input, signature_b64);

    free(header_b64);
    free(payload_b64);
    free(signing_input);
    free(signature_b64);
    return token;
}

static int constant_time_compare(const unsigned char* a, size_t a_len,
                                 const unsigned char* b, size_t b_len) {
    size_t i;
    unsigned char diff = 0;

    if (a_len != b_len) return 0;
    for (i = 0; i < a_len; i++) {
        diff |= (unsigned char)(a[i] ^ b[i]);
    }
    return diff == 0;
}

static int extract_json_string(const char* json, const char* key, char* out, size_t out_size) {
    char search[128];
    const char* start;
    const char* end;
    size_t len;

    if (!json || !key || !out || out_size == 0) return 0;

    snprintf(search, sizeof(search), "\"%s\":\"", key);
    start = strstr(json, search);
    if (!start) return 0;
    start += strlen(search);
    end = strchr(start, '"');
    if (!end) return 0;

    len = (size_t)(end - start);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return 1;
}

static int extract_json_long(const char* json, const char* key, long* value) {
    char search[128];
    const char* start;

    if (!json || !key || !value) return 0;

    snprintf(search, sizeof(search), "\"%s\":", key);
    start = strstr(json, search);
    if (!start) return 0;
    start += strlen(search);
    *value = strtol(start, NULL, 10);
    return 1;
}

static int verify_compact_jwt(const char* token, char** payload_out) {
    const char* first_dot;
    const char* second_dot;
    char* signing_input;
    char* payload_part;
    char* signature_part;
    unsigned char expected_digest[JWT_SIG_LEN];
    unsigned int expected_digest_len = JWT_SIG_LEN;
    unsigned char* provided_digest;
    size_t provided_len;
    unsigned char* payload_bytes;
    size_t payload_len;
    const char* secret = auth_get_jwt_secret();

    if (!token || !payload_out) return 0;

    first_dot = strchr(token, '.');
    if (!first_dot) return 0;
    second_dot = strchr(first_dot + 1, '.');
    if (!second_dot) return 0;

    signing_input = strndup(token, (size_t)(second_dot - token));
    payload_part = strndup(first_dot + 1, (size_t)(second_dot - first_dot - 1));
    signature_part = strdup(second_dot + 1);
    if (!signing_input || !payload_part || !signature_part) {
        free(signing_input);
        free(payload_part);
        free(signature_part);
        return 0;
    }

    HMAC(EVP_sha256(),
         secret, (int)strlen(secret),
         (const unsigned char*)signing_input, strlen(signing_input),
         expected_digest, &expected_digest_len);

    provided_digest = base64url_decode_bytes(signature_part, &provided_len);
    if (!provided_digest ||
        !constant_time_compare(expected_digest, expected_digest_len, provided_digest, provided_len)) {
        free(signing_input);
        free(payload_part);
        free(signature_part);
        free(provided_digest);
        return 0;
    }

    payload_bytes = base64url_decode_bytes(payload_part, &payload_len);
    if (!payload_bytes) {
        free(signing_input);
        free(payload_part);
        free(signature_part);
        free(provided_digest);
        return 0;
    }

    free(signing_input);
    free(payload_part);
    free(signature_part);
    free(provided_digest);

    *payload_out = (char*)payload_bytes;
    return 1;
}

char* auth_hash_password(const char* password) {
    unsigned char salt[SALT_LEN];
    unsigned char hash[SHA256_DIGEST_LENGTH];
    char* result = malloc(SALT_LEN * 2 + SHA256_DIGEST_LENGTH * 2 + 2);
    unsigned int len = SHA256_DIGEST_LENGTH;
    int i;
    int pos = 0;

    if (!result) return NULL;

    RAND_bytes(salt, SALT_LEN);
    HMAC(EVP_sha256(), salt, SALT_LEN,
         (unsigned char*)password, strlen(password),
         hash, &len);

    for (i = 0; i < SALT_LEN; i++) pos += sprintf(result + pos, "%02x", salt[i]);
    result[pos++] = ':';
    for (i = 0; i < SHA256_DIGEST_LENGTH; i++) pos += sprintf(result + pos, "%02x", hash[i]);
    result[pos] = '\0';

    return result;
}

int auth_verify_password(const char* password, const char* stored_hash) {
    char* colon;
    int salt_len;
    unsigned char salt[SALT_LEN];
    unsigned char expected_hash[SHA256_DIGEST_LENGTH];
    unsigned char computed_hash[SHA256_DIGEST_LENGTH];
    unsigned int len = SHA256_DIGEST_LENGTH;
    int i;
    unsigned char diff = 0;

    if (!password || !stored_hash) return 0;

    colon = strchr(stored_hash, ':');
    if (!colon) return 0;
    salt_len = (int)(colon - stored_hash);
    if (salt_len != SALT_LEN * 2) return 0;

    for (i = 0; i < SALT_LEN; i++) sscanf(stored_hash + i * 2, "%2hhx", &salt[i]);
    for (i = 0; i < SHA256_DIGEST_LENGTH; i++) sscanf(colon + 1 + i * 2, "%2hhx", &expected_hash[i]);

    HMAC(EVP_sha256(), salt, SALT_LEN,
         (unsigned char*)password, strlen(password),
         computed_hash, &len);

    for (i = 0; i < SHA256_DIGEST_LENGTH; i++) diff |= (unsigned char)(computed_hash[i] ^ expected_hash[i]);
    return diff == 0;
}

char* auth_create_jwt(int user_id, const char* username, const char* role, time_t exp_seconds) {
    char header[64];
    char payload[768];
    time_t now = time(NULL);
    time_t exp = now + exp_seconds;

    snprintf(header, sizeof(header), "{\"alg\":\"HS256\",\"typ\":\"JWT\"}");
    snprintf(payload, sizeof(payload),
             "{\"user_id\":%d,\"username\":\"%s\",\"role\":\"%s\",\"iat\":%ld,\"exp\":%ld}",
             user_id, username ? username : "", role ? role : "",
             (long)now, (long)exp);

    return jwt_sign_compact(header, payload);
}

int auth_verify_jwt(const char* token, int* user_id, char* username, char* role) {
    char* payload = NULL;
    long exp = 0;
    long uid = 0;

    if (!token || !user_id) return 0;
    if (!verify_compact_jwt(token, &payload)) return 0;
    if (!extract_json_long(payload, "exp", &exp) || exp < (long)time(NULL)) {
        free(payload);
        return 0;
    }
    if (!extract_json_long(payload, "user_id", &uid)) {
        free(payload);
        return 0;
    }

    *user_id = (int)uid;
    if (username) extract_json_string(payload, "username", username, 128);
    if (role) extract_json_string(payload, "role", role, 64);
    free(payload);
    return 1;
}

char* auth_create_token(int user_id, const char* username, const char* role) {
    const char* exp_env = getenv("JWT_EXPIRATION_SECONDS");
    time_t exp_seconds = exp_env && exp_env[0] ? (time_t)strtol(exp_env, NULL, 10) : 86400;
    return auth_create_jwt(user_id, username, role, exp_seconds);
}

int auth_verify_token(const char* token, int* user_id, char* username, char* role) {
    if (auth_verify_jwt(token, user_id, username, role)) {
        return 1;
    }

    if (!token || !user_id) return 0;

    {
        const char* id_pos = strstr(token, "\"user_id\":");
        if (!id_pos) return 0;
        *user_id = atoi(id_pos + 10);
    }

    if (username) extract_json_string(token, "username", username, 128);
    if (role) extract_json_string(token, "role", role, 64);
    return 1;
}

int auth_get_user_id_from_token(const char* auth_header) {
    const char* token = auth_header;
    int user_id = -1;
    char username[128] = {0};
    char role[64] = {0};

    if (!auth_header) return -1;
    if (strncmp(auth_header, "Bearer ", 7) == 0) token = auth_header + 7;

    if (auth_verify_token(token, &user_id, username, role)) return user_id;
    return -1;
}

char* auth_create_oauth_state(time_t exp_seconds) {
    unsigned char nonce[16];
    char nonce_hex[33];
    char header[64];
    char payload[256];
    time_t now = time(NULL);

    RAND_bytes(nonce, sizeof(nonce));
    bytes_to_hex(nonce, sizeof(nonce), nonce_hex);

    snprintf(header, sizeof(header), "{\"alg\":\"HS256\",\"typ\":\"JWT\"}");
    snprintf(payload, sizeof(payload),
             "{\"nonce\":\"%s\",\"purpose\":\"oauth_state\",\"iat\":%ld,\"exp\":%ld}",
             nonce_hex, (long)now, (long)(now + exp_seconds));
    return jwt_sign_compact(header, payload);
}

int auth_verify_oauth_state(const char* state) {
    char* payload = NULL;
    char purpose[32] = {0};
    long exp = 0;

    if (!state) return 0;
    if (!verify_compact_jwt(state, &payload)) return 0;
    if (!extract_json_long(payload, "exp", &exp) || exp < (long)time(NULL)) {
        free(payload);
        return 0;
    }
    if (!extract_json_string(payload, "purpose", purpose, sizeof(purpose)) ||
        strcmp(purpose, "oauth_state") != 0) {
        free(payload);
        return 0;
    }
    free(payload);
    return 1;
}
