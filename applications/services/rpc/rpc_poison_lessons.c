#include <applications/services/poison_lessons/poison_lessons.h>

bool poison_lessons_rpc_advance(
    PoisonLessonProgress* progress,
    const PoisonLesson* lesson,
    const char* evidence_digest) {
    return poison_lesson_advance(progress, lesson, evidence_digest);
}
