#include "poison_migration.h"

#include <string.h>

static bool copy_path(char destination[257], const char* source) {
    if(!source) return false;
    size_t length = strnlen(source, 257u);
    if(length == 0u || length > 256u) return false;
    memcpy(destination, source, length + 1u);
    return true;
}

static bool has_prefix(const char* value, const char* prefix) {
    return value && strncmp(value, prefix, strlen(prefix)) == 0;
}
static bool valid_source(const char* path) {
    return (has_prefix(path, "/int/") || has_prefix(path, "/ext/")) && strstr(path, "..") == NULL;
}
static bool valid_logical(const char* path) {
    return has_prefix(path, "/apps/") || has_prefix(path, "/scripts/") ||
           has_prefix(path, "/captures/") || has_prefix(path, "/settings/") ||
           has_prefix(path, "/unknown/");
}

void poison_migration_init(PoisonMigration* migration) {
    if(migration) memset(migration, 0, sizeof(*migration));
}

bool poison_migration_add(
    PoisonMigration* migration,
    const char* source_path,
    const char* logical_path,
    const char* backup_path,
    uint64_t source_bytes,
    uint64_t required_free_bytes,
    uint64_t backup_bytes,
    const uint8_t source_digest[POISON_MIGRATION_DIGEST_SIZE],
    const uint8_t backup_digest[POISON_MIGRATION_DIGEST_SIZE],
    bool backup_verified,
    PoisonMigrationClassification classification) {
    if(!migration || migration->state != PoisonMigrationPrepared ||
       migration->count >= POISON_MIGRATION_MAX_ENTRIES || !source_digest || !backup_digest ||
       !valid_source(source_path) || !valid_logical(logical_path) || !backup_verified ||
       classification > PoisonMigrationUnknown)
        return false;
    for(size_t i = 0; i < migration->count; i++)
        if(strcmp(migration->entries[i].source_path, source_path) == 0) return false;
    PoisonMigrationEntry* entry = &migration->entries[migration->count];
    if(!copy_path(entry->source_path, source_path) ||
       !copy_path(entry->logical_path, logical_path) ||
       !copy_path(entry->backup_path, backup_path))
        return false;
    entry->source_bytes = source_bytes;
    entry->required_free_bytes = required_free_bytes;
    entry->backup_bytes = backup_bytes;
    entry->backup_verified = backup_verified;
    entry->classification = classification;
    entry->active = true;
    memcpy(entry->source_digest, source_digest, POISON_MIGRATION_DIGEST_SIZE);
    memcpy(entry->backup_digest, backup_digest, POISON_MIGRATION_DIGEST_SIZE);
    migration->count++;
    return true;
}

bool poison_migration_verify(PoisonMigration* migration, uint64_t available_free_bytes) {
    if(!migration || migration->state != PoisonMigrationPrepared || migration->count == 0u)
        return false;
    uint64_t required = 0u;
    for(size_t i = 0; i < migration->count; i++) {
        PoisonMigrationEntry* entry = &migration->entries[i];
        if(!entry->active || !entry->backup_verified ||
           UINT64_MAX - required < entry->required_free_bytes)
            return false;
        required += entry->required_free_bytes;
    }
    if(required > available_free_bytes) return false;
    migration->state = PoisonMigrationVerified;
    return true;
}

bool poison_migration_commit(PoisonMigration* migration) {
    if(!migration || migration->state != PoisonMigrationVerified) return false;
    migration->state = PoisonMigrationCommitted;
    return true;
}
bool poison_migration_rollback(PoisonMigration* migration) {
    if(!migration || (migration->state != PoisonMigrationVerified &&
                      migration->state != PoisonMigrationCommitted))
        return false;
    migration->state = PoisonMigrationRolledBack;
    for(size_t i = 0; i < migration->count; i++)
        migration->entries[i].active = false;
    return true;
}
