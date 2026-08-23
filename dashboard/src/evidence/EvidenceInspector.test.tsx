import { describe, expect, it } from "vitest";
import { validateEvidenceSummary } from "./EvidenceInspector";
describe("EvidenceInspector", () => { it("rejects malformed digests", () => { expect(() => validateEvidenceSummary({ evidenceId: "e", caseId: "c", sha256: "bad", length: 1, mediaType: "text/plain", derived: false })).toThrow(); }); });
