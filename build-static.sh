#!/bin/bash
# Build static libraries and binaries from source
# This script compiles curl, sqlite3, and openssl as static libraries
# then builds fully static client and agent binaries

set -e

PROJECT_DIR="$(pwd)"
BUILD_DIR="$PROJECT_DIR/static-build"
INSTALL_DIR="$PROJECT_DIR/static-libs"

echo "================================================================"
echo "GoLemon C2 - Static Binary Builder for Arch Linux"
echo "================================================================"
echo ""
echo "[*] This script will:"
echo "    1. Download and compile static versions of dependencies"
echo "    2. Build fully static client and agent binaries"
echo ""
echo "[!] This may take 10-20 minutes depending on your system"
echo ""
read -p "Continue? (y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    exit 1
fi

# Create build directories
mkdir -p "$BUILD_DIR" "$INSTALL_DIR"

echo ""
echo "================================================================"
echo "Step 1/4: Building static OpenSSL"
echo "================================================================"
cd "$BUILD_DIR"

if [ ! -f "$INSTALL_DIR/lib/libssl.a" ]; then
    echo "[*] Downloading OpenSSL 3.0.13..."
    wget -q https://www.openssl.org/source/openssl-3.0.13.tar.gz
    tar -xzf openssl-3.0.13.tar.gz
    cd openssl-3.0.13
    
    echo "[*] Configuring OpenSSL..."
    ./config --prefix="$INSTALL_DIR" no-shared no-tests
    
    echo "[*] Compiling OpenSSL (this may take a while)..."
    make -j$(nproc) > /dev/null 2>&1
    make install > /dev/null 2>&1
    
    cd "$BUILD_DIR"
    rm -rf openssl-3.0.13 openssl-3.0.13.tar.gz
    echo "[+] OpenSSL static library built successfully"
else
    echo "[+] OpenSSL static library already exists, skipping"
fi

echo ""
echo "================================================================"
echo "Step 2/4: Building static zlib"
echo "================================================================"
cd "$BUILD_DIR"

if [ ! -f "$INSTALL_DIR/lib/libz.a" ]; then
    echo "[*] Downloading zlib 1.3.1..."
    wget -q https://zlib.net/zlib-1.3.1.tar.gz
    tar -xzf zlib-1.3.1.tar.gz
    cd zlib-1.3.1
    
    echo "[*] Configuring zlib..."
    ./configure --prefix="$INSTALL_DIR" --static
    
    echo "[*] Compiling zlib..."
    make -j$(nproc) > /dev/null 2>&1
    make install > /dev/null 2>&1
    
    cd "$BUILD_DIR"
    rm -rf zlib-1.3.1 zlib-1.3.1.tar.gz
    echo "[+] zlib static library built successfully"
else
    echo "[+] zlib static library already exists, skipping"
fi

echo ""
echo "================================================================"
echo "Step 3/4: Building static libcurl"
echo "================================================================"
cd "$BUILD_DIR"

if [ ! -f "$INSTALL_DIR/lib/libcurl.a" ]; then
    echo "[*] Downloading curl 8.6.0..."
    wget -q https://curl.se/download/curl-8.6.0.tar.gz
    tar -xzf curl-8.6.0.tar.gz
    cd curl-8.6.0
    
    echo "[*] Configuring curl..."
    PKG_CONFIG_PATH="$INSTALL_DIR/lib/pkgconfig" \
    LDFLAGS="-L$INSTALL_DIR/lib" \
    CPPFLAGS="-I$INSTALL_DIR/include" \
    ./configure --prefix="$INSTALL_DIR" \
        --disable-shared \
        --enable-static \
        --with-openssl="$INSTALL_DIR" \
        --with-zlib="$INSTALL_DIR" \
        --disable-ldap \
        --disable-ldaps \
        --disable-rtsp \
        --without-libidn2 \
        --without-libpsl \
        --without-brotli \
        --without-nghttp2 \
        --without-nghttp3 \
        --without-ngtcp2
    
    echo "[*] Compiling curl..."
    make -j$(nproc) > /dev/null 2>&1
    make install > /dev/null 2>&1
    
    cd "$BUILD_DIR"
    rm -rf curl-8.6.0 curl-8.6.0.tar.gz
    echo "[+] curl static library built successfully"
else
    echo "[+] curl static library already exists, skipping"
fi

echo ""
echo "================================================================"
echo "Step 4/4: Building static sqlite3"
echo "================================================================"
cd "$BUILD_DIR"

if [ ! -f "$INSTALL_DIR/lib/libsqlite3.a" ]; then
    echo "[*] Downloading SQLite 3.45.0..."
    wget -q https://www.sqlite.org/2024/sqlite-autoconf-3450000.tar.gz
    tar -xzf sqlite-autoconf-3450000.tar.gz
    cd sqlite-autoconf-3450000
    
    echo "[*] Configuring SQLite..."
    ./configure --prefix="$INSTALL_DIR" --disable-shared --enable-static
    
    echo "[*] Compiling SQLite..."
    make -j$(nproc) > /dev/null 2>&1
    make install > /dev/null 2>&1
    
    cd "$BUILD_DIR"
    rm -rf sqlite-autoconf-3450000 sqlite-autoconf-3450000.tar.gz
    echo "[+] SQLite static library built successfully"
else
    echo "[+] SQLite static library already exists, skipping"
fi

# Clean up build directory
rm -rf "$BUILD_DIR"

echo ""
echo "================================================================"
echo "Building static client and agent binaries"
echo "================================================================"
cd "$PROJECT_DIR"

echo "[*] Building static client..."
gcc -Wall -Wextra -O2 -o client client.c \
    -static \
    -I"$INSTALL_DIR/include" \
    -L"$INSTALL_DIR/lib" \
    -lutil -ldl -lsqlite3 -lcurl -lz -lssl -lcrypto -lpthread -lm

echo "[*] Building static agent..."
gcc -Wall -Wextra -O2 -o agent agent.c \
    -static \
    -I"$INSTALL_DIR/include" \
    -L"$INSTALL_DIR/lib" \
    -lutil -lcurl -lz -lssl -lcrypto -lpthread -lm

echo "[*] Stripping binaries..."
strip client agent

echo ""
echo "================================================================"
echo "Build Complete!"
echo "================================================================"
echo ""
ls -lh client agent
echo ""
echo "Verifying binaries:"
ldd client 2>/dev/null || echo "✓ client: Fully static binary"
ldd agent 2>/dev/null || echo "✓ agent: Fully static binary"
echo ""
echo "[+] Static binaries are ready for deployment!"
echo ""
echo "Note: Static libraries are saved in ./static-libs/"
echo "      You can reuse them for future builds"
