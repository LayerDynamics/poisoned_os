import { describe, expect, it } from "vitest";
import { appendLessonStep, validateLessonForAssignment } from "./LessonAuthor";
import type { LessonPack } from "./LessonRunner";

const lesson: LessonPack = { id: "lesson", version: 1, title: "Lesson", signature: "0".repeat(64), steps: [{ id: "one", kind: "explanation", title: "One" }] };
describe("LessonAuthor", () => { it("validates and versions authored steps", () => { expect(validateLessonForAssignment(lesson)).toEqual([]); expect(appendLessonStep(lesson, { id: "two", kind: "reset", title: "Two" }).version).toBe(2); }); });
