FROM --platform=$BUILDPLATFORM node:16.12.0-bullseye-slim AS next-build

WORKDIR /app
COPY package.json yarn.lock ./
RUN yarn

COPY next-env.d.ts next.config.js tsconfig.json ./
COPY ./src ./src
COPY ./public ./public
RUN yarn build
RUN yarn export

FROM --platform=$BUILDPLATFORM golang:1.17.7-bullseye AS go-build
ARG TARGETOS TARGETARCH
WORKDIR /app/ofelia
RUN curl -fsSL https://github.com/mcuadros/ofelia/archive/refs/tags/v0.3.6.tar.gz \
  | tar xzvpf - --strip-components=1
RUN GOOS=$TARGETOS GOARCH=$TARGETARCH \
  go build \
  -o /usr/local/bin/ofelia

# WORKDIR /app/supervisord
# RUN curl -fsSL https://github.com/ochinchina/supervisord/archive/refs/tags/v0.7.3.tar.gz \
#   | tar xzvpf - --strip-components=1
# RUN go get github.com/UnnoTed/fileb0x && go generate
# RUN GOOS=$TARGETOS GOARCH=$TARGETARCH \
#   go build \
#   -tags release \
#   -o /usr/local/bin/supervisord

FROM nginx:1.21 AS runner

ENV DEBIAN_FRONTEND=noninteractive \
  LE_WORKING_DIR=/opt/acme.sh

RUN set -ex && \
  apt-get update && \
  apt-get install --yes --no-install-recommends \
  ca-certificates \
  curl \
  jq \
  git \
  gnupg \
  openssl \
  python3 \
  python3-pip \
  && \
  pip3 install supervisor && \
  curl -fsSL https://tailscale.com/install.sh | sh && \
  apt-get clean && rm -rf /var/lib/apt/lists/* /var/log/apt /var/log/dpkg.log

RUN mv /etc/nginx/conf.d/default.conf /etc/nginx/conf.d/default.conf.orig

COPY docker/docker-entrypoint.sh /
RUN chmod +x /docker-entrypoint.sh

COPY docker/template /template
COPY --from=next-build /app/out /www
COPY --from=go-build /usr/local/bin/ofelia /usr/local/bin/

ENTRYPOINT ["/docker-entrypoint.sh"]

