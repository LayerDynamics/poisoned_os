#include "../test.h"
#include "../../../../services/poison_lessons/poison_assignments.h"

MU_TEST(poison_lesson_assignments_are_scoped_and_role_checked) {
    PoisonLesson lesson;
    PoisonLessonAssignmentStore store;
    PoisonLessonAssignment listed[2];
    PoisonLessonProgress progress;
    uint8_t index = 0xffu;
    mu_check(poison_lesson_init(&lesson, "poison.lesson", 1u, true));
    mu_check(poison_lesson_add_step(&lesson, "step", PoisonLessonExplanation, "Step", NULL));
    poison_lesson_assignments_init(&store);
    mu_check(!poison_lesson_assignment_create(
        &store, &lesson, "a1", "class-a", "teacher", 10u, PoisonRoleStudent, &index));
    mu_check(poison_lesson_assignment_create(
        &store, &lesson, "a1", "class-a", "teacher", 10u, PoisonRoleInstructor, &index));
    mu_check(poison_lesson_assignment_bind(&store, index, "student-1", PoisonRoleInstructor));
    mu_check(!poison_lesson_assignment_bind(&store, index, "student-2", PoisonRoleInstructor));
    mu_check(
        poison_lesson_assignment_list(&store, "class-a", PoisonRoleInstructor, listed, 2u) == 1u);
    mu_check(
        poison_lesson_assignment_list(&store, "class-b", PoisonRoleInstructor, listed, 2u) == 0u);
    mu_check(poison_lesson_assignment_collect(&store, index, PoisonRoleStudent, &progress));
    mu_check(!poison_lesson_assignment_reset(&store, index, PoisonRoleStudent));
    mu_check(poison_lesson_assignment_reset(&store, index, PoisonRoleInstructor));
}

MU_TEST_SUITE(poison_assignments_suite) {
    MU_RUN_TEST(poison_lesson_assignments_are_scoped_and_role_checked);
}
void poison_assignments_run_tests(void) {
    MU_RUN_SUITE(poison_assignments_suite);
}
