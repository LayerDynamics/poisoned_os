use poisoned_bridge::evidence::{EvidenceIndex, IndexedEvidence};

fn record(id: &str) -> IndexedEvidence { IndexedEvidence { evidence_id: id.into(), case_id: "case".into(), content_sha256: "00".repeat(32), content_length: 1, media_type: "text/plain".into(), source_path: "/ext/evidence/a".into() } }

#[test]
fn index_is_derived_rebuildable_and_stably_ordered() {
    let index = EvidenceIndex::rebuild(vec![record("b"), record("a")]).unwrap();
    assert_eq!(index.ordered().map(|item| item.evidence_id.as_str()).collect::<Vec<_>>(), vec!["a", "b"]);
    assert!(index.get("a").is_some());
}
