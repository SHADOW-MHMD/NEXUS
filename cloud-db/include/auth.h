#ifndef AUTH_H
#define AUTH_H

#include <stddef.h>
#include <time.h>

// Password hashing & verification
char* auth_hash_password(const char* password);
int auth_verify_password(const char* password, const char* stored_hash);

// JWT/Token operations
char* auth_create_jwt(int user_id, const char* username, const char* role, time_t exp_seconds);
int auth_verify_jwt(const char* token, int* user_id, char* username, char* role);
char* auth_create_token(int user_id, const char* username, const char* role);
int auth_verify_token(const char* token, int* user_id, char* username, char* role);
int auth_get_user_id_from_token(const char* auth_header);

// OAuth state helpers
char* auth_create_oauth_state(time_t exp_seconds);
int auth_verify_oauth_state(const char* state);

#endif
