use poisoned_bridge::evidence::{verify::verify_records, IndexedEvidence};

#[test]
fn verifier_reports_malformed_digest_instead_of_repairing() {
    let record = IndexedEvidence { evidence_id: "a".into(), case_id: "c".into(), content_sha256: "AA".repeat(32), content_length: 1, media_type: "text/plain".into(), source_path: "/ext/evidence/a".into() };
    let report = verify_records(&[record]);
    assert!(!report.accepted);
    assert_eq!(report.errors.len(), 1);
}
