location /${CHINACHU_PATH}/ {
    proxy_pass http://${CHINACHU_HOST}:${CHINACHU_PORT}/;
    proxy_set_header Host ${CHINACHU_HOST};
    proxy_set_header Upgrade $http_upgrade;

}
