#pragma once
#include <QLabel>
#include <QMouseEvent>
class ClickOnceLabel :public QLabel
{
	Q_OBJECT
public:
	ClickOnceLabel(QWidget* parent = nullptr);
protected:
	void mouseReleaseEvent(QMouseEvent* ev) override;
signals:
	void clicked(QString);
private:

};
