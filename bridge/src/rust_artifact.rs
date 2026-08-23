use serde::Deserialize;

pub const TARGET: &str = "thumbv7em-none-eabihf";
pub const API_VERSION: u16 = 1;
pub const ABI_VERSION: u16 = 1;

#[derive(Debug, Deserialize, serde::Serialize, PartialEq, Eq)]
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
    pub fn validate(&self) -> Result<(), &'static str> {
        if self.target != TARGET || self.api_version != API_VERSION || self.abi_version != ABI_VERSION {
            return Err("target or ABI mismatch");
        }
        if self.entry != "poison_rust_entry" || self.digest.len() != 64 || !self.digest.bytes().all(|byte| byte.is_ascii_hexdigit() && !byte.is_ascii_uppercase()) {
            return Err("entry or digest mismatch");
        }
        if self.imports.iter().any(|name| !name.starts_with("poison_")) {
            return Err("unsupported import");
        }
        if self.relocations.iter().any(|relocation| !matches!(relocation, 0 | 2 | 3 | 10)) {
            return Err("unsupported relocation");
        }
        if self.capabilities.iter().any(|capability| capability.is_empty() || capability.len() > 64) {
            return Err("invalid capability");
        }
        Ok(())
    }

    pub fn from_json(bytes: &[u8]) -> Result<Self, &'static str> {
        let manifest: Self = serde_json::from_slice(bytes).map_err(|_| "invalid native manifest")?;
        manifest.validate()?;
        Ok(manifest)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn valid() -> NativeArtifactManifest {
        NativeArtifactManifest {
            target: TARGET.into(), api_version: API_VERSION, abi_version: ABI_VERSION,
            entry: "poison_rust_entry".into(), imports: vec!["poison_storage_open".into()],
            relocations: vec![0, 2, 3], capabilities: vec!["storage.project.read".into()], digest: "a".repeat(64),
        }
    }

    #[test]
    fn rejects_unsafe_admission_fields() {
        assert!(valid().validate().is_ok());
        let mut manifest = valid(); manifest.imports.push("furi_secret".into());
        assert_eq!(manifest.validate(), Err("unsupported import"));
        let mut manifest = valid(); manifest.relocations.push(7);
        assert_eq!(manifest.validate(), Err("unsupported relocation"));
    }

    #[test]
    fn parses_only_valid_json_manifests() {
        let json = serde_json::to_vec(&valid()).unwrap();
        assert!(NativeArtifactManifest::from_json(&json).is_ok());
        assert!(NativeArtifactManifest::from_json(b"{}").is_err());
    }
}
