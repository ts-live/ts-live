#!/bin/bash

set -e

[ -z "${FQDN}" -o -z "${MIRAKURUN_HOST}" ] && \
  echo "Please set FQDN[${FQDN}] and MIRAKURUN_HOST[${MIRAKURUN_HOST}] environment variables" && \
  exit 1

export MIRAKURUN_PORT="${MIRAKURUN_PORT:=40772}"
export NGINX_HTTPS_PORT="${NGINX_HTTPS_PORT:=443}"

[ -z "${ACCOUNT_EMAIL}" -o -z "${DNSAPI}" ] && \
  echo "Please set ACCOUNT_EMAIL[${ACCOUNT_EMAIL}] and DNSAPI[${DNSAPI}]" && \
  exit 1

[ -d /etc/nginx/conf.d ] || mkdir -p /etc/nginx/conf.d
[ -d /etc/nginx/ssl/acme.sh ] || mkdir -p /etc/nginx/ssl/acme.sh

[ -f /etc/nginx/ssl/dhparam.pem ] || \
  openssl dhparam -out /etc/nginx/ssl/dhparam.pem 2048

cp -f /nginx-template/conf/nginx.conf /etc/nginx/nginx.conf
envsubst '${FQDN} ${MIRAKURUN_HOST} ${MIRAKURUN_PORT} ${NGINX_HTTPS_PORT} ${ORIGIN_TRIAL_TOKEN}' < /nginx-template/conf/conf.d/ssl.conf > /etc/nginx/conf.d/ssl.conf

cp -f /nginx-template/ssl/ssl.conf /etc/nginx/ssl/ssl.conf

[ -d "/etc/nginx/ssl/${FQDN}" ] || mkdir -p "/etc/nginx/ssl/${FQDN}"

[ -f /etc/nginx/ssl/acme.sh.conf ] || echo 'CERT_HOME="/etc/nginx/ssl"' > /etc/nginx/ssl/acme.sh.conf

[ -f "/etc/nginx/ssl/${FQDN}/fullchain.cer" ] || \
  acme.sh --register-account -m "${ACCOUNT_EMAIL}"

acme.sh --issue --dns "${DNSAPI}" -d "${FQDN}" || true

busybox crond -b -L /var/log/crond.log
nginx -g "daemon off;"
