import { describe, expect, it } from "vitest";
import type { WorkspaceResetPreview } from "./WorkspaceReset";
describe("WorkspaceReset", () => { it("keeps preview target explicit", () => { const preview: WorkspaceResetPreview = { snapshotId: "s", workspacePath: "/cases/case-1", affectedPaths: ["/cases/case-1/evidence"] }; expect(preview.workspacePath).toBe("/cases/case-1"); expect(preview.affectedPaths.some((path) => path.startsWith("/cases/case-2"))).toBe(false); }); });
