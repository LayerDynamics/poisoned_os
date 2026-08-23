#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]

pub mod device;
pub mod evidence;
pub mod storage;
pub mod ui;

pub const API_VERSION: u16 = 1;
pub const ABI_VERSION: u16 = 1;

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ByteSlice {
    pub ptr: *const u8,
    pub len: usize,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Status {
    pub code: i32,
}

impl Status {
    pub const OK: Self = Self { code: 0 };
    pub const INVALID_ARGUMENT: Self = Self { code: 1 };
    pub const DENIED: Self = Self { code: 2 };
    pub const CANCELLED: Self = Self { code: 3 };
    pub const RESOURCE_LIMIT: Self = Self { code: 4 };
}
