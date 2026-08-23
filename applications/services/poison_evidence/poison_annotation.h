#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POISON_ANNOTATION_ID_MAX     (64u)
#define POISON_ANNOTATION_AUTHOR_MAX (64u)
#define POISON_ANNOTATION_TEXT_MAX   (1024u)
#define POISON_ANNOTATION_TAG_MAX    (64u)
#define POISON_ANNOTATION_TAGS_MAX   (32u)

typedef struct {
    char annotation_id[POISON_ANNOTATION_ID_MAX + 1u];
    char evidence_id[POISON_ANNOTATION_ID_MAX + 1u];
    char author_id[POISON_ANNOTATION_AUTHOR_MAX + 1u];
    uint64_t created_at_ms;
    char text[POISON_ANNOTATION_TEXT_MAX + 1u];
    size_t tags_count;
    char tags[POISON_ANNOTATION_TAGS_MAX][POISON_ANNOTATION_TAG_MAX + 1u];
} PoisonAnnotationRecord;

bool poison_annotation_validate(
    const char* annotation_id,
    const char* evidence_id,
    const char* text);
bool poison_annotation_append_persistent(const PoisonAnnotationRecord* record);
