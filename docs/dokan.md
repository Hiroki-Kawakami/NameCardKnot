# dokan — 近接交換した接続情報で P2P 通信路を張るライブラリ（`components/dokan`）

> **ステータス（v1 実装完了）**: 記述子 codec・データ面エンジン（ストリーム多重化／フロー制御）・
> セッションランタイム（ロック＋タスク）・`dokan_open`・**WiFi P2P transport**・**host sim
> transport** を実装。host 単体テスト 3 本 green、実機 ESP32-S3 ×2 で名刺 PDF の転送＋受信側での
> 共有画像表示まで確認済み。`dokan_wifi.c` は board 非依存（同一の `esp_wifi`/`lwip` API）。

**dokan**（土管）は、**狭帯域・近接の経路で交換した短い「接続記述子」文字列**を入力に、
**WiFi 等の本通信路を確立し、その上でデータ交換する**ためのコンポーネント。本 App では交換経路に
HotKnot（[`bsp_hotknot_*`](../esp-devkit/bsp/inc/bsp.h)）を使うが、**dokan 自体は交換経路に依存
しない** — 記述子は普通の文字列なので QR・NFC・手入力など何で運んでもよい。データ面は**数百 KB
の画像を複数同時に流せるストリーム多重化**を前提に設計している。

責務の分割:

```
   ┌─────────────┐  ①接続記述子(文字列 ≤128B)  ┌─────────────┐
   │  デバイス A  │ ─── 近接交換経路で受け渡し ──→ │  デバイス B  │
   │ (例: HotKnot │                              │ (例: HotKnot │
   │   master)    │ ←──────────────────────────  │   slave)     │
   └─────────────┘                              └─────────────┘
          │  ②記述子から本通信路を確立 (Host / Client)  │
          │     WiFi SoftAP/STA → TCP → ストリーム多重化 │
          └──────────────  ③データ交換  ───────────────┘
```

- **①交換経路**: dokan の外。dokan は「≤128B の文字列を生成／受理」する API だけを提供し、運搬は
  アプリが行う（本 App は HotKnot）。
- **②本通信路**: dokan の中核。記述子をパースして transport（WiFi P2P / host sim）を Host /
  Client として起動する。
- **③データ交換**: 確立後、**1 セッション上に複数ストリーム**を多重化して送受信する。

## 設計方針（要件）

1. **接続情報は文字列で交換する。** 交換経路の payload 上限（HotKnot は
   `BSP_HOTKNOT_MAX_PAYLOAD` = 128 byte）に必ず収める。印字可能 ASCII にして QR・手入力にも耐える。
2. **記述子に App 識別子（4 文字）を必須で含める。** 別 App 向け／別バージョンの記述子を取り
   違えないためのゲート。dokan は自分の App ID と一致しない記述子を `dokan_open` で弾く。App ID は
   KDF の salt にも使い、別 App が同一シードでも別 SoftAP になるようにする。
3. **記述子交換〜Host/Client 初期化開始まではステートレス。** dokan は「発行した記述子の控え」を
   一切持たない。記述子は**それ単体で本通信路を一意に決める自己完結データ**であり、
   - 記述子の**生成は純粋関数**（乱数シードを入れて文字列を返すだけ。副作用なし）。
   - **Host 側が生成した記述子である必要はない。** 正しい記述子でありさえすれば、Client が生成して
     Host へ渡したものでも両者が接続できる。dokan は「自分が発行したか」を検証しない。
   - **最初に状態を持つ呼び出しは `dokan_open()`**（＝transport 初期化開始）。それ以前は無状態。
4. **ロールは対称な記述子＋`dokan_open` の引数で決める。** 記述子はロールを含まない。どちらが Host
   になるかは dokan の関知しないアプリ方針（自然なのは交換経路の向き＝HotKnot の master/slave に
   固定対応）。
5. **transport は差し替え可能。** v1 は **WiFi P2P（SoftAP+STA）**と **host sim** を実装。
   Bluetooth・ESP-NOW 等を後から足せる vtable 構成（BSP の `bsp_display_t` / `bsp_hotknot_t` と
   同じ struct-inheritance 方式）。

## アーキテクチャ

dokan は **「reliable な順序バイトストリーム（transport）」の上に「ストリーム多重化＋フロー制御
（dokan core）」を載せ、それを公開 API として見せる**3 層。多重化・再送境界・フロー制御は
**dokan core 側**にあり、transport は「繋がった後の双方向 reliable バイトストリーム」を提供する
だけ（TCP / 将来の BT RFCOMM が該当）。datagram 系（ESP-NOW・BLE GATT）を足す場合は transport 側に
順序・再送のシムを噛ませる。

