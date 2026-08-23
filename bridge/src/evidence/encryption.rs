pub trait EvidenceCipher: Send + Sync { fn encrypt(&self, plaintext: &[u8]) -> Result<Vec<u8>, &'static str>; }

pub struct UnavailableCipher;
impl EvidenceCipher for UnavailableCipher { fn encrypt(&self, _plaintext: &[u8]) -> Result<Vec<u8>, &'static str> { Err("secure key storage unavailable") } }
