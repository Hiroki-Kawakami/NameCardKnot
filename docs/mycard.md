# カードの本体 Flash 保存（`app/CardStore`）

`.mnc.pdf` 1 枚を本体 Flash の専用 2MB パーティションに**独自バイナリレイアウト 1 つ**
（FileSystem なし）で保存する仕組み。`cardstore::Store` はパーティションラベルを取る
インスタンスで、現在 2 つ:

- **`cardstore::mycard()`**（パーティション `mycard`）: SD から選んだ「マイカード」。
  `HomeScreen` から即座に開ける。
- **`cardstore::lastcard()`**（パーティション `lastcard`）: スリープ突入時に書かれる
  「表示中カード」のキャッシュ（元 PDF + デコード済み表示 L8）。起動時の復帰が SD
  なし・デコードなしで済む（後述）。

表示用の画像キャッシュは生 L8 で置き、**表示中の画像 blob だけをオンデマンドで
mmap**して `lv_image` に渡すので、開くときに RAM へロードしない。

> **パーティション全体は mmap しない**（重要）。無印 ESP32 の読み出しデータ用 mmap
> 窓（DROM）は約 4MB しかなく、その大半を `.rodata`（約 1.9MB）が占める。ここに 2MB
> を恒久マップすると窓が枯渇し、**paper ボードが無言でフリーズ**する（S3 は窓構成が
> 違うので無事）。そのため**ヘッダは RAM にキャッシュ**し、**PDF はコピーで読み**、
> **画像 blob だけを表示中のみ mmap**する（離脱で munmap）。

関連: 保存元 PDF のパースは [`docs/namecard_pdf.md`](namecard_pdf.md)、画像デコードは
[`docs/image_processor.md`](image_processor.md)。

## パーティション

`esp32s3/partitions.csv` / `esp32/partitions.csv`（両ボード同一）:

```
mycard,   data, 0x40,    0x410000, 2M,
lastcard, data, 0x41,    0x610000, 2M,
```

`factory`(4M, 0x10000–0x410000) 直後の **64KB アライン**位置に 2M（=32×64KB）ずつ。
名前検索（subtype は custom）:
`esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, <label>)`。
読み書きは `esp_partition_read`/`erase_range`/`write`、表示は blob 単位の
`esp_partition_mmap`（`ESP_PARTITION_MMAP_DATA`）。

## レイアウト

```
off 0x0000  Header スロット（先頭 1 セクタ=4KB、128B×32 スロット）
off 0x1000  BLOB_PDF      元 .mnc.pdf 全体（バイトコピー）       sector-aligned
   …        BLOB_DISPLAY  540×960 L8（生ピクセル）              sector-aligned
   …        BLOB_PREVIEW  169×300 L8（Home サムネ）             sector-aligned
   …        BLOB_NAME     32px L8（名前ラスタ）                 sector-aligned
```

各 blob は**セクタ境界に整列**（per-blob erase が隣を侵さないため）。画像のメタ
（w/h/stride/levels/format）は Header 側に持ち、blob 先頭ポインタ（`base + offset`）を
そのまま `lv_image_dsc.data` に向けられる。`Header`/`Blob` は `app/CardStore.hpp`。
mycard は BLOB 4 種すべて、lastcard は PDF + DISPLAY のみ（PREVIEW/NAME は空索引）。

- `format == FMT_L8` の blob は MMIO 表示用。`Store::map_image(id)` が **RAII の
  `MappedImage`**（その blob だけを mmap、破棄で munmap）を返し、`.view()` の
  `L8View`（`app/L8View.hpp`）を `l8view_fill_lv_dsc`（`app/lv_image_adapter.hpp`）で
  `lv_image_dsc_t` にゼロコピー変換する。**`MappedImage` を表示中だけ保持**し、画面を
  離れたら破棄する（`HomeScreen` は preview/name を `onDisappear` で解放し
  `onAppear` で再取得、`NameCardData`(cached) は display を自身の寿命で保持）。
- `Store::mount()` は**ヘッダだけを `esp_partition_read` で RAM キャッシュ**（`s_hdr`）
  し、`available()`/`header()`/`blob_len()` はそれを見る。`read_blob(id,buf,len)` は
  blob をコピー取得（PDF 用）。`available()` は `magic`/`version`/`blob_count` と
  `header_crc`（索引部の crc32、`nckpdf::crc32` 流用）で判定。

## 電源断耐性（使用範囲のみ erase、ヘッダはスロット追記）

`Store::Writer` の順序は **erase → blob write → commit（の繰り返し可）**。

1. `begin()`: **先頭ヘッダセクタを erase** → 全スロット無効化。
2. `write_blob()`: その blob が占めるセクタ**だけ**を erase してから write。全消去は
   しない（即時の再インポート＝上書きでも触る範囲は最小）。
3. `commit()`: magic 入り Header を**次の空き 128B スロットへ追記 write**（erase なし）
   し、ヘッダキャッシュを読み直す。**blob ごとに commit してよく**、読み手は最後の
   有効スロットを採用する — lastcard はこれで「PDF だけ commit 済み・DISPLAY 書き込み
   中にリセット」でも PDF が生き残る。`abort()` が取り消せるのは最初の commit 前だけ。

