fn main() {
    let policy = poison_builder::BuildPolicy::default();
    if let Err(error) = poison_builder::validate_policy(policy) {
        eprintln!("builder policy invalid: {error}");
        std::process::exit(2);
    }
    println!("poison-builder policy ready");
}
