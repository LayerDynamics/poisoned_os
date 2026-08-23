use poisoned_bridge::generated_packages::{
    PackageCatalogFreshness as WireFreshness, PackageCatalogRecord as WireRecord,
    PackageCatalogSource as WireSource, PackageCatalogState as WireState,
};
use poisoned_bridge::packages::{
    CatalogFreshness, CatalogRecord, CatalogState, PackageCatalog, PackageSource,
};

fn record(id: &str, path: &str, digest: &str) -> CatalogRecord {
    CatalogRecord {
        id: id.into(),
        version: "1.0.0".into(),
        signer: "release".into(),
        digest: digest.into(),
        source: PackageSource::BundledRelease,
        source_path: path.into(),
        freshness: CatalogFreshness::Fresh,
        state: CatalogState::Available,
        verified: true,
        signer_revoked: false,
        conflicted: false,
        compatible: true,
        capability_mask: 3,
    }
}

#[test]
fn catalog_is_deterministic_and_rejects_duplicate_sources() {
    let digest = "00".repeat(32);
    let mut catalog = PackageCatalog::default();
    catalog.add(record("org.b", "/catalog/b", &digest)).unwrap();
    catalog.add(record("org.a", "/catalog/a", &digest)).unwrap();
    assert_eq!(
        catalog
            .ordered()
            .map(|item| item.id.as_str())
            .collect::<Vec<_>>(),
        vec!["org.a", "org.b"]
    );
    assert!(catalog.add(record("org.a", "/catalog/a", &digest)).is_err());
}

#[test]
fn stale_or_missing_metadata_is_not_installable_and_cache_survives_restart() {
    let digest = "11".repeat(32);
    let mut catalog = PackageCatalog::default();
    catalog.add(record("org.a", "/catalog/a", &digest)).unwrap();
    assert_eq!(catalog.installable().count(), 1);
    assert!(catalog.mark_source_missing(PackageSource::BundledRelease, "/catalog/a"));
    assert_eq!(catalog.installable().count(), 0);
    let restored = PackageCatalog::from_cached_json(&catalog.cached_json().unwrap()).unwrap();
    assert_eq!(restored, catalog);
}

#[test]
fn conflicting_digests_are_quarantined_from_installation() {
    let mut catalog = PackageCatalog::default();
    let mut other = record("org.a", "/import/a", &"22".repeat(32));
    catalog
        .add(record("org.a", "/catalog/a", &"11".repeat(32)))
        .unwrap();
    other.source = PackageSource::ImportedFile;
    catalog.add(other).unwrap();
    assert_eq!(catalog.installable().count(), 0);
}

#[test]
fn device_inventory_reconciliation_quarantines_disappeared_sources() {
    let digest = "33".repeat(32);
    let mut catalog = PackageCatalog::default();
    catalog
        .add(record("org.present", "/device/present", &digest))
        .unwrap();
    catalog
        .add(record("org.missing", "/device/missing", &digest))
        .unwrap();
    let paths = vec![String::from("/device/present")];
    assert_eq!(
        catalog.reconcile_source_paths(PackageSource::BundledRelease, &paths),
        1
    );
    assert_eq!(
        catalog.reconcile_source_paths(PackageSource::DeviceStorage, &paths),
        0
    );
    let mut device_record = record("org.device", "/device/old", &digest);
    device_record.source = PackageSource::DeviceStorage;
    catalog.add(device_record).unwrap();
    assert_eq!(
        catalog.reconcile_source_paths(PackageSource::DeviceStorage, &paths),
        1
    );
    assert_eq!(catalog.installable().count(), 1);
}

#[test]
fn revoked_metadata_remains_visible_but_never_installable() {
    let mut revoked = record("org.revoked", "/catalog/revoked", &"44".repeat(32));
    revoked.state = CatalogState::Revoked;
    revoked.verified = false;
    revoked.signer_revoked = true;
    let mut catalog = PackageCatalog::default();
    catalog.add(revoked).unwrap();
    assert_eq!(catalog.ordered().count(), 1);
    assert_eq!(catalog.installable().count(), 0);
}

#[test]
fn generated_device_record_converts_without_losing_security_state() {
    let converted = CatalogRecord::try_from(WireRecord {
        id: "org.device".into(),
        version: "2.0.0".into(),
        signer: "release".into(),
        digest: "55".repeat(32),
        source: WireSource::DeviceStorage as i32,
        source_path: "/ext/apps/PoisonedOS/org.device".into(),
        freshness: WireFreshness::Fresh as i32,
        state: WireState::Installed as i32,
        verified: true,
        signer_revoked: false,
        conflicted: false,
        ordinal: 0,
        generation: 7,
        compatible: true,
        capability_mask: 0x55,
    })
    .unwrap();
    assert_eq!(converted.source, PackageSource::DeviceStorage);
    assert_eq!(converted.state, CatalogState::Installed);
    assert_eq!(converted.capability_mask, 0x55);
}
