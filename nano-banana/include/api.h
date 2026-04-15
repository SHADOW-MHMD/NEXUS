#ifndef API_H
#define API_H

#include <microhttpd.h>
#include <sqlite3.h>

// HTTP response handler
enum MHD_Result handle_request(void* cls, struct MHD_Connection* connection,
                               const char* url, const char* method,
                               const char* version, const char* upload_data,
                               size_t* upload_data_size, void** con_cls);

// Initialize API with database
void api_init(sqlite3* db);

// JSON utilities
char* json_error(const char* message);
char* json_success(const char* message);

#endif
