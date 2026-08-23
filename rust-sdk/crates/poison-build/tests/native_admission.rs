use poison_build::{NativeArtifactManifest, ABI_VERSION, API_VERSION, TARGET};

fn manifest() -> NativeArtifactManifest {
    NativeArtifactManifest { target: TARGET.into(), api_version: API_VERSION, abi_version: ABI_VERSION, entry: "poison_rust_entry".into(), imports: vec!["poison_storage_open".into()], relocations: vec![0, 2, 3], capabilities: vec!["storage.project.read".into()], digest: "a".repeat(64) }
}

#[test]
fn valid_native_manifest_is_admitted() { assert!(manifest().validate(TARGET, API_VERSION, ABI_VERSION).is_ok()); }

#[test]
fn wrong_target_or_host_import_is_rejected() {
    let mut candidate = manifest();
    candidate.target = "thumbv8m-none-eabihf".into();
    assert!(candidate.validate(TARGET, API_VERSION, ABI_VERSION).is_err());
    let mut candidate = manifest();
    candidate.imports = vec!["furi_private_symbol".into()];
    assert!(candidate.validate(TARGET, API_VERSION, ABI_VERSION).is_err());
}

#[test]
fn unsupported_relocation_and_digest_are_rejected() {
    let mut candidate = manifest();
    candidate.relocations = vec![42];
    assert!(candidate.validate(TARGET, API_VERSION, ABI_VERSION).is_err());
    let mut candidate = manifest();
    candidate.digest = "A".repeat(64);
    assert!(candidate.validate(TARGET, API_VERSION, ABI_VERSION).is_err());
}
