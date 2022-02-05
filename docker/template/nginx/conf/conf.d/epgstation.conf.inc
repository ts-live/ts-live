location /${EPGSTATION_PATH}/ {
    proxy_pass http://${EPGSTATION_HOST}:${EPGSTATION_PORT};
}
