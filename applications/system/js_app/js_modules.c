#include <core/common_defines.h>
#include "js_modules.h"
#include <m-array.h>
#include <dialogs/dialogs.h>
#include <assets_icons.h>
#include <storage/storage.h>
#include <mjs/common/frozen/frozen.h>
#include <mbedtls/sha256.h>

#include "modules/js_flipper.h"
#include "modules/js_poison_device.h"
#include "modules/js_poison_evidence.h"
#include "modules/js_poison_storage.h"
#include "js_capabilities.h"
#ifdef FW_CFG_unit_tests
#include "modules/js_tests.h"
#endif

#define TAG "JS modules"

// Absolute path is used to make possible plugin load from CLI
#define MODULES_PATH                  "/ext/apps_data/js_app/plugins"
#define JS_PROJECT_MANIFEST_MAX_BYTES (32u * 1024u)
#define JS_PROJECT_MAX_MEMBERS        (33u)

typedef struct {
    FuriString* name;
    const JsModuleConstructor create;
    const JsModuleDestructor destroy;
    void* context;
    mjs_val_t* exports;
    bool source_module;
} JsModuleData;

typedef struct {
    struct mjs* mjs;
    mjs_val_t exports;
} JsSourceModule;

// not using:
//   - a dict because ordering is required
//   - a bptree because it forces a sorted ordering
//   - an rbtree because i deemed it more tedious to implement, and with the
//     amount of modules in use (under 10 in the overwhelming majority of cases)
//     i bet it's going to be slower than a plain array
ARRAY_DEF(JsModuleArray, JsModuleData, M_POD_OPLIST); //-V658
#define M_OPL_JsModuleArray_t() ARRAY_OPLIST(JsModuleArray)

typedef struct {
    JsModuleDescriptor descriptor;
    bool managed_only;
} JsBuiltinModule;

static const JsBuiltinModule modules_builtin[] = {
    {{"flipper", js_flipper_create, NULL, NULL}, false},
    {{"flipper", js_poison_device_create, NULL, NULL}, true},
    {{"device", js_poison_device_create, NULL, NULL}, true},
    {{"storage", js_poison_storage_create, js_poison_storage_destroy, NULL}, true},
    {{"evidence", js_poison_evidence_create, js_poison_evidence_destroy, NULL}, true},
#ifdef FW_CFG_unit_tests
    {{"tests", js_tests_create, NULL, NULL}, false},
#endif
};

struct JsModules {
    struct mjs* mjs;
    JsModuleArray_t modules;
    PluginManager* plugin_manager;
    CompositeApiResolver* resolver;
    uint32_t granted_capabilities;
    bool enforce_capabilities;
    char project_root[JS_PROJECT_PATH_MAX];
    char entry_base[JS_PROJECT_PATH_MAX];
};

static bool js_modules_relative_from_script(
    const char* script_path,
    const char* project_root,
    char output[JS_PROJECT_PATH_MAX]) {
    const size_t root_length = strlen(project_root);
    if(strncmp(script_path, project_root, root_length) != 0 || script_path[root_length] != '/')
        return false;
    const char* relative = script_path + root_length + 1u;
    const char* separator = strrchr(relative, '/');
    const size_t length = separator ? (size_t)(separator - relative) : 0u;
    if(length >= JS_PROJECT_PATH_MAX) return false;
    memcpy(output, relative, length);
    output[length] = '\0';
    return true;
}

static bool js_modules_normalize_source_request(
    const char* base,
    size_t base_len,
    const char* request,
    size_t request_len,
    char output[JS_PROJECT_PATH_MAX]) {
    if(!base || !request || request_len == 0u || request[0] == '/' ||
       base_len + (base_len ? 1u : 0u) + request_len >= JS_PROJECT_PATH_MAX) {
        return false;
    }
    char combined[JS_PROJECT_PATH_MAX];
    size_t combined_length = 0u;
    if(base_len) {
        memcpy(combined, base, base_len);
        combined_length = base_len;
        combined[combined_length++] = '/';
    }
    memcpy(combined + combined_length, request, request_len);
    combined_length += request_len;
    combined[combined_length] = '\0';

    size_t output_length = 0u;
    size_t cursor = 0u;
    while(cursor < combined_length) {
        const size_t segment_start = cursor;
        while(cursor < combined_length && combined[cursor] != '/') {
            const unsigned char byte = (unsigned char)combined[cursor];
            if(byte < 0x20u || byte == 0x7fu || byte == '\\') return false;
            ++cursor;
        }
        const size_t segment_length = cursor - segment_start;
        if(segment_length == 0u) return false;
        if(segment_length == 1u && combined[segment_start] == '.') {
        } else if(
            segment_length == 2u && combined[segment_start] == '.' &&
            combined[segment_start + 1u] == '.') {
            if(output_length == 0u) return false;
            while(output_length > 0u && output[output_length - 1u] != '/')
                --output_length;
            if(output_length > 0u) --output_length;
        } else {
            if(output_length > 0u) output[output_length++] = '/';
            if(output_length + segment_length >= JS_PROJECT_PATH_MAX) return false;
            memcpy(output + output_length, combined + segment_start, segment_length);
            output_length += segment_length;
        }
        if(cursor < combined_length) ++cursor;
    }
    if(output_length == 0u) return false;
    output[output_length] = '\0';
    return true;
}

