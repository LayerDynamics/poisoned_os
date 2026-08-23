import { describe, expect, it } from "vitest";
import { enqueueAssignmentSync, mergeAssignmentProgress, mergeAssignmentSync, type AssignmentSyncRecord } from "./AssignmentSync";

const base = (revision: number, completedSteps: number): AssignmentSyncRecord => ({
  assignmentId: "a1", workspaceId: "class-a", revision,
  assignment: { learnerId: "student-1", lessonId: "lesson" },
  progress: { learnerId: "student-1", lessonId: "lesson", lessonVersion: 1, completedSteps, evidenceDigests: [], complete: false },
});

describe("AssignmentSync", () => {
  it("queues idempotently and keeps the newest revision", () => {
    const queued = enqueueAssignmentSync(enqueueAssignmentSync([], base(1, 1)), base(1, 1));
    expect(enqueueAssignmentSync(queued, base(2, 2))).toHaveLength(1);
    expect(enqueueAssignmentSync(queued, base(2, 2))[0].revision).toBe(2);
  });
  it("merges scoped progress without copying protected contents", () => {
    const merged = mergeAssignmentProgress({ ...base(1, 1).progress!, evidenceDigests: ["b"] }, { ...base(1, 2).progress!, evidenceDigests: ["a"] });
    expect(merged.completedSteps).toBe(2);
    expect(merged.evidenceDigests).toEqual(["a", "b"]);
  });
  it("rejects cross-learner merges and sorts records deterministically", () => {
    expect(() => mergeAssignmentSync([], [{ ...base(1, 1), assignmentId: "bad id" }])).toThrow();
    expect(mergeAssignmentSync([base(1, 1)], [{ ...base(2, 2), assignmentId: "a2" }, base(2, 2)].map((item) => ({ ...item, assignmentId: item.assignmentId })))).toHaveLength(2);
  });
});
