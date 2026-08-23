import { useState, type ReactElement } from "react";

export interface ConfirmationRequest {
  sessionId: bigint;
  role: number;
  commandDigest: string;
  targetDigest: string;
  consequenceDigest: string;
  policyVersion: number;
  consequence: string;
  physicalRequired: boolean;
}

export function validateConfirmationRequest(request: ConfirmationRequest): void {
  if (request.sessionId === 0n || request.role < 0 || request.role > 4 || request.policyVersion < 1) throw new Error("invalid confirmation binding");
  for (const digest of [request.commandDigest, request.targetDigest, request.consequenceDigest]) if (!/^[0-9a-f]{64}$/.test(digest)) throw new Error("invalid confirmation digest");
  if (!request.consequence || request.consequence.length > 128) throw new Error("invalid displayed consequence");
}

export function ConfirmationDialog({ request, onApprove, onCancel }: { request: ConfirmationRequest; onApprove: () => Promise<void>; onCancel: () => void }): ReactElement {
  const [busy, setBusy] = useState(false);
  const approve = async () => { validateConfirmationRequest(request); setBusy(true); try { await onApprove(); } finally { setBusy(false); } };
  return <dialog open aria-labelledby="confirmation-title"><h2 id="confirmation-title">Confirm device action</h2><p>{request.consequence}</p><button type="button" disabled={busy} onClick={() => void approve()}>Approve on device</button><button type="button" disabled={busy} onClick={onCancel}>Cancel</button></dialog>;
}
