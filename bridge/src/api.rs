use axum::{
    extract::{ws::{Message, WebSocket, WebSocketUpgrade}, Path, State},
    http::{header::HeaderMap, StatusCode},
    response::{IntoResponse, Response},
    routing::{get, post},
    Json, Router,
};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use serde::Serialize;
use serde::Deserialize;
use std::sync::Arc;
use uuid::Uuid;

use crate::{auth::{AuthError, OriginAuthenticator}, device::{DeviceError, DeviceRegistry, DeviceTransport}, evidence::{sqlite::SqliteEvidenceStore, IndexedEvidence}, transports::{ble, usb}};

pub const MAX_BODY_BYTES: usize = 1024;

#[derive(Clone)]
pub struct AppState {
    pub auth: OriginAuthenticator,
    pub devices: DeviceRegistry,
    pub evidence: Arc<SqliteEvidenceStore>,
    pub builder: Arc<tokio::sync::Mutex<poison_builder::BuilderStore>>,
}

#[derive(Serialize)]
struct DeviceList { devices: Vec<crate::device::DeviceDescriptor> }

#[derive(Serialize)]
struct SessionResponse { session_id: Uuid, device_id: String }

#[derive(Serialize)]
struct ErrorBody { error: &'static str }

#[derive(Debug, Deserialize)]
struct CreateBuildJob { idempotency_key: String }

#[derive(Debug, Deserialize)]
struct FinalizeBuildJob {
    source_bytes: u64,
    source_digest: String,
    lock_digest: String,
    toolchain_digest: String,
    api_version: u16,
    target: String,
}

#[derive(Debug, Deserialize)]
struct FinishBuildJob {
    output_bytes: u64,
    target: String,
    api_version: u16,
    abi_version: u16,
    entry: String,
    imports: Vec<String>,
    relocations: Vec<u32>,
    capabilities: Vec<String>,
    digest: String,
}

#[derive(Debug, Serialize)]
struct BuildJobResponse {
    id: String,
    state: &'static str,
    source_bytes: u64,
    log_bytes: usize,
    has_artifact: bool,
}

pub fn router(state: AppState) -> Router {
    Router::new()
        .route("/v1/devices", get(list_devices))
        .route("/v1/devices/{id}/sessions", post(open_session))
        .route("/v1/sessions/{id}/stream", get(stream_session))
        .route("/v1/rust/artifacts/validate", post(validate_rust_artifact))
        .route("/v1/rust/build/jobs", post(create_build_job))
        .route("/v1/rust/build/jobs/{id}", get(build_job_status))
        .route("/v1/rust/build/jobs/{id}/finalize", post(finalize_build_job))
        .route("/v1/rust/build/jobs/{id}/start", post(start_build_job))
        .route("/v1/rust/build/jobs/{id}/finish", post(finish_build_job))
        .route("/v1/rust/build/jobs/{id}/cancel", post(cancel_build_job))
        .route("/v1/evidence/index", get(list_evidence).post(upsert_evidence))
        .with_state(state)
        .layer(axum::extract::DefaultBodyLimit::max(MAX_BODY_BYTES))
}

async fn validate_rust_artifact(State(state): State<AppState>, headers: HeaderMap, body: axum::body::Bytes) -> Response {
    if let Err(error) = authorize(&state, &headers) { return auth_error_response(error); }
    match crate::rust_artifact::NativeArtifactManifest::from_json(&body) {
        Ok(manifest) => Json(serde_json::json!({"accepted": true, "target": manifest.target, "apiVersion": manifest.api_version, "abiVersion": manifest.abi_version, "digest": manifest.digest})).into_response(),
        Err(error) => (StatusCode::UNPROCESSABLE_ENTITY, Json(serde_json::json!({"accepted": false, "error": error}))).into_response(),
    }
}

async fn create_build_job(State(state): State<AppState>, headers: HeaderMap, Json(request): Json<CreateBuildJob>) -> Response {
    if let Err(error) = authorize(&state, &headers) { return auth_error_response(error); }
    let mut builder = state.builder.lock().await;
    match builder.create_job(&request.idempotency_key) {
        Ok(id) => (StatusCode::CREATED, Json(serde_json::json!({"id": id}))).into_response(),
        Err(error) => (StatusCode::UNPROCESSABLE_ENTITY, Json(serde_json::json!({"error": error}))).into_response(),
    }
}

async fn finalize_build_job(State(state): State<AppState>, headers: HeaderMap, Path(id): Path<String>, Json(request): Json<FinalizeBuildJob>) -> Response {
    if let Err(error) = authorize(&state, &headers) { return auth_error_response(error); }
    let provenance = poison_builder::Provenance {
        source_digest: request.source_digest,
        lock_digest: request.lock_digest,
        toolchain_digest: request.toolchain_digest,
        api_version: request.api_version,
        target: request.target,
    };
    let mut builder = state.builder.lock().await;
    match builder.finalize_inputs(&id, request.source_bytes, provenance) {
        Ok(()) => StatusCode::NO_CONTENT.into_response(),
        Err(error) => (StatusCode::UNPROCESSABLE_ENTITY, Json(serde_json::json!({"error": error}))).into_response(),
    }
}

async fn start_build_job(State(state): State<AppState>, headers: HeaderMap, Path(id): Path<String>) -> Response {
    if let Err(error) = authorize(&state, &headers) { return auth_error_response(error); }
    let mut builder = state.builder.lock().await;
    match builder.start(&id) {
        Ok(()) => StatusCode::NO_CONTENT.into_response(),
        Err(error) => (StatusCode::UNPROCESSABLE_ENTITY, Json(serde_json::json!({"error": error}))).into_response(),
    }
}

async fn cancel_build_job(State(state): State<AppState>, headers: HeaderMap, Path(id): Path<String>) -> Response {
    if let Err(error) = authorize(&state, &headers) { return auth_error_response(error); }
    let mut builder = state.builder.lock().await;
    match builder.cancel(&id) {
        Ok(()) => StatusCode::NO_CONTENT.into_response(),
        Err(error) => (StatusCode::UNPROCESSABLE_ENTITY, Json(serde_json::json!({"error": error}))).into_response(),
    }
}

async fn finish_build_job(State(state): State<AppState>, headers: HeaderMap, Path(id): Path<String>, Json(request): Json<FinishBuildJob>) -> Response {
    if let Err(error) = authorize(&state, &headers) { return auth_error_response(error); }
    let manifest = poison_builder::NativeArtifactManifest {
        target: request.target,
        api_version: request.api_version,
        abi_version: request.abi_version,
        entry: request.entry,
        imports: request.imports,
        relocations: request.relocations,
        capabilities: request.capabilities,
        digest: request.digest,
    };
    let mut builder = state.builder.lock().await;
    match builder.publish_native_artifact(&id, manifest, request.output_bytes) {
        Ok(()) => StatusCode::NO_CONTENT.into_response(),
        Err(error) => (StatusCode::UNPROCESSABLE_ENTITY, Json(serde_json::json!({"error": error}))).into_response(),
    }
}

async fn build_job_status(State(state): State<AppState>, headers: HeaderMap, Path(id): Path<String>) -> Response {
    if let Err(error) = authorize(&state, &headers) { return auth_error_response(error); }
    let builder = state.builder.lock().await;
    let Some(job) = builder.job(&id) else { return error_response(StatusCode::NOT_FOUND, "build-job-not-found"); };
    Json(BuildJobResponse {
        id: job.id.clone(),
        state: match job.state {
            poison_builder::JobState::Created => "created",
            poison_builder::JobState::InputsFinalized => "inputs-finalized",
            poison_builder::JobState::Running => "running",
            poison_builder::JobState::Succeeded => "succeeded",
            poison_builder::JobState::Failed => "failed",
            poison_builder::JobState::Cancelled => "cancelled",
            poison_builder::JobState::Expired => "expired",
        },
        source_bytes: job.source_bytes,
        log_bytes: job.logs.len(),
        has_artifact: job.native_artifact.is_some(),
    }).into_response()
}

async fn list_evidence(State(state): State<AppState>, headers: HeaderMap) -> Response {
    if let Err(error) = authorize(&state, &headers) { return auth_error_response(error); }
    match state.evidence.load_all() {
        Ok(records) => Json(records).into_response(),
        Err(_) => error_response(StatusCode::INTERNAL_SERVER_ERROR, "evidence-index-unavailable"),
    }
}

async fn upsert_evidence(State(state): State<AppState>, headers: HeaderMap, Json(record): Json<IndexedEvidence>) -> Response {
    if let Err(error) = authorize(&state, &headers) { return auth_error_response(error); }
    match state.evidence.upsert(&record) {
        Ok(()) => (StatusCode::CREATED, Json(record)).into_response(),
        Err(_) => error_response(StatusCode::UNPROCESSABLE_ENTITY, "invalid-evidence-record"),
    }
}

async fn list_devices(State(state): State<AppState>, headers: HeaderMap) -> Response {
    if let Err(error) = authorize(&state, &headers) { return auth_error_response(error); }
    Json(DeviceList { devices: state.devices.devices() }).into_response()
}

async fn open_session(State(state): State<AppState>, Path(id): Path<String>, headers: HeaderMap) -> Response {
    if let Err(error) = authorize(&state, &headers) { return auth_error_response(error); }
    match state.devices.open_session(&id).await {
        Ok(session) => (StatusCode::CREATED, Json(SessionResponse { session_id: session.id, device_id: session.device_id })).into_response(),
        Err(DeviceError::NotFound) => error_response(StatusCode::NOT_FOUND, "device-not-found"),
        Err(DeviceError::Capacity) => error_response(StatusCode::CONFLICT, "session-capacity"),
        Err(DeviceError::Busy) => error_response(StatusCode::CONFLICT, "device-busy"),
        Err(DeviceError::SessionNotFound) => error_response(StatusCode::NOT_FOUND, "session-not-found"),
    }
}

async fn stream_session(ws: WebSocketUpgrade, State(state): State<AppState>, Path(id): Path<Uuid>, headers: HeaderMap) -> Response {
    let origin = headers.get("origin").and_then(|value| value.to_str().ok());
    let protocols = headers.get("sec-websocket-protocol").and_then(|value| value.to_str().ok());
    let protocol = match state.auth.authorize_websocket(origin, protocols) {
        Ok(protocol) => protocol,
        Err(error) => return auth_error_response(error),
    };
    let Some(session) = state.devices.session(id).await else {
        return error_response(StatusCode::NOT_FOUND, "session-not-found");
    };
    ws.protocols([protocol]).on_upgrade(move |socket| stream_socket(socket, session.id, session.device_id, state.devices.clone())).into_response()
}

async fn stream_socket(mut socket: WebSocket, session_id: Uuid, device_id: String, devices: DeviceRegistry) {
    let Some(device) = devices.device(&device_id) else {
        let _ = socket.send(Message::Close(None)).await;
        let _ = devices.close_session(session_id).await;
        return;
    };
    let stream = match device.transport {
        DeviceTransport::Usb => usb::open_rpc_stream(&device.id).await,
        DeviceTransport::Ble => ble::open_rpc_stream(&device.id).await,
    };
    let Ok(stream) = stream else {
        let _ = socket.send(Message::Close(None)).await;
        let _ = devices.close_session(session_id).await;
        return;
    };
    let (mut reader, mut writer) = tokio::io::split(stream);
    let mut buffer = vec![0u8; 1024];
    loop {
        tokio::select! {
            message = socket.recv() => {
                let Some(Ok(message)) = message else { break };
                match message {
                    Message::Binary(bytes) if bytes.len() <= MAX_BODY_BYTES => {
                        if writer.write_all(&bytes).await.is_err() { break; }
                    }
                    Message::Ping(value) => { if socket.send(Message::Pong(value)).await.is_err() { break; } }
                    Message::Close(_) => break,
                    Message::Binary(_) => { let _ = socket.send(Message::Close(None)).await; break; }
                    _ => {}
                }
            }
            result = reader.read(&mut buffer) => {
                match result {
                    Ok(0) | Err(_) => break,
                    Ok(size) => {
                        if socket.send(Message::Binary(buffer[..size].to_vec().into())).await.is_err() { break; }
                    }
                }
            }
        }
    }
    let _ = devices.close_session(session_id).await;
}

fn authorize(state: &AppState, headers: &HeaderMap) -> Result<(), AuthError> {
    let origin = headers.get("origin").and_then(|value| value.to_str().ok());
    let token = headers.get("x-poison-origin-token").and_then(|value| value.to_str().ok());
    state.auth.authorize(origin, token)
}

fn auth_error_response(error: AuthError) -> Response {
    let (status, code) = match error { AuthError::MissingOrigin | AuthError::InvalidOrigin => (StatusCode::FORBIDDEN, "origin-denied"), AuthError::MissingToken | AuthError::InvalidToken => (StatusCode::UNAUTHORIZED, "token-denied"), AuthError::SecureStorage(_) => (StatusCode::INTERNAL_SERVER_ERROR, "secure-storage") };
    error_response(status, code)
}

fn error_response(status: StatusCode, code: &'static str) -> Response { (status, Json(ErrorBody { error: code })).into_response() }

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BindError { NonLoopback }

pub fn validate_bind(address: std::net::SocketAddr) -> Result<(), BindError> {
    if address.ip().is_loopback() { Ok(()) } else { Err(BindError::NonLoopback) }
}
