#![no_std]

/// Minimal admission fixture; execution is only enabled after ADR-0006 selects a runtime.
#[unsafe(no_mangle)]
pub extern "C" fn poison_rust_wasm_entry() -> i32 {
    0
}
