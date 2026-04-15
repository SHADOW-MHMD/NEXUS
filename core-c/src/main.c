#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <microhttpd.h>
#include <sqlite3.h>
#include "db.h"
#include "api.h"

static struct MHD_Daemon* httpd_daemon = NULL;

void signal_handler(int sig) {
    (void)sig;  // Suppress unused parameter warning
    if (httpd_daemon) {
        MHD_stop_daemon(httpd_daemon);
    }
    printf("\nServer stopped.\n");
    exit(0);
}

int main(int argc, char** argv) {
    printf("=============================================================================\n");
    printf("  NEXUS INVENTORY MANAGER - C Backend\n");
    printf("  HTTP REST API Server with SQLite Database\n");
    printf("=============================================================================\n\n");
    
    // Initialize database
    const char* db_path = "nexus_data.db";
    if (argc > 1) {
        db_path = argv[1];
    }
    
    printf("[*] Initializing database at: %s\n", db_path);
    sqlite3* db = db_init(db_path);
    if (!db) {
        fprintf(stderr, "[ERROR] Failed to initialize database\n");
        return 1;
    }
    
    printf("[*] Creating schema...\n");
    db_create_schema(db);
    
    printf("[*] Seeding default data...\n");
    db_seed_data(db);
    
    printf("[*] Database initialized successfully\n\n");
    
    // Initialize API
    api_init(db);
    
    // Start HTTP server
    unsigned short port = 8080;
    if (argc > 2) {
        port = atoi(argv[2]);
    }
    
    printf("[*] Starting HTTP server on port %d\n", port);
    httpd_daemon = MHD_start_daemon(MHD_USE_AUTO | MHD_USE_INTERNAL_POLLING_THREAD,
                                    port,
                                    NULL, NULL,
                                    &handle_request, NULL,
                                    MHD_OPTION_END);
    
    if (!httpd_daemon) {
        fprintf(stderr, "[ERROR] Failed to start HTTP daemon\n");
        db_close(db);
        return 1;
    }
    
    printf("[*] Server running at http://localhost:%d\n", port);
    printf("[*] API endpoints:\n");
    printf("    POST   /api/auth/login        - User login\n");
    printf("    GET    /api/items             - List items\n");
    printf("    POST   /api/items             - Create item\n");
    printf("    GET    /api/users             - List users\n");
    printf("    GET    /api/issues            - List issues\n");
    printf("[*] Press Ctrl+C to stop\n\n");
    
    // Setup signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Keep running
    while (1) {
        sleep(1);
    }
    
    return 0;
}
