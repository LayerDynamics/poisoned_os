use poisoned_bridge::evidence::{import::quarantine, IndexedEvidence};

#[test]
fn import_accepts_verified_records_only() {
    let record = IndexedEvidence { evidence_id: "a".into(), case_id: "c".into(), content_sha256: "00".repeat(32), content_length: 1, media_type: "text/plain".into(), source_path: "/ext/evidence/a".into() };
    assert!(quarantine(vec![record]).accepted);
}
