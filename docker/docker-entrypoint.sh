#!/bin/bash

set -e

[ "${CERT_PROVIDER}" != "acme.sh" -a "${CERT_PROVIDER}" != "tailscale" ] && \
  echo "Please select CERT_PROVIDER[${CERT_PROVIDER}] from acme.sh or tailscale" && \
  exit 1

export MIRAKURUN_PORT="${MIRAKURUN_PORT:=40772}"
export NGINX_HTTPS_PORT="${NGINX_HTTPS_PORT:=443}"

if [ "${CERT_PROVIDER}" = "acme.sh" ]; then
  [ -z "${ACCOUNT_EMAIL}" -o -z "${DNSAPI}" ] && \
    echo "Please set ACCOUNT_EMAIL[${ACCOUNT_EMAIL}] and DNSAPI[${DNSAPI}]" && \
    exit 1
elif [ "${CERT_PROVIDER}" = "tailscale" ]; then
  [ -z "${TAILSCALE_HOSTNAME}" -o -z "${TAILSCALE_DOMAINNAME}" -o -z "${TAILSCALE_AUTHKEY}" ] && \
    echo "Please set TAILSCALE_HOSTNAME[${TAILSCALE_HOSTNAME}] TAILSCALE_DOMAINNAME[${TAILSCALE_DOMAINNAME}] and TAILSCALE_AUTHKEY[${TAILSCALE_AUTHKEY}]" && \
    exit 1
  export FQDN=${FQDN:=${TAILSCALE_HOSTNAME}.${TAILSCALE_DOMAINNAME}.ts.net}
fi

[ -z "${FQDN}" -o -z "${MIRAKURUN_HOST}" ] && \
  echo "Please set FQDN[${FQDN}] and MIRAKURUN_HOST[${MIRAKURUN_HOST}] environment variables" && \
  exit 1

# common settings
[ -d /etc/nginx/conf.d ] || mkdir -p /etc/nginx/conf.d

[ -f /etc/nginx/ssl/dhparam.pem ] || \
  openssl dhparam -out /etc/nginx/ssl/dhparam.pem 2048

cp -f /template/nginx/ssl/ssl.conf /etc/nginx/ssl/ssl.conf
cp -f /template/nginx/conf/nginx.conf /etc/nginx/nginx.conf

[ -d /etc/yacron.d ] || mkdir -p /etc/yacron.d

if [ "${CERT_PROVIDER}" = "acme.sh" ]; then
  curl https://raw.githubusercontent.com/acmesh-official/acme.sh/master/acme.sh | \
    sh -s -- --install-online -m ${ACCOUNT_EMAIL} --home /opt/acme.sh --cert-home /etc/nginx/ssl --force

  export CERT_FILE="ssl/${FQDN}/fullchain.cer"
  export KEY_FILE="ssl/${FQDN}/${FQDN}.key"

  [ -d "/etc/nginx/ssl/${FQDN}" ] || mkdir -p "/etc/nginx/ssl/${FQDN}"
  [ -d /etc/nginx/ssl/acme.sh ] || mkdir -p /etc/nginx/ssl/acme.sh

  [ -f /etc/nginx/ssl/acme.sh.conf ] || echo 'CERT_HOME="/etc/nginx/ssl"' > /etc/nginx/ssl/acme.sh.conf

  [ -f "/etc/nginx/ssl/${FQDN}/fullchain.cer" ] || \
    /opt/acme.sh/acme.sh --register-account -m "${ACCOUNT_EMAIL}"

  /opt/acme.sh/acme.sh --issue --dns "${DNSAPI}" -d "${FQDN}" || true
  cp -f /template/ofelia/ofelia_acme.sh.ini /etc/ofelia.ini
  cp -f /template/supervisord_acme.sh.conf /etc/supervisord.conf

elif [ "${CERT_PROVIDER}" = "tailscale" ]; then
  export CERT_FILE="ssl/tailscale.cer"
  export KEY_FILE="ssl/tailscale.key"

  cp -f /template/ofelia/ofelia_tailscale.ini /etc/ofelia.ini
  cp -f /template/supervisord_tailscale.conf /etc/supervisord.conf

  (
    until /usr/bin/tailscale up --authkey ${TAILSCALE_AUTHKEY} --hostname ${TAILSCALE_HOSTNAME} ; do
      sleep 1
    done
    /usr/bin/tailscale cert --cert-file /etc/nginx/ssl/tailscale.cer --key-file /etc/nginx/ssl/tailscale.key ${FQDN}
    /usr/local/bin/supervisorctl start nginx
  ) &
fi

# common setting again
envsubst '${FQDN} ${MIRAKURUN_HOST} ${MIRAKURUN_PORT} ${NGINX_HTTPS_PORT} ${ORIGIN_TRIAL_TOKEN} ${CERT_FILE} ${KEY_FILE}' \
  < /template/nginx/conf/conf.d/ssl.conf \
  > /etc/nginx/conf.d/ssl.conf

# Chinachu/EPGStation settings
# default
export CHINACHU_PATH=${CHINACHU_PATH:=chinachu}
export CHINACHU_HOST=${CHINACHU_HOST:=chinachu}
export CHINACHU_PORT=${CHINACHU_PORT:=10772}

export EPGSTATION_PATH=${EPGSTATION_PATH:=epgstation}
export EPGSTATION_HOST=${EPGSTATION_HOST:=epgstation}
export EPGSTATION_PORT=${EPGSTATION_PORT:=8888}

# nginx include
rm -f /etc/nginx/conf.d/epgstation.conf.inc
rm -f /etc/nginx/conf.d/chinachu.conf.inc

if [ -n "${USE_CHINACHU}" ]; then
  envsubst '${CHINACHU_PATH} ${CHINACHU_HOST} ${CHINACHU_PORT}' \
    < /template/nginx/conf/conf.d/chinachu.conf.inc \
    > /etc/nginx/conf.d/chinachu.conf.inc
fi

if [ -n "${USE_EPGSTATION}" ]; then
  envsubst '${EPGSTATION_PATH} ${EPGSTATION_HOST} ${EPGSTATION_PORT}' \
    < /template/nginx/conf/conf.d/epgstation.conf.inc \
    > /etc/nginx/conf.d/epgstation.conf.inc
fi

exec /usr/local/bin/supervisord --nodaemon -c /etc/supervisord.conf
