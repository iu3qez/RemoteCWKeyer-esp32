// RustRemoteCWKeyer - Build Script
//
// Runs code generation from parameters.yaml before compilation.

use std::process::Command;

fn main() {
    // ESP-IDF environment setup (MUST be first!)
    embuild::espidf::sysenv::output();

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
}
