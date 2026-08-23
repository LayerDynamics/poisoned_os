FROM node:22.14.0-bookworm

COPY --from=ghcr.io/astral-sh/uv:0.8.22 /uv /uvx /bin/

RUN apt-get update \
    && apt-get install --no-install-recommends --yes ca-certificates curl git openssl python3 tar xz-utils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

ARG POISON_SOURCE_REPOSITORY=https://github.com/LayerDynamics/poisoned_os.git
ARG POISON_SOURCE_REF=main
RUN git clone --depth 1 --branch "$POISON_SOURCE_REF" "$POISON_SOURCE_REPOSITORY" /repo

RUN corepack enable \
    && corepack prepare pnpm@10.32.1 --activate \
    && pnpm --dir /repo/dashboard install --frozen-lockfile

ENV ARTIFACT_ROOT=/repo \
    ARTIFACT_OUTPUT=/srv
EXPOSE 8080
CMD ["python3", "server.py"]
