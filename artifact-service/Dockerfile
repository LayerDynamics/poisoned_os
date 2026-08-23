FROM node:22.14.0-bookworm

COPY --from=ghcr.io/astral-sh/uv:0.8.22 /uv /uvx /bin/

RUN apt-get update \
    && apt-get install --no-install-recommends --yes ca-certificates curl git openssl python3 tar xz-utils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN corepack enable \
    && corepack prepare pnpm@10.32.1 --activate \
    && pnpm --dir dashboard install --frozen-lockfile

ENV ARTIFACT_ROOT=/src \
    ARTIFACT_OUTPUT=/srv
EXPOSE 8080
CMD ["python3", "artifact-service/server.py"]