```
inc/dokan.h          公開 API: 記述子 codec + セッション + ストリーム + イベント (C, esp_err_t)
inc/dokan_types.h    ロール / イベント / transport id / opts / config の型
src/dokan_session.c  セッション統括・多重化/フロー制御・ロック・transport bridge・close/destroy
src/dokan_descriptor.c  記述子 codec（transport 非依存: ヘッダ + base64url(params||crc8)）
src/dokan_codec.c    base64url / base32 / CRC-8       src/dokan_sha256.c  in-tree SHA-256（KDF 用）
src/dokan_random.c   乱数（device esp_fill_random / host /dev/urandom）
src/dokan_wifi_params.c  WiFi params のバイトレイアウト + SSID/PSK 導出（KDF）
src/dokan_os.h       再帰ミューテックス + タスク同一性（host pthread / device FreeRTOS）
src/dokan_transport.{h,c}  transport vtable + host コールバック + id→transport レジストリ
src/dokan_runtime.c  dokan_open（記述子 parse → transport 生成 → session 結線 → start）
  └ src/dokan_wifi.c  WiFi P2P transport（SoftAP/STA + 静的IP + TCP、`#ifdef ESP_PLATFORM`）
  └ src/dokan_sim.c   host loopback transport（localhost TCP、`#ifndef ESP_PLATFORM`）
```

### ロール

```c
typedef enum {
    DOKAN_ROLE_HOST,    // 本通信路を立てる側（WiFi では SoftAP）
    DOKAN_ROLE_CLIENT,  // 立った通信路に参加する側（WiFi では STA）
} dokan_role_t;
```

transport 中立な 2 ロール。**両端は必ず反対のロールを取る。**ストリーム ID の偶奇割り当てにも使う
（後述: Host=偶数 / Client=奇数）。

## 接続記述子（descriptor）

≤128B・印字可能 ASCII の自己完結文字列。**ヘッダ（App と transport を識別）＋ transport 固有
payload（base64url）**。

```
 ┌── magic "DK"     2B   スキーム識別
 │┌─ version '1'    1B
 ││┌── app id       4B   App 識別子（必須・印字可能 ASCII。例 "NCKN"）
 ││││  ┌ transport  1B   'W'=wifi  'S'=sim  'B'=bt(将来)
 ││││  │
 DK1NCKNW<base64url( transport-params || crc8 )>
