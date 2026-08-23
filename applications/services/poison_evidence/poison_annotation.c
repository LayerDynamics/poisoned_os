#include "poison_annotation.h"
#include "poison_evidence.h"
#include "poison_evidence_i.h"

#include <furi.h>
#include <storage/storage.h>
#include <string.h>

#define POISON_ANNOTATION_ROOT "/ext/evidence/annotations"
#define POISON_ANNOTATION_TEMP "/ext/evidence/.tmp"

static bool poison_annotation_mkdir(Storage* storage, const char* path) {
    const FS_Error result = storage_common_mkdir(storage, path);
    return result == FSE_OK || result == FSE_EXIST;
}

bool poison_annotation_validate(
    const char* annotation_id,
    const char* evidence_id,
    const char* text) {
    return poison_evidence_id_validate(annotation_id) &&
           poison_evidence_id_validate(evidence_id) && text && text[0] != '\0' &&
           strnlen(text, POISON_ANNOTATION_TEXT_MAX + 1u) <= POISON_ANNOTATION_TEXT_MAX;
}

bool poison_annotation_append_persistent(const PoisonAnnotationRecord* record) {
    if(!record ||
       !poison_annotation_validate(record->annotation_id, record->evidence_id, record->text) ||
       !poison_evidence_id_validate(record->author_id) || record->created_at_ms == 0u ||
       record->tags_count > POISON_ANNOTATION_TAGS_MAX ||
       !poison_evidence_record_exists_global(record->evidence_id)) {
        return false;
    }
    for(size_t index = 0u; index < record->tags_count; ++index)
        if(!poison_evidence_id_validate(record->tags[index])) return false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool written = poison_annotation_mkdir(storage, "/ext/evidence") &&
                   poison_annotation_mkdir(storage, POISON_ANNOTATION_ROOT) &&
                   poison_annotation_mkdir(storage, POISON_ANNOTATION_TEMP);
    char temporary_path[192u];
    char final_path[192u];
    snprintf(
        temporary_path,
        sizeof(temporary_path),
        POISON_ANNOTATION_TEMP "/%s.annotation",
        record->annotation_id);
    snprintf(
        final_path, sizeof(final_path), POISON_ANNOTATION_ROOT "/%s.pann", record->annotation_id);
    written = written && !storage_file_exists(storage, final_path);

    File* file = storage_file_alloc(storage);
    written = written && file &&
              storage_file_open(file, temporary_path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
#define POISON_ANNOTATION_WRITE(value, length)                                         \
    do {                                                                               \
        if(written) written = storage_file_write(file, (value), (length)) == (length); \
    } while(false)
    static const uint8_t magic[8u] = {'P', 'O', 'I', 'S', 'A', 'N', 'N', '1'};
    POISON_ANNOTATION_WRITE(magic, sizeof(magic));
    POISON_ANNOTATION_WRITE(record->annotation_id, sizeof(record->annotation_id));
    POISON_ANNOTATION_WRITE(record->evidence_id, sizeof(record->evidence_id));
    POISON_ANNOTATION_WRITE(record->author_id, sizeof(record->author_id));
    uint8_t timestamp[8u];
    for(size_t index = 0u; index < sizeof(timestamp); ++index)
        timestamp[index] = (uint8_t)(record->created_at_ms >> (index * 8u));
    POISON_ANNOTATION_WRITE(timestamp, sizeof(timestamp));
    POISON_ANNOTATION_WRITE(record->text, sizeof(record->text));
    const uint8_t tag_count = (uint8_t)record->tags_count;
    POISON_ANNOTATION_WRITE(&tag_count, sizeof(tag_count));
    for(size_t index = 0u; index < record->tags_count; ++index)
        POISON_ANNOTATION_WRITE(record->tags[index], sizeof(record->tags[index]));
#undef POISON_ANNOTATION_WRITE
    if(written) written = storage_file_sync(file);
    if(file && storage_file_is_open(file)) written = storage_file_close(file) && written;
    if(file) storage_file_free(file);
    if(written) written = storage_common_rename(storage, temporary_path, final_path) == FSE_OK;
    if(!written) (void)storage_common_remove(storage, temporary_path);
    memset(timestamp, 0, sizeof(timestamp));
    furi_record_close(RECORD_STORAGE);
    return written;
}
