FROM node:16-bullseye-slim AS next-build

WORKDIR /app
COPY package.json yarn.lock ./
RUN yarn

COPY next-env.d.ts next.config.js tsconfig.json ./
COPY ./src ./src
COPY ./public ./public
RUN yarn build
RUN yarn export

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
  pip3 install yacron && \
  curl -fsSL https://tailscale.com/install.sh | sh && \
  apt-get clean && rm -rf /var/lib/apt/lists/* /var/log/apt /var/log/dpkg.log

RUN mv /etc/nginx/conf.d/default.conf /etc/nginx/conf.d/default.conf.orig

COPY docker/docker-entrypoint.sh /
RUN chmod +x /docker-entrypoint.sh

COPY docker/template /template
COPY --from=next-build /app/out /www

ENTRYPOINT ["/docker-entrypoint.sh"]