static bool
    js_modules_project_root_from_script(const char* script_path, char output[JS_PROJECT_PATH_MAX]) {
    if(!script_path || !output) return false;
    const char* marker = strstr(script_path, "/versions/");
    if(!marker) return false;
    const char* digest = marker + strlen("/versions/");
    for(size_t i = 0; i < 64u; ++i) {
        const char c = digest[i];
        if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    if(digest[64u] != '/' && digest[64u] != '\0') return false;
    const size_t root_length = (size_t)(digest + 64u - script_path);
    if(root_length == 0u || root_length >= JS_PROJECT_PATH_MAX) return false;
    memcpy(output, script_path, root_length);
    output[root_length] = '\0';
    return true;
}

static bool js_modules_member_from_script(
    const char* script_path,
    const char* project_root,
    char output[JS_PROJECT_PATH_MAX]) {
    const size_t root_length = strlen(project_root);
    if(strncmp(script_path, project_root, root_length) != 0 || script_path[root_length] != '/')
        return false;
    const char* relative = script_path + root_length + 1u;
    const size_t length = strlen(relative);
    if(length == 0u || length >= JS_PROJECT_PATH_MAX) return false;
    memcpy(output, relative, length + 1u);
    return true;
}

static bool js_project_relative_path_valid(const char* path) {
    if(!path || path[0] == '\0' || path[0] == '/' || strlen(path) >= JS_PROJECT_PATH_MAX ||
       strchr(path, '\\')) {
        return false;
    }
    const char* segment = path;
    for(const char* cursor = path;; ++cursor) {
        const unsigned char byte = (unsigned char)*cursor;
        if((byte < 0x20u && byte != '\0') || byte == 0x7fu) return false;
        if(*cursor == '/' || *cursor == '\0') {
            const size_t length = (size_t)(cursor - segment);
            if(length == 0u || (length == 1u && segment[0] == '.') ||
               (length == 2u && segment[0] == '.' && segment[1] == '.')) {
                return false;
            }
            if(*cursor == '\0') break;
            segment = cursor + 1u;
        }
    }
    return true;
}

static bool js_project_hex_digest_valid(const char* digest) {
    if(!digest || strlen(digest) != 64u) return false;
    for(size_t index = 0u; index < 64u; ++index) {
        const char c = digest[index];
        if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

static void js_project_digest_hex(const uint8_t digest[32u], char output[65u]) {
    static const char hex[] = "0123456789abcdef";
    for(size_t index = 0u; index < 32u; ++index) {
        output[index * 2u] = hex[digest[index] >> 4u];
        output[index * 2u + 1u] = hex[digest[index] & 0x0fu];
    }
    output[64u] = '\0';
}

static bool js_project_file_sha256(const char* path, uint32_t expected_size, char output[65u]) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    bool valid = file && storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
                 storage_file_size(file) == expected_size && mbedtls_sha256_starts(&hash, 0) == 0;
    uint8_t buffer[512u];
    uint32_t remaining = expected_size;
    while(valid && remaining > 0u) {
        const uint32_t requested = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        const uint16_t received = storage_file_read(file, buffer, requested);
        valid = received == requested && mbedtls_sha256_update(&hash, buffer, received) == 0;
        remaining -= valid ? received : 0u;
    }
    uint8_t digest[32u];
    if(valid) valid = mbedtls_sha256_finish(&hash, digest) == 0;
    mbedtls_sha256_free(&hash);
    if(file) storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    memset(buffer, 0, sizeof(buffer));
    if(!valid) return false;
    js_project_digest_hex(digest, output);
    memset(digest, 0, sizeof(digest));
    return true;
}

static char* js_project_read_bounded(const char* path, uint32_t maximum, uint32_t* size) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FileInfo info;
    if(storage_common_stat(storage, path, &info) != FSE_OK || file_info_is_dir(&info) ||
       info.size > maximum || info.size > UINT32_MAX) {
        furi_record_close(RECORD_STORAGE);
        return NULL;
    }
    const uint32_t length = (uint32_t)info.size;
    char* content = malloc((size_t)length + 1u);
    File* file = content ? storage_file_alloc(storage) : NULL;
    const bool valid = file && storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
                       storage_file_read(file, content, length) == length &&
                       !storage_file_get_error(file);
    if(file) storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    if(!valid) {
        if(content) {
            memset(content, 0, (size_t)length + 1u);
            free(content);
        }
        return NULL;
    }
    content[length] = '\0';
    if(size) *size = length;
    return content;
}

static bool js_project_manifest_load(
    const char* script_path,
    const char* expected_digest,
    char project_root[JS_PROJECT_PATH_MAX],
    char** manifest,
    uint32_t* manifest_size) {
    if(!js_project_hex_digest_valid(expected_digest) ||
       !js_modules_project_root_from_script(script_path, project_root)) {
        return false;
    }
    const char* root_digest = strrchr(project_root, '/');
    if(!root_digest || strcmp(root_digest + 1u, expected_digest) != 0) return false;
    char manifest_path[JS_PROJECT_PATH_MAX];
    const int written =
        snprintf(manifest_path, sizeof(manifest_path), "%s/project.json", project_root);
    if(written <= 0 || written >= (int)sizeof(manifest_path)) return false;
    uint32_t size = 0u;
    char* source = js_project_read_bounded(manifest_path, JS_PROJECT_MANIFEST_MAX_BYTES, &size);
    char actual_digest[65u];
    const bool valid = source && js_project_file_sha256(manifest_path, size, actual_digest) &&
                       strcmp(actual_digest, expected_digest) == 0;
    if(!valid) {
        if(source) {
            memset(source, 0, (size_t)size + 1u);
            free(source);
        }
        return false;
    }
    *manifest = source;
    *manifest_size = size;
    return true;
}

static bool js_project_member_descriptor(
    const char* manifest,
    uint32_t manifest_size,
    const char* wanted_path,
    char expected_sha256[65u],
    uint32_t* expected_size) {
    size_t matches = 0u;
    for(size_t index = 0u; index < JS_PROJECT_MAX_MEMBERS; ++index) {
        struct json_token token = JSON_INVALID_TOKEN;
        if(json_scanf_array_elem(manifest, (int)manifest_size, ".files", (int)index, &token) < 0) {
            break;
        }
        char* path = NULL;
        char* sha256 = NULL;
        unsigned long bytes = 0u;
        const bool parsed =
            token.type == JSON_TYPE_OBJECT_END &&
            json_scanf(
                token.ptr, token.len, "{path:%Q,sha256:%Q,bytes:%lu}", &path, &sha256, &bytes) ==
                3 &&
            bytes <= UINT32_MAX && js_project_relative_path_valid(path) &&
            js_project_hex_digest_valid(sha256);
        if(!parsed) {
            free(path);
            free(sha256);
            return false;
        }
        if(strcmp(path, wanted_path) == 0) {
            ++matches;
            strcpy(expected_sha256, sha256);
            *expected_size = (uint32_t)bytes;
        }
        free(path);
        free(sha256);
    }
    struct json_token excess = JSON_INVALID_TOKEN;
    if(json_scanf_array_elem(
           manifest, (int)manifest_size, ".files", JS_PROJECT_MAX_MEMBERS, &excess) >= 0) {
        return false;
    }
    return matches == 1u;
}

static bool js_project_member_file_valid(
    const char* project_root,
    const char* manifest,
    uint32_t manifest_size,
    const char* relative,
    uint32_t* member_size) {
    char expected_sha256[65u];
    uint32_t expected_size = 0u;
    if(!js_project_relative_path_valid(relative) ||
       !js_project_member_descriptor(
           manifest, manifest_size, relative, expected_sha256, &expected_size)) {
        return false;
    }
    char absolute[JS_PROJECT_PATH_MAX];
    const int written = snprintf(absolute, sizeof(absolute), "%s/%s", project_root, relative);
    char actual_sha256[65u];
    const bool valid = written > 0 && written < (int)sizeof(absolute) &&
                       js_project_file_sha256(absolute, expected_size, actual_sha256) &&
                       strcmp(actual_sha256, expected_sha256) == 0;
    if(valid && member_size) *member_size = expected_size;
    return valid;
}

bool js_modules_verify_managed_project(
    const char* script_path,
    const char* project_digest,
    uint32_t source_bytes,
    uint16_t module_count) {
    char project_root[JS_PROJECT_PATH_MAX];
    char* manifest = NULL;
    uint32_t manifest_size = 0u;
    if(!js_project_manifest_load(
           script_path, project_digest, project_root, &manifest, &manifest_size)) {
        return false;
    }
    unsigned format = 0u;
    char* runtime = NULL;
    char* entrypoint = NULL;
    char* dependencies = NULL;
    const bool header_valid = json_scanf(
                                  manifest,
                                  (int)manifest_size,
                                  "{format:%u,runtime:%Q,entrypoint:%Q,dependencies:%Q}",
                                  &format,
                                  &runtime,
                                  &entrypoint,
                                  &dependencies) == 4 &&
                              format == 1u && runtime && strcmp(runtime, "poison-mjs-1") == 0 &&
                              dependencies && strcmp(dependencies, "poison-js.lock") == 0 &&
                              js_project_relative_path_valid(entrypoint);
    char script_member[JS_PROJECT_PATH_MAX];
    bool valid = header_valid &&
                 js_modules_member_from_script(script_path, project_root, script_member) &&
                 strcmp(entrypoint, script_member) == 0;
    uint32_t actual_source_bytes = 0u;
    uint16_t actual_modules = 0u;
    bool lock_present = false;
    bool entrypoint_present = false;
    for(size_t index = 0u; valid && index < JS_PROJECT_MAX_MEMBERS; ++index) {
        struct json_token token = JSON_INVALID_TOKEN;
        if(json_scanf_array_elem(manifest, (int)manifest_size, ".files", (int)index, &token) < 0) {
            break;
        }
        char* path = NULL;
        char* sha256 = NULL;
        unsigned long bytes = 0u;
        valid =
            token.type == JSON_TYPE_OBJECT_END &&
            json_scanf(
                token.ptr, token.len, "{path:%Q,sha256:%Q,bytes:%lu}", &path, &sha256, &bytes) ==
                3 &&
            bytes <= UINT32_MAX && js_project_relative_path_valid(path) &&
            js_project_hex_digest_valid(sha256) && strcmp(path, "project.json") != 0;
        if(valid) {
            uint32_t verified_size = 0u;
            valid = js_project_member_file_valid(
                        project_root, manifest, manifest_size, path, &verified_size) &&
                    verified_size == (uint32_t)bytes;
        }
        if(valid) {
            lock_present |= strcmp(path, "poison-js.lock") == 0;
            entrypoint_present |= strcmp(path, entrypoint) == 0;
            const char* extension = strrchr(path, '.');
            if(extension && (strcmp(extension, ".js") == 0 || strcmp(extension, ".mjs") == 0 ||
                             strcmp(extension, ".cjs") == 0)) {
                if(actual_modules == UINT16_MAX || bytes > UINT32_MAX - actual_source_bytes)
                    valid = false;
                else {
                    ++actual_modules;
                    actual_source_bytes += (uint32_t)bytes;
                }
            }
        }
        free(path);
        free(sha256);
    }
    if(valid && lock_present) {
        char lock_path[JS_PROJECT_PATH_MAX];
        const int written =
            snprintf(lock_path, sizeof(lock_path), "%s/poison-js.lock", project_root);
        uint32_t lock_size = 0u;
        char* lock =
            written > 0 && written < (int)sizeof(lock_path) ?
                js_project_read_bounded(lock_path, JS_PROJECT_MANIFEST_MAX_BYTES, &lock_size) :
                NULL;
        char* lock_schema = NULL;
        char* lock_runtime = NULL;
        char* lock_entrypoint = NULL;
        valid = lock &&
                json_scanf(
                    lock,
                    (int)lock_size,
                    "{schema:%Q,runtime:%Q,entrypoint:%Q}",
                    &lock_schema,
                    &lock_runtime,
                    &lock_entrypoint) == 3 &&
                strcmp(lock_schema, "poison.javascript.lock/v1") == 0 &&
                strcmp(lock_runtime, "poison-mjs-1") == 0 &&
                strcmp(lock_entrypoint, entrypoint) == 0;
        free(lock_schema);
        free(lock_runtime);
        free(lock_entrypoint);
        if(lock) {
            memset(lock, 0, (size_t)lock_size + 1u);
            free(lock);
        }
    }
    valid = valid && lock_present && entrypoint_present &&
            (source_bytes == 0u || source_bytes == actual_source_bytes) &&
            (module_count == 0u || module_count == actual_modules);
    free(runtime);
    free(entrypoint);
    free(dependencies);
    memset(manifest, 0, (size_t)manifest_size + 1u);
    free(manifest);
    return valid;
}

static bool js_project_member_data_valid(
    const JsModules* modules,
    const char* relative,
    const uint8_t* data,
    uint32_t size) {
    if(!modules || !modules->project_root[0] || !relative || (!data && size > 0u)) return false;
    const char* digest = strrchr(modules->project_root, '/');
    if(!digest) return false;
    char project_root[JS_PROJECT_PATH_MAX];
    char* manifest = NULL;
    uint32_t manifest_size = 0u;
    if(!js_project_manifest_load(
           modules->project_root, digest + 1u, project_root, &manifest, &manifest_size)) {
        return false;
    }
    char expected_sha256[65u];
    uint32_t expected_size = 0u;
    bool valid = js_project_member_descriptor(
                     manifest, manifest_size, relative, expected_sha256, &expected_size) &&
                 expected_size == size;
    uint8_t actual_digest[32u];
    char actual_sha256[65u];
    if(valid) {
        mbedtls_sha256_context hash;
        mbedtls_sha256_init(&hash);
        valid = mbedtls_sha256_starts(&hash, 0) == 0 &&
                mbedtls_sha256_update(&hash, data, size) == 0 &&
                mbedtls_sha256_finish(&hash, actual_digest) == 0;
        mbedtls_sha256_free(&hash);
        if(valid) {
            js_project_digest_hex(actual_digest, actual_sha256);
            valid = strcmp(actual_sha256, expected_sha256) == 0;
        }
        memset(actual_digest, 0, sizeof(actual_digest));
    }
    memset(manifest, 0, (size_t)manifest_size + 1u);
    free(manifest);
    return valid;
}

static bool js_lock_identifier_valid(const char* value, bool package_name) {
    if(!value || value[0] == '\0' || strlen(value) >= 128u) return false;
    size_t slashes = 0u;
    for(const char* cursor = value; *cursor; ++cursor) {
        const char c = *cursor;
        if(c == '/') {
            if(!package_name || ++slashes > 1u || cursor == value || cursor[1] == '\0')
                return false;
            continue;
        }
        if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '@' || c == '.' || c == '_' || c == '-' || c == '+')) {
            return false;
        }
    }
    return value[0] != '.' && strstr(value, "..") == NULL;
}

