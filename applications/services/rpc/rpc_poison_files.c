#include "rpc_i.h"

#include <applications/services/poison_vfs/poison_vfs_paths.h>
#include <furi.h>
#include <mbedtls/sha256.h>
#include <storage/storage.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POISON_FILE_TRANSFER_MAX_BYTES   (16u * 1024u * 1024u)
#define POISON_FILE_TRANSFER_CHUNK_BYTES (512u)
#define POISON_FILE_TRANSFER_SUFFIX      ".poison-upload"

typedef struct {
    RpcSession* session;
    Storage* storage;
    File* file;
    bool active;
    bool replay_only;
    bool hash_active;
    char operation_id[65u];
    char target_path[POISON_VFS_PATH_MAX + 1u];
    char temporary_path[POISON_VFS_PATH_MAX + 1u];
    uint64_t expected_size;
    uint64_t received_size;
    uint8_t expected_sha256[32u];
    bool has_last_chunk;
    uint64_t last_chunk_offset;
    size_t last_chunk_size;
    uint8_t last_chunk_sha256[32u];
    mbedtls_sha256_context sha256;
} RpcPoisonFiles;

static bool rpc_poison_files_operation_id_valid(const char* operation_id) {
    if(!operation_id) return false;
    const size_t length = strnlen(operation_id, sizeof(((RpcPoisonFiles*)0)->operation_id));
    if(length == 0u || length >= sizeof(((RpcPoisonFiles*)0)->operation_id)) return false;
    for(size_t index = 0u; index < length; ++index) {
        const unsigned char byte = (unsigned char)operation_id[index];
        if(!isalnum(byte) && byte != '-' && byte != '_' && byte != '.') return false;
    }
    return true;
}

static bool rpc_poison_files_hex_decode(const char* input, uint8_t output[32u]) {
    if(!input || strlen(input) != 64u) return false;
    for(size_t index = 0u; index < 32u; ++index) {
        const char high = input[index * 2u];
        const char low = input[index * 2u + 1u];
        if(!((high >= '0' && high <= '9') || (high >= 'a' && high <= 'f')) ||
           !((low >= '0' && low <= '9') || (low >= 'a' && low <= 'f'))) {
            return false;
        }
        const uint8_t high_value = high <= '9' ? (uint8_t)(high - '0') :
                                                 (uint8_t)(high - 'a' + 10);
        const uint8_t low_value = low <= '9' ? (uint8_t)(low - '0') : (uint8_t)(low - 'a' + 10);
        output[index] = (uint8_t)((high_value << 4u) | low_value);
    }
    return true;
}

static bool rpc_poison_files_chunk_digest_valid(
    const uint8_t* data,
    size_t length,
    const char* expected_hex,
    uint8_t digest[32u]) {
    uint8_t expected[32u];
    const bool valid = rpc_poison_files_hex_decode(expected_hex, expected) &&
                       mbedtls_sha256(data, length, digest, 0) == 0 &&
                       memcmp(expected, digest, 32u) == 0;
    memset(expected, 0, sizeof(expected));
    if(!valid) memset(digest, 0, 32u);
    return valid;
}

static bool rpc_poison_files_existing_matches(
    Storage* storage,
    const char* path,
    uint64_t expected_size,
    const uint8_t expected_digest[32u]) {
    File* file = storage_file_alloc(storage);
    if(!file) return false;
    bool valid = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
                 storage_file_size(file) == expected_size;
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    valid = valid && mbedtls_sha256_starts(&hash, 0) == 0;
    uint8_t buffer[POISON_FILE_TRANSFER_CHUNK_BYTES];
    uint64_t remaining = expected_size;
    while(valid && remaining > 0u) {
        const size_t requested = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        const size_t received = storage_file_read(file, buffer, requested);
        valid = received == requested && mbedtls_sha256_update(&hash, buffer, received) == 0;
        remaining -= received;
    }
    uint8_t actual_digest[32u];
    valid = valid && mbedtls_sha256_finish(&hash, actual_digest) == 0 &&
            memcmp(actual_digest, expected_digest, sizeof(actual_digest)) == 0;
    mbedtls_sha256_free(&hash);
    memset(buffer, 0, sizeof(buffer));
    memset(actual_digest, 0, sizeof(actual_digest));
    if(storage_file_is_open(file)) storage_file_close(file);
    storage_file_free(file);
    return valid;
}

