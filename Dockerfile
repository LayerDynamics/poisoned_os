FROM node:22.14.0-bookworm AS build

COPY --from=ghcr.io/astral-sh/uv:0.8.22 /uv /uvx /bin/

RUN apt-get update \
    && apt-get install --no-install-recommends --yes ca-certificates curl git openssl python3 tar xz-utils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

ARG POISON_RELEASE_VERSION=1.0.0
ARG POISON_RELEASE_CHANNEL=developer
ARG POISON_RELEASE_KEY_ID
ARG POISON_RELEASE_PRIVATE_KEY_B64

RUN corepack enable \
    && corepack prepare pnpm@10.32.1 --activate \
    && pnpm --dir dashboard install --frozen-lockfile \
    && pnpm --dir web-installer install --frozen-lockfile

RUN marauder_version="$(sed -n 's/.*"version": "\([^"]*\)".*/\1/p' /src/provenance/marauder.lock.json | head -1)" \
    && marauder_url="$(sed -n 's/.*"installerBundleUrl": "\([^"]*\)".*/\1/p' /src/provenance/marauder.lock.json)" \
    && marauder_sha256="$(sed -n 's/.*"installerBundleSha256": "\([^"]*\)".*/\1/p' /src/provenance/marauder.lock.json)" \
    && marauder_path="/src/dist/marauder/${marauder_version}/marauder-installer-assets.zip" \
    && mkdir -p "$(dirname "$marauder_path")" \
    && curl --fail --location --silent --show-error "$marauder_url" --output "$marauder_path" \
    && printf '%s  %s\n' "$marauder_sha256" "$marauder_path" | sha256sum --check --status

RUN FBT_NO_SYNC=1 SOURCE_DATE_EPOCH=0 ./fbt \
    DEBUG=0 COMPACT=1 DIST_SUFFIX=poisonedos \
    UPDATE_VERSION_STRING="$POISON_RELEASE_VERSION" updater_package

RUN pnpm --dir web-installer typecheck \
    && pnpm --dir web-installer test \
    && pnpm --dir web-installer build

RUN printf '%s' "$POISON_RELEASE_PRIVATE_KEY_B64" | base64 --decode >/dev/null \
    && POISON_RELEASE_PRIVATE_KEY_B64="$POISON_RELEASE_PRIVATE_KEY_B64" \
       uv run --no-project --python 3.11 python tools/release/build_web_installer_distribution.py \
         --root /src \
         --package /src/dist/f7-C/flipper-z-f7-update-poisonedos.tgz \
         --output /src/web-installer/dist \
         --version "$POISON_RELEASE_VERSION" \
         --channel "$POISON_RELEASE_CHANNEL" \
         --key-id "$POISON_RELEASE_KEY_ID"

FROM caddy:2.10.2-alpine AS serve

COPY --from=build /src/web-installer/dist /srv
COPY Caddyfile /etc/caddy/Caddyfile
