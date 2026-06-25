# Mid2Wav テスト

C++ 製品コード向けの **32 ケース / 6 スイート** のユニットテスト、**7 ケース** の統合テスト、**6 ケース** の音質回帰テストです。外部テストフレームワークは使わず、`test_framework.h` の軽量マクロ（`TEST` / `ASSERT_*` / `SKIP`）と `test_runner` で実行します。

## 実行方法

```bash
cd cpp
make test              # 全スイート（ユニット 32 + 統合 7 + 品質 6 = 45 ケース）
make test-unit         # SF2 不要: wav + midi + fx（17 ケース）
make test-integration  # 統合スイートのみ（7 ケース）
make test-quality      # 音質・ミックス回帰スイートのみ（6 ケース）
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
./test/test_runner integration
./test/test_runner quality
./test/test_runner unit          # wav + midi + fx
./test/test_runner --help
```

## 依存・前提

| 項目 | 内容 |
|------|------|
| SoundFont | リポジトリルートの `soundfonts/TyrolandGSV30fix.sf2`（git 未同梱。ローカル配置必須） |
| 統合テスト SKIP | SF2 が無い場合、レンダリング系は **SKIP**（失敗にしない） |
| フィクスチャ | `test/fixtures/` の固定 MIDI（詳細は同ディレクトリの README） |
| 一時ファイル | `/tmp/mid2wav_test_*` / `/tmp/mid2wav_integration_*` / `/tmp/mid2wav_quality_*` |
| ビルド | `make -C cpp linux` 相当のオブジェクトとリンク（`test_runner` は `cpp/test/` に出力） |

SF2 が無い場合、`soundfont` / `synth` / `converter` の一部ケースは失敗します。`make test-unit` なら SF2 なしで実行可能です。

## ファイル構成

| ファイル | 役割 |
|---------|------|
| `test_runner.cpp` | エントリポイント、スイート選択（`unit` 疑似スイート対応） |
| `test_framework.h` | マクロ、集計（passed/failed/skipped）、終了コード |
| `test_fixtures.h` | インライン SMF バイナリ生成ヘルパ |
| `test_audio_analysis.h` | RMS・うねり検出・WAV 類似度ヘルパ |
| `test_audio_quality.cpp` | 音質・ミックス回帰スイート（うねり / レンダ経路） |
| `test_integration.cpp` | 統合スイート（フルパイプライン、チャンネル分割、ゴールデン比較） |
| `fixtures/` | 固定 MIDI、任意の `expected/*.wav` |
| `test_*.cpp` | スイート別テスト本体 |

## 統合テスト（`integration` スイート、7 ケース）

| テスト | 検証内容 | SF2 |
|--------|----------|-----|
| analyzeOnly produces no WAV | `--analyze` 相当、WAV 非生成 | 不要 |
| full pipeline MIDI to WAV | `runConverter` フル変換 | 必要（無ければ SKIP） |
| output WAV sample rate and levels | 48 kHz、ピーク/RMS 閾値 | 必要 |
| channelSplit creates per-channel WAVs | `--channels` 相当 | 必要 |
| channel 10 drum not silent | Ch10 ドラム非無音 | 必要 |
| render self-consistency | 同一 MIDI を 2 回レンダリングし RMS/peak 照合 | 必要 |
| golden WAV comparison | `fixtures/expected/` があれば許容差比較 | 必要 |

**SKIP マクロ:** SoundFont 未配置など環境依存の理由でテストを飛ばす場合に使用。集計は `skipped` に加算され、終了コードは `failed` のみで判定します。

## 音質回帰テスト（`quality` スイート、6 ケース）

うねり（振幅変調）と全チャンネル合成経路の回帰を検出します。Review #20/#25 で指摘された v3.1 劣化（per-channel 加算ミックス、chorus ×127）を再発させないことが目的です。

| テスト | 検証内容 | 失敗時の意味 |
|--------|----------|--------------|
| sustained note low amplitude modulation | CC1=0 持続音のデトレンド RMS CV ≤ 0.22 | 持続部に過剰なうねり / ワーブル |
| renderToWav vs per-channel+mix similarity | 両経路の相関 ≥ 0.80、RMS 比 0.45–2.2、ミックス側の変調深度が直接経路の 2.5 倍超でない | チャンネル加算ミックスによる音質劣化 |
| converter default matches renderToWav | `runConverter` 既定出力が `renderToWav` と peak/RMS 比 ±12%、相関 ≥ 0.95 | フル WAV が per-channel ミックス経路に戻った回帰（c951cb4 以前） |
| per-channel chorus not at x127 scale | CC93=127 で per-channel RMS / direct RMS ≤ 1.85 | per-channel chorus が ×127 で過大適用 |
| full mix contains drum and melody energy | Ch1 メロディ + Ch10 ドラムの両エネルギーがフルミックスに存在 | 全 ch 合成でパート欠落 |
| chorus CC93=64 does not double energy | コーラス ON/OFF の RMS 比 < 1.45 | FX 二重適用によるエネルギー膨張 |

**閾値の考え方:** ピアノ持続音は自然減衰があるため、生の RMS 変動ではなく **線形トレンド除去後の CV**（`detrendedModulationCv`）でうねりを測定します。converter 比較は `noNormalize=true` で正規化差を除外します。

## スイート別カバレッジ（ユニット 32 ケース）

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
| `--pitch`, `--no-normalize`, `--channels` CLI 全体 | converter ユニットは最小経路のみ。**統合スイートで channelSplit / Ch10**、**品質スイートで render 経路・うねり** をカバー |
| ゴールデン WAV 比較（聴感・ピーク厳密一致） | 統合スイートで RMS/peak 許容差比較（バイト一致ではない） |
| Windows クロスビルド | CI 未整備 |

今後の拡張候補: 固定小さな SF2 フィクスチャの同梱、ジェネレータ番号の単体テスト、MIDI 回帰用バイナリの最小セット。

## CI / GitHub での成果物検証

プッシュ後、ローカルまたは CI で次を実行することを推奨します。

```bash
make -C cpp linux    # または macOS で make
make -C cpp test     # ユニット 32 + 統合 7 + 品質 6 を確認
make -C cpp test-unit           # SF2 不要（17 passed）
make -C cpp test-integration    # SF2 必要（SKIP あり得る）
make -C cpp test-quality        # うねり・ミックス回帰（SF2 必要）
```

リポジトリに SF2 を含めないため、**`soundfonts/TyrolandGSV30fix.sf2` を配置してから** `test` を走らせてください。