static bool rpc_poison_files_prepare_parent(Storage* storage, const char* target_path) {
    if(!storage || !target_path || target_path[0] != '/' ||
       strnlen(target_path, POISON_VFS_PATH_MAX + 1u) > POISON_VFS_PATH_MAX) {
        return false;
    }
    char parent[POISON_VFS_PATH_MAX + 1u];
    strcpy(parent, target_path);
    char* separator = strrchr(parent, '/');
    if(!separator || separator == parent) return false;
    *separator = '\0';
    for(char* cursor = parent + 1u; *cursor != '\0'; ++cursor) {
        if(*cursor != '/') continue;
        *cursor = '\0';
        const bool ready = storage_dir_exists(storage, parent) ||
                           storage_common_mkdir(storage, parent) == FSE_OK;
        *cursor = '/';
        if(!ready) return false;
    }
    return storage_dir_exists(storage, parent) || storage_common_mkdir(storage, parent) == FSE_OK;
}

static void rpc_poison_files_reset(RpcPoisonFiles* files, bool remove_temporary) {
    if(files->file) {
        if(storage_file_is_open(files->file)) storage_file_close(files->file);
        storage_file_free(files->file);
        files->file = NULL;
    }
    if(files->hash_active) {
        mbedtls_sha256_free(&files->sha256);
        files->hash_active = false;
    }
    if(remove_temporary && files->temporary_path[0] != '\0')
        (void)storage_common_remove(files->storage, files->temporary_path);
    files->active = false;
    files->replay_only = false;
    files->operation_id[0] = '\0';
    files->target_path[0] = '\0';
    files->temporary_path[0] = '\0';
    files->expected_size = 0u;
    files->received_size = 0u;
    memset(files->expected_sha256, 0, sizeof(files->expected_sha256));
    files->has_last_chunk = false;
    files->last_chunk_offset = 0u;
    files->last_chunk_size = 0u;
    memset(files->last_chunk_sha256, 0, sizeof(files->last_chunk_sha256));
}

static bool rpc_poison_files_secure_role(RpcPoisonFiles* files, PoisonRole* role) {
    uint64_t session_id = 0u;
    if(!rpc_session_is_secure_dispatch_active(files->session) ||
       !rpc_session_get_secure_identity(files->session, &session_id, role)) {
        return false;
    }
    (void)session_id;
    return true;
}

static bool rpc_poison_files_cursor_parse(const char* cursor, size_t* offset) {
    if(!cursor || !offset) return false;
    if(cursor[0] == '\0') {
        *offset = 0u;
        return true;
    }
    size_t value = 0u;
    for(size_t index = 0u; cursor[index] != '\0'; ++index) {
        if(cursor[index] < '0' || cursor[index] > '9') return false;
        const size_t digit = (size_t)(cursor[index] - '0');
        if(value > (SIZE_MAX - digit) / 10u) return false;
        value = value * 10u + digit;
    }
    *offset = value;
    return true;
}

static bool
    rpc_poison_files_encode_stat(pb_ostream_t* stream, const pb_field_t* field, void* const* arg) {
    const PB_Poison_FileStat* stat = *arg;
    return stat && pb_encode_tag_for_field(stream, field) &&
           pb_encode_submessage(stream, &PB_Poison_FileStat_msg, stat);
}

