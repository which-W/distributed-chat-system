#include "CustomizeEdit.h"

CustomizeEdit::CustomizeEdit(QWidget* parent)
{
	connect(this, &QLineEdit::textChanged, this, &CustomizeEdit::limitTextLength);
}

void CustomizeEdit::SetMaxLength(int maxLen)
{
	_max_len = maxLen;
}
