#!/bin/bash
# NEXUS Inventory Manager - Build and Run Script

set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}"
echo "============================================================================="
echo "  NEXUS Inventory Manager - Build Script"
echo "============================================================================="
echo -e "${NC}"

# Check dependencies
echo -e "${YELLOW}[*] Checking dependencies...${NC}"
for cmd in gcc sqlite3 pkg-config; do
    if ! command -v $cmd &> /dev/null; then
        echo -e "${RED}[ERROR] $cmd not found${NC}"
        exit 1
    fi
done

# Check for development libraries
echo -e "${YELLOW}[*] Checking development libraries...${NC}"
if ! pkg-config --exists sqlite3; then
    echo -e "${RED}[ERROR] libsqlite3-dev not found${NC}"
    exit 1
fi

if ! pkg-config --exists libmicrohttpd; then
    echo -e "${RED}[ERROR] libmicrohttpd-dev not found${NC}"
    exit 1
fi

if ! pkg-config --exists openssl; then
    echo -e "${RED}[ERROR] libssl-dev not found${NC}"
    exit 1
fi

echo -e "${GREEN}[✓] All dependencies found${NC}"

# Build
echo -e "${YELLOW}[*] Building...${NC}"
make clean > /dev/null 2>&1 || true
make

if [ $? -eq 0 ]; then
    echo -e "${GREEN}[✓] Build successful${NC}"
else
    echo -e "${RED}[ERROR] Build failed${NC}"
    exit 1
fi

# Show usage
echo ""
echo -e "${GREEN}=============================================================================${NC}"
echo -e "${GREEN}Build complete! To start the server:${NC}"
echo ""
echo -e "  ${YELLOW}./nexus_server${NC}                    # Start on default port 8080"
echo -e "  ${YELLOW}./nexus_server data.db 9000${NC}     # Custom database and port"
echo ""
echo -e "${GREEN}Then open http://localhost:8080 in your browser${NC}"
echo -e "${GREEN}Default login: admin / admin123${NC}"
echo ""
echo -e "${GREEN}=============================================================================${NC}"
