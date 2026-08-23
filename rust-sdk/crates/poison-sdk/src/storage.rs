use crate::{ByteSlice, Status};

pub const MAX_PATH: usize = 128;
pub const MAX_CHUNK: usize = 1024;

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct StorageHandle(pub u32);

pub trait Storage {
    fn open(&mut self, path: ByteSlice, mode: u32) -> Result<StorageHandle, Status>;
    fn write(&mut self, handle: StorageHandle, data: ByteSlice) -> Status;
    fn close(&mut self, handle: StorageHandle) -> Status;
}
