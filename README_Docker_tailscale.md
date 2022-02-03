# tailscale を利用した場合の Docker コンテナ設定方法

## アカウントの準備

### tailscale へのログイン、クライアントPCへの tailscale アプリの導入

[tailscale](https://tailscale.com/) にアクセスしてアカウントを作成し、画面に表示される手順に従って、普段利用するクライアントPCに tailscale アプリをインストールします。

### tailscale の認証キーの取得

[tailscale](https://tailscale.com/) に Webブラウザでアクセスすると管理コンソールの画面になります。
「Setting」メニューから Personal Settings > Keys を選んで、 Auth keys の Generate auth key... を選択します。

### MagicDNS の有効化

「DNS」メニューから Nameservers の Add nameserver を選択し、8.8.8.8 などの public DNS を設定します。その後 Enable MagicDNS のボタンを押して有効化します。

### HTTPS の有効化

「Setting」メニューから Tailnet Settings > Feature Previews を選んで、 HTTPS Beta の Enable HTTPS... を選択します。
画面に表示される内容に従って設定を進めて有効化します。途中でサブドメイン名の選択がありますが、後の設定でも使うので覚えておいてください。


## Chrome Origin Trial トークンの取得

[Chrome Origin Trials > WebGPU](https://developer.chrome.com/origintrials/#/view_trial/118219490218475521) にアクセスし、右上のサインインボタンから Google アカウントでログインしてください。
REGISTER ボタンを押して登録画面に移行し、Web Origin に 上記で登録したホスト名を `https://<FQDN>` の形式で入力してください。例えば上で取得したサブドメインが `skunk-oister` で、この後設定する `TAILSCALE_HOSTNAME` が `ts-live` なら `https://ts-live.skunk-oister.ts.net` となります。"I need a token to match all subdomains of the origin" はチェックしないままで OK です。Expected Usage は 0 to 10,000 でいいでしょう。更に下の 4 つの Terms の項目を読んで理解して、チェックボックスにチェックを入れて REGISTER ボタンを押せば登録完了です。Token が発行されますので、コピーして設定ファイルに入力します。Origin Trials Token には期限もあります（期限来たときの処理は良く知らない・・・）。

## 環境変数ファイルの設定

認証情報やドメイン名などを環境変数ファイルで与えます。[docker_env_sample](./docker_env_sample)ファイルを参考に設定してください。
以下の説明では設定したファイルを `.env` というファイル名にしています。

```.env
CERT_PROVIDER=tailscale
TAILSCALE_HOSTNAME=ts-live
TAILSCALE_DOMAINNAME=skunk-oister
TAILSCALE_AUTHKEY=tskey-XXXXXXXXXXXX-XXXXYXXXXXXXXXXXXXXX
MIRAKURUN_HOST=mirakurun
MIRAKURUN_PORT=40772
NGINX_HTTPS_PORT=443
ORIGIN_TRIAL_TOKEN=Amu7sW/oEH3ZqF6SQcPOYVpF9KYNHShFxN1GzM5DY0QW6NwGnbe2kE/YyeQdkSD+kZWhmRnUwQT85zvOA5WYfgAAAABJeyJvcmlnaW4iOiJodHRwOi8vbG9jYWxob3N0OjMwMDAiLCJmZWF0dXJlIjoiV2ViR1BVIiwiZXhwaXJ5IjoxNjUyODMxOTk5fQ==
```

## volume としてホスト・コンテナで共有するディレクトリの準備

docker コマンド、docker-compose コマンドどちらの例も、カレントディレクトリに `nginx` ディレクトリを作成し、その下に `ssl` ディレクトリと `logs` ディレクトリを作成します。また、tailscale の設定保存のために `tailscale` ディレクトリを作成します。

## docker コマンドで直接起動する場合

以下のコマンドで起動します。HTTPS の 443 ポートには tailscale 経由でアクセスするので、`-p` オプションは必要ないです。

```
docker run \
    -d \
    --env-file .env \
    -v /etc/timezone:/etc/timezone:ro \
    -v /etc/localtime:/etc/localtime:ro \
    -v $(pwd)/nginx/ssl:/etc/nginx/ssl:rw \
    -v $(pwd)/nginx/logs:/var/log/nginx:rw \
    -v $(pwd)/tailscale:/var/lib/tailscale:rw \
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
            - ./tailscale:/var/lib/tailscale:rw
        links:
            - mirakurun:mirakurun
        restart: always
```
