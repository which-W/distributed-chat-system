#pragma once
#include <QString>

// One palette for the QWidget workspace, including its child dialogs.
inline QString chatStyle(bool dark) {
    QString css = QStringLiteral(R"(
QWidget { color: @text; font-family: 'Microsoft YaHei UI'; font-size: 14px; }
QWidget#ChatDialogClass, #chat_data_wid, #chat_data_stacked, #chat_bg,
#chat_area, #Chat_Page, #friend_apply_page, #friend_info_page { background: @surface; }
#chat_user_win { background: @surface; border-right: 1px solid @border; }
#sectionTitle { font-size: 25px; font-weight: 700; padding: 20px 18px 8px; }
#search_win { background: @surface; }
QLabel { background: transparent; border: none; }
QLineEdit { background: @input; color: @text; border: 1px solid transparent;
    border-radius: 12px; padding: 5px 10px; selection-background-color: #0866ff; }
QLineEdit:focus { border-color: #0866ff; }
QLineEdit#lb_edit { padding: 0 3px; border-radius: 4px; }
QListWidget, QScrollArea { background: @surface; border: none; outline: none; }
QListWidget { padding: 6px; }
QListWidget::item { border-radius: 12px; padding: 2px; }
QListWidget::item:hover { background: @input; }
QListWidget::item:selected { background: @selected; }
ChatUserWid, ConUserItem, GroupTipItem, AddUserItem, ApplyFriendItem { background: transparent; }
#user_name_lb, #name_lb { font-size: 15px; font-weight: 600; }
#user_data_lb { color: @muted; font-size: 12px; }
GroupTipItem QLabel { color: @muted; font-size: 12px; padding-left: 12px; }
#title_wid, #friend_apply_wid { background: @surface; border-bottom: 1px solid @border; }
#title_lb, #friend_apply_lb { font-size: 20px; font-weight: 600; }
#composer { background: @surface; }
QTextEdit { color: @text; background: @input; border: none; border-radius: 16px; padding: 6px; }
#chatEmptyState, #friendEmptyState { color: @muted; font-size: 16px; padding: 24px; }
QPushButton { color: white; background: #0866ff; border: none; border-radius: 10px;
    padding: 8px 16px; }
QPushButton:hover { background: #2478ff; }
QPushButton:pressed { background: #0054d6; }
QPushButton:disabled { background: @input; color: @muted; }
#search_btn { font-size: 26px; padding: 0; background: @selected; color: #0866ff; border-radius: 20px; }
#emo_lb, #file_lb { font-size: 28px; color: #0866ff; border-radius: 16px; }
#emo_lb:hover, #file_lb:hover { background: @selected; }
QDialog, QDialog QWidget, QMenu { background: @surface; }
QDialog { border: 1px solid @border; }
QDialog QLineEdit { background: @input; }
QDialog QLabel { background: transparent; }
QDialog QPushButton { background: #0866ff; }
#cancel_btn { color: @text; background: @input; }
QMenu { border: 1px solid @border; padding: 6px; }
QMenu::item { padding: 8px 18px; }
QMenu::item:selected { background: @selected; }
QScrollBar:vertical { background: transparent; width: 7px; margin: 0; }
QScrollBar::handle:vertical { background: @border; border-radius: 3px; min-height: 28px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QProgressBar { border: none; border-radius: 4px; background: @input; color: @muted; text-align: center; min-height: 10px; }
QProgressBar::chunk { background: #0866ff; border-radius: 4px; }
)");
    css.replace("@surface", dark ? "#202124" : "#ffffff");
    css.replace("@text", dark ? "#e9edf5" : "#172033");
    css.replace("@muted", dark ? "#a2adbd" : "#788397");
    css.replace("@input", dark ? "#303238" : "#f0f2f5");
    css.replace("@border", dark ? "#3c4048" : "#e6eaf0");
    css.replace("@selected", dark ? "#243b62" : "#e8efff");
    return css;
}
