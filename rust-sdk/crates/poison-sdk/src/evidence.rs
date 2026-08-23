use crate::{ByteSlice, Status};

pub const MAX_ARTIFACT: usize = 4096;

pub trait Evidence {
    fn submit_artifact(&mut self, content: ByteSlice, derived: bool) -> Status;
}