```

- ヘッダ 8B は素のまま（codec で固定）。以降は transport が詰めたバイナリ params を base64url
  （パディング無し）した文字列。`crc8` は params 全体の整合性チェック。
- `dokan_open` は記述子の app id が呼び出し側の App ID と一致しなければ `ESP_ERR_INVALID_ARG`
  （取り違え／別 App）。
- **codec は transport 非依存**（`dokan_descriptor.c`）: `params` を**不透明なバイト列**として扱い、
  crc 検証して `(transport_id, params バイト列)` を返すだけ。**params の意味・decode・KDF は各
  transport が持つ**（WiFi は `dokan_wifi_params.c`）。`dokan_descriptor_create` は WiFi 専用の発行
  ヘルパ（公開）。

サイズ概算: header 8B + base64url(20B + 1B crc) ＝ 8 + 28 ＝ **36 文字**（WiFi の場合）。128B に余裕。

### WiFi P2P transport の params と資格情報導出

`DOKAN_ROLE_HOST` が SoftAP を立て、`DOKAN_ROLE_CLIENT` が STA で参加する。SSID/PSK を記述子に直書き
せず、**16B 固定の乱数シードから両端が決定的に同じ資格情報を導出**する。

params（バイナリ、base64url 前、多バイト整数は big-endian）:

| field   | size | 説明 |
|---------|------|------|
| flags   | 1B   | 予約（将来: band・auth 方式）|
| channel | 1B   | WiFi チャネル（1–13）|
| seed    | 16B  | 乱数シード（固定長）。SSID/PSK の導出元（= 実質の共有秘密）|
| port    | 2B   | データ用 TCP ポート |
| crc8    | 1B   | （ヘッダ外、codec が付与）|

資格情報の導出（決定的・両端共通。`app_id` を salt に含める）:

```
key  = SHA-256(app_id || seed)         // in-tree SHA-256（依存ゼロ・host/device 共用）
SSID = "dokan-" + base32(key[0..3])    // 13 文字（衝突回避用の短い接尾辞）
PSK  = base32(key[4..15])              // 20 文字（WPA2 8..63 を満たす）
```

接続の確立:

- Host は導出した SSID/PSK で SoftAP（WPA2-PSK）を起動、Client は同導出で STA 接続。
- **IP は DHCP を使わず固定**（ESP32 同士の private IP に秘密要素は無く、DHCP は遅い）。
  SoftAP=`192.168.4.1` / STA=静的 `192.168.4.2`（`dokan_wifi.c` の名前付き定数）。
- 確立後は **Host が `params.port` で `INADDR_ANY` listen、Client が `192.168.4.1:port` へ
  connect**。その TCP に dokan core の多重化フレーミングを載せる。TCP ポートは記述子（params）が
  持つので `dokan_config_t` には置かない（二重指定を避ける）。

## データ面プロトコル（フレーミング・多重化・フロー制御）

transport の reliable バイトストリーム上に、**ストリーム多重化された軽量フレーム層**を載せる
（HTTP/2 を大幅に簡略化した形）。これにより「数百 KB を一度に処理しきれない」「同時に複数の転送」を、
send/recv 一発前提にせず扱える。

### フレーム

```
| type:u8 | stream_id:u16 | length:u16 | payload[length] |     ※多バイト整数は big-endian
```

ヘッダは固定 5B、`length` は payload のバイト数のみ（ヘッダを含まない）。

| type     | code | stream_id | payload | 意味 |
|----------|------|-----------|---------|------|
| OPEN     | 0x01 | 新規 id   | `kind:u16, size_hint:u64` | ストリーム開設＋メタ |
| DATA     | 0x02 | 既存      | チャンク | データ（最大チャンクは内部定数で分割: 4096B）|
| EOF      | 0x03 | 既存      | （空）  | 送信完了（以降 DATA 無し）|
| RESET    | 0x04 | 既存      | `reason:i32`(esp_err_t) | ストリーム異常終了 |
| WINDOW   | 0x05 | 既存      | `credit:u32` | 受信側→送信側へ送信枠（バイト）を補充 |
| GOAWAY   | 0x06 | 0         | `reason:i32` | セッションの正常終了通告 |
| PING     | 0x07 | 0         | `token:u32` | keepalive（将来用・未実装）|
| PONG     | 0x08 | 0         | `token:u32` | PING への応答（将来用・未実装）|

- **stream_id**: 0 はセッション制御専用（GOAWAY / 将来の PING・PONG）。データストリームは **Host が
  偶数・Client が奇数**を採番（双方が同時に開いても衝突しない）。同時ストリーム数は内部上限 16。
- **ストリームは単方向（unidirectional）。** DATA/EOF/RESET は **open した側 → 受け取った側**へ
  流れ、`write/finish` は open した側だけが行える。受け取った側（`STREAM_OPENED`）は read 専用で、
  逆向きに送りたければ自分で別ストリームを open する。`WINDOW` は受信側 → 送信側へ戻り、`RESET` だけ
  両端から打てる。
- **複数同時転送**: 別 `stream_id` を並行に流すだけ。受信側はストリームごとに状態を持てる
  （`dokan_stream_set_user` で app 文脈を紐付け）。
- **keepalive**: PING/PONG は transport が liveness を持たない将来用（ESP-NOW/BLE 等）。WiFi/TCP
  transport は TCP keepalive + FIN/RST に任せる。GOAWAY は「セッション正常終了の通告（理由コード
  付き）」（TCP の FIN とも整合）。

### フロー制御（バックプレッシャ）と `WINDOW` ↔ `pause`/`resume`

ストリームごとに**送信枠（window, バイト＝送信側が未確認で送ってよい上限）**を持つ。受信側が「あと
何バイト受け取れるか」を `WINDOW`（credit）で広告し、送信側はそれを超えて送れない。

**既定（pause していない）の流れ** — 受信側 core は **`STREAM_DATA` コールバックが return した分
（＝アプリが消費した分）だけ `WINDOW` を自動で送り返す**。これで送信側の枠が連続的に補充され、
転送は途切れず進む:

```
送信側                                         受信側
 stream_open(size_hint=300KB)  ──OPEN──────────►  STREAM_OPENED  (初期 window = 32KB を付与)
 write(buf,300KB,&w) → w=32KB (枠ぶん)
 …送れない残りは保留…
                               ──DATA×──────────►  STREAM_DATA(chunk)  ← cb return で消費
                               ◄──WINDOW(+chunk)──  消費分を credit 返却
 STREAM_WRITABLE ◄──────────── 枠が空く
 write(残り,&w) で続き…           （以降くり返し）
