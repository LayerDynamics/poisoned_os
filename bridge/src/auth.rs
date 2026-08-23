use std::sync::Arc;
use std::fmt;

use keyring::Entry;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum AuthError {
    MissingOrigin,
    InvalidOrigin,
    MissingToken,
    InvalidToken,
    SecureStorage(String),
}

impl fmt::Display for AuthError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{self:?}")
    }
}

impl std::error::Error for AuthError {}

pub trait TokenStore: Send + Sync {
    fn load_or_create(&self) -> Result<String, AuthError>;
}

#[derive(Clone)]
pub struct KeyringTokenStore {
    service: String,
    account: String,
}

impl KeyringTokenStore {
    pub fn new(service: impl Into<String>, account: impl Into<String>) -> Self {
        Self { service: service.into(), account: account.into() }
    }
}

impl TokenStore for KeyringTokenStore {
    fn load_or_create(&self) -> Result<String, AuthError> {
        let entry = Entry::new(&self.service, &self.account)
            .map_err(|error| AuthError::SecureStorage(error.to_string()))?;
        match entry.get_password() {
            Ok(token) if !token.is_empty() => Ok(token),
            Ok(_) | Err(keyring::Error::NoEntry) => {
                let token = generate_token()?;
                entry.set_password(&token).map_err(|error| AuthError::SecureStorage(error.to_string()))?;
                Ok(token)
            }
            Err(error) => Err(AuthError::SecureStorage(error.to_string())),
        }
    }
}

#[derive(Debug, Clone)]
pub struct OriginAuthenticator {
    expected_origin: Arc<str>,
    token: Arc<[u8]>,
}

impl OriginAuthenticator {
    pub fn new(expected_origin: impl Into<String>, token: impl Into<String>) -> Result<Self, AuthError> {
        let origin = expected_origin.into();
        if !is_loopback_origin(&origin) { return Err(AuthError::InvalidOrigin); }
        let token = token.into();
        if token.is_empty() { return Err(AuthError::InvalidToken); }
        Ok(Self { expected_origin: Arc::from(origin), token: Arc::from(token.into_bytes()) })
    }

    pub fn expected_origin(&self) -> &str { &self.expected_origin }

    pub fn authorize(&self, origin: Option<&str>, token: Option<&str>) -> Result<(), AuthError> {
        let origin = origin.ok_or(AuthError::MissingOrigin)?;
        if origin != self.expected_origin.as_ref() { return Err(AuthError::InvalidOrigin); }
        let token = token.ok_or(AuthError::MissingToken)?.as_bytes();
        if !constant_time_eq(token, &self.token) { return Err(AuthError::InvalidToken); }
        Ok(())
    }

    pub fn authorize_websocket(
        &self,
        origin: Option<&str>,
        protocols: Option<&str>,
    ) -> Result<String, AuthError> {
        let origin = origin.ok_or(AuthError::MissingOrigin)?;
        if origin != self.expected_origin.as_ref() { return Err(AuthError::InvalidOrigin); }
        let protocols = protocols.ok_or(AuthError::MissingToken)?;
        let prefix = "poisoned-os.rpc.v1.";
        for protocol in protocols.split(',').map(str::trim) {
            let Some(token) = protocol.strip_prefix(prefix) else { continue };
            if constant_time_eq(token.as_bytes(), &self.token) {
                return Ok(protocol.to_owned());
            }
        }
        Err(AuthError::InvalidToken)
    }
}

fn generate_token() -> Result<String, AuthError> {
    let mut bytes = [0u8; 32];
    getrandom::fill(&mut bytes).map_err(|error| AuthError::SecureStorage(error.to_string()))?;
    let mut token = String::with_capacity(bytes.len() * 2);
    for byte in bytes { use std::fmt::Write; let _ = write!(token, "{byte:02x}"); }
    Ok(token)
}

fn constant_time_eq(left: &[u8], right: &[u8]) -> bool {
    if left.len() != right.len() { return false; }
    left.iter().zip(right).fold(0u8, |difference, (a, b)| difference | (a ^ b)) == 0
}

fn is_loopback_origin(origin: &str) -> bool {
    origin == "http://localhost" || origin == "https://localhost"
        || origin.starts_with("http://localhost:") || origin.starts_with("https://localhost:")
        || origin == "http://127.0.0.1" || origin == "https://127.0.0.1"
        || origin.starts_with("http://127.0.0.1:") || origin.starts_with("https://127.0.0.1:")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejects_non_loopback_and_requires_exact_origin_token() {
        assert_eq!(OriginAuthenticator::new("http://192.168.1.2:49000", "token").unwrap_err(), AuthError::InvalidOrigin);
        let auth = OriginAuthenticator::new("http://127.0.0.1:49000", "token").unwrap();
        assert_eq!(auth.authorize(None, Some("token")), Err(AuthError::MissingOrigin));
        assert_eq!(auth.authorize(Some("http://localhost:49000"), Some("token")), Err(AuthError::InvalidOrigin));
        assert_eq!(auth.authorize(Some("http://127.0.0.1:49000"), Some("wrong")), Err(AuthError::InvalidToken));
        assert!(auth.authorize(Some("http://127.0.0.1:49000"), Some("token")).is_ok());
    }
}