static bool js_lock_resolve_source(
    const char* lock_source,
    uint32_t lock_size,
    const char* request,
    size_t request_len,
    char relative[JS_PROJECT_PATH_MAX]) {
    if(!lock_source || !request || request_len == 0u || request_len >= 128u) return false;
    char requested[128u];
    memcpy(requested, request, request_len);
    requested[request_len] = '\0';
    if(!js_lock_identifier_valid(requested, true)) return false;
    char* schema = NULL;
    char* runtime = NULL;
    char* entrypoint = NULL;
    bool valid = json_scanf(
                     lock_source,
                     (int)lock_size,
                     "{schema:%Q,runtime:%Q,entrypoint:%Q}",
                     &schema,
                     &runtime,
                     &entrypoint) == 3 &&
                 schema && strcmp(schema, "poison.javascript.lock/v1") == 0 && runtime &&
                 strcmp(runtime, "poison-mjs-1") == 0 &&
                 js_project_relative_path_valid(entrypoint);
    size_t matches = 0u;
    for(size_t index = 0u; valid && index < 32u; ++index) {
        struct json_token token = JSON_INVALID_TOKEN;
        if(json_scanf_array_elem(
               lock_source, (int)lock_size, ".dependencies", (int)index, &token) < 0) {
            break;
        }
        char* name = NULL;
        char* version = NULL;
        char* main = NULL;
        valid =
            token.type == JSON_TYPE_OBJECT_END &&
            json_scanf(
                token.ptr, token.len, "{name:%Q,version:%Q,main:%Q}", &name, &version, &main) ==
                3 &&
            js_lock_identifier_valid(name, true) && js_lock_identifier_valid(version, false) &&
            js_project_relative_path_valid(main);
        if(valid && strcmp(name, requested) == 0) {
            size_t main_matches = 0u;
            for(size_t file_index = 0u; file_index < 32u; ++file_index) {
                struct json_token file_token = JSON_INVALID_TOKEN;
                if(json_scanf_array_elem(
                       token.ptr, token.len, ".files", (int)file_index, &file_token) < 0) {
                    break;
                }
                char* path = NULL;
                if(file_token.type != JSON_TYPE_OBJECT_END ||
                   json_scanf(file_token.ptr, file_token.len, "{path:%Q}", &path) != 1 ||
                   !js_project_relative_path_valid(path)) {
                    valid = false;
                    free(path);
                    break;
                }
                if(strcmp(path, main) == 0) ++main_matches;
                free(path);
            }
            if(main_matches != 1u) {
                valid = false;
            } else {
                ++matches;
                const int written = snprintf(
                    relative, JS_PROJECT_PATH_MAX, "vendor/%s/%s/%s", name, version, main);
                valid = written > 0 && written < (int)JS_PROJECT_PATH_MAX;
            }
        }
        free(name);
        free(version);
        free(main);
    }
    struct json_token excess = JSON_INVALID_TOKEN;
    if(valid &&
       json_scanf_array_elem(lock_source, (int)lock_size, ".dependencies", 32, &excess) >= 0) {
        valid = false;
    }
    free(schema);
    free(runtime);
    free(entrypoint);
    return valid && matches == 1u;
}

