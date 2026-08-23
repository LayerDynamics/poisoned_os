import { useMemo, useState, type FormEvent, type ReactElement } from "react";
import { FileTransferQueue, RpcFileTransferTransport, sha256Hex, type FileTransferSession } from "./FileTransferQueue";
import { FileListClient, type FileListSession } from "./FileListClient";
import { EvidenceClient, type EvidenceSession } from "../evidence/EvidenceClient";

export function FileTransferPanel({ session }: { session: FileTransferSession & FileListSession & EvidenceSession }): ReactElement {
  const queue = useMemo(() => new FileTransferQueue(new RpcFileTransferTransport(session)), [session]);
  const files = useMemo(() => new FileListClient(session), [session]);
  const evidence = useMemo(() => new EvidenceClient(session), [session]);
  const [file, setFile] = useState<File | null>(null);
  const [path, setPath] = useState("/workloads/upload.bin");
  const [status, setStatus] = useState("idle");
  const [busy, setBusy] = useState(false);
  const [entries, setEntries] = useState<readonly string[]>([]);
  const [caseId, setCaseId] = useState("default-case");
  const [caseName, setCaseName] = useState("Default evidence case");
  const [casePurpose, setCasePurpose] = useState("Dashboard file captures");
  const [retentionPolicy, setRetentionPolicy] = useState("manual");
  const [captureEvidence, setCaptureEvidence] = useState(true);
  const [evidenceIds, setEvidenceIds] = useState<readonly string[]>([]);
  const [lastEvidenceId, setLastEvidenceId] = useState("");
  const [annotationText, setAnnotationText] = useState("");
  const [exportId, setExportId] = useState("dashboard-export");

  const upload = async (event: FormEvent) => {
    event.preventDefault();
    if (!file) return;
    setBusy(true);
    setStatus("hashing");
    try {
      const data = new Uint8Array(await file.arrayBuffer());
      const digest = await sha256Hex(data);
      const operationId = `upload-${crypto.randomUUID()}`;
      if (captureEvidence) {
        setStatus("creating evidence case");
        await evidence.createCase({ caseId, name: caseName, purpose: casePurpose, retentionPolicy });
      }
      const result = await queue.upload(
        operationId,
        path,
        data,
        digest,
        (progress) => setStatus(`${progress.sentBytes}/${progress.totalBytes} bytes`),
      );
      setStatus(result.acknowledged ? `verified ${digest}` : "not acknowledged");
      const separator = path.lastIndexOf("/");
      const parent = separator > 0 ? path.slice(0, separator) : path;
      const page = await files.list(parent);
      setEntries(page.entries.map((entry) => entry.path));
      if (captureEvidence) {
        const receipt = await evidence.capture({
          evidenceId: operationId,
          caseId,
          sourcePath: path,
          contentSha256: digest,
          contentLength: data.byteLength,
          mediaType: file.type || "application/octet-stream",
        });
        setLastEvidenceId(receipt.evidenceId);
        setEvidenceIds((current) => current.includes(receipt.evidenceId) ? current : [...current, receipt.evidenceId]);
        setStatus(`evidence ${receipt.evidenceId} · ${receipt.auditSha256}`);
      }
    } catch (reason) {
      setStatus(reason instanceof Error ? reason.message : String(reason));
    } finally {
      setBusy(false);
    }
  };

  const annotate = async (event: FormEvent) => {
    event.preventDefault();
    if (!lastEvidenceId || !annotationText) return;
    setBusy(true);
    try {
      const receipt = await evidence.annotate({
        annotationId: `annotation-${crypto.randomUUID()}`,
        evidenceId: lastEvidenceId,
        text: annotationText,
        tags: ["dashboard"],
      });
      setAnnotationText("");
      setStatus(`annotation ${receipt.annotationId} appended`);
    } catch (reason) {
      setStatus(reason instanceof Error ? reason.message : String(reason));
    } finally {
      setBusy(false);
    }
  };

  const prepareExport = async (event: FormEvent) => {
    event.preventDefault();
    if (evidenceIds.length === 0) return;
    setBusy(true);
    try {
      const receipt = await evidence.prepareExport({ exportId, evidenceIds });
      setStatus(`manifest ${receipt.manifestSha256} · ${receipt.acceptedEvidenceIds} records`);
    } catch (reason) {
      setStatus(reason instanceof Error ? reason.message : String(reason));
    } finally {
      setBusy(false);
    }
  };

  return <section className="update-card" aria-labelledby="file-transfer-title">
    <div className="update-heading">
      <div><p className="eyebrow">TRANSACTIONAL DEVICE STORAGE</p><h2 id="file-transfer-title">Upload file</h2></div>
      <output className="update-state">{status}</output>
    </div>
    <form onSubmit={(event) => void upload(event)}>
      <input aria-label="Destination logical path" value={path} onChange={(event) => setPath(event.target.value)} />
      <input aria-label="File to upload" type="file" onChange={(event) => setFile(event.target.files?.[0] ?? null)} />
      <input aria-label="Evidence case ID" value={caseId} onChange={(event) => setCaseId(event.target.value)} />
      <input aria-label="Evidence case name" value={caseName} onChange={(event) => setCaseName(event.target.value)} />
      <input aria-label="Evidence case purpose" value={casePurpose} onChange={(event) => setCasePurpose(event.target.value)} />
      <input aria-label="Evidence retention policy" value={retentionPolicy} onChange={(event) => setRetentionPolicy(event.target.value)} />
      <label><input type="checkbox" checked={captureEvidence} onChange={(event) => setCaptureEvidence(event.target.checked)} /> Capture immutable evidence</label>
      <button type="submit" disabled={busy || !file}>Upload and verify</button>
    </form>
    <form onSubmit={(event) => void annotate(event)}>
      <input aria-label="Append-only evidence annotation" value={annotationText} onChange={(event) => setAnnotationText(event.target.value)} />
      <button type="submit" disabled={busy || !lastEvidenceId || !annotationText}>Append annotation</button>
    </form>
    <form onSubmit={(event) => void prepareExport(event)}>
      <input aria-label="Evidence export ID" value={exportId} onChange={(event) => setExportId(event.target.value)} />
      <button type="submit" disabled={busy || evidenceIds.length === 0}>Prepare device manifest</button>
    </form>
    {entries.length > 0 && <ul>{entries.map((entry) => <li key={entry}>{entry}</li>)}</ul>}
  </section>;
}
