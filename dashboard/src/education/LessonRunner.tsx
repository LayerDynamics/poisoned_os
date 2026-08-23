import { useMemo, useState, type ReactElement } from "react";

export type LessonStepKind = "explanation" | "device-action" | "browser-action" | "observation" | "evidence-checkpoint" | "knowledge-check" | "starter-code" | "reset";
export interface LessonStep { id: string; kind: LessonStepKind; title: string; requiredEvidenceDigest?: string; }
export interface LessonPack { id: string; version: number; title: string; signature: string; steps: readonly LessonStep[]; }
export interface LessonProgress { learnerId: string; lessonId: string; lessonVersion: number; completedSteps: number; evidenceDigests: readonly string[]; complete: boolean; }

export function advanceLesson(progress: LessonProgress, lesson: LessonPack, evidenceDigest?: string): LessonProgress {
  const step = lesson.steps[progress.completedSteps];
  if (!step || progress.lessonVersion !== lesson.version || (step.requiredEvidenceDigest && step.requiredEvidenceDigest !== evidenceDigest)) throw new Error("lesson checkpoint is not satisfied");
  return { ...progress, completedSteps: progress.completedSteps + 1, evidenceDigests: evidenceDigest ? [...progress.evidenceDigests, evidenceDigest] : progress.evidenceDigests, complete: progress.completedSteps + 1 === lesson.steps.length };
}

export function resetLesson(progress: LessonProgress): LessonProgress { return { ...progress, completedSteps: 0, evidenceDigests: [], complete: false }; }

export function LessonRunner({ lesson, learnerId }: { lesson: LessonPack; learnerId: string }): ReactElement {
  const [progress, setProgress] = useState<LessonProgress>({ learnerId, lessonId: lesson.id, lessonVersion: lesson.version, completedSteps: 0, evidenceDigests: [], complete: false });
  const current = lesson.steps[progress.completedSteps];
  const completion = useMemo(() => `${progress.completedSteps}/${lesson.steps.length}`, [lesson.steps.length, progress.completedSteps]);
  return <section aria-label="Lesson runner"><h2>{lesson.title}</h2><p role="status">Progress {completion}</p>{current ? <><h3>{current.title}</h3><button type="button" onClick={() => setProgress((value) => advanceLesson(value, lesson))}>Complete step</button></> : <p role="status">Lesson complete</p>}<button type="button" onClick={() => setProgress((value) => resetLesson(value))}>Reset learner workspace</button></section>;
}
