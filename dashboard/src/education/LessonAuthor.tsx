import type { ReactElement } from "react";
import type { LessonPack, LessonStep } from "./LessonRunner";

export function validateLessonForAssignment(lesson: LessonPack): string[] { const errors: string[] = []; if (!/^[0-9a-f]{64}$/.test(lesson.signature)) errors.push("signature"); if (lesson.steps.length === 0 || lesson.steps.length > 32) errors.push("steps"); for (const step of lesson.steps) if (!step.id || !step.title) errors.push(`step:${step.id}`); return errors; }
export function appendLessonStep(lesson: LessonPack, step: LessonStep): LessonPack { if (validateLessonForAssignment(lesson).length > 0 || lesson.steps.length >= 32) throw new Error("lesson cannot accept a step"); return { ...lesson, version: lesson.version + 1, steps: [...lesson.steps, step] }; }
export function LessonAuthor({ lesson }: { lesson: LessonPack }): ReactElement { const errors = validateLessonForAssignment(lesson); return <section aria-label="Lesson author"><h2>Author lesson</h2><p role="status">{errors.length === 0 ? "Ready for signing" : `Needs attention: ${errors.join(", ")}`}</p><ol>{lesson.steps.map((step) => <li key={step.id}>{step.title}</li>)}</ol></section>; }
