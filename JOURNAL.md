# Development Journal

Questo documento contiene informazioni critiche sullo sviluppo del progetto: decisioni architetturali, problemi risolti, e punti che possono causare rotture se modificati.

---

## 2025-12-19: Build System Configuration - CRITICAL

### Problema Risolto: Build Failure con ldproxy

**Sintomo:**
```
error: linking with `ldproxy` failed: exit status: 101
thread 'main' panicked at 'Cannot locate argument '--ldproxy-linker <linker>''
```

**Causa Radice:**
Il file `build.rs` personalizzato non chiamava `embuild::espidf::sysenv::output()`. Questa funzione è **ASSOLUTAMENTE CRITICA** perché:

1. Emette le istruzioni `cargo:rustc-link-arg` necessarie per configurare ldproxy
2. Configura l'ambiente ESP-IDF (variabili, path, compilatore)
3. Passa i parametri del linker GCC a cargo

**⚠️ BREAKING POINT - NON MODIFICARE:**

```rust
fn main() {
    // DEVE essere la PRIMA chiamata nel build.rs!
    embuild::espidf::sysenv::output();

    // ... resto del codice di generazione configurazione
}
```

**Perché è critico:**
- Senza questa chiamata, cargo non riceve i parametri `--ldproxy-linker`
- ldproxy non sa quale compilatore GCC ESP-IDF usare
- Il linking fallisce sempre, anche se tutto il resto è configurato correttamente

### Configurazione ESP-IDF Build System

**Versioni Critiche:**
- `embuild = "0.33"` in `[build-dependencies]` (Cargo.toml)
- `ESP_IDF_VERSION = "v5.3.3"` in `.cargo/config.toml`
- `esp-idf-svc = "0.51.0"` compatibile con ESP-IDF v5.3.x

**Perché queste versioni:**
- embuild 0.33 è compatibile con l'ultimo ldproxy binario precompilato
- ESP-IDF v5.3.3 è stabile e testato con esp-idf-svc 0.51.0
- Versioni diverse possono causare incompatibilità nel sistema di build

### DevContainer Setup

**Volume Mounts Critici:**
```json
{
  "mounts": [
    "source=rust-remote-cw-keyer-embuild-cache,target=/workspaces/RustRemoteCWKeyer/.embuild,type=volume",
    "source=rust-remote-cw-keyer-target-cache,target=/workspaces/RustRemoteCWKeyer/target,type=volume"
  ]
}
```

**Perché sono importanti:**
- `.embuild/` contiene ESP-IDF scaricato (~500MB)
- Senza volume mount, ESP-IDF viene riscaricato ad ogni rebuild del container
- Il primo build dopo container rebuild richiede 10-15 minuti per scaricare ESP-IDF
- I build successivi sono molto più veloci grazie alla cache

### ldproxy Binary

**Fonte:** Binari precompilati da GitHub releases di esp-rs/embuild

**⚠️ NON installare ldproxy via `cargo install`:**
- Le versioni su crates.io potrebbero non essere compatibili
- I binari precompilati sono testati con il Dockerfile ufficiale esp-rs
- In caso di problemi, reinstallare da:
  ```bash
  curl -L "https://github.com/esp-rs/embuild/releases/latest/download/ldproxy-x86_64-unknown-linux-gnu.zip" -o /tmp/ldproxy.zip
  unzip -o /tmp/ldproxy.zip -d ~/.cargo/bin/
  chmod u+x ~/.cargo/bin/ldproxy
  ```

### Multi-Target Support (ESP32-S3 e ESP32-P4)

**Configurazione:**
- Default target: `xtensa-esp32s3-espidf` (Xtensa)
- Alternative: `riscv32imafc-esp-espidf` (RISC-V per ESP32-P4)
- Build per ESP32-P4: `cargo build --target riscv32imafc-esp-espidf`

