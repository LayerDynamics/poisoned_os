use std::{collections::HashMap, sync::Arc};

use tokio::sync::Mutex;
use uuid::Uuid;

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize)]
#[serde(rename_all = "lowercase")]
pub enum DeviceTransport { Usb, Ble }

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct DeviceDescriptor {
    pub id: String,
    pub label: String,
    pub transport: DeviceTransport,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum DeviceError { NotFound, SessionNotFound, Capacity, Busy }

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Session {
    pub id: Uuid,
    pub device_id: String,
}

#[derive(Clone)]
pub struct DeviceRegistry {
    devices: Arc<Vec<DeviceDescriptor>>,
    sessions: Arc<Mutex<HashMap<Uuid, Session>>>,
    max_sessions: usize,
}

impl DeviceRegistry {
    pub fn new(devices: Vec<DeviceDescriptor>, max_sessions: usize) -> Self {
        Self { devices: Arc::new(devices), sessions: Arc::new(Mutex::new(HashMap::new())), max_sessions }
    }

    pub fn devices(&self) -> Vec<DeviceDescriptor> { self.devices.as_ref().clone() }

    pub fn device(&self, device_id: &str) -> Option<DeviceDescriptor> {
        self.devices.iter().find(|device| device.id == device_id).cloned()
    }

    pub async fn open_session(&self, device_id: &str) -> Result<Session, DeviceError> {
        if !self.devices.iter().any(|device| device.id == device_id) { return Err(DeviceError::NotFound); }
        let mut sessions = self.sessions.lock().await;
        if sessions.len() >= self.max_sessions { return Err(DeviceError::Capacity); }
        if sessions.values().any(|session| session.device_id == device_id) {
            return Err(DeviceError::Busy);
        }
        let session = Session { id: Uuid::new_v4(), device_id: device_id.to_owned() };
        sessions.insert(session.id, session.clone());
        Ok(session)
    }

    pub async fn session(&self, id: Uuid) -> Option<Session> { self.sessions.lock().await.get(&id).cloned() }

    pub async fn close_session(&self, id: Uuid) -> Result<(), DeviceError> {
        self.sessions.lock().await.remove(&id).map(|_| ()).ok_or(DeviceError::SessionNotFound)
    }
}
