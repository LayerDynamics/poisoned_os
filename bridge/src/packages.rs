use std::collections::BTreeMap;

use serde::{Deserialize, Serialize};

use crate::generated_packages::{
    PackageCatalogFreshness as WireFreshness, PackageCatalogRecord as WireRecord,
    PackageCatalogSource as WireSource, PackageCatalogState as WireState,
};

const MAX_RECORDS: usize = 512;
const MAX_TEXT: usize = 256;

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq, PartialOrd, Ord)]
pub enum PackageSource {
    DeviceStorage,
    BundledRelease,
    ImportedFile,
    LocalRepository,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
pub enum CatalogFreshness {
    Unknown,
    Fresh,
    Stale,
    Missing,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
pub enum CatalogState {
    Installed,
    Staged,
    Available,
    Incompatible,
    Disabled,
    Quarantined,
    Revoked,
    RollbackCandidate,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct CatalogRecord {
    pub id: String,
    pub version: String,
    pub signer: String,
    pub digest: String,
    pub source: PackageSource,
    pub source_path: String,
    pub freshness: CatalogFreshness,
    pub state: CatalogState,
    pub verified: bool,
    pub signer_revoked: bool,
    pub conflicted: bool,
    #[serde(default = "default_compatible")]
    pub compatible: bool,
    #[serde(default)]
    pub capability_mask: u64,
}

const fn default_compatible() -> bool {
    true
}

#[derive(Debug, Default, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct PackageCatalog {
    records: BTreeMap<CatalogKey, CatalogRecord>,
}

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
struct CatalogKey {
    id: String,
    version: String,
    source: PackageSource,
    source_path: String,
}

impl PackageCatalog {
    pub fn add(&mut self, record: CatalogRecord) -> Result<(), &'static str> {
        validate(&record)?;
        let key = CatalogKey {
            id: record.id.clone(),
            version: record.version.clone(),
            source: record.source,
            source_path: record.source_path.clone(),
        };
        if self.records.contains_key(&key) {
            return Err("duplicate catalog source");
        }
        if self.records.len() >= MAX_RECORDS {
            return Err("catalog capacity exceeded");
        }
        let conflicting_keys: Vec<CatalogKey> = self
            .records
            .iter()
            .filter(|(_, current)| {
                current.id == record.id
                    && current.version == record.version
                    && current.digest != record.digest
            })
            .map(|(existing_key, _)| existing_key.clone())
            .collect();
        for conflicting_key in conflicting_keys {
            if let Some(current) = self.records.get_mut(&conflicting_key) {
                current.conflicted = true;
            }
        }
        let mut record = record;
        record.conflicted = !self
            .records
            .values()
            .filter(|current| current.id == record.id && current.version == record.version)
            .all(|current| current.digest == record.digest);
        self.records.insert(key, record);
        Ok(())
    }

    pub fn find(&self, id: &str, version: &str, source: PackageSource) -> Option<&CatalogRecord> {
        self.records
            .values()
            .find(|record| record.id == id && record.version == version && record.source == source)
    }

    pub fn ordered(&self) -> impl Iterator<Item = &CatalogRecord> {
        self.records.values()
    }

    pub fn installable(&self) -> impl Iterator<Item = &CatalogRecord> {
        self.records.values().filter(|record| {
            record.verified
                && record.compatible
                && !record.signer_revoked
                && !record.conflicted
                && record.freshness == CatalogFreshness::Fresh
                && record.state == CatalogState::Available
        })
    }

    pub fn mark_source_missing(&mut self, source: PackageSource, source_path: &str) -> bool {
        let mut changed = false;
        for record in self.records.values_mut() {
            if record.source == source && record.source_path == source_path {
                record.freshness = CatalogFreshness::Missing;
                if record.state == CatalogState::Available {
                    record.state = CatalogState::Quarantined;
                }
                changed = true;
            }
        }
        changed
    }

