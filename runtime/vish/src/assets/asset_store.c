#include "asset_store.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
static int safe_relative(const char *s) {
    if (!s || !*s || *s == '/' || strchr(s, ':') || strchr(s, '\\')) return 0;
    const char *part = s;
    for (const char *c = s;; ++c) {
        if (*c == '/' || *c == 0) {
            size_t n = (size_t)(c - part);
            if (!n || (n == 1 && part[0] == '.') ||
                (n == 2 && part[0] == '.' && part[1] == '.')) return 0;
            if (!*c) break;
            part = c + 1;
        }
    }
    return 1;
}
AssetResult asset_read(const char *root, const char *relative, uint32_t offset,
                       void *buffer, size_t length, size_t *read_count) {
    if (read_count) *read_count = 0;
    if (!root || !*root || !safe_relative(relative) || (!buffer && length))
        return ASSET_INVALID_PATH;
    char path[512];
    int n = snprintf(path, sizeof(path), "%s/%s", root, relative);
    if (n < 0 || (size_t)n >= sizeof(path)) return ASSET_INVALID_PATH;
    FILE *f = fopen(path, "rb");
    if (!f) return errno == ENOENT ? ASSET_MISSING : ASSET_IO_ERROR;
#if LONG_MAX < UINT32_MAX
    if (offset > (uint32_t)LONG_MAX) { fclose(f); return ASSET_IO_ERROR; }
#endif
    if (fseek(f, (long)offset, SEEK_SET)) { fclose(f); return ASSET_IO_ERROR; }
    size_t got = length ? fread(buffer, 1, length, f) : 0;
    AssetResult result = ferror(f) ? ASSET_IO_ERROR :
                         (got == length ? ASSET_OK : ASSET_TRUNCATED);
    if (fclose(f) && result == ASSET_OK) result = ASSET_IO_ERROR;
    if (read_count) *read_count = got;
    return result;
}
const char *asset_result_string(AssetResult r) {
    switch (r) {
    case ASSET_OK: return "READABLE (FORMAT NOT VALIDATED)";
    case ASSET_MISSING: return "MISSING - COPY YOUR OWN PC FILE";
    case ASSET_IO_ERROR: return "IO ERROR";
    case ASSET_TRUNCATED: return "FILE TOO SHORT";
    default: return "INVALID PATH";
    }
}
