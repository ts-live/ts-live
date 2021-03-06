# acme.sh を利用した場合の Docker コンテナ設定方法

## ホスト名の準備

### DuckDNS へのログイン

[DuckDNS](https://duckdns.org) は Persona(知らない)、Twitter、GitHub、Google アカウントでの認証に対応しています。いずれも基本的な情報の読み取り権限だけしか要求してこないので、それほど心配することはないと思います。

ログインすると token が表示されているはずです。この値は後で設定に使います。

### ホスト名/IP アドレスの登録

画面の中央あたりに https:// sub domain .duckdns.org add domain という感じのフォームがあります。ここの `sub domain` に好きなホスト名を入れて `add domain` ボタンを押して、ホスト名を登録します（当たり前ですが他の人が使っていると登録できません）。

登録すると、その下にドメインの一覧が出ます。current ip の項目が登録したときのグローバル IP になっていますが、ここにこれから設定するサーバの IP アドレスを入れます。`127.0.0.1` だろうと `192.168.0.1` だろうと構いません。自分の普段使いのクライアントから到達可能な mirakurun を動かすホストの IP アドレスを入れてください。

## Chrome Origin Trial トークンの取得

[Chrome Origin Trials > WebGPU](https://developer.chrome.com/origintrials/#/view_trial/118219490218475521) にアクセスし、右上のサインインボタンから Google アカウントでログインしてください。
REGISTER ボタンを押して登録画面に移行し、Web Origin に 上記で登録したホスト名を `https://<FQDN>` の形式で入力してください。例えば mirakurun.duckdns.org を取得したなら、`https://mirakurun.duckdns.org` です。"I need a token to match all subdomains of the origin" はチェックしないままで OK です。Expected Usage は 0 to 10,000 でいいでしょう。更に下の 4 つの Terms の項目を読んで理解して、チェックボックスにチェックを入れて REGISTER ボタンを押せば登録完了です。Token が発行されますので、コピーして設定ファイルに入力します。Origin Trials Token には期限もあります（期限来たときの処理は良く知らない・・・）。

## 環境変数ファイルの設定

認証情報やドメイン名などを環境変数ファイルで与えます。[docker_env_sample](./docker_env_sample)ファイルを参考に設定してください。
以下の説明では設定したファイルを `.env` というファイル名にしています。今回、DuckDNS で mirakurun サブドメインを登録したので、下記のように設定します。
（下記では説明のために開発用の localhost 対象の Origin Trials Token を設定しています）

```.env
CERT_PROVIDER=acme.sh
FQDN=mirakurun.duckdns.org
ORIGIN_TRIAL_TOKEN=Amu7sW/oEH3ZqF6SQcPOYVpF9KYNHShFxN1GzM5DY0QW6NwGnbe2kE/YyeQdkSD+kZWhmRnUwQT85zvOA5WYfgAAAABJeyJvcmlnaW4iOiJodHRwOi8vbG9jYWxob3N0OjMwMDAiLCJmZWF0dXJlIjoiV2ViR1BVIiwiZXhwaXJ5IjoxNjUyODMxOTk5fQ==
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
