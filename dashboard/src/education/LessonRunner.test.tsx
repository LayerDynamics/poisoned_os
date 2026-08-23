import { describe, expect, it } from "vitest";
import { advanceLesson, resetLesson, type LessonPack, type LessonProgress } from "./LessonRunner";

const lesson: LessonPack = { id: "poison.getting-started", version: 1, title: "Getting Started", signature: "0".repeat(64), steps: [{ id: "one", kind: "explanation", title: "One" }, { id: "checkpoint", kind: "evidence-checkpoint", title: "Checkpoint", requiredEvidenceDigest: "a".repeat(64) }] };
const initial: LessonProgress = { learnerId: "learner-1", lessonId: lesson.id, lessonVersion: 1, completedSteps: 0, evidenceDigests: [], complete: false };
describe("LessonRunner", () => { it("requires the checkpoint digest and resets only the learner progress", () => { const next = advanceLesson(initial, lesson); expect(() => advanceLesson(next, lesson)).toThrow(); const complete = advanceLesson(next, lesson, "a".repeat(64)); expect(complete.complete).toBe(true); expect(resetLesson(complete).completedSteps).toBe(0); }); });
