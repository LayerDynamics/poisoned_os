#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POISON_MIGRATION_MAX_ENTRIES (16u)
#define POISON_MIGRATION_DIGEST_SIZE (32u)

typedef enum {
    PoisonMigrationPrepared,
    PoisonMigrationVerified,
    PoisonMigrationCommitted,
    PoisonMigrationRolledBack
} PoisonMigrationState;
typedef enum {
    PoisonMigrationCompatible,
    PoisonMigrationConvertible,
    PoisonMigrationUnsupported,
    PoisonMigrationUnknown
} PoisonMigrationClassification;

typedef struct {
    bool active;
    char source_path[257];
    char logical_path[257];
    char backup_path[257];
    uint64_t source_bytes;
    uint64_t required_free_bytes;
    uint64_t backup_bytes;
    uint8_t source_digest[POISON_MIGRATION_DIGEST_SIZE];
    uint8_t backup_digest[POISON_MIGRATION_DIGEST_SIZE];
    bool backup_verified;
    PoisonMigrationClassification classification;
} PoisonMigrationEntry;

typedef struct {
    PoisonMigrationState state;
    size_t count;
    PoisonMigrationEntry entries[POISON_MIGRATION_MAX_ENTRIES];
} PoisonMigration;

void poison_migration_init(PoisonMigration* migration);
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
    PoisonMigrationClassification classification);
bool poison_migration_verify(PoisonMigration* migration, uint64_t available_free_bytes);
bool poison_migration_commit(PoisonMigration* migration);
bool poison_migration_rollback(PoisonMigration* migration);
