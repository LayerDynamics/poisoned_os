import type { ReactElement } from "react";
import type { LessonProgress } from "./LessonRunner";

export function exportProgress(progress: readonly LessonProgress[]): string { return JSON.stringify(progress.map(({ learnerId, lessonId, lessonVersion, completedSteps, evidenceDigests, complete }) => ({ learnerId, lessonId, lessonVersion, completedSteps, evidenceDigests, complete })).sort((a, b) => a.learnerId.localeCompare(b.learnerId))); }
export function ProgressReview({ progress }: { progress: readonly LessonProgress[] }): ReactElement { return <section aria-label="Progress review"><h2>Progress</h2><ul>{progress.map((item) => <li key={`${item.learnerId}:${item.lessonId}`}>{item.learnerId}: {item.completedSteps} {item.complete ? "complete" : "in progress"}</li>)}</ul></section>; }
