use std::collections::HashMap;

pub use poison_build::NativeArtifactManifest;

pub const MAX_JOB_ID: usize = 64;
pub const MAX_LOG_BYTES: usize = 16 * 1024;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum JobState {
    Created,
    InputsFinalized,
    Running,
    Succeeded,
    Failed,
    Cancelled,
    Expired,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BuildPolicy {
    pub max_source_bytes: u64,
    pub max_output_bytes: u64,
    pub max_seconds: u32,
    pub network_enabled: bool,
}

impl Default for BuildPolicy {
    fn default() -> Self {
        Self { max_source_bytes: 2 * 1024 * 1024, max_output_bytes: 4 * 1024 * 1024, max_seconds: 300, network_enabled: false }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Provenance {
    pub source_digest: String,
    pub lock_digest: String,
    pub toolchain_digest: String,
    pub api_version: u16,
    pub target: String,
}

impl Provenance {
    pub fn canonical_bytes(&self) -> Vec<u8> {
        format!("api={}\nlock={}\nsource={}\ntarget={}\ntoolchain={}\n", self.api_version, self.lock_digest, self.source_digest, self.target, self.toolchain_digest).into_bytes()
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Job {
    pub id: String,
    pub idempotency_key: String,
    pub state: JobState,
    pub source_bytes: u64,
    pub logs: Vec<u8>,
    pub provenance: Option<Provenance>,
    pub native_artifact: Option<NativeArtifactManifest>,
}

impl Job {
    fn transition(&mut self, next: JobState) -> Result<(), &'static str> {
        let allowed = matches!((self.state, next),
            (JobState::Created, JobState::InputsFinalized) |
            (JobState::InputsFinalized, JobState::Running) |
            (JobState::Running, JobState::Succeeded) |
            (JobState::Running, JobState::Failed) |
            (JobState::Running, JobState::Cancelled) |
            (JobState::Created, JobState::Expired) |
            (JobState::InputsFinalized, JobState::Expired));
        if !allowed { return Err("invalid job transition"); }
        self.state = next;
        Ok(())
    }

    pub fn append_log(&mut self, bytes: &[u8]) -> Result<(), &'static str> {
        if self.logs.len() + bytes.len() > MAX_LOG_BYTES { return Err("log limit exceeded"); }
        self.logs.extend_from_slice(bytes);
        Ok(())
    }
}

pub struct BuilderStore {
    jobs: HashMap<String, Job>,
    idempotency: HashMap<String, String>,
    next_id: u64,
    policy: BuildPolicy,
}

impl BuilderStore {
    pub fn new(policy: BuildPolicy) -> Self { Self { jobs: HashMap::new(), idempotency: HashMap::new(), next_id: 1, policy } }

    pub fn create_job(&mut self, idempotency_key: &str) -> Result<String, &'static str> {
        if idempotency_key.is_empty() || idempotency_key.len() > MAX_JOB_ID { return Err("invalid idempotency key"); }
        if let Some(id) = self.idempotency.get(idempotency_key) { return Ok(id.clone()); }
        let id = format!("job-{}", self.next_id);
        self.next_id += 1;
        self.idempotency.insert(idempotency_key.to_string(), id.clone());
        self.jobs.insert(id.clone(), Job { id: id.clone(), idempotency_key: idempotency_key.to_string(), state: JobState::Created, source_bytes: 0, logs: Vec::new(), provenance: None, native_artifact: None });
        Ok(id)
    }

    pub fn finalize_inputs(&mut self, id: &str, source_bytes: u64, provenance: Provenance) -> Result<(), &'static str> {
        if source_bytes > self.policy.max_source_bytes { return Err("source limit exceeded"); }
        let job = self.jobs.get_mut(id).ok_or("unknown job")?;
        job.source_bytes = source_bytes;
        job.provenance = Some(provenance);
        job.transition(JobState::InputsFinalized)
    }

    pub fn start(&mut self, id: &str) -> Result<(), &'static str> { self.jobs.get_mut(id).ok_or("unknown job")?.transition(JobState::Running) }
    pub fn finish(&mut self, id: &str, output_bytes: u64) -> Result<(), &'static str> {
        if output_bytes > self.policy.max_output_bytes { return Err("output limit exceeded"); }
        self.jobs.get_mut(id).ok_or("unknown job")?.transition(JobState::Succeeded)
    }
    pub fn publish_native_artifact(&mut self, id: &str, manifest: NativeArtifactManifest, output_bytes: u64) -> Result<(), &'static str> {
        if output_bytes > self.policy.max_output_bytes { return Err("output limit exceeded"); }
        manifest.validate("thumbv7em-none-eabihf", 1, 1).map_err(|_| "native artifact admission failed")?;
        let job = self.jobs.get_mut(id).ok_or("unknown job")?;
        if job.state != JobState::Running { return Err("native artifact requires running job"); }
        job.native_artifact = Some(manifest);
        job.transition(JobState::Succeeded)
    }
    pub fn cancel(&mut self, id: &str) -> Result<(), &'static str> { self.jobs.get_mut(id).ok_or("unknown job")?.transition(JobState::Cancelled) }
    pub fn fail(&mut self, id: &str) -> Result<(), &'static str> { self.jobs.get_mut(id).ok_or("unknown job")?.transition(JobState::Failed) }
    pub fn job(&self, id: &str) -> Option<&Job> { self.jobs.get(id) }
    pub fn native_artifact(&self, id: &str) -> Option<&NativeArtifactManifest> { self.jobs.get(id)?.native_artifact.as_ref() }
}

pub fn validate_policy(policy: BuildPolicy) -> Result<(), &'static str> {
    if policy.network_enabled { return Err("builder network must be disabled"); }
    if policy.max_source_bytes == 0 || policy.max_output_bytes == 0 || policy.max_seconds == 0 { return Err("builder limits must be positive"); }
    Ok(())
}
