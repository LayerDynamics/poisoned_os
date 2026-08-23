fn main() {
    // The release runner supplies bounded corpus inputs to the target process.
    let _ = std::env::args().skip(1).take(1).next();
}

#[cfg(test)]
mod tests {
    #[test]
    fn rejects_truncated_envelope() { assert!(super::decode(&[0u8; 3]).is_err()); }
}

fn decode(input: &[u8]) -> Result<(), ()> { if input.len() < 4 { Err(()) } else { Ok(()) } }
