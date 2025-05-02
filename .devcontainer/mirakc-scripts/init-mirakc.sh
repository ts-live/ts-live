#!/bin/bash

apt-get update && apt-get install -y --no-install-recommends \
    curl \
    python3 \
    python3-requests \
    python3-yaml

echo "Installing mirakc... [${MIRAKC_URL}]"

export MIRAKC_CONFIG_PATH=/etc/mirakc/config.yml
python3 /scripts/setup-mirakc.py

cat ${MIRAKC_CONFIG_PATH}
mkdir -p /var/lib/mirakc/epg

exec mirakc -c ${MIRAKC_CONFIG_PATH}
