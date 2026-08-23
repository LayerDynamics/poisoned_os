use crate::{ByteSlice, Status};

pub const MAX_TEXT: usize = 256;

pub trait Ui {
    fn log(&mut self, level: u8, text: ByteSlice) -> Status;
    fn progress(&mut self, completed: u32, total: u32) -> Status;
}
