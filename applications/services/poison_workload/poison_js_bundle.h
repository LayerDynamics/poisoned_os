#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_JS_BUNDLE_MAX_ID           64u
#define POISON_JS_BUNDLE_MAX_VERSION      32u
#define POISON_JS_BUNDLE_MAX_ENTRYPOINT   256u
#define POISON_JS_BUNDLE_MAX_DIGEST       65u
#define POISON_JS_BUNDLE_MAX_CAPABILITIES 16u
#define POISON_JS_BUNDLE_MAX_BYTES        (4u * 1024u * 1024u)

typedef struct {
    char id[POISON_JS_BUNDLE_MAX_ID + 1u];
    char version[POISON_JS_BUNDLE_MAX_VERSION + 1u];
    char entrypoint[POISON_JS_BUNDLE_MAX_ENTRYPOINT + 1u];
    char content_digest[POISON_JS_BUNDLE_MAX_DIGEST];
    uint32_t api_version;
    uint32_t size;
    uint8_t capability_count;
} PoisonJsBundleMetadata;

bool poison_js_bundle_metadata_valid(const PoisonJsBundleMetadata* metadata);
bool poison_js_bundle_digest_valid(const char* digest);

#ifdef __cplusplus
}
#endif