```

**`pause`/`resume` はこの自動補充を止める/再開するだけ**:

- `dokan_stream_pause(st)`: 当該ストリームの **`WINDOW` 自動補充を停止**。アプリが `STREAM_DATA` を
  消費しても credit を返さない。すると送信側は**初期 window を使い切った時点で `write` が
  `*written < len`（または 0）で返り、自然に停止**する（＝バックプレッシャ）。pause 後に届くのは
  「停止時点で送信側がまだ枠を持っていた分（最大 window バイト）」までで、それ以降は無音になる。
- `dokan_stream_resume(st)`: pause 中に溜まった未返却分を **`WINDOW` で一括補充**。送信側に
  `DOKAN_EVENT_STREAM_WRITABLE` が上がり転送が再開する。

用途: 受信を遅い書き込み先（フラッシュ/SD/デコーダ）へ流していて間に合わないとき pause、捌けたら
resume。**transport の I/O タスクが常にソケットを drain する**ので、あるストリームを止めても他
ストリームは詰まらない（HoL 回避。止めたストリームのバッファ上限は window 値で決まる）。

**既定値（実機計測で調整可。`dokan_config_t`・内部定数）:**

| パラメータ | 既定 | 根拠 |
|-----------|------|------|
| 最大チャンク長（DATA payload） | **4 KB** | overhead 5B/4KB ≒ 0.12%。受信バッファ・ストリーム公平・FC 粒度・SD/flash 単位が手頃 |
| ストリーム初期 window | **32 KB**（4KB×8 in-flight） | SoftAP 直結は低 RTT で BDP 数十 KB。pause 時の最大バッファも 32KB/ストリーム |
| WINDOW 補充しきい値 | **window の ~1/2（16KB）** | チャンク毎の WINDOW を避け、消費未返却が半分溜まったら一括返却 |

受信側 pause 時の最悪バッファは `window × 同時ストリーム数`。RAM を抑えたいときは
`dokan_config_t.stream_window` を下げられる。throughput が必要なら `CONFIG_LWIP_TCP_SND_BUF_DEFAULT`
を window 以上に上げる。

## 公開 API（C）

```c
// ── identifiers ───────────────────────────────────────────────
// App 識別子。printable ASCII 4 文字。create / open に渡し、open で記述子と照合。
// 本 App は NameCardKnot.hpp に #define DOKAN_APP_ID "NCKN" を置いて使い回す。

// ── descriptor（純粋・ステートレス）──────────────────────────
esp_err_t dokan_descriptor_create(dokan_transport_id_t transport,
                                  const char app_id[4],
                                  char *out, size_t cap);          // 乱数シードで発行。副作用なし
bool      dokan_descriptor_valid(const char *descriptor, const char app_id[4]);

// ── session（ここから状態を持つ＝transport 初期化開始）────────
typedef struct dokan_session dokan_session_t;
typedef struct dokan_stream  dokan_stream_t;

typedef struct {
    uint32_t connect_timeout_ms;  // 0=既定。確立できなければ ERROR
    uint32_t stream_window;       // ストリーム初期 recv window(バイト)。0=既定(32KB)
} dokan_config_t;

typedef enum {
    DOKAN_EVENT_CONNECTED,        // セッション確立。stream_open / 受信可
    DOKAN_EVENT_DISCONNECTED,     // 相手 GOAWAY などで正常終了
    DOKAN_EVENT_ERROR,            // セッション致命（err 有効・以降無効）
    DOKAN_EVENT_STREAM_OPENED,    // 相手が開いたストリーム（stream, opts 有効）
    DOKAN_EVENT_STREAM_DATA,      // データ到着（stream, data/len。data は cb 内のみ有効）
    DOKAN_EVENT_STREAM_FINISHED,  // 相手が EOF（以降 DATA 無し）
    DOKAN_EVENT_STREAM_WRITABLE,  // 枠が空き、write を再開できる（stream 有効）
    DOKAN_EVENT_STREAM_RESET,     // 相手がストリーム異常終了（stream, err 有効）
} dokan_event_type_t;