途中で電源が切れても、有効なのは commit 済みスロットまで → 半端な blob が有効と
誤認されることはない（最初の commit 前に切れれば `available()==false`）。

> **書き込み中の生きたマッピング**: erase/write するセクタを `MappedImage` で mmap した
> ままにしないこと（書き込み後の読みがキャッシュ不整合になる）。インポートは Home から
> 起動し、Home は遷移前に `onDisappear()` で preview/name の `MappedImage` を破棄する
> ので、Writer 実行中に生きたマッピングは無い（カード表示中の `NameCardScreen` から
> インポートは起動しない）。戻ったら `onAppear()` が新ヘッダから取り直す。

シミュレータ（`#ifndef ESP_PLATFORM`）はバッキングファイル `simulator/mycard.img` /
`simulator/lastcard.img`（2MB、`SIMULATOR_MYCARD_PATH` / `SIMULATOR_LASTCARD_PATH` で
上書き可、gitignore 済）を `MAP_SHARED` で mmap し、erase=`memset 0xFF`、
write=`memcpy`＋`msync` と device と同一セマンティクスで動く。

## lastcard キャッシュ（スリープ突入時の保存と復帰）

`lastcard::save_cache(data)`（`app/LastCard.cpp`、`power` のスリープシーケンスから
呼ばれる）が表示中カードを書く。マイカード表示中は何もしない（mycard パーティションが
ある）。同一カードが既に完全キャッシュ済みなら書き込みもスキップ（毎スリープの wear
ゼロ）。書く場合は **NVS の `cpath` を消してから** erase → `BLOB_PDF` write+commit →
`cpath` セット → `BLOB_DISPLAY`（表示に使った L8 そのまま、stride は width に詰める）
write+commit の順。`cpath`（キャッシュが対応する SD パス）は「パーティションの中身が
現在のカードのものである」証明で、書き込みが中断されても**古いカードが現在のカードと
して復元されることはない**（`cpath` 不一致 → SD フォールバック）。

復帰（`NameCardData::load_lastcard`）: `cpath` 一致を呼び出し側（`initial_screen`）が
確認してから、PDF blob をコピー・パース（Info/Share 用メタデータ）、DISPLAY blob を
mmap して即表示。DISPLAY が無ければ（stage 途中の中断）キャッシュ PDF の display JPEG
を `decode_buffer` で同期デコードして表示する。どちらも SD 不要。

## インポート（`app/ImportJob`）

`FileLoader` 実装。`FileBrowserScreen`（`Mode::ImportMyCard`：`.mnc.pdf` のみ列挙）の
進捗モーダル＋ poll ループにそのまま乗る。専用 worker タスク 1 本で:

1. SD の `.mnc.pdf` 全体を RAM へ読み、`nckpdf::parse_buffer` で検証（`display_jpeg`
   必須）。
2. `Writer::begin()`。
3. `BLOB_PDF` ＝ 読み込んだバイトをそのまま書き込み。
4. `BLOB_DISPLAY` ＝ 埋め込み display JPEG を画面解像度で `decode_buffer`（16-gray）。
5. `BLOB_PREVIEW` ＝ 同 JPEG を 169×300 で再 `decode_buffer`（dither-on-dither 回避の
   ため生 JPEG から再デコード）。
6. `BLOB_NAME` ＝ `render_name_l8`（次節）。名前空・失敗時は blob を置かない（Home は
   名前画像なし）。
7. `Writer::commit()`。

進捗は段階の重み付き合算。`cancel()` は `imgproc::Progress.cancel` 経由で各デコードを
中断。完了後は `screen_manager.pop()` で Home に戻り、`onAppear()` が反映する。

## 名前ラスタ（`app/NameRaster` + `app/NameFont`）

Home では名前を**画像**で出す（フォント不要）。`render_name_l8` は名前を 48px の
フォールバックチェイン（`NameFont`：Montserrat → NotoSansJP → 埋め込み外字
supplement）で**オフスクリーン L8 canvas** に描き、**48→32px へボックス縮小**して
L8（byte=level×17, 16 階調）にする。外字は 1bpp@48px なので、この**スーパーサンプル
縮小がグレースケール AA** を生む。

LVGL の canvas/draw を使うので **`lv_lock()` 下**で呼ぶ（`ImportJob` の唯一の LVGL 接触
点）。描画先は未ロードの捨てスクリーン上の canvas なので画面には出ない。

## 開く（`NameCardData::load_cached`）

Home のカードタップ → `NameCardData::load_cached()` が**デコードせず**構築する:
`read_blob(BLOB_PDF)` で PDF を RAM(`pdf_`) にコピーして `parse_buffer`、name/url/message
と外字 supplement（`pdf_` を参照）を得る。表示画像は `map_image(BLOB_DISPLAY)` の
`MappedImage` を自身が保持し、その `L8View` を返す。`NameCardScreen` は SD 経由と同じ
`display_view()` 経由で 1:1 表示する（`NameCardData` 破棄＝離脱で munmap）。

## 検証

- `simulator/verify/mycard.txt`：No My Card → `.mnc.pdf` インポート → Home 反映 →
  カードを開く。`rm simulator/mycard.img` で空状態から開始。
- 外字経路は `images/鬛亜.mnc.pdf` を選ぶと名前ラスタが supplement で描かれる。
