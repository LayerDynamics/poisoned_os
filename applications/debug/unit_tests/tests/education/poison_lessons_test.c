#include "../test.h"
#include "../../../../services/poison_lessons/poison_lessons.h"

static const char* lesson_digest =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

MU_TEST(poison_lessons_progress_requires_checkpoint_and_resets) {
    PoisonLesson lesson;
    PoisonLessonProgress progress;
    mu_check(poison_lesson_init(&lesson, "poison.getting-started", 1u, true));
    mu_check(poison_lesson_add_step(&lesson, "welcome", PoisonLessonExplanation, "Welcome", NULL));
    mu_check(poison_lesson_add_step(
        &lesson, "checkpoint", PoisonLessonEvidenceCheckpoint, "Checkpoint", lesson_digest));
    mu_check(poison_lesson_progress_init(&progress, &lesson, "learner-1"));
    mu_check(poison_lesson_advance(&progress, &lesson, NULL));
    mu_check(!poison_lesson_advance(&progress, &lesson, "0"));
    mu_check(poison_lesson_advance(&progress, &lesson, lesson_digest));
    mu_check(progress.complete);
    mu_check(poison_lesson_reset(&progress));
    mu_check(progress.completed_steps == 0u && !progress.complete);
}

MU_TEST_SUITE(poison_lessons_suite) {
    MU_RUN_TEST(poison_lessons_progress_requires_checkpoint_and_resets);
}
void poison_lessons_run_tests(void) {
    MU_RUN_SUITE(poison_lessons_suite);
}