// kind は app 定義タグ、size_hint は総バイト数（進捗表示用のアプリ向けメタ）。
// size_hint=0 は「不明/可変長」で core は open-ended として扱う（ストリームは普通に使える）。
typedef struct { uint16_t kind; uint64_t size_hint; } dokan_stream_opts_t;

typedef struct {
    dokan_event_type_t  type;
    dokan_session_t    *session;
    dokan_stream_t     *stream;   // STREAM_* のみ
    const uint8_t      *data;     // STREAM_DATA のみ（cb 内のみ有効）
    size_t              len;      // STREAM_DATA
    dokan_stream_opts_t opts;     // STREAM_OPENED のみ
    esp_err_t           err;      // ERROR / STREAM_RESET
} dokan_event_t;

// transport の I/O タスク文脈で呼ばれる。短く保ち UI へは marshal すること。
typedef void (*dokan_event_cb_t)(const dokan_event_t *ev, void *arg);

esp_err_t dokan_open(const char *descriptor, dokan_role_t role,
                     const char app_id[4], const dokan_config_t *cfg,
                     dokan_event_cb_t cb, void *arg, dokan_session_t **out);
esp_err_t dokan_close(dokan_session_t *s);   // 全ストリーム＋transport を畳む。冪等

// ── streams（CONNECTED 後）─────────────────────────────────────
// ストリームは単方向: open した側だけが write/finish でき、相手は read 専用。
esp_err_t dokan_stream_open(dokan_session_t *s, const dokan_stream_opts_t *opts,
                            dokan_stream_t **out);
// open した側のみ。非ブロッキング: 受理した分を *written に返す。*written<len は枠満杯（WRITABLE 待ち）。
esp_err_t dokan_stream_write(dokan_stream_t *st, const void *data, size_t len, size_t *written);
esp_err_t dokan_stream_finish(dokan_stream_t *st);                  // open した側のみ。EOF を送る
esp_err_t dokan_stream_reset(dokan_stream_t *st, esp_err_t reason); // 両端可。異常終了
esp_err_t dokan_stream_pause(dokan_stream_t *st);                   // 受信側のみ。バックプレッシャ
esp_err_t dokan_stream_resume(dokan_stream_t *st);                  // 受信側のみ
void      dokan_stream_release(dokan_stream_t *st);                 // 終わった handle を解放（両端可）
// 相手が開いたストリームに app 状態（デコーダ・ファイルハンドル等）を紐付ける
void  dokan_stream_set_user(dokan_stream_t *st, void *user);
void *dokan_stream_get_user(dokan_stream_t *st);
```

数百 KB の画像送信の典型: 送信側は `dokan_stream_open({kind, size_hint=総バイト})` → ループで
`dokan_stream_write`（短ければ `STREAM_WRITABLE` 待ち）→ `dokan_stream_finish`。受信側は
`STREAM_OPENED` で `size_hint` から進捗バーを用意し、`STREAM_DATA` を逐次デコーダ／バッファへ、
詰まれば `pause`、消費して `resume`、`STREAM_FINISHED` で完了。終わった handle は
`dokan_stream_release` で解放する。

### ライフサイクル

```
 dokan_descriptor_create() ──(文字列)──┐   ← ステートレス（純粋関数）
                                       ▼
 [近接交換経路で両端が同一記述子を保持]
                                       ▼
 dokan_open(role) ─► CONNECTED ─┬─► (stream_open / STREAM_* …)* ─► DISCONNECTED
                                └─► ERROR
                                       ▼
                                 dokan_close()  ← どの状態からでも安全に畳める
