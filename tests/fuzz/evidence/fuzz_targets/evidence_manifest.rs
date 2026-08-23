fn main() { let _ = std::env::args().skip(1).take(1).next(); }

#[cfg(test)]
mod tests {
    #[test]
    fn rejects_empty_digest() { assert!(super::digest_ok("").is_err()); }
}

fn digest_ok(value: &str) -> Result<(), ()> { if value.len() == 64 && value.bytes().all(|byte| byte.is_ascii_hexdigit()) { Ok(()) } else { Err(()) } }
