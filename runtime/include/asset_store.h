#ifndef NFSMW_ASSET_STORE_H
#define NFSMW_ASSET_STORE_H
#include <stddef.h>
#include <stdint.h>
#define NFSMW_ASSET_ROOT "ux0:data/nfsmw"
typedef enum { ASSET_OK, ASSET_MISSING, ASSET_IO_ERROR, ASSET_TRUNCATED,
               ASSET_INVALID_PATH } AssetResult;
/* Reads only the requested window; never allocates based on file contents.
 * Root is trusted configuration. Relative path rejects traversal and devices. */
AssetResult asset_read(const char *root, const char *relative, uint32_t offset,
                       void *buffer, size_t length, size_t *read_count);
const char *asset_result_string(AssetResult result);
#endif
