#ifndef AUTH_H
#define AUTH_H

// Password hashing & verification
char* auth_hash_password(const char* password);
int auth_verify_password(const char* password, const char* stored_hash);

// JWT/Token operations (simplified)
char* auth_create_token(int user_id, const char* username, const char* role);
int auth_verify_token(const char* token, int* user_id, char* username, char* role);
int auth_get_user_id_from_token(const char* auth_header);

#endif
