#pragma once
#include "BubbleFrame.h"
#include <QEvent>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
class TextBuble : public BubbleFrame {
    Q_OBJECT
  public:
    TextBuble(ChatRole role, const QString& text, QWidget* parent = nullptr);

  protected:
    bool eventFilter(QObject* o, QEvent* e);

  private:
    void adjustTextHeight();
    void setPlainText(const QString& text);
    void initStyleSheet();

  private:
    QTextEdit* m_pTextEdit;
};
