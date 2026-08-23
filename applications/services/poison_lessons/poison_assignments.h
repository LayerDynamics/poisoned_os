#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "poison_lessons.h"
#include "../rpc/poison_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_LESSON_MAX_ASSIGNMENTS 16u

typedef struct {
    char assignment_id[POISON_LESSON_MAX_ID];
    char workspace_id[POISON_LESSON_MAX_ID];
    char instructor_id[POISON_LESSON_MAX_ID];
    char learner_id[POISON_LESSON_MAX_ID];
    PoisonLessonProgress progress;
    uint32_t due_at;
    bool active;
    bool bound;
} PoisonLessonAssignment;

typedef struct {
    PoisonLessonAssignment assignments[POISON_LESSON_MAX_ASSIGNMENTS];
    uint8_t count;
} PoisonLessonAssignmentStore;

void poison_lesson_assignments_init(PoisonLessonAssignmentStore* store);
bool poison_lesson_assignment_create(
    PoisonLessonAssignmentStore* store,
    const PoisonLesson* lesson,
    const char* assignment_id,
    const char* workspace_id,
    const char* instructor_id,
    uint32_t due_at,
    PoisonRole role,
    uint8_t* index);
bool poison_lesson_assignment_bind(
    PoisonLessonAssignmentStore* store,
    uint8_t index,
    const char* learner_id,
    PoisonRole role);
bool poison_lesson_assignment_collect(
    const PoisonLessonAssignmentStore* store,
    uint8_t index,
    PoisonRole role,
    PoisonLessonProgress* progress);
bool poison_lesson_assignment_reset(
    PoisonLessonAssignmentStore* store,
    uint8_t index,
    PoisonRole role);
size_t poison_lesson_assignment_list(
    const PoisonLessonAssignmentStore* store,
    const char* workspace_id,
    PoisonRole role,
    PoisonLessonAssignment* output,
    size_t capacity);

#ifdef __cplusplus
}
#endif