```

## 並行モデルと破棄

- **単一の再帰ミューテックス**（`dokan_os.h`: host pthread / device FreeRTOS）が、transport の I/O
  タスクと App スレッドからのエンジンアクセスを直列化する。
- **イベントはロック保持中にインライン配送**（`STREAM_DATA` の `data` は受信バッファを指したまま
  有効＝コピー不要）。再帰ロックなので **cb 内から `dokan_stream_*` を呼べる**（同スレッド再入）。
- **送出 2 段**: エンジンは outbound キューに直列化し、transport の `write`（非ブロッキング、受理
  バイト数を返す）が吐く。残りは I/O タスクが書込可になったとき `on_writable` で吐く（App から
  見えるのは window 側の `dokan_stream_write` / `STREAM_WRITABLE`）。これで `dokan_stream_write` は
  常に即返りし、ソケットの詰まりは outbound キュー（上限 ≈ window）が吸収する。
- **破棄**: `dokan_close` は **App スレッドから同期**（transport `stop` で I/O タスクを join →
  解放）。**cb 内（I/O タスク上）からの close は遅延**（フラグを立てて返し、タスクがコールバックを
  抜けてから自分で畳む）。エラー時はイベントを別スレッドへ marshal して、そこで close するのが安全。

> **App 側の注意**: イベント cb は I/O タスクで走る。LVGL 等 UI を直接触らず、フラグ／カウンタを
> 立てて UI スレッド側（lv_timer 等）でポーリングして反映すること（毎チャンク `lv_async_call`
> すると UI/EPD スレッドと競合して I/O タスクが詰まる）。

## transport 抽象（`src/dokan_transport.h`）

各バックエンドが `dokan_transport_t` を先頭に埋めた struct を実装する。transport の責務は **「繋がった
後の reliable 順序バイトストリーム」**だけで、フレーミング／多重化／フロー制御は dokan core が担う。
transport は**自分の I/O タスク**を持ち、session が渡した host コールバックでイベントを上げる。

```c
typedef struct {                       // session 側コールバック（ctx = session）
    void (*on_connected)(void *ctx);
    void (*on_bytes)(void *ctx, const uint8_t *data, size_t len);
    void (*on_writable)(void *ctx);
    void (*on_error)(void *ctx, esp_err_t err);
    void (*on_closed)(void *ctx);      // 遅延 close 後にタスクが畳むとき
    void (*bind_task)(void *ctx);      // I/O タスクが自分を登録（close-from-cb 判定用）
    bool (*poll_close)(void *ctx);     // cb 内 close が要求されたか
    void *ctx;
} dokan_transport_host_t;

struct dokan_transport {
    dokan_transport_id_t id;           // 記述子ヘッダの transport 文字に対応
    esp_err_t (*start)(dokan_transport_t *self, dokan_role_t role,
                       const char app_id[DOKAN_APP_ID_LEN],   // KDF salt（WiFi が使う）
                       const uint8_t *params, size_t plen,
                       const dokan_config_t *cfg, const dokan_transport_host_t *host);
    size_t    (*write)(dokan_transport_t *self, const uint8_t *data, size_t len); // 非ブロッキング
    void      (*stop)(dokan_transport_t *self);  // I/O タスクを join（クロススレッドで呼ぶ）
};
```

`dokan_open`（`dokan_runtime.c`）が記述子の transport 文字 → `dokan_transport_create()` で
インスタンスを生成し、session に結線して `start()` する。新しい transport は実装＋レジストリに
1 行追加するだけ。

**v1 の transport 実装:**

- **WiFi（`dokan_wifi.c`、device）**: SoftAP/STA を起動し TCP listen/connect。1 セッション 1
  FreeRTOS タスクが `select`（20ms タイムアウト）で recv/書込可/stop をポーリング。送信は非
  ブロッキング、recv バッファはヒープ（スタックオーバーフロー回避）。WiFi の init/deinit を
  transport が所有し、teardown は stop フラグ＋完了セマフォで join 相当。
- **sim（`dokan_sim.c`、host）**: シードから導出した localhost ポートで HOST listen / CLIENT
  connect。1 セッション 1 pthread、self-pipe で wake。WiFi と同じ非ブロッキング送出＋outbound
  キュー経路を辿る（`test_runtime` が SO_SNDBUF を絞ってこの経路を実走させる）。

## エラーと復帰

- dokan は確立失敗・タイムアウト・相手離脱・I/O 失敗を **`DOKAN_EVENT_ERROR`（err 付き）**で、
  ストリーム単位の異常を **`DOKAN_EVENT_STREAM_RESET`** で上げる。`dokan_close` は**どの状態からでも
  安全・冪等**で、呼ぶと **transport（WiFi）まで落とす**ので、再試行や再起動が clean。
- どの画面に戻るか・状態をどう片付けるかは App が書く。これに足る情報（err コード・どのストリーム
  か）はイベントに載せる。

## セキュリティ

- **脅威モデルは近接ペアリング。** 記述子（＝WiFi シード）は HotKnot の超近接（接触相当）でしか
  渡らない前提なので、PSK 相当が平文で乗ることは許容（シードを知る者は誰でも SoftAP に参加可
  ＝パスワード共有と同じ強度）。app id を KDF salt に入れ、別 App とは SoftAP が分離する。
- **記述子は短命。** セッションごとに新シードを発行し、`dokan_close` で SoftAP を落とす。控えは
  持たない（ステートレス方針と一致）。
- **機密の直書き禁止（プロジェクト規約）。** SSID/PSK は実値をコードに書かず必ずシードから導出
  する。ESP32 同士の固定 private IP（`192.168.4.x`）は秘密要素が無いため定数で持つ。
- **追加暗号は未実装**。WPA2 で経路は暗号化される。必要ならフレーム層にシード由来鍵の AEAD を後付け
  できる（v1 は proximity で十分とする）。

## テスト

host 単体テストは `components/dokan/test/run.sh`（gcc, `-pthread`, ESP-IDF 無し、
`test/shim/esp_err.h`）。`test_*.c` を総当りでビルド・実行する。**全て green**:

- `test_descriptor.c` — SHA-256 ベクタ・base64url/base32/crc8・KDF 決定性・記述子の往復／app id
  照合／crc 破壊／サイズ上限。
- `test_session.c` — データ面エンジンを transport 無しで検証（2 エンジンをメモリパイプで直結）:
  大容量（200KB・window サイクル）・複数ストリーム同時・pause/resume・reset・transport 部分書込。
- `test_runtime.c` — **end-to-end**: 1 プロセス内で host/client 2 セッションを `dokan_open` し、
  sim transport（localhost）で接続→100KB 転送→FINISHED→`dokan_close` まで通し。

WiFi transport は device 専用なので host テスト不可。実機検証は ESP32-S3 ×2 で行い、名刺 PDF の転送
＋受信側での共有画像表示まで確認済み（下記「App 統合」）。

## App 統合

### 画面構成（2 フェーズ）

名刺交換は **HotKnot フェーズ → dokan フェーズ**の 2 段で、画面も分かれている：

```
[ HotKnotScreen ]  app/screens/HotKnotScreen.{hpp,cpp}（旧 TransferScreen）
  ├ ShareScreen   role=master, offer=1,              accept=「相手の名刺も受け取る」
  └ ReceiveScreen role=slave,  offer=「自分の名刺も送る」, accept=1
       master: dokan_descriptor_create(WIFI,"NCKN",d) を生成して HotKnot で送信
       slave : 受信して dokan_descriptor_valid で検証
            │  RAM の TransferStart{role, offer, accept, descriptor, own} を渡す
            ▼  screen_manager.load() （再起動も NVS も挟まない）