static bool js_builtin_resolve_source(
    const char* request,
    size_t request_len,
    char relative[JS_PROJECT_PATH_MAX]) {
    static const struct {
        const char* name;
        const char* file;
    } builtins[] = {
        {"assert", "assert.js"},
        {"buffer", "buffer.js"},
        {"crypto", "crypto.js"},
        {"events", "events.js"},
        {"fs", "fs.js"},
        {"http", "http.js"},
        {"https", "http.js"},
        {"net", "net.js"},
        {"os", "os.js"},
        {"path", "path.js"},
        {"process", "process.js"},
        {"promise", "promise.js"},
        {"querystring", "querystring.js"},
        {"stream", "stream.js"},
        {"string_decoder", "string_decoder.js"},
        {"timers", "timers.js"},
        {"tls", "tls.js"},
        {"url", "url.js"},
        {"util", "util.js"},
    };
    if(request_len > 5u && strncmp(request, "node:", 5u) == 0) {
        request += 5u;
        request_len -= 5u;
    }
    for(size_t index = 0u; index < COUNT_OF(builtins); ++index) {
        if(strlen(builtins[index].name) == request_len &&
           strncmp(request, builtins[index].name, request_len) == 0) {
            const int written =
                snprintf(relative, JS_PROJECT_PATH_MAX, "_poison/node/%s", builtins[index].file);
            return written > 0 && written < (int)JS_PROJECT_PATH_MAX;
        }
    }
    return false;
}

JsModules* js_modules_create(
    struct mjs* mjs,
    CompositeApiResolver* resolver,
    uint32_t granted_capabilities,
    bool enforce_capabilities,
    const char* script_path) {
    JsModules* modules = malloc(sizeof(JsModules));
    if(!modules) return NULL;
    modules->mjs = mjs;
    JsModuleArray_init(modules->modules);

    modules->plugin_manager = plugin_manager_alloc(
        PLUGIN_APP_ID, PLUGIN_API_VERSION, composite_api_resolver_get(resolver));

    modules->resolver = resolver;
    modules->granted_capabilities = granted_capabilities;
    modules->enforce_capabilities = enforce_capabilities;
    modules->project_root[0] = '\0';
    modules->entry_base[0] = '\0';
    if(enforce_capabilities &&
       (!js_modules_project_root_from_script(script_path, modules->project_root) ||
        !js_modules_relative_from_script(script_path, modules->project_root, modules->entry_base) ||
        !js_modules_verify_managed_project(
            script_path, strrchr(modules->project_root, '/') + 1u, 0u, 0u))) {
        plugin_manager_free(modules->plugin_manager);
        JsModuleArray_clear(modules->modules);
        free(modules);
        return NULL;
    }

    return modules;
}

bool js_modules_is_managed(const JsModules* modules) {
    return modules && modules->enforce_capabilities;
}

