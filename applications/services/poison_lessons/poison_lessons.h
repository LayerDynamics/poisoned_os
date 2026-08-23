#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_LESSON_MAX_ID       64u
#define POISON_LESSON_MAX_TITLE    96u
#define POISON_LESSON_MAX_STEPS    32u
#define POISON_LESSON_MAX_EVIDENCE 65u

typedef enum {
    PoisonLessonExplanation,
    PoisonLessonDeviceAction,
    PoisonLessonBrowserAction,
    PoisonLessonObservation,
    PoisonLessonEvidenceCheckpoint,
    PoisonLessonKnowledgeCheck,
    PoisonLessonStarterCode,
    PoisonLessonReset,
} PoisonLessonStepKind;

typedef struct {
    char id[POISON_LESSON_MAX_ID];
    PoisonLessonStepKind kind;
    char title[POISON_LESSON_MAX_TITLE];
    char required_evidence_digest[POISON_LESSON_MAX_EVIDENCE];
} PoisonLessonStep;

typedef struct {
    char lesson_id[POISON_LESSON_MAX_ID];
    uint32_t version;
    bool signature_valid;
    uint8_t step_count;
    PoisonLessonStep steps[POISON_LESSON_MAX_STEPS];
} PoisonLesson;

typedef struct {
    char learner_id[POISON_LESSON_MAX_ID];
    char lesson_id[POISON_LESSON_MAX_ID];
    uint32_t lesson_version;
    uint8_t completed_steps;
    bool complete;
    char evidence_digests[POISON_LESSON_MAX_STEPS][POISON_LESSON_MAX_EVIDENCE];
} PoisonLessonProgress;

void poison_lessons_on_system_start(void);
bool poison_lesson_init(
    PoisonLesson* lesson,
    const char* id,
    uint32_t version,
    bool signature_valid);
bool poison_lesson_add_step(
    PoisonLesson* lesson,
    const char* id,
    PoisonLessonStepKind kind,
    const char* title,
    const char* evidence_digest);
bool poison_lesson_progress_init(
    PoisonLessonProgress* progress,
    const PoisonLesson* lesson,
    const char* learner_id);
bool poison_lesson_advance(
    PoisonLessonProgress* progress,
    const PoisonLesson* lesson,
    const char* evidence_digest);
bool poison_lesson_reset(PoisonLessonProgress* progress);

#ifdef __cplusplus
}
#endif
