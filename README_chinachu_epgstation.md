# Docker コンテナのおまけ機能: chinachu/EPGStation のリバースプロキシ

[README_Docker_tailscale.md](./README_Docker_tailscale.md) で設定した場合、もう少し設定を追加することで nginx のリバースプロキシに Chinachu/EPGStation を含めることが出来ます。tailscale なので外出先からも予約確認が出来たりして便利です。

## 設定方法

`.env` ファイルに `USE_CHINACHU=1` と記載します。EPGStation の場合は `USE_EPGSTATION=1` と記載します。文字列長さで判定しているので、`USE_CHINACHU=0` としても有効になりますので注意してください。

Chinachu/EPGStation が同一 docker-compose にいない場合やポート番号が標準と違う場合、リバースプロキシのサブディレクトリを別の名前にしたい場合などは下記の設定をします。

```
CHINACHU_HOST=chinachu
CHINACHU_PORT=10772
CHINACHU_PATH=chinachu

EPGSTATION_HOST=epgstation
EPGSTATION_PORT=8888
EPGSTATION_PATH=epgstation
```