const char* js_modules_project_root(const JsModules* modules) {
    return js_modules_is_managed(modules) && modules->project_root[0] ? modules->project_root :
                                                                        NULL;
}

bool js_modules_resolve_project_path(
    const JsModules* modules,
    const char* path,
    char output[JS_PROJECT_PATH_MAX]) {
    const char* root = js_modules_project_root(modules);
    if(!root || !path || !output) return false;
    while(*path == '/')
        ++path;
    if(path[0] == '\0') {
        strlcpy(output, root, JS_PROJECT_PATH_MAX);
        return true;
    }
    const char* segment = path;
    for(const char* cursor = path;; ++cursor) {
        const char c = *cursor;
        if(c == '\\' || (unsigned char)c < 0x20u) return false;
        if(c == '/' || c == '\0') {
            const size_t length = (size_t)(cursor - segment);
            if(length == 0u || (length == 1u && segment[0] == '.') ||
               (length == 2u && segment[0] == '.' && segment[1] == '.')) {
                return false;
            }
            if(c == '\0') break;
            segment = cursor + 1;
        }
    }
    const size_t root_length = strlen(root);
    const size_t path_length = strlen(path);
    if(root_length + 1u + path_length >= JS_PROJECT_PATH_MAX) return false;
    memcpy(output, root, root_length);
    output[root_length] = '/';
    memcpy(output + root_length + 1u, path, path_length + 1u);
    return true;
}

#ifdef FW_CFG_unit_tests
bool js_modules_test_resolve_project_path(
    const char* script_path,
    const char* path,
    char output[JS_PROJECT_PATH_MAX]) {
    JsModules modules = {.enforce_capabilities = true};
    return js_modules_project_root_from_script(script_path, modules.project_root) &&
           js_modules_resolve_project_path(&modules, path, output);
}

bool js_modules_test_resolve_source_path(
    const char* script_path,
    const char* base,
    const char* request,
    char output[JS_PROJECT_PATH_MAX]) {
    JsModules modules = {.enforce_capabilities = true};
    char relative[JS_PROJECT_PATH_MAX];
    return js_modules_project_root_from_script(script_path, modules.project_root) && base &&
           request &&
           js_modules_normalize_source_request(
               base, strlen(base), request, strlen(request), relative) &&
           js_modules_resolve_project_path(&modules, relative, output);
}

bool js_modules_test_resolve_bare_source_path(
    const char* script_path,
    const char* lock_source,
    const char* request,
    char output[JS_PROJECT_PATH_MAX]) {
    JsModules modules = {.enforce_capabilities = true};
    char relative[JS_PROJECT_PATH_MAX];
    return js_modules_project_root_from_script(script_path, modules.project_root) && lock_source &&
           request &&
           js_lock_resolve_source(
               lock_source, strlen(lock_source), request, strlen(request), relative) &&
           js_modules_resolve_project_path(&modules, relative, output);
}

bool js_modules_test_resolve_builtin_source_path(
    const char* script_path,
    const char* request,
    char output[JS_PROJECT_PATH_MAX]) {
    JsModules modules = {.enforce_capabilities = true};
    char relative[JS_PROJECT_PATH_MAX];
    return js_modules_project_root_from_script(script_path, modules.project_root) && request &&
           js_builtin_resolve_source(request, strlen(request), relative) &&
           js_modules_resolve_project_path(&modules, relative, output);
}
#endif

void js_modules_destroy(JsModules* instance) {
    for
        M_EACH(module, instance->modules, JsModuleArray_t) {
            FURI_LOG_T(TAG, "Tearing down %s", furi_string_get_cstr(module->name));
            if(module->destroy) module->destroy(module->context);
            if(module->exports) {
                mjs_disown(instance->mjs, module->exports);
                free(module->exports);
            }
            furi_string_free(module->name);
        }
    plugin_manager_free(instance->plugin_manager);
    JsModuleArray_clear(instance->modules);
    free(instance);
}

JsModuleData* js_find_loaded_module(JsModules* instance, const char* name) {
    for
        M_EACH(module, instance->modules, JsModuleArray_t) {
            if(furi_string_cmp_str(module->name, name) == 0) return module;
        }
    return NULL;
}

static void js_source_module_destroy(void* context) {
    JsSourceModule* source = context;
    if(!source) return;
    mjs_disown(source->mjs, &source->exports);
    free(source);
}

static bool js_source_suffix(const char* path) {
    const char* extension = strrchr(path, '.');
    return extension && (strcmp(extension, ".js") == 0 || strcmp(extension, ".mjs") == 0 ||
                         strcmp(extension, ".cjs") == 0);
}

static bool js_source_file_info(const char* path, uint32_t* size) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FileInfo info;
    const bool valid = storage_common_stat(storage, path, &info) == FSE_OK &&
                       !file_info_is_dir(&info) && info.size <= 256u * 1024u;
    if(valid && size) *size = (uint32_t)info.size;
    furi_record_close(RECORD_STORAGE);
    return valid;
}

static bool js_source_resolve(
    JsModules* modules,
    const char* name,
    size_t name_len,
    const char* base,
    size_t base_len,
    char relative[JS_PROJECT_PATH_MAX],
    char absolute[JS_PROJECT_PATH_MAX],
    uint32_t* source_size) {
    char normalized[JS_PROJECT_PATH_MAX];
    if(!js_modules_normalize_source_request(base, base_len, name, name_len, normalized))
        return false;
    const char* candidates[] = {"", ".js", "/index.js"};
    const size_t candidate_count = js_source_suffix(normalized) ? 1u : COUNT_OF(candidates);
    for(size_t index = 0u; index < candidate_count; ++index) {
        const int written =
            snprintf(relative, JS_PROJECT_PATH_MAX, "%s%s", normalized, candidates[index]);
        if(written <= 0 || written >= (int)JS_PROJECT_PATH_MAX ||
           !js_modules_resolve_project_path(modules, relative, absolute)) {
            continue;
        }
        if(js_source_file_info(absolute, source_size)) return true;
    }
    return false;
}

