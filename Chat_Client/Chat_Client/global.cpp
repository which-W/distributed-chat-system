#include "global.h"

QString gate_url_prefix = "";
//刷新qss
std::function<void(QWidget*)> repolish = [](QWidget* w) {
	w->style()->unpolish(w);
	w->style()->polish(w);
};

std::function<QString(QString)> xosString = [](QString input) {
	QByteArray byteArray = input.toUtf8(); // 将输入字符串转换为字节数组
	QByteArray hash = QCryptographicHash::hash(byteArray, QCryptographicHash::Sha256); // 使用 Sha256 进行加密
	return QString(hash.toHex()); // 返回十六进制格式的加密结果
};

std::vector<QString>  strs = { "hello world !",
							 "nice to meet u",
							 "New year，new life",
							"You have to love yourself",
							"My love is written in the wind ever since the whole world is you" };

std::vector<QString> heads = {
	":/res/head_1.jpg",
	":/res/head_2.jpg",
	":/res/head_3.jpg",
	":/res/head_4.jpg",
	":/res/head_5.jpg"
};

std::vector<QString> names = {
	"xzz",
	"golang",
	"cpp",
	"java",
	"nodejs",
	"python",
	"rust"
};
