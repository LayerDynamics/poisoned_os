import { create } from "@bufbuild/protobuf";
import { CommandStatus, MainSchema, type Main } from "../generated/flipper_pb";
import {
  AnnotationSchema,
  CaseSchema,
  EvidenceRecordSchema,
  ExportManifestSchema,
  type Annotation,
  type Case as EvidenceCase,
  type EvidenceRecord,
  type ExportManifest,
} from "../generated/poison_evidence_pb";

export interface EvidenceSession {
  request(request: Main, signal?: AbortSignal): Promise<Main>;
}

export interface EvidenceCaptureRequest {
  readonly evidenceId: string;
  readonly caseId: string;
  readonly sourcePath: string;
  readonly contentSha256: string;
  readonly contentLength: number;
  readonly mediaType: string;
}

export interface CaseCreateRequest {
  readonly caseId: string;
  readonly name: string;
  readonly purpose: string;
  readonly retentionPolicy: string;
}

export interface AnnotationCreateRequest {
  readonly annotationId: string;
  readonly evidenceId: string;
  readonly text: string;
  readonly tags: readonly string[];
}

export interface ExportPrepareRequest {
  readonly exportId: string;
  readonly evidenceIds: readonly string[];
}

const IDENTIFIER = /^[A-Za-z0-9_-][A-Za-z0-9._-]{0,63}$/;
const EXPORT_SCHEMA = "poison.evidence-manifest/v1";
const EXPORT_BATCH_SIZE = 8;
const MAX_EXPORT_EVIDENCE = 10_000;

export class EvidenceClient {
  private nextCommandId = 5_000;

  public constructor(private readonly session: EvidenceSession) {}

  public async createCase(input: CaseCreateRequest, signal?: AbortSignal): Promise<EvidenceCase> {
    if (!IDENTIFIER.test(input.caseId) || !input.name || input.name.length > 128 ||
        !input.purpose || input.purpose.length > 256 || !input.retentionPolicy ||
        input.retentionPolicy.length > 64) {
      throw new Error("case request is outside its bounds");
    }
    const response = await this.session.request(create(MainSchema, {
      commandId: this.nextCommandId++,
      content: {
        case: "poisonCase",
        value: create(CaseSchema, {
          caseId: input.caseId,
          name: input.name,
          purpose: input.purpose,
          retentionPolicy: input.retentionPolicy,
        }),
      },
    }), signal);
    if (response.commandStatus !== CommandStatus.OK || response.content.case !== "poisonCase") {
      throw new Error("device rejected case creation");
    }
    const record = response.content.value;
    if (record.caseId !== input.caseId || record.name !== input.name ||
        record.purpose !== input.purpose || record.retentionPolicy !== input.retentionPolicy ||
        !IDENTIFIER.test(record.ownerId) || record.createdAtMs <= 0n) {
      throw new Error("device returned an invalid case receipt");
    }
    return record;
  }

  public async capture(input: EvidenceCaptureRequest, signal?: AbortSignal): Promise<EvidenceRecord> {
    if (!IDENTIFIER.test(input.evidenceId) || !IDENTIFIER.test(input.caseId) ||
        !/^\/(?:config|profiles|apps|scripts|workloads|cases|evidence|lessons|exports|int|ext)(?:\/[^/\\\0.][^/\\\0]*|\/[.][^./\\\0][^/\\\0]*)*$/.test(input.sourcePath) ||
        input.sourcePath.length > 256 || !/^[0-9a-f]{64}$/.test(input.contentSha256) ||
        !Number.isSafeInteger(input.contentLength) || input.contentLength < 1 ||
        input.contentLength > 16 * 1024 * 1024 || !input.mediaType || input.mediaType.length > 96) {
      throw new Error("evidence capture request is outside its bounds");
    }
    const response = await this.session.request(create(MainSchema, {
      commandId: this.nextCommandId++,
      content: {
        case: "poisonEvidenceRecord",
        value: create(EvidenceRecordSchema, {
          evidenceId: input.evidenceId,
          caseId: input.caseId,
          sourceAppId: "poison.dashboard.file-transfer",
          contentSha256: input.contentSha256,
          contentLength: BigInt(input.contentLength),
          mediaType: input.mediaType,
          sourcePath: input.sourcePath,
        }),
      },
    }), signal);
    if (response.commandStatus !== CommandStatus.OK || response.content.case !== "poisonEvidenceRecord") {
      throw new Error("device rejected evidence capture");
    }
    const record = response.content.value;
    if (record.evidenceId !== input.evidenceId || record.caseId !== input.caseId ||
        record.sourcePath !== input.sourcePath || record.contentSha256 !== input.contentSha256 ||
        record.contentLength !== BigInt(input.contentLength) || !/^[0-9a-f]{64}$/.test(record.auditSha256) ||
        !/^[0-9a-f]{64}$/.test(record.previousAuditSha256)) {
      throw new Error("device returned an invalid evidence receipt");
    }
    return record;
  }

