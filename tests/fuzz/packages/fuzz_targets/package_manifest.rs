fn main() { let _ = std::env::args().skip(1).take(1).next(); }

#[cfg(test)]
mod tests {
    #[test]
    fn rejects_parent_path() { assert!(super::valid_path("../escape").is_err()); }
}

fn valid_path(path: &str) -> Result<(), ()> { if path.is_empty() || path.starts_with('/') || path.split('/').any(|part| part == "..") { Err(()) } else { Ok(()) } }
