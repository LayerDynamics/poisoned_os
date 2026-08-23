use crate::device::{DeviceDescriptor, DeviceTransport};
use crate::transports::DeviceStream;
use serde::Serialize;
use std::io;
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};
use tokio::time::{timeout, Duration};
use tokio_serial::{SerialPortBuilderExt, SerialPortType};

const SERIAL_BAUD: u32 = 230_400;
const CLI_TIMEOUT: Duration = Duration::from_secs(10);
const FLIPPER_USB_VENDOR_ID: u16 = 0x0483;
const FLIPPER_RUNTIME_PRODUCT_ID: u16 = 0x5740;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct UsbSerialPort {
    pub device: String,
    pub serial_number: Option<String>,
    pub vendor_id: u16,
    pub product_id: u16,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct UsbSerialInventory {
    pub schema: &'static str,
    pub ports: Vec<UsbSerialPort>,
}

pub trait UsbAdapter: Send + Sync {
    fn discover(&self) -> Vec<DeviceDescriptor>;
}

#[derive(Default)]
pub struct SystemUsbAdapter;

impl UsbAdapter for SystemUsbAdapter {
    fn discover(&self) -> Vec<DeviceDescriptor> {
        enumerate_usb_serial_ports().unwrap_or_default().into_iter().filter_map(|port| {
            if !is_flipper_runtime_ids(port.vendor_id, port.product_id) { return None; }
            let serial = port.serial_number.as_deref().unwrap_or("unknown");
            Some(DeviceDescriptor {
                id: port.device,
                label: format!("Flipper USB ({serial})"),
                transport: DeviceTransport::Usb,
            })
        }).collect()
    }
}

pub fn enumerate_usb_serial_ports() -> Result<Vec<UsbSerialPort>, tokio_serial::Error> {
    let mut ports = tokio_serial::available_ports()?
        .into_iter()
        .filter_map(|port| {
            let SerialPortType::UsbPort(info) = port.port_type else { return None };
            Some(UsbSerialPort {
                device: port.port_name,
                serial_number: info.serial_number,
                vendor_id: info.vid,
                product_id: info.pid,
            })
        })
        .collect::<Vec<_>>();
    ports.sort_by(|left, right| left.device.cmp(&right.device));
    Ok(ports)
}

pub fn usb_serial_inventory() -> Result<UsbSerialInventory, tokio_serial::Error> {
    Ok(UsbSerialInventory {
        schema: "poison.usb-serial-ports/v1",
        ports: enumerate_usb_serial_ports()?,
    })
}

pub fn is_flipper_runtime_ids(vendor_id: u16, product_id: u16) -> bool {
    vendor_id == FLIPPER_USB_VENDOR_ID && product_id == FLIPPER_RUNTIME_PRODUCT_ID
}

pub fn descriptor(id: impl Into<String>, label: impl Into<String>) -> DeviceDescriptor {
    DeviceDescriptor { id: id.into(), label: label.into(), transport: DeviceTransport::Usb }
}

pub async fn open_rpc_stream(path: &str) -> io::Result<DeviceStream> {
    let mut serial = tokio_serial::new(path, SERIAL_BAUD).open_native_async()?;
    start_rpc_session(&mut serial).await?;
    Ok(DeviceStream::new(serial))
}

pub async fn start_rpc_session<S>(stream: &mut S) -> io::Result<()>
where
    S: AsyncRead + AsyncWrite + Unpin,
{
    stream.write_all(b"\r").await?;
    stream.flush().await?;
    read_until(stream, b">: ").await?;
    stream.write_all(b"start_rpc_session\r").await?;
    stream.flush().await?;
    read_until(stream, b"\n").await
}

async fn read_until<S: AsyncRead + Unpin>(stream: &mut S, marker: &[u8]) -> io::Result<()> {
    timeout(CLI_TIMEOUT, async {
        let mut received = Vec::with_capacity(128);
        let mut byte = [0u8; 1];
        while received.len() <= 4096 {
            stream.read_exact(&mut byte).await?;
            received.push(byte[0]);
            if received.ends_with(marker) { return Ok(()); }
        }
        Err(io::Error::new(io::ErrorKind::InvalidData, "Flipper CLI response exceeded bound"))
    }).await.map_err(|_| io::Error::new(io::ErrorKind::TimedOut, "Flipper CLI did not enter RPC mode"))?
}

#[cfg(test)]
mod tests {
    use super::{is_flipper_runtime_ids, start_rpc_session, UsbSerialInventory, UsbSerialPort};
    use tokio::io::{AsyncReadExt, AsyncWriteExt};

    #[test]
    fn recognizes_only_the_flipper_runtime_usb_identity() {
        assert!(is_flipper_runtime_ids(0x0483, 0x5740));
        assert!(!is_flipper_runtime_ids(0x17e9, 0x6000));
        assert!(!is_flipper_runtime_ids(0x0483, 0xdf11));
    }

    #[test]
    fn native_inventory_is_machine_readable_without_losing_usb_identity() {
        let inventory = UsbSerialInventory {
            schema: "poison.usb-serial-ports/v1",
            ports: vec![UsbSerialPort {
                device: "/dev/cu.usbmodemflip_Test1".to_owned(),
                serial_number: Some("flip_Test1".to_owned()),
                vendor_id: 0x0483,
                product_id: 0x5740,
            }],
        };

        let value = serde_json::to_value(inventory).unwrap();
        assert_eq!(value["schema"], "poison.usb-serial-ports/v1");
        assert_eq!(value["ports"][0]["serialNumber"], "flip_Test1");
        assert_eq!(value["ports"][0]["vendorId"], 0x0483);
        assert_eq!(value["ports"][0]["productId"], 0x5740);
    }

    #[tokio::test]
    async fn enters_cli_rpc_mode_before_forwarding_binary_frames() {
        let (mut client, mut device) = tokio::io::duplex(256);
        let device_task = tokio::spawn(async move {
            let mut wake = [0u8; 1];
            device.read_exact(&mut wake).await.unwrap();
            assert_eq!(&wake, b"\r");
            device.write_all(b"\r\n>: ").await.unwrap();
            let mut command = [0u8; 18];
            device.read_exact(&mut command).await.unwrap();
            assert_eq!(&command, b"start_rpc_session\r");
            device.write_all(b"start_rpc_session\r\n").await.unwrap();
        });

        start_rpc_session(&mut client).await.unwrap();
        device_task.await.unwrap();
    }
}
