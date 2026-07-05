/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "Strings.hpp"

namespace {

Strings make_en() {
    Strings s{};
    s.app_name = "Name Card Knot";
    s.close = "Close";
    s.cancel = "Cancel";
    s.ok = "OK";
    s.back = "Back";
    s.home = "Home";
    s.settings = "Settings";
    s.share = "Share";
    s.receive = "Receive";
    s.gallery = "Gallery";
    s.url = "URL";
    s.message = "Message";
    s.error = "Error";
    s.no_items = "No Items";
    s.sd_card_not_found = "SD card not found.";
    s.loading = "Loading...";
    s.cancelling = "Cancelling...";
    s.cannot_open_fmt = "Cannot open %s";
    s.items_page_fmt = "%d Items (%d/%d)";

    s.open_from_sd = "Open from SD";
    s.my_card = "My Card";
    s.no_my_card = "No My Card";
    s.import_from_sd = "Import from SD Card";

    s.repository = "Repository";
    s.developer = "Developer";
    s.date_time = "Date & Time";
    s.languages = "Languages";
    s.acknowledgements = "Acknowledgements";

    s.no_image = "No image";

    s.cannot_load_image = "Cannot load image";
    s.image_1 = "Image 1";
    s.image_2 = "Image 2";

    s.sd_required_to_receive = "An SD card is required to receive a card in return.";
    s.also_receive_return = "Also receive their card in return";

    s.sd_required_to_receive_cards = "An SD card is required to receive cards.";
    s.invalid_descriptor = "Received an invalid descriptor.";
    s.also_send_return = "Also send my card in return";

    s.handshaking = "Handshaking";
    s.exchanging_card = "Exchanging Card";
    s.sending_card = "Sending Card";
    s.receiving_card = "Receiving Card";
    s.finalizing = "Finalizing";
    s.received_new_card = "Received a new card.";
    s.card_sent = "Your card has been sent.";
    s.no_cards_exchanged = "No cards were exchanged.";
    s.could_not_connect = "Could not connect to the other device.";
    s.failed_save_received = "Failed to save the received card.";
    s.connection_lost = "The connection was lost before the transfer completed.";
    s.error_detail_fmt = "%s (%s)";
    s.transfer_progress_fmt = "%u.%u / %u.%u KB";

    s.notice = "Notice";
    s.share_failed = "Share Failed";
    s.receive_failed = "Receive Failed";
    s.transfer_failed = "Transfer Failed";
    s.transfer_complete = "Transfer Complete";
    s.press_reset_hint = "Press the reset button on the back of the device to restart.";
    s.open_card = "Open Card";

    s.devices_separated = "The devices were separated before the transfer completed.";
    s.card_exchange_failed = "Card exchange failed.";
    s.hold_screen_hint = "Hold this screen against the other device's screen to start sharing.";
    s.hotknot_approach = "HotKnot Approach";
    s.keep_held_hint = "Keep the devices held together until this finishes.";
    s.connection_failed_retry = "Connection failed. Please try again.";

    s.hold_button_wake = "Hold the side button to wake";
    s.press_button_wake = "Press the side button to wake";

    s.language_title = "Language / 言語";
    return s;
}

Strings make_ja() {
    Strings s{};
    s.app_name = "Name Card Knot";
    s.close = "閉じる";
    s.cancel = "キャンセル";
    s.ok = "OK";
    s.back = "戻る";
    s.home = "ホーム";
    s.settings = "設定";
    s.share = "共有";
    s.receive = "受信";
    s.gallery = "ギャラリー";
    s.url = "URL";
    s.message = "メッセージ";
    s.error = "エラー";
    s.no_items = "アイテムがありません";
    s.sd_card_not_found = "SD カードが見つかりません";
    s.loading = "読み込み中...";
    s.cancelling = "キャンセル中...";
    s.cannot_open_fmt = "%s を開けません";
    s.items_page_fmt = "%d 件 (%d/%d)";

    s.open_from_sd = "SDから開く";
    s.my_card = "自分の名札";
    s.no_my_card = "自分の名札がありません";
    s.import_from_sd = "SD カードからインポート";

    s.repository = "Repository";
    s.developer = "Developer";
    s.date_time = "日時";
    s.languages = "言語";
    s.acknowledgements = "Acknowledgements";

    s.no_image = "画像なし";

    s.cannot_load_image = "画像を読み込めません";
    s.image_1 = "画像 1";
    s.image_2 = "画像 2";

    s.sd_required_to_receive = "返信を受け取るには SD カードが必要です";
    s.also_receive_return = "相手からの返信を許可する";

    s.sd_required_to_receive_cards = "名札データの受信には SD カードが必要です";
    s.invalid_descriptor = "無効なデータを受信しました";
    s.also_send_return = "自分の名札データを返信する";

    s.handshaking = "データ転送準備中";
    s.exchanging_card = "名札データを交換中";
    s.sending_card = "名札データを送信中";
    s.receiving_card = "名札データを受信中";
    s.finalizing = "完了処理中";
    s.received_new_card = "新しい名札データを受信しました";
    s.card_sent = "名札データを送信しました";
    s.no_cards_exchanged = "名札データは交換されませんでした";
    s.could_not_connect = "相手の端末に接続できませんでした";
    s.failed_save_received = "受信した名札データの保存に失敗しました";
    s.connection_lost = "転送が完了する前に接続が切断されました";
    s.error_detail_fmt = "%s (%s)";
    s.transfer_progress_fmt = "%u.%u / %u.%u KB";

    s.notice = "お知らせ";
    s.share_failed = "共有に失敗しました";
    s.receive_failed = "受信に失敗しました";
    s.transfer_failed = "転送に失敗しました";
    s.transfer_complete = "転送が完了しました";
    s.press_reset_hint = "本体裏面のリセットボタンを押して再起動してください";
    s.open_card = "名札データを開く";

    s.devices_separated = "転送が完了する前に端末が離れました";
    s.card_exchange_failed = "名札データの交換に失敗しました";
    s.hold_screen_hint = "画面同士を重ねると共有が始まります";
    s.hotknot_approach = "HotKnot 接続";
    s.keep_held_hint = "端末を重ねたままお待ちください";
    s.connection_failed_retry = "接続に失敗しました。もう一度お試しください";

    s.hold_button_wake = "側面のボタンを長押しして起動";
    s.press_button_wake = "側面のボタンを押して起動";

    s.language_title = "Language / 言語";
    return s;
}

const Strings En = make_en();
const Strings Ja = make_ja();
const Strings *s_active = &En;

}  // namespace

const Strings &S() { return *s_active; }

namespace strings {

void set(Lang lang) {
    s_active = (lang == Lang::Ja) ? &Ja : &En;
}

}  // namespace strings
