import type { ReactElement } from "react";
import type { LessonPack } from "./LessonRunner";
import { createAssignment, type LearnerAssignment } from "./ClassroomManager";

export function assignLesson(lesson: LessonPack, learnerIds: readonly string[], dueAt?: string): LearnerAssignment[] { return [...new Set(learnerIds)].map((learnerId) => createAssignment(learnerId, lesson, dueAt)); }
export function AssignmentManager({ assignments }: { assignments: readonly LearnerAssignment[] }): ReactElement { return <section aria-label="Assignment manager"><h2>Assignments</h2><ul>{assignments.map((assignment) => <li key={`${assignment.learnerId}:${assignment.lessonId}`}>{assignment.learnerId} — {assignment.dueAt ?? "no due date"}</li>)}</ul></section>; }
