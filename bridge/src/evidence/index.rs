use std::collections::BTreeMap;

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct IndexedEvidence {
    pub evidence_id: String,
    pub case_id: String,
    pub content_sha256: String,
    pub content_length: u64,
    pub media_type: String,
    pub source_path: String,
}

#[derive(Debug, Default, Clone)]
pub struct EvidenceIndex { records: BTreeMap<String, IndexedEvidence> }

impl EvidenceIndex {
    pub fn upsert(&mut self, record: IndexedEvidence) -> Result<(), &'static str> {
        if record.evidence_id.is_empty() || !is_digest(&record.content_sha256) || !record.source_path.starts_with('/') { return Err("invalid evidence record"); }
        self.records.insert(record.evidence_id.clone(), record);
        Ok(())
    }
    pub fn remove(&mut self, evidence_id: &str) -> bool { self.records.remove(evidence_id).is_some() }
    pub fn get(&self, evidence_id: &str) -> Option<&IndexedEvidence> { self.records.get(evidence_id) }
    pub fn ordered(&self) -> impl Iterator<Item = &IndexedEvidence> { self.records.values() }
    pub fn rebuild(records: impl IntoIterator<Item = IndexedEvidence>) -> Result<Self, &'static str> { let mut index = Self::default(); for record in records { index.upsert(record)?; } Ok(index) }
    pub fn load_sqlite(store: &crate::evidence::sqlite::SqliteEvidenceStore) -> Result<Self, String> {
        let records = store.load_all()?;
        Self::rebuild(records).map_err(str::to_owned)
    }
    pub fn persist_sqlite(&self, store: &crate::evidence::sqlite::SqliteEvidenceStore) -> Result<(), String> {
        for record in self.ordered() { store.upsert(record)?; }
        Ok(())
    }
}

pub(crate) fn is_digest(value: &str) -> bool { value.len() == 64 && value.bytes().all(|byte| byte.is_ascii_hexdigit() && !byte.is_ascii_uppercase()) }
