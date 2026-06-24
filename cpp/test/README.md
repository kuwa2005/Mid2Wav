# Mid2Wav ユニットテスト

C++ 製品コード向けの **32 ケース / 6 スイート** のユニットテストです。外部テストフレームワークは使わず、`test_framework.h` の軽量マクロ（`TEST` / `ASSERT_*`）と `test_runner` で実行します。

## 実行方法

```bash
cd cpp
make test              # 全スイート（32 ケース）
make test-wav          # wav のみ
make test-midi
make test-soundfont
make test-synth
make test-fx
make test-converter
```

スイートを絞る場合:

```bash
cd cpp
./test/test_runner midi synth
./test/test_runner --help
```

## 依存・前提

| 項目 | 内容 |
|------|------|
| SoundFont | リポジトリルートの `soundfonts/TyrolandGSV30fix.sf2`（git 未同梱。ローカル配置必須） |
| 一時ファイル | `/tmp/mid2wav_test_*`（converter スイートが MID/WAV を生成・削除） |
| ビルド | `make -C cpp linux` 相当のオブジェクトとリンク（`test_runner` は `cpp/test/` に出力） |

SF2 が無い場合、`soundfont` / `synth` / `converter` の一部ケースはスキップまたは失敗します。

## ファイル構成

| ファイル | 役割 |
|---------|------|
| `test_runner.cpp` | エントリポイント、スイート選択 |
| `test_framework.h` | マクロ、集計、終了コード |
| `test_fixtures.h` | インライン SMF バイナリ生成ヘルパ |
| `test_*.cpp` | スイート別テスト本体 |

## スイート別カバレッジ（全 32 ケース）

### `wav`（3）

| テスト | 検証内容 |
|--------|----------|
| WavReader/WavWriter round-trip | 16bit PCM ステレオの書き込み→読み込み一致 |
| WavWriter rejects empty buffers | 空バッファの拒否 |
| mixFromChannelWavs | チャンネル別 WAV のミックスダウン |

**対象コード:** `wav_file.cpp`

### `midi`（8）

| テスト | 検証内容 |
|--------|----------|
| Parse simple note on/off | ノート on/off、ノート数・長さ |
| Tempo meta event (90 BPM) | メタイベント 0x51、テンポマップ（tick 0 の重複更新含む） |
| CC bank select MSB/LSB | CC0/CC32 と 14bit bank |
| GS SysEx reverb level | Roland GS DT1 リバーブレベル |
| GS SysEx part mode (Ch10 melody) | Ch10 メロディモードと bank 挙動 |
| MidiExpression::getValueAtTick | 表現 CC の tick 補間 |
| timeToTick / tickToSeconds round-trip | 時間↔tick 変換 |
| analyzeTracks drum channel | 解析 API のドラムチャンネル検出 |

**対象コード:** `midi_file.cpp`, `midi_expression` 関連

### `soundfont`（4）

| テスト | 検証内容 |
|--------|----------|
| Load TyrolandGSV30fix.sf2 | RIFF 読み込み、プリセット/サンプル数 |
| findPreset GM piano and drum kit | bank/program によるプリセット解決 |
| Preset/instrument generators present | ジェネレータ列の存在 |
| Reject invalid SF2 | 不正ファイルの拒否 |

**対象コード:** `soundfont.cpp`

### `synth`（8）

| テスト | 検証内容 |
|--------|----------|
| Drum rendering (bank=128) | ドラム bank のレンダリングと非無音 |
| Melody rendering (bank=0) | メロディプリセットとゾーン解決 |
| Note on/off envelope | エンベロープ減衰 |
| 25 simultaneous voices | ポリフォニー / ボイス割当 |
| Channel 10 drum via MIDI | Ch10 既定ドラム、`effectiveBankMSB=128` |
| Channel 10 melody mode via MIDI render | GS Part Mode 後のメロディレンダ |
| Reverb CC91 via MIDI render | CC91 有無で出力エネルギー差 |
| Fallback sine mode | SF2 無し時のサイン波フォールバック |

**対象コード:** `sf_synth.cpp`, `midi_render` 経路

### `fx`（6）

| テスト | 検証内容 |
|--------|----------|
| SimpleReverb changes signal | リバーブで信号変化 |
| SimpleReverb drumMode | ドラム用リバーブモード |
| Chorus changes signal | コーラス ON で変化 |
| Chorus bypass at CC=0 | CC=0 でバイパス |
| Delay produces echo | ディレイエコー |
| Delay bypass at mix=0 | mix=0 でバイパス |

**対象コード:** `fx_reverb.cpp`, `fx_chorus.cpp`, `fx_delay.cpp`（相当モジュール）

### `converter`（3）

| テスト | 検証内容 |
|--------|----------|
| formatAnalysisText output | 解析テキスト整形 |
| runConverter analyzeOnly | `--analyze` 相当、WAV 非生成 |
| runConverter full render | フル変換、出力 WAV 存在 |

**対象コード:** `converter.cpp`

## カバレッジの限界（未テスト・弱い領域）

自動テストは **回帰防止と主要パスのスモーク** が目的です。以下はまだ十分にカバーしていません（`doc/ai/README.md` のギャップ一覧と対応）。

| 領域 | 状態 |
|------|------|
| SF2 `pmod` / `imod`、default modulator カーブ | 未テスト（未実装に近い） |
| `sm24` / ステレオ sample pair | 未テスト |
| NRPN / アフタータッチ / XG SysEx | 未テスト |
| `--pitch`, `--no-normalize`, `--channels` CLI 全体 | converter は最小経路のみ |
| ゴールデン WAV 比較（聴感・ピーク厳密一致） | なし（エネルギー閾値のみ） |
| Windows クロスビルド | CI 未整備 |

今後の拡張候補: 固定小さな SF2 フィクスチャの同梱、ジェネレータ番号の単体テスト、MIDI 回帰用バイナリの最小セット。

## CI / GitHub での成果物検証

プッシュ後、ローカルまたは CI で次を実行することを推奨します。

```bash
make -C cpp linux    # または macOS で make
make -C cpp test     # 32 passed を確認
```

リポジトリに SF2 を含めないため、**`soundfonts/TyrolandGSV30fix.sf2` を配置してから** `test` を走らせてください。