[ TransferScreen ]  app/screens/TransferScreen.{hpp,cpp}（単一具象・両ロール共通）
       dokan_open → HELLO 交換 → 条件付き CARD 転送 → 加重進捗 → 自動保存 → bsp_power_restart
```

当初は GT911 復帰のため HotKnot 後に一度再起動してから dokan を始める設計だったが、dokan の転送が
数秒で終わるため**再起動は交換完了後の 1 回だけ**にした（NVS handoff は不要）。転送中はタッチが死んだ
ままなので **TransferScreen は非インタラクティブ**（キャンセルボタンなし＝中断は物理リセット）。

### TransferScreen のプロトコル（App 層・dokan の上）

- 役割対応は **master→HOST / slave→CLIENT**（アプリ方針）。記述子はロールを含まない。
- **`KIND_HELLO`（小）と `KIND_CARD`（PDF）の 2 種**をストリーム `opts.kind` で区別（開く順では
  なく kind で判定）。
- **HELLO 交換**: `CONNECTED` 後に両端が HELLO を 1 本ずつ開き `{version, offer, accept, name}`
  を送って finish。相手 HELLO を受け取ると `will_send = my.offer && peer.accept && 自分の名刺あり`、
  `will_recv = peer.offer && my.accept` を確定（両端で同じ AND になる）。`Share→Receive` は常に発生、
  `Receive→Share` は両チェックボックスが立つときだけ。
- **CARD 転送**: `will_send` の側が `KIND_CARD`（`size_hint = SharePdfStream.size()`）を開き、
  `SharePdfStream`（`MyCardStore` の share-only PDF）を 4KB ずつ pump（window 満杯は
  `STREAM_WRITABLE` で再開）。受信側は `STREAM_OPENED` で `.nck_transfer`（隠し temp）を開き、
  `STREAM_DATA` を追記、`STREAM_FINISHED` で確定。
- **自動保存（power-fail safe）**: 受信は `RECEIVED_CARDS_DIR`（`/sdcard/ReceivedCards`、
  `NameCardKnot.hpp`）に集約。隠し temp `.nck_transfer` に書き、完了後 `SharedCardData::open` で
  検証 → 同ディレクトリの `<name>.snc.pdf`（衝突は連番）に rename。中断・不正は temp を削除するだけで
  壊れた `.snc.pdf` は残さない。後で GalleryScreen がこのディレクトリを読む想定。
- **加重進捗**: 1 本のバーを `(sent + received) / (send_total + recv_total)` で更新（方向数に依らない）。
- **相手名表示**: `will_recv` のときだけ peer HELLO の name を表示（受信専用＝相手は表示しない側に
  なる。My Card 未設定で `offer=0` の端末の name は誰にも表示されないので空でよい）。
- **終了の握り（ACK）**: `dokan_close` は送信ウィンドウに残った未送出バイトのフラッシュを保証しない
  （GOAWAY を best-effort で流して即 transport stop）。そのため「自分のカードを送り終えた」だけで
  close すると相手のインフライト受信が切れる（特に HOST が双方向で I/O タスクが詰まると顕著）。対策として
  **受信し切った側が 0 バイトの `KIND_ACK` ストリームを開いて finish**し、**送った側は相手の ACK を
  受け取るまで close しない**。ACK は極小なので graceful close で確実に届く。完了判定は「自分の inbound
  を受信し切った（finalize 済み）」かつ「自分の outbound を相手が ACK した」で、両ロール対称（role 依存の
  close 順や grace は廃止、ACK 不達時のみタイムアウトで fallback）。
- **スレッド分離**: dokan イベントは I/O タスクで届くので、UI / temp 確定 / `dokan_close` / 再起動は
  すべて `lv_timer` ポーリング側（LVGL スレッド）が atomics を見て駆動する。

シミュレータには HotKnot 実装が無いため（`bsp_hotknot_begin` が失敗）この経路は実機検証
（ESP32-S3 ×2）。`dokan` 自体の sim transport は host 単体テスト（下記）でカバー。

## ビルド配線

`components/` 配下の他コンポーネント（`image_processor` / `namecard_pdf`）と同じ自己記述方式。

```
components/dokan/
  inc/    dokan.h, dokan_types.h
  src/    dokan_session.c, dokan_descriptor.{c,h}, dokan_codec.{c,h}, dokan_sha256.{c,h},
          dokan_random.{c,h}, dokan_wifi_params.{c,h}, dokan_os.h,
          dokan_transport.{c,h}, dokan_runtime.c, dokan_wifi.c, dokan_sim.c
  test/   run.sh, shim/esp_err.h, test_{descriptor,session,runtime}.c
  CMakeLists.txt
