#![no_std]

use poison_sdk::Status;

pub type Entry = extern "C" fn() -> Status;

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Exit {
    pub status: Status,
    pub resources_released: bool,
}

pub fn invoke(entry: Entry) -> Exit {
    Exit { status: entry(), resources_released: true }
}