**Toolchain:**
- Default: `esp` (Xtensa, per ESP32-S3)
- RISC-V toolchain installato automaticamente da espup
- Entrambi configurati nel devcontainer con `ESP_BOARD=esp32s3,esp32p4`

---

## Decisioni Architetturali

### Build Script (build.rs)

**Ordine di esecuzione CRITICO:**
1. `embuild::espidf::sysenv::output()` - SEMPRE PRIMO
2. Generazione codice da parameters.yaml (Python)
3. Cargo rerun-if-changed directives

**Razionale:**
L'output di embuild deve essere emesso PRIMA di qualsiasi altra operazione per garantire che tutte le variabili d'ambiente e configurazioni siano disponibili durante la compilazione del progetto.

### DevContainer vs Host Build

**Scelta:** DevContainer con Debian bookworm-slim + espup

**Perché:**
- Evita conflitti tra toolchain GCC (problema con espressif/idf immagini)
- Installazione pulita e riproducibile
- Compatibile con template ufficiale esp-rs
- Supporto multi-target out-of-the-box

**Alternativa scartata:**
- Base image `espressif/idf:v5.5.1` causava conflitti tra GCC 14.2 (ESP-IDF) e GCC 15.2 (Rust ESP)

---

## Problemi Noti e Soluzioni

### Problema: "GLIBC 2.39 not found"
**Causa:** Build artifacts da container precedente
**Soluzione:** `cargo clean` dopo rebuild del container

### Problema: ESP-IDF non scaricato al primo build
**Causa:** Variabile `ESP_IDF_VERSION` non impostata o formato errato
**Soluzione:** Usare formato stringa semplice `"v5.3.3"` invece di `{ value = "v5.3", force = true }`

### Problema: Build lento (10-15 minuti) dopo rebuild container
**Causa:** ESP-IDF viene riscaricato (~500MB)
**Soluzione:** Volume mounts per `.embuild/` e `target/` (già configurato)

---

## Riferimenti

### Template Ufficiale
- Repository: https://github.com/esp-rs/esp-idf-template
- Usato come riferimento per configurazione build system

### Documentazione
- ESP-RS Book: https://esp-rs.github.io/book/
- embuild: https://github.com/esp-rs/embuild
- esp-idf-svc releases: https://github.com/esp-rs/esp-idf-svc/releases

### File di Configurazione Critici
1. `build.rs` - Build script con embuild setup
2. `Cargo.toml` - Dipendenze e versioni embuild
3. `.cargo/config.toml` - Target e ESP_IDF_VERSION
4. `.devcontainer/Dockerfile` - Installazione toolchain
5. `.devcontainer/devcontainer.json` - Volume mounts

---

## Note per il Futuro

### Prima di Modificare build.rs
- ⚠️ NON rimuovere `embuild::espidf::sysenv::output()`
- ⚠️ DEVE rimanere la prima chiamata in `fn main()`
- Testare sempre con `cargo clean && cargo build` dopo modifiche

### Prima di Aggiornare Dipendenze
- Verificare compatibilità tra esp-idf-svc e ESP-IDF version
- Controllare changelog di embuild per breaking changes
- Testare build completo prima di committare

### Prima di Rebuild DevContainer
- Il primo build dopo rebuild richiederà 10-15 minuti
- ESP-IDF verrà riscaricato da zero se i volume mounts non funzionano
- Verificare che i volume Docker siano preservati

---

## Checklist: Sintomi di Build System Rotto

- [ ] Errore "Cannot locate argument '--ldproxy-linker'"
  → Controllare build.rs chiama embuild::espidf::sysenv::output()

- [ ] Build molto lento (>15 min) anche dopo primo build
  → Controllare volume mounts in devcontainer.json

- [ ] "GLIBC not found" o linking errors strani
  → Fare `cargo clean` dopo rebuild container

- [ ] ESP-IDF version mismatch
  → Verificare ESP_IDF_VERSION in .cargo/config.toml

- [ ] ldproxy panic anche con configurazione corretta
  → Reinstallare ldproxy da binari precompilati GitHub
