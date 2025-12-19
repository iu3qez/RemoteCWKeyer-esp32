ARG VARIANT=bookworm-slim
FROM debian:${VARIANT}
ENV DEBIAN_FRONTEND=noninteractive
ENV LC_ALL=C.UTF-8
ENV LANG=C.UTF-8

# Arguments
ARG CONTAINER_USER=vscode
ARG CONTAINER_GROUP=vscode
# Support both ESP32-S3 (Xtensa) and ESP32-P4 (RISC-V)
ARG ESP_BOARD=esp32s3,esp32p4
ARG GITHUB_TOKEN

# Install dependencies and Node.js
RUN apt-get update \
    && apt-get install -y pkg-config curl gcc clang libudev-dev unzip xz-utils \
    git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0 \
    vim bash-completion ca-certificates gnupg \
    && mkdir -p /etc/apt/keyrings \
    && curl -fsSL https://deb.nodesource.com/gpgkey/nodesource-repo.gpg.key | gpg --dearmor -o /etc/apt/keyrings/nodesource.gpg \
    && echo "deb [signed-by=/etc/apt/keyrings/nodesource.gpg] https://deb.nodesource.com/node_18.x nodistro main" | tee /etc/apt/sources.list.d/nodesource.list \
    && apt-get update \
    && apt-get install -y nodejs \
    && apt-get clean -y && rm -rf /var/lib/apt/lists/* /tmp/library-scripts

# Set users
RUN if ! getent group ${CONTAINER_GROUP} > /dev/null; then \
        groupadd --gid 1000 ${CONTAINER_GROUP}; \
    fi && \
    if ! getent passwd ${CONTAINER_USER} > /dev/null; then \
        useradd --uid 1000 --gid ${CONTAINER_GROUP} -m -s /bin/bash ${CONTAINER_USER}; \
    fi && \
    usermod -aG dialout ${CONTAINER_USER}

USER ${CONTAINER_USER}
WORKDIR /home/${CONTAINER_USER}

# Install rustup
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- \
    --default-toolchain none -y --profile minimal

# Update envs
ENV PATH=${PATH}:/home/${CONTAINER_USER}/.cargo/bin

# Install extra crates
RUN ARCH=$($HOME/.cargo/bin/rustup show | grep "Default host" | sed -e 's/.* //') && \
    curl -L "https://github.com/esp-rs/espup/releases/latest/download/espup-${ARCH}" -o "${HOME}/.cargo/bin/espup" && \
    chmod u+x "${HOME}/.cargo/bin/espup" && \
    curl -L "https://github.com/esp-rs/espflash/releases/latest/download/cargo-espflash-${ARCH}.zip" -o "${HOME}/.cargo/bin/cargo-espflash.zip" && \
    unzip "${HOME}/.cargo/bin/cargo-espflash.zip" -d "${HOME}/.cargo/bin/" && \
    rm "${HOME}/.cargo/bin/cargo-espflash.zip" && \
    chmod u+x "${HOME}/.cargo/bin/cargo-espflash" && \
    curl -L "https://github.com/esp-rs/espflash/releases/latest/download/espflash-${ARCH}.zip" -o "${HOME}/.cargo/bin/espflash.zip" && \
    unzip "${HOME}/.cargo/bin/espflash.zip" -d "${HOME}/.cargo/bin/" && \
    rm "${HOME}/.cargo/bin/espflash.zip" && \
    chmod u+x "${HOME}/.cargo/bin/espflash" && \
    curl -L "https://github.com/esp-rs/embuild/releases/latest/download/ldproxy-${ARCH}.zip" -o "${HOME}/.cargo/bin/ldproxy.zip" && \
    unzip "${HOME}/.cargo/bin/ldproxy.zip" -d "${HOME}/.cargo/bin/" && \
    rm "${HOME}/.cargo/bin/ldproxy.zip" && \
    chmod u+x "${HOME}/.cargo/bin/ldproxy" && \
    curl -L "https://github.com/esp-rs/esp-web-flash-server/releases/latest/download/web-flash-${ARCH}.zip" -o "${HOME}/.cargo/bin/web-flash.zip" && \
    unzip "${HOME}/.cargo/bin/web-flash.zip" -d "${HOME}/.cargo/bin/" && \
    rm "${HOME}/.cargo/bin/web-flash.zip" && \
    chmod u+x "${HOME}/.cargo/bin/web-flash"

# Install Xtensa Rust and ESP-IDF
# espup automatically installs the latest compatible ESP-IDF version
RUN if [ -n "${GITHUB_TOKEN}" ]; then export GITHUB_TOKEN=${GITHUB_TOKEN}; fi  \
    && ${HOME}/.cargo/bin/espup install \
    --targets "${ESP_BOARD}" \
    --log-level debug \
    --export-file /home/${CONTAINER_USER}/export-esp.sh

# Set default toolchain
# For ESP32-S3 (Xtensa) use esp toolchain, for ESP32-P4 (RISC-V) use nightly
# Since we support both, we'll use esp as default (most compatible)
RUN rustup default esp && \
    rustup toolchain install nightly --component rust-src

# Install Python packages for parameter code generation
RUN pip install --no-cache-dir --break-system-packages jsonschema pyyaml gcovr

# Activate ESP environment
RUN echo "source /home/${CONTAINER_USER}/export-esp.sh" >> ~/.bashrc

CMD [ "/bin/bash" ]
