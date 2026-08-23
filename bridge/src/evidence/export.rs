use super::index::IndexedEvidence;

pub fn deterministic_manifest(records: impl IntoIterator<Item = IndexedEvidence>) -> Result<Vec<u8>, serde_json::Error> {
    let mut records: Vec<_> = records.into_iter().collect();
    records.sort_by(|left, right| left.evidence_id.cmp(&right.evidence_id));
    serde_json::to_vec(&records)
}
