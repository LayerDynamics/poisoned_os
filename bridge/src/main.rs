use std::net::SocketAddr;
use std::path::PathBuf;
use std::sync::Arc;

use poisoned_bridge::{
    api::{router, validate_bind, AppState},
    auth::{KeyringTokenStore, TokenStore, OriginAuthenticator},
    device::DeviceRegistry,
    evidence::sqlite::SqliteEvidenceStore,
    transports::{usb::{usb_serial_inventory, SystemUsbAdapter, UsbAdapter}, ble::SystemBleAdapter},
};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut arguments = std::env::args().skip(1);
    if let Some(command) = arguments.next() {
        if command != "list-usb-serial-json" || arguments.next().is_some() {
            return Err(format!("unknown poisoned-bridge command: {command}").into());
        }
        println!("{}", serde_json::to_string(&usb_serial_inventory()?)?);
        return Ok(());
    }

    let address: SocketAddr = std::env::var("POISON_BRIDGE_ADDR").unwrap_or_else(|_| "127.0.0.1:49000".to_owned()).parse()?;
    validate_bind(address).map_err(|_| "bridge must bind to loopback")?;
    let origin = std::env::var("POISON_BRIDGE_ORIGIN").unwrap_or_else(|_| "http://127.0.0.1:49000".to_owned());
    let store = KeyringTokenStore::new("poisonedos", "bridge-origin-token");
    let token = store.load_or_create()?;
    eprintln!("POISON_BRIDGE_TOKEN={token}");
    let usb = SystemUsbAdapter.discover();
    let ble = SystemBleAdapter.discover().await;
    let mut devices = usb;
    devices.extend(ble);
    let evidence_path = std::env::var_os("POISON_BRIDGE_EVIDENCE_DB").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("poisonedos-evidence.sqlite"));
    let evidence = Arc::new(SqliteEvidenceStore::open(&evidence_path)?);
    let builder = Arc::new(tokio::sync::Mutex::new(poison_builder::BuilderStore::new(poison_builder::BuildPolicy::default())));
    let state = AppState { auth: OriginAuthenticator::new(origin, token)?, devices: DeviceRegistry::new(devices, 4), evidence, builder };
    let listener = tokio::net::TcpListener::bind(address).await?;
    axum::serve(listener, router(state)).await?;
    Ok(())
}
