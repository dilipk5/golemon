#!/bin/bash
# Build static binaries using Docker (Ubuntu container)
# This script builds fully static client and agent binaries on systems
# that don't have static libraries available (like Arch Linux)

set -e

echo "[*] Building static binaries using Docker..."
echo "[!] This requires Docker to be installed and running"
echo ""

# Check if Docker is available
if ! command -v docker &> /dev/null; then
    echo "[ERROR] Docker is not installed or not in PATH"
    echo "Install Docker: sudo pacman -S docker"
    exit 1
fi

# Check if Docker daemon is running
if ! docker info &> /dev/null; then
    echo "[ERROR] Docker daemon is not running"
    echo "Start Docker: sudo systemctl start docker"
    exit 1
fi

# Create a temporary Dockerfile
cat > Dockerfile.static-build << 'EOF'
FROM ubuntu:22.04

# Install build dependencies
RUN apt-get update && apt-get install -y \
    gcc \
    make \
    libcurl4-openssl-dev \
    libsqlite3-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
EOF

echo "[*] Building Docker image..."
docker build -t golemon-static-builder -f Dockerfile.static-build .

echo ""
echo "[*] Building static client binary..."
docker run --rm \
    -v "$(pwd):/build" \
    golemon-static-builder \
    make client-static

echo ""
echo "[*] Building static agent binary..."
docker run --rm \
    -v "$(pwd):/build" \
    golemon-static-builder \
    make agent-static

echo ""
echo "[+] Static binaries built successfully!"
echo ""
echo "Verifying binaries:"
ls -lh client agent

echo ""
echo "Checking dependencies:"
ldd client 2>/dev/null || echo "client: Fully static binary ✓"
ldd agent 2>/dev/null || echo "agent: Fully static binary ✓"

# Cleanup
rm -f Dockerfile.static-build

echo ""
echo "[+] Done! Static binaries are ready for deployment."
