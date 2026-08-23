use poisoned_bridge::diagnostics::*;

fn counters() -> DiagnosticCounters { DiagnosticCounters { session_established: 1, transport_errors: 0, dropped_frames: 0, retried_frames: 0, command_failures: 0, app_crashes: 0, policy_denials: 0, package_verifications: 0, package_revocations: 0, update_stages: 0, update_health: 0, update_rollbacks: 0, recoveries: 0, javascript_starts: 0, javascript_terminals: 0, javascript_crashes: 0, javascript_limits: 0, javascript_recoveries: 0 } }

#[test]
fn support_bundle_requires_preview_and_digest_only_file_references() {
    let event = DiagnosticEvent { event_id: 1, category: "transportError".into(), summary: "timeout".into(), timestamp_ms: 10, correlation_digest: "00".repeat(32) };
    let file = BundleFile { path: "/ext/log.txt".into(), sha256: "ab".repeat(32), size: 12 };
    assert_eq!(SupportBundle::new(Consent { previewed: false, accepted_at_ms: 1 }, vec![], counters(), vec![event.clone()], vec![file.clone()]), Err(BundleError::ConsentRequired));
    assert!(SupportBundle::new(Consent { previewed: true, accepted_at_ms: 1 }, vec![], counters(), vec![event], vec![file]).is_ok());
}

#[test]
fn support_bundle_rejects_private_paths_and_uppercase_digests() {
    let file = BundleFile { path: "/int/private.key".into(), sha256: "AA".repeat(32), size: 1 };
    assert_eq!(SupportBundle::new(Consent { previewed: true, accepted_at_ms: 1 }, vec![], counters(), vec![], vec![file]), Err(BundleError::InvalidField));
}
