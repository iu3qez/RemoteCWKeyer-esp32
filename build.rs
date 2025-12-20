// RustRemoteCWKeyer - Build Script
//
// Runs code generation from parameters.yaml before compilation.

use std::process::Command;

fn main() {
    // ESP-IDF environment setup (MUST be first!)
    embuild::espidf::sysenv::output();

    // Track partition file changes
    println!("cargo:rerun-if-changed=partitions.csv");

    // Get git version info
    let version = env!("CARGO_PKG_VERSION");
    let git_hash = Command::new("git")
        .args(["rev-parse", "--short", "HEAD"])
        .output()
        .ok()
        .and_then(|o| String::from_utf8(o.stdout).ok())
        .map(|s| s.trim().to_string())
        .unwrap_or_else(|| "unknown".to_string());

    println!("cargo:rustc-env=GIT_HASH={}", git_hash);
    println!("cargo:rustc-env=VERSION_STRING=RustKeyer v{}-g{}", version, git_hash);

    // Run Python code generator
    let status = Command::new("python3")
        .arg("scripts/gen_config.py")
        .status()
        .expect("Failed to run config generator. Is Python 3 installed?");

    if !status.success() {
        panic!("Config generation failed. Check scripts/gen_config.py output.");
    }

    // Rebuild if parameters.yaml changes
    println!("cargo:rerun-if-changed=parameters.yaml");

    // Rebuild if codegen script changes
    println!("cargo:rerun-if-changed=scripts/gen_config.py");

    // Rebuild if git HEAD changes
    println!("cargo:rerun-if-changed=.git/HEAD");
}
