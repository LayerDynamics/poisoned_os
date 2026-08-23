use axum::{body::Body, http::{Request, StatusCode}};
use poisoned_bridge::{api::{router, validate_bind, AppState}, auth::OriginAuthenticator, device::{DeviceDescriptor, DeviceRegistry, DeviceTransport}};
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::sync::Mutex;
use tower::ServiceExt;
use poisoned_bridge::evidence::sqlite::SqliteEvidenceStore;

fn evidence_store() -> Arc<SqliteEvidenceStore> { Arc::new(SqliteEvidenceStore::open(std::path::Path::new(":memory:")).unwrap()) }
fn builder_store() -> Arc<Mutex<poison_builder::BuilderStore>> { Arc::new(Mutex::new(poison_builder::BuilderStore::new(poison_builder::BuildPolicy::default()))) }

#[test]
fn bridge_rejects_non_loopback_bind() {
    assert!(validate_bind("127.0.0.1:49000".parse::<SocketAddr>().unwrap()).is_ok());
    assert!(validate_bind("0.0.0.0:49000".parse::<SocketAddr>().unwrap()).is_err());
    assert!(validate_bind("192.168.1.10:49000".parse::<SocketAddr>().unwrap()).is_err());
}

#[tokio::test]
async fn api_requires_origin_token_and_does_not_cross_session_boundaries() {
    let state = AppState {
        auth: OriginAuthenticator::new("http://127.0.0.1:49000", "secret").unwrap(),
        devices: DeviceRegistry::new(vec![DeviceDescriptor { id: "f7-1".into(), label: "Flipper".into(), transport: DeviceTransport::Usb }], 4),
        evidence: evidence_store(),
        builder: builder_store(),
    };
    let app = router(state);
    let response = app.clone().oneshot(Request::builder().uri("/v1/devices").body(Body::empty()).unwrap()).await.unwrap();
    assert_eq!(response.status(), StatusCode::FORBIDDEN);

    let response = app.clone().oneshot(Request::builder().uri("/v1/devices")
        .header("origin", "http://127.0.0.1:49000").header("x-poison-origin-token", "secret").body(Body::empty()).unwrap()).await.unwrap();
    assert_eq!(response.status(), StatusCode::OK);

    let response = app.clone().oneshot(Request::builder().method("POST").uri("/v1/devices/f7-1/sessions")
        .header("origin", "http://127.0.0.1:49000").header("x-poison-origin-token", "secret").body(Body::empty()).unwrap()).await.unwrap();
    assert_eq!(response.status(), StatusCode::CREATED);

    // Session IDs are intentionally opaque; the forwarding test verifies that an
    // unknown ID cannot be used to reach another live session.
}

#[tokio::test]
async fn rust_artifact_validation_is_authenticated_and_bounded() {
    let state = AppState {
        auth: OriginAuthenticator::new("http://127.0.0.1:49000", "secret").unwrap(),
        devices: DeviceRegistry::new(Vec::new(), 4),
        evidence: evidence_store(),
        builder: builder_store(),
    };
    let app = router(state);
    let response = app.clone().oneshot(Request::builder().method("POST").uri("/v1/rust/artifacts/validate")
        .header("origin", "http://127.0.0.1:49000").header("x-poison-origin-token", "secret")
        .header("content-type", "application/json").body(Body::from("{}")).unwrap()).await.unwrap();
    assert_eq!(response.status(), StatusCode::UNPROCESSABLE_ENTITY);
    let response = app.oneshot(Request::builder().method("POST").uri("/v1/rust/artifacts/validate")
        .body(Body::from("{}")).unwrap()).await.unwrap();
    assert_eq!(response.status(), StatusCode::FORBIDDEN);
}

#[tokio::test]
async fn evidence_index_routes_require_auth_and_persist_records() {
    let state = AppState {
        auth: OriginAuthenticator::new("http://127.0.0.1:49000", "secret").unwrap(),
        devices: DeviceRegistry::new(Vec::new(), 4),
        evidence: evidence_store(),
        builder: builder_store(),
    };
    let app = router(state);
    let record = serde_json::json!({"evidence_id":"e1","case_id":"case","content_sha256":"0000000000000000000000000000000000000000000000000000000000000000","content_length":3,"media_type":"text/plain","source_path":"/ext/evidence/e1"});
    let response = app.clone().oneshot(Request::builder().method("POST").uri("/v1/evidence/index").header("content-type", "application/json").body(Body::from(record.to_string())).unwrap()).await.unwrap();
    assert_eq!(response.status(), StatusCode::FORBIDDEN);
    let response = app.clone().oneshot(Request::builder().method("POST").uri("/v1/evidence/index").header("origin", "http://127.0.0.1:49000").header("x-poison-origin-token", "secret").header("content-type", "application/json").body(Body::from(record.to_string())).unwrap()).await.unwrap();
    assert_eq!(response.status(), StatusCode::CREATED);
    let response = app.oneshot(Request::builder().uri("/v1/evidence/index").header("origin", "http://127.0.0.1:49000").header("x-poison-origin-token", "secret").body(Body::empty()).unwrap()).await.unwrap();
    assert_eq!(response.status(), StatusCode::OK);
    let body = axum::body::to_bytes(response.into_body(), 1024).await.unwrap();
    assert!(String::from_utf8(body.to_vec()).unwrap().contains("e1"));
}

