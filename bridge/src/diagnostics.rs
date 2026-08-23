use serde::Serialize;

pub const MAX_COMPONENTS: usize = 32;
pub const MAX_EVENTS: usize = 64;
pub const MAX_FILES: usize = 32;

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct DiagnosticCounters {
    pub session_established: u32,
    pub transport_errors: u32,
    pub dropped_frames: u32,
    pub retried_frames: u32,
    pub command_failures: u32,
    pub app_crashes: u32,
    pub policy_denials: u32,
    pub package_verifications: u32,
    pub package_revocations: u32,
    pub update_stages: u32,
    pub update_health: u32,
    pub update_rollbacks: u32,
    pub recoveries: u32,
    pub javascript_starts: u32,
    pub javascript_terminals: u32,
    pub javascript_crashes: u32,
    pub javascript_limits: u32,
    pub javascript_recoveries: u32,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct DiagnosticEvent {
    pub event_id: u64,
    pub category: String,
    pub summary: String,
    pub timestamp_ms: u64,
    pub correlation_digest: String,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
pub struct Component { pub name: String, pub version: String }

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
pub struct BundleFile { pub path: String, pub sha256: String, pub size: u64 }

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
pub struct Consent { pub previewed: bool, #[serde(rename = "acceptedAtMs")] pub accepted_at_ms: u64 }

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
pub struct SupportBundle {
    pub schema: &'static str,
    pub consent: Consent,
    pub components: Vec<Component>,
    pub counters: DiagnosticCounters,
    pub events: Vec<DiagnosticEvent>,
    pub files: Vec<BundleFile>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BundleError { ConsentRequired, TooManyItems, InvalidField }

impl SupportBundle {
    pub fn new(consent: Consent, components: Vec<Component>, counters: DiagnosticCounters, events: Vec<DiagnosticEvent>, files: Vec<BundleFile>) -> Result<Self, BundleError> {
        if !consent.previewed || consent.accepted_at_ms == 0 { return Err(BundleError::ConsentRequired); }
        if components.len() > MAX_COMPONENTS || events.len() > MAX_EVENTS || files.len() > MAX_FILES { return Err(BundleError::TooManyItems); }
        if components.iter().any(|component| component.name.len() > 64 || component.version.len() > 64)
            || events.iter().any(|event| event.category.len() > 24 || event.summary.is_empty() || event.summary.len() > 96 || !is_hex(&event.correlation_digest, 64))
            || files.iter().any(|file| !file.path.starts_with("/ext/") || !is_hex(&file.sha256, 64)) { return Err(BundleError::InvalidField); }
        Ok(Self { schema: "poison.support-bundle/v1", consent, components, counters, events, files })
    }
}

fn is_hex(value: &str, length: usize) -> bool { value.len() == length && value.bytes().all(|byte| byte.is_ascii_hexdigit() && !byte.is_ascii_uppercase()) }
