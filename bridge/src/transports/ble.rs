use crate::{device::{DeviceDescriptor, DeviceTransport}, transports::DeviceStream};
use btleplug::{
    api::{Central, CharPropFlags, Manager as _, Peripheral as _, ScanFilter, WriteType},
    platform::{Manager, Peripheral},
};
use futures_util::StreamExt;
use std::{io, time::Duration};
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    sync::watch,
    time::{sleep, timeout},
};
use uuid::Uuid;

const DISCOVERY_WINDOW: Duration = Duration::from_secs(2);
const CONNECT_TIMEOUT: Duration = Duration::from_secs(10);
const GATT_CHUNK_BYTES: usize = 243;
const SERVICE_UUID: &str = "8fe5b3d5-2e7f-4a98-2a48-7acc60fe0000";
const TX_UUID: &str = "19ed82ae-ed21-4c9d-4145-228e61fe0000";
const RX_UUID: &str = "19ed82ae-ed21-4c9d-4145-228e62fe0000";
const FLOW_UUID: &str = "19ed82ae-ed21-4c9d-4145-228e63fe0000";
const STATUS_UUID: &str = "19ed82ae-ed21-4c9d-4145-228e64fe0000";

#[derive(Default)]
pub struct SystemBleAdapter;

impl SystemBleAdapter {
    pub async fn discover(&self) -> Vec<DeviceDescriptor> {
        let mut descriptors = Vec::new();
        for peripheral in discover_peripherals().await.unwrap_or_default() {
            let Ok(Some(properties)) = peripheral.properties().await else { continue };
            let Some(label) = properties.local_name else { continue };
            descriptors.push(DeviceDescriptor {
                id: format!("ble:{}", peripheral.id()),
                label,
                transport: DeviceTransport::Ble,
            });
        }
        descriptors
    }
}

async fn discover_peripherals() -> io::Result<Vec<Peripheral>> {
    let manager = Manager::new().await.map_err(bt_error)?;
    let adapters = manager.adapters().await.map_err(bt_error)?;
    let service = parse_uuid(SERVICE_UUID)?;
    let mut devices = Vec::new();
    for adapter in adapters {
        adapter.start_scan(ScanFilter { services: vec![service] }).await.map_err(bt_error)?;
        sleep(DISCOVERY_WINDOW).await;
        devices.extend(adapter.peripherals().await.map_err(bt_error)?);
        let _ = adapter.stop_scan().await;
    }
    devices.sort_by_key(|peripheral| peripheral.id().to_string());
    devices.dedup_by_key(|peripheral| peripheral.id().to_string());
    Ok(devices)
}

pub async fn open_rpc_stream(id: &str) -> io::Result<DeviceStream> {
    let wanted = id.strip_prefix("ble:").ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "invalid BLE device id"))?;
    let peripheral = timeout(CONNECT_TIMEOUT, async {
        discover_peripherals().await?.into_iter().find(|device| device.id().to_string() == wanted)
            .ok_or_else(|| io::Error::new(io::ErrorKind::NotFound, "Flipper BLE device was not found"))
    }).await.map_err(|_| io::Error::new(io::ErrorKind::TimedOut, "BLE discovery timed out"))??;
    timeout(CONNECT_TIMEOUT, peripheral.connect()).await
        .map_err(|_| io::Error::new(io::ErrorKind::TimedOut, "BLE connection timed out"))?
        .map_err(bt_error)?;
    peripheral.discover_services().await.map_err(bt_error)?;
    let tx = characteristic(&peripheral, TX_UUID, CharPropFlags::INDICATE | CharPropFlags::NOTIFY)?;
    let rx = characteristic(&peripheral, RX_UUID, CharPropFlags::WRITE_WITHOUT_RESPONSE)?;
    let flow = characteristic(&peripheral, FLOW_UUID, CharPropFlags::INDICATE | CharPropFlags::NOTIFY)?;
    let status = characteristic(&peripheral, STATUS_UUID, CharPropFlags::WRITE)?;
    peripheral.subscribe(&tx).await.map_err(bt_error)?;
    peripheral.subscribe(&flow).await.map_err(bt_error)?;
    peripheral.write(&status, &[1, 0, 0, 0], WriteType::WithResponse).await.map_err(bt_error)?;
    let initial_credit = decode_credit(&peripheral.read(&flow).await.map_err(bt_error)?)?;
    let mut notifications = peripheral.notifications().await.map_err(bt_error)?;
    let (credit_tx, credit_rx) = watch::channel(initial_credit);
    let (host, device) = tokio::io::duplex(4096);
    let (mut from_host, mut to_host) = tokio::io::split(device);
    let notify_tx_uuid = tx.uuid;
    let notify_flow_uuid = flow.uuid;
    tokio::spawn(async move {
        while let Some(notification) = notifications.next().await {
            if notification.uuid == notify_tx_uuid {
                if to_host.write_all(&notification.value).await.is_err() { break; }
            } else if notification.uuid == notify_flow_uuid {
                if let Ok(credit) = decode_credit(&notification.value) {
                    let _ = credit_tx.send(credit);
                }
            }
        }
    });
    let outbound_peripheral = peripheral.clone();
    tokio::spawn(async move {
        let mut credit_rx = credit_rx;
        let mut available = *credit_rx.borrow_and_update();
        let mut buffer = [0u8; GATT_CHUNK_BYTES];
        'outbound: while let Ok(size) = from_host.read(&mut buffer).await {
            if size == 0 { break; }
            let mut offset = 0usize;
            while offset < size {
                while available == 0 {
                    if credit_rx.changed().await.is_err() { break 'outbound; }
                    available = *credit_rx.borrow_and_update();
                }
                let chunk = (size - offset).min(GATT_CHUNK_BYTES).min(available);
                if outbound_peripheral.write(
                    &rx,
                    &buffer[offset..offset + chunk],
                    WriteType::WithoutResponse,
                ).await.is_err() { break 'outbound; }
                offset += chunk;
                available -= chunk;
            }
        }
        let _ = outbound_peripheral.write(&status, &[0, 0, 0, 0], WriteType::WithResponse).await;
        let _ = outbound_peripheral.disconnect().await;
    });
    Ok(DeviceStream::new(host))
}

fn characteristic(
    peripheral: &Peripheral,
    uuid: &str,
    required: CharPropFlags,
) -> io::Result<btleplug::api::Characteristic> {
    let uuid = parse_uuid(uuid)?;
    peripheral.characteristics().into_iter().find(|candidate| {
        candidate.uuid == uuid && candidate.properties.intersects(required)
    }).ok_or_else(|| io::Error::new(io::ErrorKind::Unsupported, format!("missing Flipper BLE characteristic {uuid}")))
}

fn parse_uuid(value: &str) -> io::Result<Uuid> {
    Uuid::parse_str(value).map_err(|error| io::Error::new(io::ErrorKind::InvalidData, error))
}

fn decode_credit(value: &[u8]) -> io::Result<usize> {
    let bytes: [u8; 4] = value.try_into().map_err(|_| io::Error::new(io::ErrorKind::InvalidData, "invalid BLE flow credit"))?;
    Ok(u32::from_be_bytes(bytes) as usize)
}

fn bt_error(error: impl std::fmt::Display) -> io::Error {
    io::Error::other(error.to_string())
}

#[cfg(test)]
mod tests {
    use super::decode_credit;

    #[test]
    fn decodes_firmware_big_endian_flow_credit() {
        assert_eq!(decode_credit(&[0, 0, 1, 230]).unwrap(), 486);
        assert!(decode_credit(&[0, 1]).is_err());
    }
}
