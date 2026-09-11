#pragma once
#include <QLineEdit>
#include <QDebug>

class CustomizeEdit : public QLineEdit {
    Q_OBJECT
  public:
    CustomizeEdit(QWidget* parent = nullptr);
    void SetMaxLength(int maxLen);

  protected:
    void focusOutEvent(QFocusEvent* event) override {
        // 执行失去焦点时的处理逻辑
        // qDebug() << "CustomizeEdit focusout";
        // 调用基类的focusOutEvent()方法，保证基类的行为得到执行
        QLineEdit::focusOutEvent(event);
        // 发送失去焦点得信号
        emit sig_foucus_out();
    }

  private:
    void limitTextLength(QString text) {
        if (_max_len <= 0) {
            return;
        }

        if (text.toUtf8().size() <= _max_len) return;
        const int cursor = cursorPosition();
        while (!text.isEmpty() && text.toUtf8().size() > _max_len) {
            const bool pair = text.size() > 1 && text.back().isLowSurrogate()
                              && text.at(text.size() - 2).isHighSurrogate();
            text.chop(pair ? 2 : 1);
        }
        setText(text);
        setCursorPosition(qMin(cursor, int(text.size())));
    }

    int _max_len;
  signals:
    void sig_foucus_out();
};