  public async annotate(input: AnnotationCreateRequest, signal?: AbortSignal): Promise<Annotation> {
    if (!IDENTIFIER.test(input.annotationId) || !IDENTIFIER.test(input.evidenceId) ||
        !input.text || input.text.length > 1024 || input.tags.length > 32 ||
        input.tags.some((tag) => !IDENTIFIER.test(tag))) {
      throw new Error("annotation request is outside its bounds");
    }
    const response = await this.session.request(create(MainSchema, {
      commandId: this.nextCommandId++,
      content: {
        case: "poisonAnnotation",
        value: create(AnnotationSchema, {
          annotationId: input.annotationId,
          evidenceId: input.evidenceId,
          text: input.text,
          tags: [...input.tags],
        }),
      },
    }), signal);
    if (response.commandStatus !== CommandStatus.OK || response.content.case !== "poisonAnnotation") {
      throw new Error("device rejected annotation");
    }
    const annotation = response.content.value;
    if (annotation.annotationId !== input.annotationId || annotation.evidenceId !== input.evidenceId ||
        annotation.text !== input.text || annotation.createdAtMs <= 0n ||
        !IDENTIFIER.test(annotation.authorId) || annotation.tags.length !== input.tags.length ||
        annotation.tags.some((tag, index) => tag !== input.tags[index])) {
      throw new Error("device returned an invalid annotation receipt");
    }
    return annotation;
  }

  public async prepareExport(input: ExportPrepareRequest, signal?: AbortSignal): Promise<ExportManifest> {
    if (!IDENTIFIER.test(input.exportId) || input.evidenceIds.length < 1 ||
        input.evidenceIds.length > MAX_EXPORT_EVIDENCE ||
        input.evidenceIds.some((identifier) => !IDENTIFIER.test(identifier))) {
      throw new Error("export request is outside its bounds");
    }
    const evidenceIds = [...input.evidenceIds].sort();
    if (evidenceIds.some((identifier, index) => index > 0 && identifier === evidenceIds[index - 1])) {
      throw new Error("export evidence IDs must be unique");
    }

    let receipt: ExportManifest | null = null;
    for (let offset = 0, batchIndex = 0; offset < evidenceIds.length; offset += EXPORT_BATCH_SIZE, batchIndex += 1) {
      const batch = evidenceIds.slice(offset, offset + EXPORT_BATCH_SIZE);
      const finalize = offset + batch.length === evidenceIds.length;
      const response = await this.session.request(create(MainSchema, {
        commandId: this.nextCommandId++,
        content: {
          case: "poisonExportManifest",
          value: create(ExportManifestSchema, {
            exportId: input.exportId,
            schema: EXPORT_SCHEMA,
            evidenceIds: batch,
            batchIndex,
            finalize,
          }),
        },
      }), signal);
      if (response.commandStatus !== CommandStatus.OK || response.content.case !== "poisonExportManifest") {
        throw new Error("device rejected export preparation");
      }
      receipt = response.content.value;
      const accepted = Math.min(offset + batch.length, evidenceIds.length);
      if (receipt.exportId !== input.exportId || receipt.schema !== EXPORT_SCHEMA ||
          receipt.batchIndex !== batchIndex || receipt.finalize !== finalize ||
          receipt.acceptedEvidenceIds !== accepted || receipt.evidenceIds.length !== 0 ||
          (!finalize && receipt.manifestSha256 !== "") ||
          (finalize && !/^[0-9a-f]{64}$/.test(receipt.manifestSha256))) {
        throw new Error("device returned an invalid export receipt");
      }
    }
    if (!receipt?.finalize) throw new Error("device did not finalize export preparation");
    return receipt;
  }
}
