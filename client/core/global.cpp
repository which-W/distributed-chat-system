#include "global.h"

QString gate_url_prefix = "";
bool allow_insecure_transport = false;
//刷新qss
std::function<void(QWidget*)> repolish = [](QWidget* w) {
	w->style()->unpolish(w);
	w->style()->polish(w);
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
