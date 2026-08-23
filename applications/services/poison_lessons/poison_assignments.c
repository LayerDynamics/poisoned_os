#include "poison_assignments.h"

#include <string.h>

static bool assignment_id_valid(const char* value) {
    if(!value || value[0] == '\0' || strlen(value) >= POISON_LESSON_MAX_ID) return false;
    for(const char* cursor = value; *cursor; ++cursor) {
        if(!((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= '0' && *cursor <= '9') ||
             *cursor == '.' || *cursor == '-' || *cursor == '_')) {
            return false;
        }
    }
    return true;
}

static bool role_can_manage(PoisonRole role) {
    return role == PoisonRoleOwner || role == PoisonRoleInstructor;
}

static bool
    assignment_matches(const PoisonLessonAssignment* assignment, const char* workspace_id) {
    return assignment && assignment->active && workspace_id &&
           strcmp(assignment->workspace_id, workspace_id) == 0;
}

void poison_lesson_assignments_init(PoisonLessonAssignmentStore* store) {
    if(store) memset(store, 0, sizeof(*store));
}

bool poison_lesson_assignment_create(
    PoisonLessonAssignmentStore* store,
    const PoisonLesson* lesson,
    const char* assignment_id,
    const char* workspace_id,
    const char* instructor_id,
    uint32_t due_at,
    PoisonRole role,
    uint8_t* index) {
    if(!store || !lesson || !lesson->signature_valid || !role_can_manage(role) ||
       store->count >= POISON_LESSON_MAX_ASSIGNMENTS || !assignment_id_valid(assignment_id) ||
       !assignment_id_valid(workspace_id) || !assignment_id_valid(instructor_id) || !index) {
        return false;
    }
    for(uint8_t i = 0; i < store->count; ++i) {
        if(store->assignments[i].active &&
           strcmp(store->assignments[i].assignment_id, assignment_id) == 0)
            return false;
    }
    PoisonLessonAssignment* assignment = &store->assignments[store->count];
    memset(assignment, 0, sizeof(*assignment));
    strncpy(assignment->assignment_id, assignment_id, sizeof(assignment->assignment_id) - 1u);
    strncpy(assignment->workspace_id, workspace_id, sizeof(assignment->workspace_id) - 1u);
    strncpy(assignment->instructor_id, instructor_id, sizeof(assignment->instructor_id) - 1u);
    assignment->due_at = due_at;
    assignment->active = true;
    if(!poison_lesson_progress_init(&assignment->progress, lesson, "unassigned")) {
        memset(assignment, 0, sizeof(*assignment));
        return false;
    }
    *index = store->count++;
    return true;
}

bool poison_lesson_assignment_bind(
    PoisonLessonAssignmentStore* store,
    uint8_t index,
    const char* learner_id,
    PoisonRole role) {
    if(!store || index >= store->count || !role_can_manage(role) ||
       !assignment_id_valid(learner_id))
        return false;
    PoisonLessonAssignment* assignment = &store->assignments[index];
    if(!assignment->active || assignment->bound) return false;
    strncpy(assignment->learner_id, learner_id, sizeof(assignment->learner_id) - 1u);
    strncpy(
        assignment->progress.learner_id, learner_id, sizeof(assignment->progress.learner_id) - 1u);
    assignment->bound = true;
    return true;
}

bool poison_lesson_assignment_collect(
    const PoisonLessonAssignmentStore* store,
    uint8_t index,
    PoisonRole role,
    PoisonLessonProgress* progress) {
    if(!store || index >= store->count || !progress || !store->assignments[index].active ||
       !store->assignments[index].bound ||
       (role != PoisonRoleOwner && role != PoisonRoleInstructor && role != PoisonRoleStudent)) {
        return false;
    }
    *progress = store->assignments[index].progress;
    return true;
}

bool poison_lesson_assignment_reset(
    PoisonLessonAssignmentStore* store,
    uint8_t index,
    PoisonRole role) {
    if(!store || index >= store->count || !role_can_manage(role) ||
       !store->assignments[index].active || !store->assignments[index].bound) {
        return false;
    }
    return poison_lesson_reset(&store->assignments[index].progress);
}

size_t poison_lesson_assignment_list(
    const PoisonLessonAssignmentStore* store,
    const char* workspace_id,
    PoisonRole role,
    PoisonLessonAssignment* output,
    size_t capacity) {
    if(!store || !workspace_id || !assignment_id_valid(workspace_id) || !output ||
       capacity == 0u || role >= PoisonRoleCount)
        return 0u;
    size_t copied = 0u;
    for(uint8_t i = 0; i < store->count && copied < capacity; ++i) {
        const PoisonLessonAssignment* assignment = &store->assignments[i];
        if(!assignment_matches(assignment, workspace_id)) continue;
        if(role == PoisonRoleStudent && !assignment->bound) continue;
        output[copied++] = *assignment;
    }
    return copied;
}
