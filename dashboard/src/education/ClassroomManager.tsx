import type { ReactElement } from "react";
import type { LessonProgress, LessonPack } from "./LessonRunner";

export interface LearnerAssignment { learnerId: string; lessonId: string; dueAt?: string; progress?: LessonProgress; }
export function createAssignment(learnerId: string, lesson: LessonPack, dueAt?: string): LearnerAssignment { if (!/^[a-z0-9_-]{1,64}$/.test(learnerId)) throw new Error("learner ID must be pseudonymous and bounded"); return { learnerId, lessonId: lesson.id, dueAt }; }
export function ClassroomManager({ assignments }: { assignments: readonly LearnerAssignment[] }): ReactElement { return <section aria-label="Classroom assignments"><h2>Classroom</h2><ul>{assignments.map((assignment) => <li key={`${assignment.learnerId}:${assignment.lessonId}`}>{assignment.learnerId}: {assignment.lessonId} {assignment.progress?.complete ? "complete" : "assigned"}</li>)}</ul></section>; }
