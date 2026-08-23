pub const TARGET: &str = "thumbv7em-none-eabihf";
pub const API_VERSION: u16 = 1;
pub const ABI_VERSION: u16 = 1;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct NativeArtifactManifest {
    pub target: String,
    pub api_version: u16,
    pub abi_version: u16,
    pub entry: String,
    pub imports: Vec<String>,
    pub relocations: Vec<u32>,
    pub capabilities: Vec<String>,
    pub digest: String,
}

impl NativeArtifactManifest {
    pub fn canonical_bytes(&self) -> Vec<u8> {
        let mut out = format!("abi={}\napi={}\ndigest={}\nentry={}\ntarget={}\n", self.abi_version, self.api_version, self.digest, self.entry, self.target);
        out.push_str("capabilities=");
        out.push_str(&self.capabilities.join(","));
        out.push('\n');
        out.push_str("imports=");
        out.push_str(&self.imports.join(","));
        out.push('\n');
        out.push_str("relocations=");
        out.push_str(&self.relocations.iter().map(u32::to_string).collect::<Vec<_>>().join(","));
        out.push('\n');
        out.into_bytes()
    }

    pub fn validate(&self, expected_target: &str, expected_api: u16, expected_abi: u16) -> Result<(), &'static str> {
        if self.target != expected_target || self.api_version != expected_api || self.abi_version != expected_abi { return Err("target or ABI mismatch"); }
        if self.entry != "poison_rust_entry" { return Err("entry symbol mismatch"); }
        if self.digest.len() != 64 || !self.digest.bytes().all(|byte| byte.is_ascii_hexdigit() && !byte.is_ascii_uppercase()) { return Err("invalid artifact digest"); }
        if self.imports.iter().any(|name| !name.starts_with("poison_")) { return Err("unsupported import"); }
        if self.relocations.iter().any(|relocation| !matches!(relocation, 0 | 2 | 3 | 10)) { return Err("unsupported relocation"); }
        if self.capabilities.iter().any(|capability| capability.is_empty() || capability.len() > 64) { return Err("invalid capability"); }
        Ok(())
    }
}
