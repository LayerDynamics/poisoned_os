use poisoned_bridge::rust_artifact::{NativeArtifactManifest, API_VERSION, ABI_VERSION, TARGET};

#[test]
fn bridge_admission_matches_native_contract() {
    let manifest = NativeArtifactManifest {
        target: TARGET.into(), api_version: API_VERSION, abi_version: ABI_VERSION,
        entry: "poison_rust_entry".into(), imports: vec!["poison_storage_open".into()],
        relocations: vec![0, 2, 3], capabilities: vec!["storage.project.read".into()], digest: "b".repeat(64),
    };
    assert!(manifest.validate().is_ok());
}
