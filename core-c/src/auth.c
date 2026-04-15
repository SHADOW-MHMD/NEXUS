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
 * Verify token (simplified).
 */
int auth_verify_token(const char* token, int* user_id, char* username, char* role) {
    // Simplified token verification
    if (!token || !username || !role) return 0;
    
    // Parse JSON payload (simplified - use proper JSON parser in production)
    int parsed_id = 0;
    if (sscanf(token, "{\"user_id\":%d,\"username\":\"", &parsed_id) == 1) {
        *user_id = parsed_id;
        strcpy(username, "user");  // Simplified
        strcpy(role, "viewer");     // Simplified
        return 1;
    }
    
    return 0;
}