static char* js_source_read(
    JsModules* modules,
    const char* relative,
    const char* path,
    uint32_t source_size) {
    char* source = malloc((size_t)source_size + 1u);
    if(!source) return NULL;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    const bool valid = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
                       storage_file_read(file, source, source_size) == source_size &&
                       !storage_file_get_error(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    source[source_size] = '\0';
    if(!valid ||
       !js_project_member_data_valid(modules, relative, (const uint8_t*)source, source_size)) {
        memset(source, 0, (size_t)source_size + 1u);
        free(source);
        return NULL;
    }
    return source;
}

static bool js_source_locked_request(
    JsModules* modules,
    const char* name,
    size_t name_len,
    char relative[JS_PROJECT_PATH_MAX]) {
    char lock_path[JS_PROJECT_PATH_MAX];
    if(!js_modules_resolve_project_path(modules, "poison-js.lock", lock_path)) return false;
    uint32_t lock_size = 0u;
    char* lock_source =
        js_project_read_bounded(lock_path, JS_PROJECT_MANIFEST_MAX_BYTES, &lock_size);
    const bool valid = lock_source &&
                       js_project_member_data_valid(
                           modules, "poison-js.lock", (const uint8_t*)lock_source, lock_size) &&
                       js_lock_resolve_source(lock_source, lock_size, name, name_len, relative);
    if(lock_source) {
        memset(lock_source, 0, (size_t)lock_size + 1u);
        free(lock_source);
    }
    return valid;
}

static mjs_val_t js_source_require(
    JsModules* modules,
    const char* name,
    size_t name_len,
    const char* base,
    size_t base_len) {
    char relative[JS_PROJECT_PATH_MAX];
    char absolute[JS_PROJECT_PATH_MAX];
    uint32_t source_size = 0u;
    if(!js_source_resolve(
           modules, name, name_len, base, base_len, relative, absolute, &source_size)) {
        mjs_prepend_errorf(
            modules->mjs, MJS_FILE_READ_ERROR, "project module is missing or invalid");
        return MJS_UNDEFINED;
    }
    JsModuleData* cached = js_find_loaded_module(modules, relative);
    if(cached) {
        if(cached->source_module && cached->context)
            return ((JsSourceModule*)cached->context)->exports;
        mjs_prepend_errorf(modules->mjs, MJS_TYPE_ERROR, "module identity collision");
        return MJS_UNDEFINED;
    }
    if(!js_thread_account(
           modules->mjs, &(JsLimitsUsage){.source_bytes = source_size, .modules = 1u})) {
        mjs_prepend_errorf(modules->mjs, MJS_TYPE_ERROR, "javascript module limit exceeded");
        return MJS_UNDEFINED;
    }
    char* source_code = js_source_read(modules, relative, absolute, source_size);
    JsSourceModule* source_module = malloc(sizeof(*source_module));
    if(!source_code || !source_module) {
        if(source_code) {
            memset(source_code, 0, (size_t)source_size + 1u);
            free(source_code);
        }
        free(source_module);
        mjs_prepend_errorf(modules->mjs, MJS_OUT_OF_MEMORY, "project module allocation failed");
        return MJS_UNDEFINED;
    }
    source_module->mjs = modules->mjs;
    source_module->exports = mjs_mk_object(modules->mjs);
    mjs_own(modules->mjs, &source_module->exports);
    JsModuleData module = {
        .name = furi_string_alloc_set_str(relative),
        .destroy = js_source_module_destroy,
        .context = source_module,
        .source_module = true,
    };
    JsModuleArray_push_at(modules->modules, 0, module);

    char module_base[JS_PROJECT_PATH_MAX];
    strlcpy(module_base, relative, sizeof(module_base));
    char* separator = strrchr(module_base, '/');
    if(separator)
        *separator = '\0';
    else
        module_base[0] = '\0';
    static const char prefix[] = "(function(module,exports,require,__filename,__dirname){\n";
    static const char suffix_format[] =
        "\n})(__poison_module,__poison_module.exports,function(name){return "
        "__poison_require_from(name,\"%s\");},\"%s\",\"%s\");";
    const size_t wrapper_capacity = sizeof(prefix) + source_size + sizeof(suffix_format) +
                                    strlen(module_base) * 2u + strlen(relative) + 1u;
    char* wrapper = malloc(wrapper_capacity);
    if(!wrapper) {
        memset(source_code, 0, (size_t)source_size + 1u);
        free(source_code);
        mjs_prepend_errorf(modules->mjs, MJS_OUT_OF_MEMORY, "project module wrapper failed");
        return MJS_UNDEFINED;
    }
    const int written = snprintf(wrapper, wrapper_capacity, "%s%s", prefix, source_code);
    int suffix_written = -1;
    if(written > 0 && (size_t)written < wrapper_capacity) {
        suffix_written = snprintf(
            wrapper + written,
            wrapper_capacity - (size_t)written,
            suffix_format,
            module_base,
            relative,
            module_base);
    }
    memset(source_code, 0, (size_t)source_size + 1u);
    free(source_code);
    if(suffix_written <= 0 || (size_t)suffix_written >= wrapper_capacity - (size_t)written) {
        memset(wrapper, 0, wrapper_capacity);
        free(wrapper);
        mjs_prepend_errorf(modules->mjs, MJS_OUT_OF_MEMORY, "project module wrapper overflow");
        return MJS_UNDEFINED;
    }

    mjs_val_t module_object = mjs_mk_object(modules->mjs);
    mjs_own(modules->mjs, &module_object);
    mjs_set(modules->mjs, module_object, "exports", ~0, source_module->exports);
    const mjs_val_t global = mjs_get_global(modules->mjs);
    mjs_set(modules->mjs, global, "__poison_module", ~0, module_object);
    const mjs_err_t error = mjs_exec(modules->mjs, wrapper, NULL);
    memset(wrapper, 0, wrapper_capacity);
    free(wrapper);
    if(error == MJS_OK) {
        source_module->exports = mjs_get(modules->mjs, module_object, "exports", ~0);
    }
    mjs_del(modules->mjs, global, "__poison_module", ~0);
    mjs_disown(modules->mjs, &module_object);
    return error == MJS_OK ? source_module->exports : MJS_UNDEFINED;
}

mjs_val_t js_module_require(JsModules* modules, const char* name, size_t name_len) {
    if(!modules || !name || name_len == 0u || name_len >= 128u) {
        if(modules) mjs_prepend_errorf(modules->mjs, MJS_BAD_ARGS_ERROR, "invalid module name");
        return MJS_UNDEFINED;
    }
    char bounded_name[128u];
    memcpy(bounded_name, name, name_len);
    bounded_name[name_len] = '\0';
    name = bounded_name;
    const bool source_request =
        name_len >= 2u && name[0] == '.' &&
        (name[1] == '/' || (name[1] == '.' && name_len >= 3u && name[2] == '/'));
    if(modules->enforce_capabilities && source_request) {
        return js_source_require(
            modules, name, name_len, modules->entry_base, strlen(modules->entry_base));
    }
    if(modules->enforce_capabilities) {
        char builtin_source[JS_PROJECT_PATH_MAX];
        if(js_builtin_resolve_source(name, name_len, builtin_source)) {
            return js_source_require(modules, builtin_source, strlen(builtin_source), "", 0u);
        }
        char locked_source[JS_PROJECT_PATH_MAX];
        if(js_source_locked_request(modules, name, name_len, locked_source)) {
            return js_source_require(modules, locked_source, strlen(locked_source), "", 0u);
        }
    }
    // Ignore the initial part of the module name
    const char* optional_module_prefix = "@" JS_SDK_VENDOR "/fz-sdk/";
    const size_t optional_module_prefix_length = strlen(optional_module_prefix);
    if(name_len >= optional_module_prefix_length &&
       strncmp(name, optional_module_prefix, optional_module_prefix_length) == 0) {
        name += optional_module_prefix_length;
        name_len -= optional_module_prefix_length;
    }

    if(modules->enforce_capabilities &&
       !js_capability_module_allowed(name, modules->granted_capabilities)) {
        mjs_prepend_errorf(
            modules->mjs,
            MJS_TYPE_ERROR,
            "capability denied for module \"%.*s\"",
            (int)name_len,
            name);
        return MJS_UNDEFINED;
    }

    if(modules->enforce_capabilities &&
       !js_thread_account(modules->mjs, &(JsLimitsUsage){.modules = 1u})) {
        mjs_prepend_errorf(modules->mjs, MJS_TYPE_ERROR, "javascript module limit exceeded");
        return MJS_UNDEFINED;
    }

    // Check if module is already installed
    JsModuleData* module_inst = js_find_loaded_module(modules, name);
    if(module_inst) return module_inst->exports ? *module_inst->exports : MJS_UNDEFINED;

    bool module_found = false;
    // Check built-in modules
    for(size_t i = 0; i < COUNT_OF(modules_builtin); i++) { //-V1008
        const JsBuiltinModule* builtin = &modules_builtin[i];
        if(builtin->managed_only != modules->enforce_capabilities) continue;
        size_t name_compare_len = strlen(builtin->descriptor.name);

        if(name_compare_len != name_len) {
            continue;
        }

        if(strncmp(name, builtin->descriptor.name, name_compare_len) == 0) {
            JsModuleData module = {
                .create = builtin->descriptor.create,
                .destroy = builtin->descriptor.destroy,
                .name = furi_string_alloc_set_str(name),
            };
            JsModuleArray_push_at(modules->modules, 0, module);
            module_found = true;
            FURI_LOG_I(TAG, "Using built-in module %s", name);
            break;
        }
    }

    // External module load
    if(!module_found) {
        FuriString* deslashed_name = furi_string_alloc_set_str(name);
        furi_string_replace_all_str(deslashed_name, "/", "__");
        FuriString* module_path = furi_string_alloc();
        furi_string_printf(
            module_path, "%s/js_%s.fal", MODULES_PATH, furi_string_get_cstr(deslashed_name));
        FURI_LOG_I(
            TAG, "Loading external module %s from %s", name, furi_string_get_cstr(module_path));
        do {
            uint32_t plugin_cnt_last = plugin_manager_get_count(modules->plugin_manager);
            PluginManagerError load_error = plugin_manager_load_single(
                modules->plugin_manager, furi_string_get_cstr(module_path));
            if(load_error != PluginManagerErrorNone) {
                FURI_LOG_E(
                    TAG,
                    "Module %s load error. It may depend on other modules that are not yet loaded.",
                    name);
                break;
            }
            const JsModuleDescriptor* plugin =
                plugin_manager_get_ep(modules->plugin_manager, plugin_cnt_last);
            furi_assert(plugin);

            if(furi_string_cmp_str(deslashed_name, plugin->name) != 0) {
                FURI_LOG_E(TAG, "Module name mismatch %s", plugin->name);
                break;
            }
            JsModuleData module = {
                .create = plugin->create,
                .destroy = plugin->destroy,
                .name = furi_string_alloc_set_str(name),
            };
            JsModuleArray_push_at(modules->modules, 0, module);

            if(plugin->api_interface) {
                FURI_LOG_I(TAG, "Added module API to composite resolver: %s", plugin->name);
                composite_api_resolver_add(modules->resolver, plugin->api_interface);
            }

            module_found = true;
        } while(0);
        furi_string_free(module_path);
        furi_string_free(deslashed_name);
    }

    // Run module constructor
    mjs_val_t module_object = MJS_UNDEFINED;
    if(module_found) {
        module_inst = js_find_loaded_module(modules, name);
        furi_assert(module_inst);
        if(module_inst->create) { //-V779
            module_inst->context = module_inst->create(modules->mjs, &module_object, modules);
        }
        if(module_object != MJS_UNDEFINED) {
            module_inst->exports = malloc(sizeof(*module_inst->exports));
            if(module_inst->exports) {
                *module_inst->exports = module_object;
                mjs_own(modules->mjs, module_inst->exports);
            } else {
                module_object = MJS_UNDEFINED;
                mjs_prepend_errorf(
                    modules->mjs, MJS_OUT_OF_MEMORY, "module cache allocation failed");
            }
        }
    }

    if(module_object == MJS_UNDEFINED) { //-V547
        mjs_prepend_errorf(modules->mjs, MJS_BAD_ARGS_ERROR, "\"%s\" module load fail", name);
    }

    return module_object;
}

mjs_val_t js_module_require_from(
    JsModules* modules,
    const char* name,
    size_t name_len,
    const char* base,
    size_t base_len) {
    if(!modules || !modules->enforce_capabilities || !name || !base) return MJS_UNDEFINED;
    const bool source_request =
        name_len >= 2u && name[0] == '.' &&
        (name[1] == '/' || (name[1] == '.' && name_len >= 3u && name[2] == '/'));
    if(source_request) return js_source_require(modules, name, name_len, base, base_len);
    char builtin_source[JS_PROJECT_PATH_MAX];
    if(js_builtin_resolve_source(name, name_len, builtin_source)) {
        return js_source_require(modules, builtin_source, strlen(builtin_source), "", 0u);
    }
    char locked_source[JS_PROJECT_PATH_MAX];
    if(js_source_locked_request(modules, name, name_len, locked_source)) {
        return js_source_require(modules, locked_source, strlen(locked_source), "", 0u);
    }
    return js_module_require(modules, name, name_len);
}

void* js_module_get(JsModules* modules, const char* name) {
    FuriString* module_name = furi_string_alloc_set_str(name);
    JsModuleData* module_inst = js_find_loaded_module(modules, name);
    furi_string_free(module_name);
    return module_inst ? module_inst->context : NULL;
}

mjs_err_t js_modules_run_event_loop(JsModules* modules) {
    if(!modules) return MJS_BAD_ARGS_ERROR;
    JsModuleData* module = js_find_loaded_module(modules, "event_loop");
    if(!module || !module->exports) {
        return mjs_set_errorf(
            modules->mjs, MJS_TYPE_ERROR, "managed asynchronous runtime is unavailable");
    }
    const mjs_val_t run = mjs_get(modules->mjs, *module->exports, "run", ~0);
    if(!mjs_is_function(run) && !mjs_is_foreign(run)) {
        return mjs_set_errorf(
            modules->mjs, MJS_TYPE_ERROR, "managed asynchronous runtime cannot run");
    }
    mjs_val_t result = MJS_UNDEFINED;
    return mjs_apply(modules->mjs, &result, run, *module->exports, 0, NULL);
}

typedef enum {
    JsSdkCompatStatusCompatible,
    JsSdkCompatStatusFirmwareTooOld,
    JsSdkCompatStatusFirmwareTooNew,
} JsSdkCompatStatus;

/**
 * @brief Checks compatibility between the firmware and the JS SDK version
 *        expected by the script
 */
static JsSdkCompatStatus
    js_internal_sdk_compatibility_status(int32_t exp_major, int32_t exp_minor) {
    if(exp_major < JS_SDK_MAJOR) return JsSdkCompatStatusFirmwareTooNew;
    if(exp_major > JS_SDK_MAJOR || exp_minor > JS_SDK_MINOR)
        return JsSdkCompatStatusFirmwareTooOld;
    return JsSdkCompatStatusCompatible;
}

static const JsValueDeclaration js_sdk_version_arg_list[] = {
    JS_VALUE_SIMPLE(JsValueTypeInt32),
    JS_VALUE_SIMPLE(JsValueTypeInt32),
};
static const JsValueArguments js_sdk_version_args = JS_VALUE_ARGS(js_sdk_version_arg_list);

void js_sdk_compatibility_status(struct mjs* mjs) {
    int32_t major, minor;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &js_sdk_version_args, &major, &minor);
    JsSdkCompatStatus status = js_internal_sdk_compatibility_status(major, minor);
    switch(status) {
    case JsSdkCompatStatusCompatible:
        mjs_return(mjs, mjs_mk_string(mjs, "compatible", ~0, 0));
        return;
    case JsSdkCompatStatusFirmwareTooOld:
        mjs_return(mjs, mjs_mk_string(mjs, "firmwareTooOld", ~0, 0));
        return;
    case JsSdkCompatStatusFirmwareTooNew:
        mjs_return(mjs, mjs_mk_string(mjs, "firmwareTooNew", ~0, 0));
        return;
    }
}

