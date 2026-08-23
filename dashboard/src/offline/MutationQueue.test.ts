import { describe, expect, it } from "vitest";
import { acknowledgeMutation, createMutation, pendingMutations } from "./MutationQueue";
describe("MutationQueue", () => { it("keeps unacknowledged mutations separate from device commit", () => { const a = createMutation("b", "annotation", { text: "x" }); const b = acknowledgeMutation(createMutation("a", "case", { name: "x" })); expect(pendingMutations([a, b]).map((item) => item.id)).toEqual(["b"]); }); });
