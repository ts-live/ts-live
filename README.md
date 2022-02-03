# TS-Live!

ブラウザで mirakurun の MPEGTS ストリームを直接再生する実験的 Web アプリ

## ライセンス

MIT-License（内部で利用している ffmpeg は LGPL です。他にも多数のライブラリを使っているので個別に確認してください）

## run

```
$ yarn
$ yarn dev
```

3000 番ポートで起動したことをメッセージで確認すること（3000 じゃないときはどこかで別のアプリが 3000 を開いてます）。

## 操作方法

なんかそれっぽいところに mirakurun のサーバの URL（http://mirakurun:40772 みたいに）を入れると自動的にサービス一覧を取得するので選ぶだけ。

もしも暴走したっぽいときはタブを閉じて別タブで URL 入れ直すのが良いです（WebAssembly が暴れてるときの一般的対処）。

## 制限事項

- WebAssembly thread を使う（=SharedArrayBuffer を使う）関係で Secure Context 必須
- mixed content の関係で mirakurun サーバと http/https 混在は出来ない
- [WebGPU](https://chromestatus.com/feature/6213121689518080) を使っているため、localhost:3000 以外で起動するには Origin Trials キーの取得・設定が必要

上記の上 2 つの制約から、「http://localhostでアクセスしてmirakurunにhttp接続」または「httpsサーバに接続してmirakurunにもhttps接続」のどちらかでないと動きません。localhost:3000 以外（https 接続する場合）では Origin Trials キーを取得＆設定するか、ブラウザ起動時にコマンドライン引数をつける必要があります。

## ファイル再生機能

開発を楽にする目的なのであまり利便性とか力を入れてません。

## TS-Live を Docker で使う方法

TS-Live の Docker コンテナは、TS-Live の html/js/wasm ファイルを https で配信すると共に、/api 以下を別ホストの mirakurun にリバースプロキシする nginx を起動します。

このコンテナでは HTTPS の証明書にいわゆるオレオレ証明書ではなく、きちんと有効な証明書を取得できるようにしてあります。証明書の取得は [acme.sh](https://acme.sh) または [tailscale](https://tailscale.com/) を使っていて、どちらも自動で取得＆更新できるスクリプトが組み込んであります。

acme.sh を利用する場合は有効な HTTPS 証明書を取得するためにドメイン名が必要になります。無料で済まそうとすると利用できる DNS プロバイダが限られてしまいますが、少なくとも [DuckDNS](https://duckdns.org) では無料でサブドメインを取得することができます。

acme.sh は DuckDNS 以外にも AWS Route53、CloudFlare Domain、Conoha などの多くの DNS プロバイダに対応しています。詳しくは https://github.com/acmesh-official/acme.sh/wiki/dnsapi を参照してください。

acme.sh を利用する場合は [README_Docker_acme.md](./README_Docker_acme.md) を、tailscale を利用する場合は [README_tailscale.md](README_tailscale.md) を参照してください。


