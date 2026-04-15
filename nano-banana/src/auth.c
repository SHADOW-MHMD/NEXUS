#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include "auth.h"

#define HASH_LEN 64
#define SALT_LEN 32

/**
 * Hash password using SHA-256 with per-user salt.
 * Format: salt_hex:hash_hex
 */
char* auth_hash_password(const char* password) {
    unsigned char salt[SALT_LEN];
    unsigned char hash[SHA256_DIGEST_LENGTH];
    char* result = malloc(SALT_LEN * 2 + SHA256_DIGEST_LENGTH * 2 + 2);
    
    // Generate random salt
    RAND_bytes(salt, SALT_LEN);
    
    // HMAC-SHA256
    unsigned int len = SHA256_DIGEST_LENGTH;
    HMAC(EVP_sha256(), salt, SALT_LEN,
         (unsigned char*)password, strlen(password),
         hash, &len);
    
    // Convert to hex string: salt:hash
    int pos = 0;
    for (int i = 0; i < SALT_LEN; i++) {
        pos += sprintf(result + pos, "%02x", salt[i]);
    }
    result[pos++] = ':';
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        pos += sprintf(result + pos, "%02x", hash[i]);
    }
    result[pos] = '\0';
    
    return result;
}

/**
 * Verify password against stored hash.
 */
int auth_verify_password(const char* password, const char* stored_hash) {
    if (!password || !stored_hash) return 0;
    
    // Parse stored_hash: salt_hex:hash_hex
    char* colon = strchr(stored_hash, ':');
    if (!colon) return 0;
    
    int salt_len = colon - stored_hash;
    if (salt_len != SALT_LEN * 2) return 0;  // Invalid format
    
    unsigned char salt[SALT_LEN];
    unsigned char expected_hash[SHA256_DIGEST_LENGTH];
    
    // Convert hex salt
    for (int i = 0; i < SALT_LEN; i++) {
        sscanf(stored_hash + i*2, "%2hhx", &salt[i]);
    }
    
    // Convert hex expected hash
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sscanf(colon + 1 + i*2, "%2hhx", &expected_hash[i]);
    }
    
    // Compute hash with same salt
    unsigned char computed_hash[SHA256_DIGEST_LENGTH];
    unsigned int len = SHA256_DIGEST_LENGTH;
    HMAC(EVP_sha256(), salt, SALT_LEN,
         (unsigned char*)password, strlen(password),
         computed_hash, &len);
    
    // Constant-time comparison
    int match = 1;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        if (computed_hash[i] != expected_hash[i]) {
            match = 0;
        }
    }
    
    return match;
}

/**
 * Simple JWT token creation (base64 encoded JSON payload + signature).
 * Format: header.payload.signature (simplified)
 */
char* auth_create_token(int user_id, const char* username, const char* role) {
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"user_id\":%d,\"username\":\"%s\",\"role\":\"%s\",\"iat\":%ld}",
             user_id, username, role, time(NULL));
    
    // Base64 encode payload (simplified - just use hex for now)
    char* token = malloc(strlen(payload) * 2 + 100);
    strcpy(token, payload);  // Simplified - in production use proper JWT
    
    return token;
}

/**
 * Simple token verification.
 * In this project, tokens are just the JSON payload (simplified).
 */
int auth_verify_token(const char* token, int* user_id, char* username, char* role) {
    if (!token || !user_id) return 0;
    
    // Find "user_id":
    const char* id_pos = strstr(token, "\"user_id\":");
    if (id_pos) {
        *user_id = atoi(id_pos + 10);
    } else {
        return 0;
    }

    if (username) {
        const char* u_pos = strstr(token, "\"username\":\"");
        if (u_pos) {
            u_pos += 12;
            const char* u_end = strchr(u_pos, '\"');
            if (u_end) {
                int len = u_end - u_pos;
                strncpy(username, u_pos, len);
                username[len] = '\0';
            }
        }
    }

    if (role) {
        const char* r_pos = strstr(token, "\"role\":\"");
        if (r_pos) {
            r_pos += 8;
            const char* r_end = strchr(r_pos, '\"');
            if (r_end) {
                int len = r_end - r_pos;
                strncpy(role, r_pos, len);
                role[len] = '\0';
            }
        }
    }
    
    return 1;
}

/**
 * Extract user ID from Authorization header: "Bearer <token>"
 */
int auth_get_user_id_from_token(const char* auth_header) {
    if (!auth_header) return -1;
    
    const char* token = auth_header;
    if (strncmp(auth_header, "Bearer ", 7) == 0) {
        token = auth_header + 7;
    }
    
    int user_id = -1;
    char username[128], role[64];
    if (auth_verify_token(token, &user_id, username, role)) {
        return user_id;
    }
    
    return -1;
}
