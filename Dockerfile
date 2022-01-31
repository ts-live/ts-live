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
  apt-get install --yes --no-install-recommends ca-certificates cron curl openssl busybox-static && \
  curl -sSL https://get.acme.sh | sh && \
  mkdir -p /opt/acme.sh/ca/acme-v01.api.letsencrypt.org && \
  crontab -l | sed "s|acme.sh --cron|acme.sh --cron --renew-hook \"nginx -s reload\"|g" | crontab - && \
  ln -s /opt/acme.sh/acme.sh /usr/bin/acme.sh && \
  apt-get clean && rm -rf /var/lib/apt/lists/* /var/log/apt /var/log/dpkg.log

COPY docker/docker-entrypoint.sh /
RUN chmod +x /docker-entrypoint.sh

COPY docker/nginx-template /nginx-template
COPY --from=next-build /app/out /www

ENTRYPOINT ["/docker-entrypoint.sh"]

