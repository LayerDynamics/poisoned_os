use crate::Status;

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DeviceInfo {
    pub api_version: u16,
    pub abi_version: u16,
    pub firmware_major: u16,
    pub firmware_minor: u16,
}

pub trait Device {
    fn info(&self) -> DeviceInfo;
    fn check_capability(&self, capability: u32) -> Status;
    fn cancelled(&self) -> bool;
}