static void rpc_poison_files_list_process(const PB_Main* request, void* context) {
    RpcPoisonFiles* files = context;
    const PB_Poison_FileListRequest* list_request = &request->content.poison_file_list_request;
    PoisonRole role = PoisonRoleObserver;
    PoisonVfsResolvedPath resolved;
    size_t offset = 0u;
    const size_t page_size = list_request->page_size == 0u ? 16u : list_request->page_size;
    bool valid =
        request->which_content == PB_Main_poison_file_list_request_tag && !request->has_next &&
        rpc_poison_files_secure_role(files, &role) && page_size <= 16u &&
        rpc_poison_files_cursor_parse(list_request->cursor, &offset) &&
        poison_vfs_resolve_path(list_request->path, role, PoisonVfsOperationRead, &resolved);
    File* directory = NULL;
    PB_Poison_FileStat* entries = NULL;
    size_t entry_count = 0u;
    if(valid) {
        directory = storage_file_alloc(files->storage);
        entries = malloc((page_size + 1u) * sizeof(*entries));
        valid = directory && entries && storage_dir_open(directory, resolved.backing_path);
    }
    if(valid) {
        FileInfo info;
        char name[256u];
        size_t seen = 0u;
        while(entry_count < page_size + 1u && storage_dir_read(directory, &info, name, 255u)) {
            if(seen++ < offset) continue;
            PB_Poison_FileStat* stat = &entries[entry_count];
            *stat = (PB_Poison_FileStat)PB_Poison_FileStat_init_zero;
            const size_t base_length = strlen(list_request->path);
            const bool root = base_length == 1u;
            if(base_length + (root ? 0u : 1u) + strlen(name) > POISON_VFS_PATH_MAX) {
                valid = false;
                break;
            }
            strcpy(stat->path, list_request->path);
            if(!root) strcat(stat->path, "/");
            strcat(stat->path, name);
            stat->size = info.size;
            stat->directory = file_info_is_dir(&info);
            char backing_path[POISON_VFS_PATH_MAX + 1u];
            if(strlen(resolved.backing_path) + 1u + strlen(name) <= POISON_VFS_PATH_MAX) {
                strcpy(backing_path, resolved.backing_path);
                strcat(backing_path, "/");
                strcat(backing_path, name);
                uint32_t timestamp = 0u;
                if(storage_common_timestamp(files->storage, backing_path, &timestamp) == FSE_OK)
                    stat->modified_at_ms = (uint64_t)timestamp * 1000u;
            }
            ++entry_count;
        }
    }
    if(directory) {
        if(storage_file_is_open(directory)) storage_dir_close(directory);
        storage_file_free(directory);
    }
    if(!valid) {
        free(entries);
        rpc_send_and_release_empty(
            files->session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }

    const bool more = entry_count > page_size;
    const size_t send_count = more ? page_size : entry_count;
    if(send_count == 0u) {
        PB_Main response = PB_Main_init_zero;
        response.command_id = request->command_id;
        response.command_status = PB_CommandStatus_OK;
        response.which_content = PB_Main_poison_file_list_response_tag;
        rpc_send_and_release(files->session, &response);
    }
    for(size_t index = 0u; index < send_count; ++index) {
        PB_Main response = PB_Main_init_zero;
        response.command_id = request->command_id;
        response.command_status = PB_CommandStatus_OK;
        response.has_next = index + 1u < send_count;
        response.which_content = PB_Main_poison_file_list_response_tag;
        response.content.poison_file_list_response.entries.funcs.encode =
            rpc_poison_files_encode_stat;
        response.content.poison_file_list_response.entries.arg = &entries[index];
        if(more && index + 1u == send_count)
            snprintf(
                response.content.poison_file_list_response.next_cursor,
                sizeof(response.content.poison_file_list_response.next_cursor),
                "%lu",
                (unsigned long)(offset + send_count));
        rpc_send_and_release(files->session, &response);
    }
    memset(entries, 0, (page_size + 1u) * sizeof(*entries));
    free(entries);
}

static void rpc_poison_files_begin_process(const PB_Main* request, void* context) {
    RpcPoisonFiles* files = context;
    const PB_Poison_FileTransferBegin* begin = &request->content.poison_file_transfer_begin;
    PoisonRole role = PoisonRoleObserver;
    PoisonVfsResolvedPath resolved;
    uint8_t expected_digest[32u];
    const size_t suffix_length = sizeof(POISON_FILE_TRANSFER_SUFFIX) - 1u;
    bool valid = request->which_content == PB_Main_poison_file_transfer_begin_tag &&
                 !request->has_next && !files->active &&
                 rpc_poison_files_secure_role(files, &role) &&
                 rpc_poison_files_operation_id_valid(begin->operation_id) &&
                 begin->size <= POISON_FILE_TRANSFER_MAX_BYTES &&
                 rpc_poison_files_hex_decode(begin->sha256, expected_digest) &&
                 poison_vfs_resolve_path(begin->path, role, PoisonVfsOperationWrite, &resolved) &&
                 strlen(resolved.backing_path) + suffix_length <= POISON_VFS_PATH_MAX;
    const bool target_exists = valid && storage_file_exists(files->storage, resolved.backing_path);
    const bool replay_only =
        target_exists && rpc_poison_files_existing_matches(
                             files->storage, resolved.backing_path, begin->size, expected_digest);
    valid = valid && (!target_exists || replay_only);
    if(valid && !replay_only)
        valid = rpc_poison_files_prepare_parent(files->storage, resolved.backing_path);
    if(valid) {
        strcpy(files->operation_id, begin->operation_id);
        strcpy(files->target_path, resolved.backing_path);
        strcpy(files->temporary_path, resolved.backing_path);
        strcat(files->temporary_path, POISON_FILE_TRANSFER_SUFFIX);
        valid = !storage_file_exists(files->storage, files->temporary_path);
    }
    if(valid && !replay_only) {
        files->file = storage_file_alloc(files->storage);
        valid = files->file &&
                storage_file_open(files->file, files->temporary_path, FSAM_WRITE, FSOM_CREATE_NEW);
    }
    if(valid) {
        mbedtls_sha256_init(&files->sha256);
        valid = mbedtls_sha256_starts(&files->sha256, 0) == 0;
        files->hash_active = valid;
    }
    if(valid) {
        files->active = true;
        files->replay_only = replay_only;
        files->expected_size = begin->size;
        memcpy(files->expected_sha256, expected_digest, sizeof(expected_digest));
    } else {
        rpc_poison_files_reset(files, true);
    }
    memset(expected_digest, 0, sizeof(expected_digest));
    rpc_send_and_release_empty(
        files->session,
        request->command_id,
        valid ? PB_CommandStatus_OK : PB_CommandStatus_ERROR_INVALID_PARAMETERS);
}

static void rpc_poison_files_chunk_process(const PB_Main* request, void* context) {
    RpcPoisonFiles* files = context;
    const PB_Poison_FileTransferChunk* chunk = &request->content.poison_file_transfer_chunk;
    PoisonRole role = PoisonRoleObserver;
    uint8_t chunk_digest[32u];
    bool valid = request->which_content == PB_Main_poison_file_transfer_chunk_tag &&
                 !request->has_next && files->active &&
                 rpc_poison_files_secure_role(files, &role) &&
                 strcmp(chunk->operation_id, files->operation_id) == 0 && chunk->data.size > 0u &&
                 chunk->data.size <= POISON_FILE_TRANSFER_CHUNK_BYTES &&
                 rpc_poison_files_chunk_digest_valid(
                     chunk->data.bytes, chunk->data.size, chunk->sha256, chunk_digest);
    (void)role;
    const bool retry = valid && files->has_last_chunk &&
                       chunk->offset == files->last_chunk_offset &&
                       chunk->data.size == files->last_chunk_size &&
                       memcmp(chunk_digest, files->last_chunk_sha256, sizeof(chunk_digest)) == 0;
    valid = valid && (retry || (chunk->offset == files->received_size &&
                                chunk->data.size <= files->expected_size - files->received_size));
    if(valid && !retry) {
        valid = (files->replay_only ||
                 storage_file_write(files->file, chunk->data.bytes, chunk->data.size) ==
                     chunk->data.size) &&
                mbedtls_sha256_update(&files->sha256, chunk->data.bytes, chunk->data.size) == 0;
    }
    if(valid && !retry) {
        files->last_chunk_offset = chunk->offset;
        files->last_chunk_size = chunk->data.size;
        memcpy(files->last_chunk_sha256, chunk_digest, sizeof(chunk_digest));
        files->has_last_chunk = true;
        files->received_size += chunk->data.size;
    }
    if(!valid) rpc_poison_files_reset(files, true);
    memset(chunk_digest, 0, sizeof(chunk_digest));
    rpc_send_and_release_empty(
        files->session,
        request->command_id,
        valid ? PB_CommandStatus_OK : PB_CommandStatus_ERROR_INVALID_PARAMETERS);
}

static void rpc_poison_files_complete_process(const PB_Main* request, void* context) {
    RpcPoisonFiles* files = context;
    const PB_Poison_FileTransferComplete* complete =
        &request->content.poison_file_transfer_complete;
    PoisonRole role = PoisonRoleObserver;
    uint8_t complete_digest[32u];
    uint8_t actual_digest[32u];
    bool valid =
        request->which_content == PB_Main_poison_file_transfer_complete_tag &&
        !request->has_next && files->active && rpc_poison_files_secure_role(files, &role) &&
        strcmp(complete->operation_id, files->operation_id) == 0 &&
        complete->size == files->expected_size && files->received_size == files->expected_size &&
        rpc_poison_files_hex_decode(complete->sha256, complete_digest) &&
        memcmp(complete_digest, files->expected_sha256, sizeof(complete_digest)) == 0 &&
        mbedtls_sha256_finish(&files->sha256, actual_digest) == 0 &&
        memcmp(actual_digest, files->expected_sha256, sizeof(actual_digest)) == 0;
    (void)role;
    if(files->hash_active) {
        mbedtls_sha256_free(&files->sha256);
        files->hash_active = false;
    }
    if(valid && !files->replay_only)
        valid = storage_file_sync(files->file) && storage_file_close(files->file);
    if(files->file) {
        storage_file_free(files->file);
        files->file = NULL;
    }
    if(valid && !files->replay_only)
        valid = !storage_file_exists(files->storage, files->target_path) &&
                storage_common_rename(files->storage, files->temporary_path, files->target_path) ==
                    FSE_OK;
    if(!valid && files->temporary_path[0] != '\0')
        (void)storage_common_remove(files->storage, files->temporary_path);
    files->active = false;
    files->replay_only = false;
    files->operation_id[0] = '\0';
    files->target_path[0] = '\0';
    files->temporary_path[0] = '\0';
    files->expected_size = 0u;
    files->received_size = 0u;
    memset(files->expected_sha256, 0, sizeof(files->expected_sha256));
    files->has_last_chunk = false;
    files->last_chunk_offset = 0u;
    files->last_chunk_size = 0u;
    memset(files->last_chunk_sha256, 0, sizeof(files->last_chunk_sha256));
    memset(complete_digest, 0, sizeof(complete_digest));
    memset(actual_digest, 0, sizeof(actual_digest));
    rpc_send_and_release_empty(
        files->session,
        request->command_id,
        valid ? PB_CommandStatus_OK : PB_CommandStatus_ERROR_INVALID_PARAMETERS);
}

void* rpc_system_poison_files_alloc(RpcSession* session) {
    RpcPoisonFiles* files = malloc(sizeof(*files));
    furi_check(files);
    memset(files, 0, sizeof(*files));
    files->session = session;
    files->storage = furi_record_open(RECORD_STORAGE);
    RpcHandler handler = {
        .message_handler = rpc_poison_files_list_process,
        .decode_submessage = NULL,
        .context = files,
    };
    rpc_add_handler(session, PB_Main_poison_file_list_request_tag, &handler);
    handler.message_handler = rpc_poison_files_begin_process;
    rpc_add_handler(session, PB_Main_poison_file_transfer_begin_tag, &handler);
    handler.message_handler = rpc_poison_files_chunk_process;
    rpc_add_handler(session, PB_Main_poison_file_transfer_chunk_tag, &handler);
    handler.message_handler = rpc_poison_files_complete_process;
    rpc_add_handler(session, PB_Main_poison_file_transfer_complete_tag, &handler);
    return files;
}

void rpc_system_poison_files_free(void* context) {
    if(!context) return;
    RpcPoisonFiles* files = context;
    rpc_poison_files_reset(files, true);
    furi_record_close(RECORD_STORAGE);
    memset(files, 0, sizeof(*files));
    free(files);
}
