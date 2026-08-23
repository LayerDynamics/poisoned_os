use poisoned_bridge::evidence::{sqlite::SqliteEvidenceStore, EvidenceIndex, IndexedEvidence};

fn record(id: &str) -> IndexedEvidence { IndexedEvidence { evidence_id: id.into(), case_id: "case".into(), content_sha256: "00".repeat(32), content_length: 1, media_type: "text/plain".into(), source_path: "/ext/evidence/a".into() } }

#[test]
fn sqlite_store_survives_restart_and_orders_rows() {
    let path = std::env::temp_dir().join(format!("poison-evidence-{}.sqlite", std::process::id()));
    let _ = std::fs::remove_file(&path);
    { let store = SqliteEvidenceStore::open(&path).unwrap(); store.upsert(&record("b")).unwrap(); store.upsert(&record("a")).unwrap(); }
    let store = SqliteEvidenceStore::open(&path).unwrap();
    assert_eq!(store.load_all().unwrap().iter().map(|item| item.evidence_id.as_str()).collect::<Vec<_>>(), vec!["a", "b"]);
    drop(store);
    let _ = std::fs::remove_file(path);
}

#[test]
fn sqlite_store_rejects_invalid_records() {
    let path = std::env::temp_dir().join(format!("poison-evidence-invalid-{}.sqlite", std::process::id()));
    let _ = std::fs::remove_file(&path);
    let store = SqliteEvidenceStore::open(&path).unwrap();
    let mut invalid = record("bad"); invalid.content_sha256 = "not-a-digest".into();
    assert!(store.upsert(&invalid).is_err());
    drop(store);
    let _ = std::fs::remove_file(path);
}

#[test]
fn index_round_trips_through_sqlite_store() {
    let path = std::env::temp_dir().join(format!("poison-evidence-index-{}.sqlite", std::process::id()));
    let _ = std::fs::remove_file(&path);
    let store = SqliteEvidenceStore::open(&path).unwrap();
    let index = EvidenceIndex::rebuild(vec![record("b"), record("a")]).unwrap();
    index.persist_sqlite(&store).unwrap();
    let restored = EvidenceIndex::load_sqlite(&store).unwrap();
    assert_eq!(restored.ordered().map(|item| item.evidence_id.as_str()).collect::<Vec<_>>(), vec!["a", "b"]);
    drop(store);
    let _ = std::fs::remove_file(path);
}
