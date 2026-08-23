#include "poison_vfs_paths.h"

#include <string.h>

typedef struct {
    const char* name;
    PoisonVfsNamespace vfs_namespace;
    const char* backing_prefix;
    bool read_only;
} PoisonVfsNamespaceInfo;

static const PoisonVfsNamespaceInfo namespace_info[] = {
    {"system", PoisonVfsNamespaceSystem, "/int/system", true},
    {"config", PoisonVfsNamespaceConfig, "/int/config", false},
    {"profiles", PoisonVfsNamespaceProfiles, "/int/config/profiles", false},
    {"apps", PoisonVfsNamespaceApps, "/ext/apps", false},
    {"scripts", PoisonVfsNamespaceScripts, "/ext/scripts", false},
    {"workloads", PoisonVfsNamespaceWorkloads, "/ext/workloads", false},
    {"cases", PoisonVfsNamespaceCases, "/ext/cases", false},
    {"evidence", PoisonVfsNamespaceEvidence, "/ext/evidence", false},
    {"lessons", PoisonVfsNamespaceLessons, "/ext/lessons", false},
    {"exports", PoisonVfsNamespaceExports, "/ext/exports", false},
    {"int", PoisonVfsNamespaceInternal, "/int", false},
    {"ext", PoisonVfsNamespaceExternal, "/ext", false},
};

static bool poison_vfs_is_valid_byte(unsigned char byte) {
    return byte >= 0x20u && byte != 0x7Fu && byte != '\\';
}

bool poison_vfs_normalize_path(const char* input, char output[POISON_VFS_PATH_MAX + 1u]) {
    if(!input || !output || input[0] != '/') return false;
    size_t length = strnlen(input, POISON_VFS_PATH_MAX + 1u);
    if(length == 0 || length > POISON_VFS_PATH_MAX) return false;
    size_t write_index = 0;
    bool previous_separator = false;
    for(size_t index = 0; index < length; ++index) {
        unsigned char byte = (unsigned char)input[index];
        if(!poison_vfs_is_valid_byte(byte)) return false;
        if(byte == '/') {
            if(previous_separator) continue;
            previous_separator = true;
        } else {
            previous_separator = false;
        }
        if(write_index >= POISON_VFS_PATH_MAX) return false;
        output[write_index++] = (char)byte;
    }
    while(write_index > 1u && output[write_index - 1u] == '/')
        --write_index;
    output[write_index] = '\0';
    char* segment = output + 1u;
    while(*segment) {
        char* end = strchr(segment, '/');
        size_t segment_length = end ? (size_t)(end - segment) : strlen(segment);
        if(segment_length == 0 || (segment_length == 1u && segment[0] == '.') ||
           (segment_length == 2u && segment[0] == '.' && segment[1] == '.')) {
            return false;
        }
        segment = end ? end + 1u : segment + segment_length;
    }
    return true;
}

static const PoisonVfsNamespaceInfo*
    poison_vfs_find_namespace(const char* normalized, size_t* name_length) {
    const char* start = normalized + 1u;
    const char* separator = strchr(start, '/');
    size_t length = separator ? (size_t)(separator - start) : strlen(start);
    if(name_length) *name_length = length;
    for(size_t index = 0; index < sizeof(namespace_info) / sizeof(namespace_info[0]); ++index) {
        if(strlen(namespace_info[index].name) == length &&
           strncmp(namespace_info[index].name, start, length) == 0) {
            return &namespace_info[index];
        }
    }
    return NULL;
}

static bool poison_vfs_role_can_write(PoisonRole role, PoisonVfsNamespace vfs_namespace) {
    if(role == PoisonRoleObserver || role >= PoisonRoleCount) return false;
    if(vfs_namespace == PoisonVfsNamespaceSystem || vfs_namespace == PoisonVfsNamespaceInternal ||
       vfs_namespace == PoisonVfsNamespaceExternal) {
        return role == PoisonRoleOwner;
    }
    return true;
}

bool poison_vfs_resolve_path(
    const char* logical_path,
    PoisonRole role,
    PoisonVfsOperation operation,
    PoisonVfsResolvedPath* resolved) {
    if(!resolved) return false;
    char normalized[POISON_VFS_PATH_MAX + 1u];
    if(!poison_vfs_normalize_path(logical_path, normalized)) return false;
    size_t namespace_length = 0;
    const PoisonVfsNamespaceInfo* info = poison_vfs_find_namespace(normalized, &namespace_length);
    if(!info || (operation == PoisonVfsOperationWrite &&
                 (info->read_only || !poison_vfs_role_can_write(role, info->vfs_namespace)))) {
        return false;
    }
    const char* suffix = normalized + 1u + namespace_length;
    if(*suffix == '/') ++suffix;
    if(strlen(info->backing_prefix) + (*suffix ? 1u + strlen(suffix) : 0u) > POISON_VFS_PATH_MAX)
        return false;
    memset(resolved, 0, sizeof(*resolved));
    resolved->vfs_namespace = info->vfs_namespace;
    resolved->read_only = info->read_only;
    strcpy(resolved->logical_path, normalized);
    strcpy(resolved->backing_path, info->backing_prefix);
    if(*suffix) {
        strcat(resolved->backing_path, "/");
        strcat(resolved->backing_path, suffix);
    }
    return true;
}
