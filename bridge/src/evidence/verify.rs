use super::{index::{is_digest, IndexedEvidence}, report::VerificationReport};

pub fn verify_records(records: &[IndexedEvidence]) -> VerificationReport {
    let mut report = VerificationReport::default();
    for record in records {
        if !is_digest(&record.content_sha256) { report.errors.push(format!("{}: invalid digest", record.evidence_id)); }
        if !record.source_path.starts_with('/') { report.errors.push(format!("{}: invalid path", record.evidence_id)); }
    }
    report.accepted = report.errors.is_empty();
    report
}
