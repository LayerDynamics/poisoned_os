#include "poison_lessons.h"

#include <applications/services/poison_startup.h>

#include <string.h>

static bool id_valid(const char* value, size_t maximum) {
    if(!value || value[0] == '\0' || strlen(value) >= maximum) return false;
    for(const char* cursor = value; *cursor; ++cursor)
        if(!((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= '0' && *cursor <= '9') ||
             *cursor == '.' || *cursor == '-' || *cursor == '_'))
            return false;
    return true;
}

static bool digest_valid_or_empty(const char* value) {
    if(!value || value[0] == '\0') return true;
    if(strlen(value) != POISON_LESSON_MAX_EVIDENCE - 1u) return false;
    for(size_t i = 0; i < POISON_LESSON_MAX_EVIDENCE - 1u; ++i)
        if(!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f')))
            return false;
    return true;
}

void poison_lessons_on_system_start(void) {
    if(!poison_startup_is_runtime_boot()) return;
}

bool poison_lesson_init(
    PoisonLesson* lesson,
    const char* id,
    uint32_t version,
    bool signature_valid) {
    if(!lesson || !id_valid(id, POISON_LESSON_MAX_ID) || version == 0u || !signature_valid)
        return false;
    memset(lesson, 0, sizeof(*lesson));
    strncpy(lesson->lesson_id, id, sizeof(lesson->lesson_id) - 1u);
    lesson->version = version;
    lesson->signature_valid = signature_valid;
    return true;
}

bool poison_lesson_add_step(
    PoisonLesson* lesson,
    const char* id,
    PoisonLessonStepKind kind,
    const char* title,
    const char* evidence_digest) {
    if(!lesson || !lesson->signature_valid || lesson->step_count >= POISON_LESSON_MAX_STEPS ||
       !id_valid(id, POISON_LESSON_MAX_ID) || !title || title[0] == '\0' ||
       strlen(title) >= POISON_LESSON_MAX_TITLE || kind > PoisonLessonReset ||
       !digest_valid_or_empty(evidence_digest))
        return false;
    PoisonLessonStep* step = &lesson->steps[lesson->step_count++];
    strncpy(step->id, id, sizeof(step->id) - 1u);
    strncpy(step->title, title, sizeof(step->title) - 1u);
    step->kind = kind;
    if(evidence_digest)
        strncpy(
            step->required_evidence_digest,
            evidence_digest,
            sizeof(step->required_evidence_digest) - 1u);
    return true;
}

bool poison_lesson_progress_init(
    PoisonLessonProgress* progress,
    const PoisonLesson* lesson,
    const char* learner_id) {
    if(!progress || !lesson || !lesson->signature_valid || lesson->step_count == 0u ||
       !id_valid(learner_id, POISON_LESSON_MAX_ID))
        return false;
    memset(progress, 0, sizeof(*progress));
    strncpy(progress->learner_id, learner_id, sizeof(progress->learner_id) - 1u);
    strncpy(progress->lesson_id, lesson->lesson_id, sizeof(progress->lesson_id) - 1u);
    progress->lesson_version = lesson->version;
    return true;
}

bool poison_lesson_advance(
    PoisonLessonProgress* progress,
    const PoisonLesson* lesson,
    const char* evidence_digest) {
    if(!progress || !lesson || progress->complete || progress->lesson_version != lesson->version ||
       progress->completed_steps >= lesson->step_count)
        return false;
    const PoisonLessonStep* step = &lesson->steps[progress->completed_steps];
    if(step->required_evidence_digest[0] != '\0' &&
       (!evidence_digest || strcmp(step->required_evidence_digest, evidence_digest) != 0))
        return false;
    if(evidence_digest && !digest_valid_or_empty(evidence_digest)) return false;
    if(evidence_digest)
        strncpy(
            progress->evidence_digests[progress->completed_steps],
            evidence_digest,
            POISON_LESSON_MAX_EVIDENCE - 1u);
    ++progress->completed_steps;
    progress->complete = progress->completed_steps == lesson->step_count;
    return true;
}

bool poison_lesson_reset(PoisonLessonProgress* progress) {
    if(!progress) return false;
    progress->completed_steps = 0;
    progress->complete = false;
    memset(progress->evidence_digests, 0, sizeof(progress->evidence_digests));
    return true;
}
