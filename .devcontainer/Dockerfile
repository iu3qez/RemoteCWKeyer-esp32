ARG DOCKER_TAG=v5.5.1
FROM espressif/idf:${DOCKER_TAG}

ENV LC_ALL=C.UTF-8
ENV LANG=C.UTF-8

RUN apt-get update -y && apt-get install udev curl -y 

# Install Node.js 18.x (LTS)
RUN curl -fsSL https://deb.nodesource.com/setup_18.x | bash - \
    && apt-get install -y nodejs \
	&& apt-get install -y vim \
	&& apt-get install -y bash-completion 
# Install Python packages for parameter code generation (Feature 4)
# - jsonschema: Validate parameters.yaml against JSON Schema
# - pyyaml: Already included in ESP-IDF, but listed here for clarity
#RUN pip install --no-cache-dir jsonschema

ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=${USER_UID}

RUN if getent group "${USER_GID}" > /dev/null; then \
			existing_group="$(getent group "${USER_GID}" | cut -d: -f1)"; \
			if [ "${existing_group}" != "${USERNAME}" ]; then \
				groupmod -n "${USERNAME}" "${existing_group}"; \
			fi; \
		else \
			groupadd --gid "${USER_GID}" "${USERNAME}"; \
		fi \
	&& if getent passwd "${USER_UID}" > /dev/null; then \
			existing_user="$(getent passwd "${USER_UID}" | cut -d: -f1)"; \
			existing_home="$(getent passwd "${USER_UID}" | cut -d: -f6)"; \
			if [ "${existing_user}" != "${USERNAME}" ]; then \
				usermod -l "${USERNAME}" "${existing_user}"; \
			fi; \
			if [ "${existing_home}" != "/home/${USERNAME}" ]; then \
				usermod -d "/home/${USERNAME}" -m "${USERNAME}"; \
			fi; \
			usermod -g "${USER_GID}" "${USERNAME}"; \
		else \
			useradd --uid "${USER_UID}" --gid "${USER_GID}" -m -s /bin/bash "${USERNAME}"; \
		fi \
	&& usermod -aG dialout "${USERNAME}"

RUN echo "source /opt/esp/idf/export.sh > /dev/null 2>&1" >> ~/.bashrc
RUN echo "source /opt/esp/idf/export.sh > /dev/null 2>&1" >> /home/${USERNAME}/.bashrc

RUN mkdir -p \
	/home/${USERNAME}/.vscode-server/bin \
	/home/${USERNAME}/.vscode-server/extensions \
	/home/${USERNAME}/.vscode-server/data/User \
	/home/${USERNAME}/.vscode-server/data/Machine \
	&& chown -R ${USERNAME}:${USERNAME} /home/${USERNAME}/.vscode-server
RUN /opt/esp/python_env/idf5.5_py3.12_env/bin/pip  install --no-cache-dir  jsonschema 
RUN /opt/esp/python_env/idf5.5_py3.12_env/bin/pip  install --no-cache-dir  gcovr

USER ${USERNAME}

ENTRYPOINT [ "/opt/esp/entrypoint.sh" ]

CMD ["/bin/bash", "-c"]
