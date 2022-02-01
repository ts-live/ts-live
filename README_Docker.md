# TS-Live を Docker で使う方法

TS-Live の Docker コンテナは、TS-Live の html/js/wasm ファイルを https で配信すると共に、/api 以下を別ホストの mirakurun にリバースプロキシする nginx を起動します。

このコンテナではHTTPSの証明書にいわゆるオレオレ証明書ではなく、きちんと有効な証明書を取得できるようにしてあります。証明書の取得は [acme.sh](https://acme.sh) を使っているので自動で取得＆更新されます。

有効な HTTPS 証明書を取得するためにドメイン名が必要になります。無料で済まそうとすると利用できる DNS プロバイダが限られてしまいますが、少なくとも [DuckDNS](https://duckdns.org) では無料でサブドメインを取得することができます。

acme.sh は DuckDNS 以外にも AWS Route53、CloudFlare Domain、Conohaなどの多くの DNS プロバイダに対応しています。詳しくは https://github.com/acmesh-official/acme.sh/wiki/dnsapi を参照してください。

下記では DuckDNS を使用するときの手順を説明します。

## ホスト名の準備

### DuckDNS へのログイン

[DuckDNS](https://duckdns.org) は Persona(知らない)、Twitter、GitHub、Google アカウントでの認証に対応しています。いずれも基本的な情報の読み取り権限だけしか要求してこないので、それほど心配することはないと思います。

ログインすると token が表示されているはずです。この値は後で設定に使います。

### ホスト名/IPアドレスの登録

画面の中央あたりに https:// sub domain .duckdns.org add domain という感じのフォームがあります。ここの `sub domain` に好きなホスト名を入れて `add domain` ボタンを押して、ホスト名を登録します（当たり前ですが他の人が使っていると登録できません）。

登録すると、その下にドメインの一覧が出ます。current ipの項目が登録したときのグローバルIPになっていますが、ここにこれから設定するサーバのIPアドレスを入れます。`127.0.0.1` だろうと `192.168.0.1` だろうと構いません。自分の普段使いのクライアントから到達可能な mirakurun を動かすホストのIPアドレスを入れてください。


## 環境変数ファイルの設定

認証情報やドメイン名などを環境変数ファイルで与えます。[docker_env_sample](./docker_env_sample)ファイルを参考に設定してください。
以下の説明では設定したファイルを `.env` というファイル名にしています。今回、DuckDNSでmirakurunサブドメインを登録したので、下記のように設定します。

```.env
FQDN=mirakurun.duckdns.org
MIRAKURUN_HOST=mirakurun
MIRAKURUN_PORT=40772
NGINX_HTTPS_PORT=443
ACCOUNT_EMAIL=my_email_address@example.com
DuckDNS_Token=XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX
ACCOUNT_CONF_PATH=/etc/nginx/ssl/acme.sh.conf
DNSAPI=dns_duckdns
```

## volume としてホスト・コンテナで共有するディレクトリの準備

docker コマンド、docker-compose コマンドどちらの例も、カレントディレクトリに `nginx` ディレクトリを作成し、その下に `ssl` ディレクトリと `logs` ディレクトリを作成します。


## docker コマンドで直接起動する場合

以下のコマンドで起動します。

```
docker run \
    -d \
    -p 443:443 \
    --env-file .env \
    -v /etc/timezone:/etc/timezone:ro \
    -v /etc/localtime:/etc/localtime:ro \
    -v $(pwd)/nginx/ssl:/etc/nginx/ssl:rw \
    -v $(pwd)/nginx/logs:/var/log/nginx:rw \
    ghcr.io/kounoike/ts-live:latest
```


## docker-compose に追加する場合

以下の設定を `docker-compose.yml` の `services` セクションに追加して、`docker-compose` コマンドで起動します。

```
    ts-live:
        image: ghcr.io/kounoike/ts-live:main
        env_file: .env
        volumes:
            - /etc/timezone:/etc/timezone:ro
            - /etc/localtime:/etc/localtime:ro
            - ./nginx/ssl:/etc/nginx/ssl:rw
            - ./nginx/logs:/var/log/nginx:rw
        links:
            - mirakurun:mirakurun
        ports:
            - "443:443"
        restart: always
```

