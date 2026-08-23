use poison_builder::{validate_policy, BuildPolicy, BuilderStore, JobState, NativeArtifactManifest, Provenance};

fn provenance() -> Provenance {
    Provenance { source_digest: "a".repeat(64), lock_digest: "b".repeat(64), toolchain_digest: "c".repeat(64), api_version: 1, target: "thumbv7em-none-eabihf".into() }
}

#[test]
fn idempotent_jobs_do_not_duplicate_builds() {
    let mut store = BuilderStore::new(BuildPolicy::default());
    let first = store.create_job("request-1").unwrap();
    let second = store.create_job("request-1").unwrap();
    assert_eq!(first, second);
    store.finalize_inputs(&first, 1024, provenance()).unwrap();
    store.start(&first).unwrap();
    store.finish(&first, 2048).unwrap();
    assert_eq!(store.job(&first).unwrap().state, JobState::Succeeded);
}

#[test]
fn policy_and_limits_fail_closed() {
    assert!(validate_policy(BuildPolicy { network_enabled: true, ..BuildPolicy::default() }).is_err());
    let mut store = BuilderStore::new(BuildPolicy::default());
    let id = store.create_job("request-2").unwrap();
    assert!(store.finalize_inputs(&id, BuildPolicy::default().max_source_bytes + 1, provenance()).is_err());
    assert!(store.start(&id).is_err());
}

#[test]
fn lifecycle_rejects_terminal_reuse_and_bounds_logs() {
    let mut store = BuilderStore::new(BuildPolicy::default());
    let id = store.create_job("request-3").unwrap();
    store.finalize_inputs(&id, 10, provenance()).unwrap();
    store.start(&id).unwrap();
    store.cancel(&id).unwrap();
    assert!(store.finish(&id, 1).is_err());
    assert!(store.job(&id).unwrap().logs.len() == 0);
}

fn native_manifest() -> NativeArtifactManifest {
    NativeArtifactManifest { target: "thumbv7em-none-eabihf".into(), api_version: 1, abi_version: 1, entry: "poison_rust_entry".into(), imports: vec!["poison_storage_open".into()], relocations: vec![0, 2, 3], capabilities: vec!["storage.project.read".into()], digest: "d".repeat(64) }
}

#[test]
fn native_artifact_is_admitted_before_job_success() {
    let mut store = BuilderStore::new(BuildPolicy::default());
    let id = store.create_job("request-native").unwrap();
    store.finalize_inputs(&id, 10, provenance()).unwrap();
    store.start(&id).unwrap();
    store.publish_native_artifact(&id, native_manifest(), 1024).unwrap();
    assert_eq!(store.job(&id).unwrap().state, JobState::Succeeded);
    assert!(store.native_artifact(&id).is_some());
}

#[test]
fn native_artifact_rejects_unsupported_imports() {
    let mut store = BuilderStore::new(BuildPolicy::default());
    let id = store.create_job("request-invalid-native").unwrap();
    store.finalize_inputs(&id, 10, provenance()).unwrap();
    store.start(&id).unwrap();
    let mut manifest = native_manifest();
    manifest.imports.push("host_exec".into());
    assert!(store.publish_native_artifact(&id, manifest, 1024).is_err());
    assert_eq!(store.job(&id).unwrap().state, JobState::Running);
}
