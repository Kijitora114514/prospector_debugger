# Prospector Debugger

ProspectorをZMK Split Central用の簡易デバッガーとして使用するための、個人用ZMKモジュールです。
XIAO nRF52840を搭載したProspectorで、左右Peripheralの状態、バッテリー残量、
アクティブレイヤーおよび物理キーやBLE接続の異常を画面上で確認できます。

> [!IMPORTANT]
> このモジュールは、**Zephyr 4.1を使用するバージョンのZMKにのみ対応**しています。
> それ以外のZephyr/ZMKバージョンでのビルドや動作は想定していません。

> [!WARNING]
> このリポジトリは作者個人の環境向けです。MITライセンスの範囲内で自由に使用・改変して
> 構いませんが、継続的なメンテナンス、ZMK更新への追従、導入支援、不具合対応などは行いません。
> 各自の責任で使用してください。

## このリポジトリについて

このリポジトリは、carrefinho氏の
[prospector-zmk-module `feat/new-status-screens`ブランチ](https://github.com/carrefinho/prospector-zmk-module/tree/feat/new-status-screens)
をコピーし、Debugger画面と監視機能を追加・変更した派生物です。元リポジトリの公式版ではありません。

元プロジェクトが提供するProspector用ドライバ、画面基盤、およびClassic、Radii、Field、Operatorの
各ステータス画面を含んでいます。この派生版ではDebugger画面をデフォルトにしています。

## 追加・変更した機能

### Debugger画面

- 左側に最大7件のログを表示
- 8件目以降は最も古いログを削除して上詰め
- ログ種別に応じたアイコン表示
  - Message：白
  - Warning：黄
  - Error：赤
  - Connect：白
  - Disconnect：赤
- 右上にPeripheralの接続状態とバッテリー残量を表示
- 右下に現在のレイヤー番号を表示
- 起動確認用として`Good Morning.`を表示

### Peripheral状態とバッテリー表示

- 接続時に`P1 Connect`、切断時に`P1 Disconnect`の形式でログを表示
- バッテリー残量が0～99%の場合はパーセント表示
- 100%の場合はConnectアイコンを表示
- バッテリー残量に応じた既存の色分けを維持

### 物理キーのチャタリング監視

CentralがPeripheralから受信した物理キーのPRESS/RELEASEだけを監視します。
マウスホイール、ロータリーエンコーダ、マクロなどの入力は対象外です。

- 同じキーの`PRESS → RELEASE → PRESS`が30ms以内に発生した回数を記録
- 30ms以内の再押下が1秒間に3回以上発生すると`{キー} Chattering`をWarning表示
- 同じキーの押下が1秒間に10回以上発生した場合もWarning表示
- 検出結果を表示するだけで、キー入力の抑制やデバウンス設定の変更は行わない

### キー押しっぱなし監視

- Peripheralおよび物理キー位置ごとに押下開始時刻を記録
- 30秒以上押下が続くと`{キー} Holding`をWarning表示
- Shift、Ctrl、Alt、GUI、Hold-Tapおよびレイヤー操作キーは対象外
- Peripheral切断時に押下中だったキーを記録
- 再接続後もCentral側に押下状態が残っている場合は`{キー} Holding Down`をError表示
- 検出のみを行い、入力イベントは変更しない

### BLE再接続頻度の監視

Peripheralごとに、直近10分間の予期しない切断回数を最大5件分記録します。

| 切断回数 | 種別 | 表示例 |
| --- | --- | --- |
| 2回 | Message | `P1 Reconnect` |
| 3～4回 | Warning | `P1 Unstable 3` |
| 5回以上 | Error | `P1 Failure 5+` |

監視メッセージは次の再接続時に表示し、その場合は従来の`P1 Connect`を表示しません。
電源OFF、Remote User Termination、Central側からの切断など、正常と判別できる切断は集計対象外です。

## 必要条件

- Prospector
- XIAO nRF52840
- DongleをCentralとして使用するZMK Split構成
- **Zephyr 4.1版のZMK**

他のボード、ディスプレイ、Zephyrバージョンでの動作は保証しません。

## 導入

`config/west.yml`の`remotes`と`projects`に、このモジュールを追加します。
ZMK側のリビジョンは、必ずZephyr 4.1を使用するものを指定してください。

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: kijitora114514
      url-base: https://github.com/Kijitora114514

  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main # Zephyr 4.1を使用している期間のみ対応
      import: app/west.yml
    - name: prospector_debugger
      remote: kijitora114514
      revision: main

  self:
    path: config
```

Dongleの`build.yaml`に`prospector_adapter`シールドを追加します。

```yaml
---
include:
  - board: xiao_ble//zmk
    shield: [YOUR_KEYBOARD_SHIELD]_dongle prospector_adapter
```

## 画面の選択

Debugger画面がデフォルトです。元プロジェクト由来の画面へ変更する場合は、`.conf`にいずれか1つを
設定してください。

```ini
CONFIG_PROSPECTOR_STATUS_SCREEN_CLASSIC=y
# CONFIG_PROSPECTOR_STATUS_SCREEN_RADII=y
# CONFIG_PROSPECTOR_STATUS_SCREEN_FIELD=y
# CONFIG_PROSPECTOR_STATUS_SCREEN_OPERATOR=y
```

Debugger画面を明示的に選択する場合は次の設定を使用します。

```ini
CONFIG_PROSPECTOR_STATUS_SCREEN_DEBUGGER=y
```

## Peripheralのペアリング順

Peripheral番号はペアリング順です。ファームウェアを書き込んだ後、左側を先に、右側を後に
ペアリングしてください。3台以上の場合も左から順にペアリングします。

## 主な設定

設定はキーボードの`.conf`に追加します。

```ini
CONFIG_PROSPECTOR_USE_AMBIENT_LIGHT_SENSOR=n
CONFIG_PROSPECTOR_FIXED_BRIGHTNESS=80
```

| 設定 | 内容 | デフォルト |
| --- | --- | --- |
| `CONFIG_PROSPECTOR_ROTATE_DISPLAY_180` | ディスプレイを180度回転 | `n` |
| `CONFIG_PROSPECTOR_USE_AMBIENT_LIGHT_SENSOR` | 環境光センサーによる自動輝度調整 | `y` |
| `CONFIG_PROSPECTOR_FIXED_BRIGHTNESS` | 自動輝度を無効にした場合の固定輝度 | `50` |
| `CONFIG_PROSPECTOR_STATUS_SCREEN_DEBUGGER` | Debugger画面を使用 | デフォルト選択 |

## ビルド容量について

XIAO nRF52840では、設定や追加機能によってRAMまたはFlash容量が不足する場合があります。
RAM overflowが発生する場合は、必要に応じてディスプレイバッファを小さくしてください。

```ini
CONFIG_LV_Z_VDB_SIZE=25
```

この設定で必ず解消するとは限りません。不要なZMK機能やウィジェットの削減が必要になる場合があります。

## 既知の制限

- ログは画面表示のみで、保存や外部出力は行わない
- ログは最大7件で、再起動すると消去される
- キー名を解決できない場合は物理キー位置を`Kxx`形式で表示する
- 監視処理は異常を検出・表示するだけで、入力動作を修正しない
- Zephyr 4.1以外には対応しない
- 今後のZMK API変更への追従予定はない

## サポート方針

自由に利用して構いませんが、このリポジトリは個人用であり、一般向け製品や正式な配布物ではありません。
Issue、質問、環境ごとのビルドエラー、機能追加依頼、ZMK最新版への対応依頼には原則として対応しません。
フォークして各自で調整してください。

## ライセンスと謝辞

MIT Licenseです。詳細は[LICENSE](LICENSE)を参照してください。

この派生版は、carrefinho氏の
[Prospector ZMK Module](https://github.com/carrefinho/prospector-zmk-module/tree/feat/new-status-screens)
を基にしています。元プロジェクトおよびコントリビューターに感謝します。
