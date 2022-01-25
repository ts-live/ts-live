# web-ts-player

ブラウザで mirakurun の MPEGTS ストリームを直接再生する実験的 Web アプリ

## LICENSE

MIT-License（内部で利用している ffmpeg は LGPL です）

## run

```
$ yarn
$ yarn dev
```

起動するポートはメッセージを参照

## 操作方法

なんかそれっぽいところに mirakurun のサーバの URL（http://mirakurun:40772 みたいに）を入れると自動的にサービス一覧を取得するので選ぶだけ。

もしも暴走したっぽいときはタブを閉じて別タブで URL 入れ直すのが良いです（WebAssembly が暴れてるときの一般的対処）。

## 制限事項

- WebAssembly thread を使う（=SharedArrayBuffer を使う）関係で Secure Context 必須
- mixed content の関係で mirakurun サーバと http/https 混在は出来ない

上記 2 制約から、「http://localhostでアクセスしてmirakurunにhttp接続」または「httpsサーバに接続してmirakurunにもhttps接続」のどちらかでないと動きません。

## ファイル再生機能

開発目的なのであまり利便性とか力を入れてません。
