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

## Origin Trial キー取得方法

[Chrome の Origin Trials ページ](https://developer.chrome.com/origintrials/#/trials/active) から `WebGPU` の REGISTER ボタンを押し、Web Origin にhttps://hostname:port を入力、Notices と Terms を良く読んで REGISTER ボタンを押して登録します。登録後の画面で出てくる `Token` を web-ts-player の設定に入力します。

## Docker イメージ

CI で ghcr に自動アップロードされるようになってます。利用方法の詳細は [README_Docker.md](./README_Docker.md) を参照してください。