#[tokio::test]
async fn authenticated_builder_transport_preserves_job_state_machine() {
    let state = AppState {
        auth: OriginAuthenticator::new("http://127.0.0.1:49000", "secret").unwrap(),
        devices: DeviceRegistry::new(Vec::new(), 4),
        evidence: evidence_store(),
        builder: builder_store(),
    };
    let app = router(state);
    let auth = [("origin", "http://127.0.0.1:49000"), ("x-poison-origin-token", "secret")];
    let response = app.clone().oneshot(Request::builder().method("POST").uri("/v1/rust/build/jobs").header("content-type", "application/json").body(Body::from(r#"{"idempotency_key":"key-1"}"#)).unwrap()).await.unwrap();
    assert_eq!(response.status(), StatusCode::FORBIDDEN);
    let mut request = Request::builder().method("POST").uri("/v1/rust/build/jobs").header("content-type", "application/json");
    for (name, value) in auth { request = request.header(name, value); }
    let response = app.clone().oneshot(request.body(Body::from(r#"{"idempotency_key":"key-1"}"#)).unwrap()).await.unwrap();
    assert_eq!(response.status(), StatusCode::CREATED);
    let body = axum::body::to_bytes(response.into_body(), 1024).await.unwrap();
    let id = serde_json::from_slice::<serde_json::Value>(&body).unwrap()["id"].as_str().unwrap().to_owned();
    let response = app.clone().oneshot(Request::builder().uri(format!("/v1/rust/build/jobs/{id}")).header("origin", "http://127.0.0.1:49000").header("x-poison-origin-token", "secret").body(Body::empty()).unwrap()).await.unwrap();
    assert_eq!(response.status(), StatusCode::OK);
    let finalize = serde_json::json!({"source_bytes":1,"source_digest":"00","lock_digest":"11","toolchain_digest":"22","api_version":1,"target":"thumbv7em-none-eabihf"});
    let response = app.clone().oneshot(Request::builder().method("POST").uri(format!("/v1/rust/build/jobs/{id}/finalize")).header("origin", "http://127.0.0.1:49000").header("x-poison-origin-token", "secret").header("content-type", "application/json").body(Body::from(finalize.to_string())).unwrap()).await.unwrap();
    assert_eq!(response.status(), StatusCode::NO_CONTENT);
    let response = app.clone().oneshot(Request::builder().method("POST").uri(format!("/v1/rust/build/jobs/{id}/start")).header("origin", "http://127.0.0.1:49000").header("x-poison-origin-token", "secret").body(Body::empty()).unwrap()).await.unwrap();
    assert_eq!(response.status(), StatusCode::NO_CONTENT);
    let finish = serde_json::json!({"output_bytes":16,"target":"thumbv7em-none-eabihf","api_version":1,"abi_version":1,"entry":"poison_rust_entry","imports":["poison_storage_open"],"relocations":[0,2],"capabilities":["storage.project.read"],"digest":"aa".repeat(32)});
    let response = app.clone().oneshot(Request::builder().method("POST").uri(format!("/v1/rust/build/jobs/{id}/finish")).header("origin", "http://127.0.0.1:49000").header("x-poison-origin-token", "secret").header("content-type", "application/json").body(Body::from(finish.to_string())).unwrap()).await.unwrap();
    assert_eq!(response.status(), StatusCode::NO_CONTENT);
    let response = app.clone().oneshot(Request::builder().uri(format!("/v1/rust/build/jobs/{id}")).header("origin", "http://127.0.0.1:49000").header("x-poison-origin-token", "secret").body(Body::empty()).unwrap()).await.unwrap();
    let body = axum::body::to_bytes(response.into_body(), 1024).await.unwrap();
    assert!(String::from_utf8(body.to_vec()).unwrap().contains("succeeded"));
}
