#[derive(Debug, Default, Clone, PartialEq, Eq)]
pub struct VerificationReport { pub accepted: bool, pub errors: Vec<String> }
