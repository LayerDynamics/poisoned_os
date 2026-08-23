use super::{index::{EvidenceIndex, IndexedEvidence}, report::VerificationReport, verify::verify_records};

pub fn quarantine(records: Vec<IndexedEvidence>) -> VerificationReport {
    let report = verify_records(&records);
    if !report.accepted { return report; }
    match EvidenceIndex::rebuild(records) { Ok(_) => report, Err(error) => VerificationReport { accepted: false, errors: vec![error.to_owned()] } }
}