```

- **device**: `devkit_idf_init(... COMPONENT_DIRS ../components ../app)` が `../components` を
  コンポーネントコンテナとして登録するので**自動検出**（編集不要）。
  `PRIV_REQUIRES` は `esp_hw_support`（seed 乱数）/ `freertos`（ロック・タスク）/ `esp_wifi` /
  `esp_netif` / `nvs_flash`（WiFi transport）。SHA-256 は in-tree なので mbedtls 不要。
- **simulator**: `simulator/CMakeLists.txt` の
  `devkit_simulator(COMPONENT_DIRS ...)` に `dokan` を登録済み。
  `dokan_sim.c` と `dokan_os.h` の host 分岐は `ESP_PLATFORM` 未定義で有効、WiFi/device 専用は
  `#ifdef ESP_PLATFORM` で囲う（host ビルドで `dokan_wifi.c` は空 TU）。
- `dokan.h` を include する `app` は `REQUIRES dokan` を持つ。

## 今後の拡張

- **本統合**（上記「予定」）: HotKnot で descriptor 交換 → NVS handoff → 再起動 → 転送画面で
  `dokan_open`、双方向の名刺交換・進捗・エラー復帰。
- **transport 追加**: Bluetooth（RFCOMM）/ ESP-NOW など。記述子 transport 文字を割り当て、vtable を
  実装してレジストリに 1 行追加する。datagram 系は順序・再送のシムを噛ませる。
- **チューニング**: 実機スループットを見て window / chunk / WiFi select タイムアウト /
  `CONFIG_LWIP_TCP_SND_BUF_DEFAULT` を調整。
- **データ面の追加機能**（必要になれば）: PING/PONG keepalive、フレーム層 AEAD。
