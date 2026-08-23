#pragma once

#include "poison_js_bundle.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_JS_BUNDLE_MAX_PATH       (256u)
#define POISON_JS_BUNDLE_MAX_ASSETS     (32u)
#define POISON_JS_BUNDLE_MAX_READ_BYTES (12u * 1024u)

typedef struct {
    char path[POISON_JS_BUNDLE_MAX_PATH + 1u];
    char sha256[POISON_JS_BUNDLE_MAX_DIGEST];
    uint32_t size;
} PoisonJsBundleAsset;

typedef struct {
    PoisonJsBundleMetadata metadata;
    uint64_t capability_mask;
    uint32_t package_generation;
    size_t asset_count;
    char capabilities[POISON_JS_BUNDLE_MAX_CAPABILITIES][65u];
    PoisonJsBundleAsset assets[POISON_JS_BUNDLE_MAX_ASSETS];
    char active_root[POISON_JS_BUNDLE_MAX_PATH + 1u];
} PoisonJsBundle;

bool poison_js_bundle_path_valid(const char* path);
bool poison_js_bundle_load_verified(
    PoisonJsBundle* bundle,
    const char* bundle_id,
    const char* version,
    const char* content_sha256,
    uint64_t granted_capabilities);
bool poison_js_bundle_still_active(const PoisonJsBundle* bundle);
const PoisonJsBundleAsset*
    poison_js_bundle_find_asset(const PoisonJsBundle* bundle, const char* asset_path);
bool poison_js_bundle_read_asset(
    const PoisonJsBundle* bundle,
    const char* asset_path,
    uint32_t offset,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_size,
    bool* eof);

#ifdef __cplusplus
}
#endif
