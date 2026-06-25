# 統合テスト用フィクスチャ

`integration` スイートが参照する固定 MIDI と（任意の）ゴールデン WAV です。

## MIDI ファイル

| ファイル | 内容 |
|---------|------|
| `simple_note.mid` | Ch1（MIDI ch0）で C4 単音（vel 100、約 1 拍） |
| `ch10_drum.mid` | Ch10（MIDI ch9）でキック（note 36、vel 110） |

いずれも SMF format 0、480 TPQ の最小ファイルです。欠損時は `test_fixtures.h` から同内容を一時生成します。

## SoundFont（リポジトリ未同梱）

統合テストのレンダリング系は **`soundfonts/TyrolandGSV30fix.sf2`** を前提とします（`cpp/` から見て `../soundfonts/`）。

- ファイルが無い場合: 該当テストは **SKIP**（失敗扱いにしない）
- 最小 SF2 をリポジトリに含める予定はありません（サイズ・ライセンスのため）

## ゴールデン WAV（任意）

`expected/simple_note.wav` を置くと、`golden WAV comparison` テストがピーク/RMS 許容差で照合します。

- 初回や SF2 環境が異なる場合: ゴールデン無しでも **自己整合性テスト**（同一 MIDI を 2 回レンダリングして比較）が通れば OK
- ゴールデンを生成する例（Tyroland 配置後）:

```bash
cd cpp
make
./Mid2Wav -i test/fixtures/simple_note.mid -o test/fixtures/expected/ \
  --sf2 ../soundfonts/TyrolandGSV30fix.sf2
mv test/fixtures/expected/simple_note.wav test/fixtures/expected/simple_note.wav
```

生成した WAV は環境依存のため **git には通常コミットしません**（`.gitignore` 対象推奨）。
