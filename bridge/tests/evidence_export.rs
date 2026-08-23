use poisoned_bridge::evidence::{export::deterministic_manifest, IndexedEvidence};

#[test]
fn repeated_exports_are_byte_identical() {
    let records = vec![IndexedEvidence { evidence_id: "b".into(), case_id: "c".into(), content_sha256: "00".repeat(32), content_length: 1, media_type: "text/plain".into(), source_path: "/ext/evidence/b".into() }, IndexedEvidence { evidence_id: "a".into(), case_id: "c".into(), content_sha256: "11".repeat(32), content_length: 2, media_type: "text/plain".into(), source_path: "/ext/evidence/a".into() }];
    assert_eq!(deterministic_manifest(records.clone()).unwrap(), deterministic_manifest(records).unwrap());
}
