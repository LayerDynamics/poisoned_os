use std::env;
use std::error::Error;
use std::path::PathBuf;

fn main() -> Result<(), Box<dyn Error>> {
    let mut arguments = env::args_os().skip(1);
    let proto_root = PathBuf::from(arguments.next().ok_or("missing proto root")?);
    let output = PathBuf::from(arguments.next().ok_or("missing output directory")?);
    let proto_files: Vec<PathBuf> = arguments.map(|name| proto_root.join(name)).collect();
    if proto_files.is_empty() {
        return Err("at least one .proto file is required".into());
    }
    if proto_files
        .iter()
        .any(|path| path.extension().is_none_or(|ext| ext != "proto"))
    {
        return Err("every input must be a .proto file".into());
    }

    let mut config = prost_build::Config::new();
    config.out_dir(output);
    config.compile_protos(&proto_files, &[proto_root])?;
    Ok(())
}