void js_is_sdk_compatible(struct mjs* mjs) {
    int32_t major, minor;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &js_sdk_version_args, &major, &minor);
    JsSdkCompatStatus status = js_internal_sdk_compatibility_status(major, minor);
    mjs_return(mjs, mjs_mk_boolean(mjs, status == JsSdkCompatStatusCompatible));
}

/**
 * @brief Asks the user whether to continue executing an incompatible script
 */
static bool js_internal_compat_ask_user(const char* message) {
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessage* dialog = dialog_message_alloc();
    dialog_message_set_header(dialog, message, 64, 0, AlignCenter, AlignTop);
    dialog_message_set_text(
        dialog, "This script may not\nwork as expected", 79, 32, AlignCenter, AlignCenter);
    dialog_message_set_icon(dialog, &I_Warning_30x23, 0, 18);
    dialog_message_set_buttons(dialog, "Go back", NULL, "Run anyway");
    DialogMessageButton choice = dialog_message_show(dialogs, dialog);
    dialog_message_free(dialog);
    furi_record_close(RECORD_DIALOGS);
    return choice == DialogMessageButtonRight;
}

void js_check_sdk_compatibility(struct mjs* mjs) {
    int32_t major, minor;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &js_sdk_version_args, &major, &minor);
    JsSdkCompatStatus status = js_internal_sdk_compatibility_status(major, minor);
    if(status != JsSdkCompatStatusCompatible) {
        FURI_LOG_E(
            TAG,
            "Script requests JS SDK %ld.%ld, firmware provides JS SDK %d.%d",
            major,
            minor,
            JS_SDK_MAJOR,
            JS_SDK_MINOR);

        const char* message = (status == JsSdkCompatStatusFirmwareTooOld) ? "Outdated Firmware" :
                                                                            "Outdated Script";
        if(!js_internal_compat_ask_user(message)) {
            JS_ERROR_AND_RETURN(mjs, MJS_NOT_IMPLEMENTED_ERROR, "Incompatible script");
        }
    }
}

