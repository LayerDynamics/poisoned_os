export interface OfflineMutation { id: string; operation: "annotation" | "case" | "file"; payload: unknown; acknowledged: boolean; }
export function createMutation(id: string, operation: OfflineMutation["operation"], payload: unknown): OfflineMutation { if (!id || payload === undefined) throw new Error("mutation requires stable id and payload"); return { id, operation, payload, acknowledged: false }; }
export function acknowledgeMutation(mutation: OfflineMutation): OfflineMutation { if (mutation.acknowledged) return mutation; return { ...mutation, acknowledged: true }; }
export function pendingMutations(mutations: readonly OfflineMutation[]): OfflineMutation[] { return mutations.filter((mutation) => !mutation.acknowledged).sort((a, b) => a.id.localeCompare(b.id)); }
