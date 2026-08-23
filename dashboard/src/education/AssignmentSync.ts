import type { LearnerAssignment } from "./ClassroomManager";
import type { LessonProgress } from "./LessonRunner";

export interface AssignmentSyncRecord {
  assignmentId: string;
  workspaceId: string;
  assignment: LearnerAssignment;
  progress?: LessonProgress;
  revision: number;
}

function boundedId(value: string): boolean { return /^[a-z0-9._-]{1,64}$/.test(value); }

function recordKey(record: Pick<AssignmentSyncRecord, "workspaceId" | "assignmentId">): string {
  return `${record.workspaceId}:${record.assignmentId}`;
}

export function enqueueAssignmentSync(queue: readonly AssignmentSyncRecord[], record: AssignmentSyncRecord): AssignmentSyncRecord[] {
  if (!boundedId(record.assignmentId) || !boundedId(record.workspaceId) || record.revision < 1) throw new Error("invalid assignment sync record");
  const key = recordKey(record);
  const existing = queue.findIndex((item) => recordKey(item) === key);
  if (existing < 0) return [...queue, record];
  if (queue[existing].revision >= record.revision) return [...queue];
  return queue.map((item, index) => index === existing ? record : item);
}

export function mergeAssignmentProgress(local: LessonProgress, remote: LessonProgress): LessonProgress {
  if (local.learnerId !== remote.learnerId || local.lessonId !== remote.lessonId || local.lessonVersion !== remote.lessonVersion) {
    throw new Error("progress scope conflict");
  }
  const evidenceDigests = [...new Set([...local.evidenceDigests, ...remote.evidenceDigests])].sort();
  const completedSteps = Math.max(local.completedSteps, remote.completedSteps);
  return { ...local, completedSteps, evidenceDigests, complete: local.complete || remote.complete };
}

export function mergeAssignmentSync(local: readonly AssignmentSyncRecord[], incoming: readonly AssignmentSyncRecord[]): AssignmentSyncRecord[] {
  let merged = [...local];
  for (const record of incoming) {
    if (!boundedId(record.assignmentId) || !boundedId(record.workspaceId) || record.revision < 1) throw new Error("invalid assignment sync record");
    const current = merged.find((item) => recordKey(item) === recordKey(record));
    if (!current) { merged.push(record); continue; }
    if (current.assignment.learnerId !== record.assignment.learnerId || current.assignment.lessonId !== record.assignment.lessonId) throw new Error("assignment scope conflict");
    const progress = current.progress && record.progress ? mergeAssignmentProgress(current.progress, record.progress) : current.progress ?? record.progress;
    if (record.revision > current.revision) merged = merged.map((item) => recordKey(item) === recordKey(record) ? { ...record, progress } : item);
    else merged = merged.map((item) => recordKey(item) === recordKey(record) ? { ...current, progress } : item);
  }
  return merged.sort((a, b) => recordKey(a).localeCompare(recordKey(b)));
}