static const char* extra_features[] = {
    "baseline", // dummy "feature"
};

/**
 * @brief Determines whether a feature is supported
 */
static bool js_internal_supports(const char* feature) {
    for(size_t i = 0; i < COUNT_OF(extra_features); i++) { // -V1008
        if(strcmp(feature, extra_features[i]) == 0) return true;
    }
    return false;
}

/**
 * @brief Determines whether all of the requested features are supported
 */
static bool js_internal_supports_all_of(struct mjs* mjs, mjs_val_t feature_arr) {
    furi_assert(mjs_is_array(feature_arr));

    for(size_t i = 0; i < mjs_array_length(mjs, feature_arr); i++) {
        mjs_val_t feature = mjs_array_get(mjs, feature_arr, i);
        const char* feature_str = mjs_get_string(mjs, &feature, NULL);
        if(!feature_str) return false;

        if(!js_internal_supports(feature_str)) return false;
    }

    return true;
}

static const JsValueDeclaration js_sdk_features_arg_list[] = {
    JS_VALUE_SIMPLE(JsValueTypeAnyArray),
};
static const JsValueArguments js_sdk_features_args = JS_VALUE_ARGS(js_sdk_features_arg_list);

void js_does_sdk_support(struct mjs* mjs) {
    mjs_val_t features;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &js_sdk_features_args, &features);
    mjs_return(mjs, mjs_mk_boolean(mjs, js_internal_supports_all_of(mjs, features)));
}

void js_check_sdk_features(struct mjs* mjs) {
    mjs_val_t features;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &js_sdk_features_args, &features);
    if(!js_internal_supports_all_of(mjs, features)) {
        FURI_LOG_E(TAG, "Script requests unsupported features");

        if(!js_internal_compat_ask_user("Unsupported Feature")) {
            JS_ERROR_AND_RETURN(mjs, MJS_NOT_IMPLEMENTED_ERROR, "Incompatible script");
        }
    }
}