    pub fn reconcile_source_paths(
        &mut self,
        source: PackageSource,
        available_paths: &[String],
    ) -> usize {
        let mut changed = 0usize;
        for record in self
            .records
            .values_mut()
            .filter(|record| record.source == source)
        {
            if !available_paths
                .iter()
                .any(|path| path == &record.source_path)
            {
                record.freshness = CatalogFreshness::Missing;
                if record.state == CatalogState::Available {
                    record.state = CatalogState::Quarantined;
                }
                changed += 1;
            }
        }
        changed
    }

    pub fn cached_json(&self) -> Result<Vec<u8>, serde_json::Error> {
        serde_json::to_vec(&self.records.values().collect::<Vec<_>>())
    }

    pub fn from_cached_json(bytes: &[u8]) -> Result<Self, serde_json::Error> {
        let records: Vec<CatalogRecord> = serde_json::from_slice(bytes)?;
        let mut catalog = Self::default();
        for record in records {
            catalog.add(record).map_err(|message| {
                serde_json::Error::io(std::io::Error::new(
                    std::io::ErrorKind::InvalidData,
                    message,
                ))
            })?;
        }
        Ok(catalog)
    }
}

fn validate(record: &CatalogRecord) -> Result<(), &'static str> {
    if record.id.is_empty()
        || record.id.len() > MAX_TEXT
        || record.version.is_empty()
        || record.version.len() > MAX_TEXT
        || record.signer.is_empty()
        || record.signer.len() > MAX_TEXT
        || record.source_path.is_empty()
        || record.source_path.len() > MAX_TEXT
    {
        return Err("invalid catalog text");
    }
    if record.digest.len() != 64
        || !record
            .digest
            .bytes()
            .all(|byte| byte.is_ascii_hexdigit() && !byte.is_ascii_uppercase())
    {
        return Err("invalid package digest");
    }
    if record.signer_revoked != (record.state == CatalogState::Revoked)
        || (record.signer_revoked && record.verified)
    {
        return Err("inconsistent revoked signer state");
    }
    Ok(())
}

impl TryFrom<WireRecord> for CatalogRecord {
    type Error = &'static str;

    fn try_from(record: WireRecord) -> Result<Self, Self::Error> {
        let source =
            match WireSource::try_from(record.source).map_err(|_| "invalid catalog source")? {
                WireSource::DeviceStorage => PackageSource::DeviceStorage,
                WireSource::BundledRelease => PackageSource::BundledRelease,
                WireSource::ImportedFile => PackageSource::ImportedFile,
                WireSource::LocalRepository => PackageSource::LocalRepository,
            };
        let freshness = match WireFreshness::try_from(record.freshness)
            .map_err(|_| "invalid catalog freshness")?
        {
            WireFreshness::Unknown => CatalogFreshness::Unknown,
            WireFreshness::Fresh => CatalogFreshness::Fresh,
            WireFreshness::Stale => CatalogFreshness::Stale,
            WireFreshness::Missing => CatalogFreshness::Missing,
        };
        let state = match WireState::try_from(record.state).map_err(|_| "invalid catalog state")? {
            WireState::Installed => CatalogState::Installed,
            WireState::Staged => CatalogState::Staged,
            WireState::Available => CatalogState::Available,
            WireState::Incompatible => CatalogState::Incompatible,
            WireState::Disabled => CatalogState::Disabled,
            WireState::Quarantined => CatalogState::Quarantined,
            WireState::Revoked => CatalogState::Revoked,
            WireState::RollbackCandidate => CatalogState::RollbackCandidate,
        };
        let converted = Self {
            id: record.id,
            version: record.version,
            signer: record.signer,
            digest: record.digest,
            source,
            source_path: record.source_path,
            freshness,
            state,
            verified: record.verified,
            signer_revoked: record.signer_revoked,
            conflicted: record.conflicted,
            compatible: record.compatible,
            capability_mask: record.capability_mask,
        };
        validate(&converted)?;
        Ok(converted)
    }
}
