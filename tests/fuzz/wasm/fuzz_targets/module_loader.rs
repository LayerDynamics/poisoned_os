fn main() { let _ = std::env::args().skip(1).take(1).next(); }

#[cfg(test)]
mod tests {
    #[test]
    fn rejects_invalid_wasm_header() { assert!(super::valid_module(&[0, 1, 2, 3]).is_err()); }
}

fn valid_module(bytes: &[u8]) -> Result<(), ()> { if bytes.len() >= 8 && bytes[..4] == [0, 0x61, 0x73, 0x6d] { Ok(()) } else { Err(()) } }
