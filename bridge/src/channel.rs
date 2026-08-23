pub const MAX_FRAME_BYTES: usize = 1024;
pub const MAX_CREDITS: u32 = 4;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ChannelError {
    Closed,
    Invalid,
    NoCredit,
    SequenceWrap,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ReceiveResult {
    Accepted,
    Duplicate,
    Gap,
}

#[derive(Debug)]
pub struct ChannelState {
    name: String,
    active: bool,
    next_transmit: u64,
    next_receive: u64,
    credits: u32,
}

impl ChannelState {
    pub fn new(name: impl Into<String>, credits: u32) -> Result<Self, ChannelError> {
        let name = name.into();
        if name.is_empty() || name.len() > 32 || credits > MAX_CREDITS {
            return Err(ChannelError::Invalid);
        }
        Ok(Self {
            name,
            active: true,
            next_transmit: 0,
            next_receive: 0,
            credits,
        })
    }

    pub fn reserve_send(&mut self, frame_bytes: usize) -> Result<u64, ChannelError> {
        self.ensure_active()?;
        if frame_bytes > MAX_FRAME_BYTES {
            return Err(ChannelError::Invalid);
        }
        if self.credits == 0 {
            return Err(ChannelError::NoCredit);
        }
        if self.next_transmit == u64::MAX {
            return Err(ChannelError::SequenceWrap);
        }
        let sequence = self.next_transmit;
        self.next_transmit += 1;
        self.credits -= 1;
        Ok(sequence)
    }

    pub fn receive(&mut self, frame_bytes: usize, sequence: u64) -> Result<ReceiveResult, ChannelError> {
        self.ensure_active()?;
        if frame_bytes > MAX_FRAME_BYTES {
            return Err(ChannelError::Invalid);
        }
        if sequence < self.next_receive {
            return Ok(ReceiveResult::Duplicate);
        }
        if sequence > self.next_receive {
            return Ok(ReceiveResult::Gap);
        }
        if self.next_receive == u64::MAX {
            return Err(ChannelError::SequenceWrap);
        }
        self.next_receive += 1;
        Ok(ReceiveResult::Accepted)
    }

    pub fn add_credits(&mut self, credits: u32) -> Result<(), ChannelError> {
        self.ensure_active()?;
        if credits > MAX_CREDITS - self.credits {
            return Err(ChannelError::Invalid);
        }
        self.credits += credits;
        Ok(())
    }

    pub fn close(&mut self) {
        self.active = false;
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn available_credits(&self) -> u32 {
        self.credits
    }

    fn ensure_active(&self) -> Result<(), ChannelError> {
        if self.active {
            Ok(())
        } else {
            Err(ChannelError::Closed)
        }
    }
}
