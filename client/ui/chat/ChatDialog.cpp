#include "ChatDialog.h"

ChatDialog::ChatDialog(QWidget *parent)
	: QWidget(parent)
	, ui(new Ui::ChatDialogClass()),_mode(ChatUIMode::ChatMode),
	_state(ChatUIMode::ChatMode), _b_loading(false)
{
	ui->setupUi(this);
	ui->search_btn->SetState("normal", "hover", "press");
	ui->search_line->setMaxLength(15);
	// 设置搜索按钮的图标和样式
	QAction* searchAction = new QAction(ui->search_line);
	searchAction->setIcon(QIcon(":/res/search.png"));
	ui->search_line->addAction(searchAction, QLineEdit::LeadingPosition);
	ui->search_line->setPlaceholderText(QStringLiteral("搜索"));
	// 设置搜索按钮的触发事件
	QAction* clearAction = new QAction(ui->search_line);
	clearAction->setIcon(QIcon(":/res/transparent.png"));
	ui->search_line->addAction(clearAction, QLineEdit::TrailingPosition);
	// 设置搜索按钮的样式
	connect(ui->search_line, &QLineEdit::textChanged, [this, clearAction](const QString& text) {
		if (!text.isEmpty()) {
			clearAction->setIcon(QIcon(":/res/delete.png"));
			ShowSearch(true);
		}
		else {
			clearAction->setIcon(QIcon(":/res/transparent.png"));
			ShowSearch(false);
		}
		});
	connect(clearAction, &QAction::triggered, [this , clearAction]() {
		ui->search_line->clear();
		clearAction->setIcon(QIcon(":/res/transparent.png"));
		ui->search_line->clearFocus();
		//清除按钮被按下则不显示搜索框
	   ShowSearch(false);
		});

	//加载自己头像
	QString head_icon = UserMgr::Getinstance()->GetIcon();
	QPixmap pixmap(head_icon); // 加载图片
	QPixmap scaledPixmap = pixmap.scaled(ui->front_bar->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation); // 将图片缩放到label的大小
	ui->front_bar->setPixmap(scaledPixmap); // 将缩放后的图片设置到QLabel上
	ui->front_bar->setScaledContents(true); // 设置QLabel自动缩放图片内容以适应大小

	ui->chat_bar->setProperty("state", "normal");

	ui->chat_bar->SetState("normal", "hover", "pressed", "selected_normal", "selected_hover", "selected_pressed");

	ui->contact_bar->SetState("normal", "hover", "pressed", "selected_normal", "selected_hover", "selected_pressed");

	AddLBGroup(ui->chat_bar);
	AddLBGroup(ui->contact_bar);



	connect(ui->chat_bar, &StateWidget::clicked, this, &ChatDialog::slot_side_chat);
	connect(ui->contact_bar, &StateWidget::clicked, this, &ChatDialog::slot_side_contact);
	connect(ui->contact_bar, &StateWidget::clicked, ui->contact_list, &ContactUserList::sig_clicked_user);
	//加载用户列表
	connect(ui->chat_search_list, &ChatUserList::sig_loading_chat_user, this, &ChatDialog::slot_loading_chat_user);
	addChatUserList(); // 添加聊天用户列表
	//设置选中条目
	SetSelectChatItem();
	//更新聊天界面信息
	SetSelectChatPage();

	ShowSearch(false); // 默认显示聊天界面

	//安装事件过滤器
	this->installEventFilter(this);
	//为searchlist 设置search edit
	ui->search_list->SetSearchEdit(ui->search_line);
	//好友申请信号连接
	connect(TcpMgr::Getinstance().get(), &TcpMgr::sig_friend_apply, this, &ChatDialog::slot_apply_friend);

	//连接认证添加好友信号
	connect(TcpMgr::Getinstance().get(), &TcpMgr::sig_add_auth_friend, this, &ChatDialog::slot_add_auth_friend);

	//链接自己认证回复信号
	connect(TcpMgr::Getinstance().get(), &TcpMgr::sig_auth_rsp, this,
		&ChatDialog::slot_auth_rsp);
	//连接搜索的跳转信号
	connect(ui->search_list, &SearchList::sig_jump_chat_item, this, &ChatDialog::slot_jump_chat);

	//连接加载更多联系人的信号和槽函数
	connect(ui->contact_list, &ContactUserList::sig_loading_contact_user, this, &ChatDialog::slot_loading_contact_user);

	//连接contactlist的add_friend_info信号
	connect(ui->contact_list, &ContactUserList::sig_switch_friend_info_page, this, &ChatDialog::slot_switch_friend_info_page);

	//连接contactlist的sig_switch_apply_friend_page信号
	connect(ui->contact_list, &ContactUserList::sig_switch_apply_friend_page, this, &ChatDialog::slot_switch_apply_friend_page);

	//连接friend_page的sig_jump_chat_item信号
	connect(ui->friend_info_page, &FriendInfoPage::sig_jump_chat_item, this, &ChatDialog::slot_jump_chat_again);
	//设置chat_page为主页面
	ui->chat_data_stacked->setCurrentWidget(ui->Chat_Page);

	connect(ui->chat_search_list, &QListWidget::itemClicked, this, &ChatDialog::slot_item_clicked);

	connect(ui->Chat_Page, &ChatPage::sig_append_send_chat_msg, this, &ChatDialog::slot_append_send_chat_msg);

	connect(TcpMgr::Getinstance().get(), &TcpMgr::sig_text_chat_msg, this,&ChatDialog::slot_chat_msg_changed);
}

ChatDialog::~ChatDialog()
{
	delete ui;

}

void ChatDialog::ShowSearch(bool bsearch)
{
	if (bsearch) {
		ui->search_list->show();
		ui->contact_list->hide();
		ui->chat_search_list->hide();
		_mode = ChatUIMode::SearchMode;
	}
	else if (_state == ChatUIMode::ChatMode) {
		ui->search_list->hide();
		ui->contact_list->hide();
		ui->chat_search_list->show();
		_mode = ChatUIMode::ChatMode;
	}
	else if (_state == ChatUIMode::ConTactMode) {
		ui->contact_list->show();
		ui->search_list->hide();
		ui->chat_search_list->hide();
		_mode = ChatUIMode::ConTactMode;
	}

}

void ChatDialog::addChatUserList()
{

	//先按照好友列表加载聊天记录，等以后客户端实现聊天记录数据库之后再按照最后信息排序
	auto friend_list = UserMgr::Getinstance()->GetChatListPerPage();
	if (friend_list.empty() == false) {
		for (auto& friend_ele : friend_list) {
			auto find_iter = _chat_items_added.find(friend_ele->_uid);
			if (find_iter != _chat_items_added.end()) {
				continue;
			}
			auto* chat_user_wid = new ChatUserWid();
			auto user_info = std::make_shared<UserInfo>(friend_ele);
			chat_user_wid->SetInfo(user_info);
			QListWidgetItem* item = new QListWidgetItem;
			item->setSizeHint(chat_user_wid->sizeHint());
			ui->chat_search_list->addItem(item);
			ui->chat_search_list->setItemWidget(item, chat_user_wid);
			_chat_items_added.insert(friend_ele->_uid, item);
		}

		//更新已加载条目
		UserMgr::Getinstance()->UpdateChatLoadedCount();
	}

	//模拟测试条目
	// 创建QListWidgetItem，并设置自定义的widget
	//for (int i = 0; i < 13; i++) {
	//	int randomValue = QRandomGenerator::global()->bounded(100); // 生成0到99之间的随机整数
	//	int str_i = randomValue % strs.size();
	//	int head_i = randomValue % heads.size();
	//	int name_i = randomValue % names.size();

	//	auto* chat_user_wid = new ChatUserWid();
	//	auto user_info = std::make_shared<UserInfo>(0, names[name_i],
	//		names[name_i], heads[head_i], 0, strs[str_i]);
	//	chat_user_wid->SetInfo(user_info);
	//	QListWidgetItem* item = new QListWidgetItem;
	//	item->setSizeHint(chat_user_wid->sizeHint());
	//	ui->chat_search_list->addItem(item);
	//	ui->chat_search_list->setItemWidget(item, chat_user_wid);
	//}
}

void ChatDialog::AddLBGroup(StateWidget* st)
{
	_lb_group.push_back(st);
}

void ChatDialog::ClearLabelState(StateWidget* st)
{
	for (auto& ele : _lb_group) {
		if (ele == st) {
			continue;
		}

		ele->ClearState();
	}
}

void ChatDialog::handleglobeQmousePress(QMouseEvent* mouseEvent)
{
	// 实现点击位置的判断和处理逻辑
   // 先判断是否处于搜索模式，如果不处于搜索模式则直接返回
	if (_mode != ChatUIMode::SearchMode) {
		return;
	}

	// 将鼠标点击位置转换为搜索列表坐标系中的位置
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	QPoint posInSearchList = ui->search_list->mapFromGlobal(mouseEvent->globalPosition().toPoint());
#else
	QPoint posInSearchList = ui->search_list->mapFromGlobal(mouseEvent->globalPos());
#endif
	// 判断点击位置是否在聊天列表的范围内
	if (!ui->search_list->rect().contains(posInSearchList)) {
		// 如果不在聊天列表内，清空输入框
		ui->search_line->clear();
		ShowSearch(false);
	}
}

void ChatDialog::SetSelectChatItem(int uid)
{
	if (ui->chat_search_list->count() <= 0) {
		return;
	}

	if (uid == 0) {
		ui->chat_search_list->setCurrentRow(0);
		QListWidgetItem* firstItem = ui->chat_search_list->item(0);
		if (!firstItem) {
			return;
		}

		//转为widget
		QWidget* widget = ui->chat_search_list->itemWidget(firstItem);
		if (!widget) {
			return;
		}

		auto con_item = qobject_cast<ChatUserWid*>(widget);
		if (!con_item) {
			return;
		}

		_cur_chat_uid = con_item->GetUserInfo()->_uid;

		return;
	}

	auto find_iter = _chat_items_added.find(uid);
	if (find_iter == _chat_items_added.end()) {
		qDebug() << "uid " << uid << " not found, set curent row 0";
		ui->chat_search_list->setCurrentRow(0);
		return;
	}

	ui->chat_search_list->setCurrentItem(find_iter.value());

	_cur_chat_uid = uid;
}

void ChatDialog::SetSelectChatPage(int uid)
{
	if (ui->chat_search_list->count() <= 0) {
		return;
	}

	if (uid == 0) {
		auto item = ui->chat_search_list->item(0);
		//转为widget
		QWidget* widget = ui->chat_search_list->itemWidget(item);
		if (!widget) {
			return;
		}

		auto con_item = qobject_cast<ChatUserWid*>(widget);
		if (!con_item) {
			return;
		}

		//设置信息
		auto user_info = con_item->GetUserInfo();
		ui->Chat_Page->SetUserInfo(user_info);
		return;
	}

	auto find_iter = _chat_items_added.find(uid);
	if (find_iter == _chat_items_added.end()) {
		return;
	}

	//转为widget
	QWidget* widget = ui->chat_search_list->itemWidget(find_iter.value());
	if (!widget) {
		return;
	}

	//判断转化为自定义的widget
	// 对自定义widget进行操作， 将item 转化为基类ListItemBase
	ListItemBase* customItem = qobject_cast<ListItemBase*>(widget);
	if (!customItem) {
		qDebug() << "qobject_cast<ListItemBase*>(widget) is nullptr";
		return;
	}

	auto itemType = customItem->GetItemType();
	if (itemType == CHAT_USER_ITEM) {
		auto con_item = qobject_cast<ChatUserWid*>(customItem);
		if (!con_item) {
			return;
		}

		//设置信息
		auto user_info = con_item->GetUserInfo();
		ui->Chat_Page->SetUserInfo(user_info);

		return;
	}
}

void ChatDialog::LoadMoreChatWid()
{
	auto friend_list = UserMgr::Getinstance()->GetChatListPerPage();
	if (friend_list.empty() == false) {
		for (auto& friend_ele : friend_list) {
			auto find_iter = _chat_items_added.find(friend_ele->_uid);
			if (find_iter != _chat_items_added.end()) {
				continue;
			}
			auto* chat_user_wid = new ChatUserWid();
			auto user_info = std::make_shared<UserInfo>(friend_ele);
			chat_user_wid->SetInfo(user_info);
			QListWidgetItem* item = new QListWidgetItem;
			item->setSizeHint(chat_user_wid->sizeHint());
			ui->chat_search_list->addItem(item);
			ui->chat_search_list->setItemWidget(item, chat_user_wid);
			_chat_items_added.insert(friend_ele->_uid, item);
		}

		//更新已加载条目
		UserMgr::Getinstance()->UpdateChatLoadedCount();
	}
}

void ChatDialog::LoadMoreConWid()
{
	auto friend_list = UserMgr::Getinstance()->GetConListPerPage();
	if (friend_list.empty() == false) {
		for (auto& friend_ele : friend_list) {
			auto* chat_user_wid = new ConUserItem();
			chat_user_wid->SetInfo(friend_ele->_uid, friend_ele->_name,
				friend_ele->_icon);
			QListWidgetItem* item = new QListWidgetItem;
			//qDebug()<<"chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
			item->setSizeHint(chat_user_wid->sizeHint());
			ui->contact_list->addItem(item);
			ui->contact_list->setItemWidget(item, chat_user_wid);
		}

		//更新已加载条目
		UserMgr::Getinstance()->UpdateContactLoadedCount();
	}
}

bool ChatDialog::eventFilter(QObject* watch, QEvent* event)
{
	if (event->type() == QEvent::MouseButtonPress) {
		QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
		handleglobeQmousePress(mouseEvent);
	}
	return QWidget::eventFilter(watch, event);
}

void ChatDialog::slot_loading_chat_user()
{
	if (_b_loading) {
		return;
	}
	_b_loading = true;

	auto* Loadingwidget = new LoadingWidget(this);
	_loadingitem = new QListWidgetItem();
	_loadingitem->setSizeHint(Loadingwidget->sizeHint());
	ui->chat_search_list->addItem(_loadingitem);
	ui->chat_search_list->setItemWidget(_loadingitem, Loadingwidget);


	// 模拟加载延时
	QTimer::singleShot(500, this, [=]() {
		// 添加新的聊天用户
		qDebug() << "add new user...";
		LoadMoreChatWid();
		if (_loadingitem) {
			ui->chat_search_list->takeItem(ui->chat_search_list->row(_loadingitem));
			_loadingitem = nullptr;
		}
		// 重置加载状态
		ui->chat_search_list->update();
		_b_loading = false; // 重置加载状态
		});



}

void ChatDialog::slot_side_chat()
{
	qDebug() << "receive side chat clicked";
	ClearLabelState(ui->chat_bar);
	ui->chat_data_stacked->setCurrentWidget(ui->Chat_Page);
	_state = ChatUIMode::ChatMode;
	ShowSearch(false);
}

void ChatDialog::slot_side_contact()
{
	qDebug() << "receive side contact clicked";
	ClearLabelState(ui->contact_bar);
	//设置
	if (_last_widget == nullptr) {
		ui->chat_data_stacked->setCurrentWidget(ui->friend_apply_page);
		_last_widget = ui->friend_apply_page;
	}
	else {
		ui->chat_data_stacked->setCurrentWidget(_last_widget);
	}

	_state = ChatUIMode::ConTactMode;
	ShowSearch(false);

}

void ChatDialog::slot_apply_friend(std::shared_ptr<AddFriendApply> applyinfo)
{
	if (applyinfo == nullptr) {
		qDebug() << "applyinfo is null";
		return;
	}
	qDebug() << "receive apply friend info, uid is " << applyinfo->_from_uid << ", name is " << applyinfo->_name;
	bool b_exist = UserMgr::Getinstance()->isAlreadyApply(applyinfo->_from_uid);
	if (b_exist) {
		return;
	}

	UserMgr::Getinstance()->AddApplyList(std::make_shared<ApplyInfo>(applyinfo));
	qDebug() << "contact_bar type:" << ui->contact_bar->metaObject()->className();
	QTimer::singleShot(0, this, [=]() {
		ui->contact_bar->ShowRedPoint(true);
		ui->contact_list->ShowRedPoint(true);
	});
	ui->friend_apply_page->AddNewApply(applyinfo);

}

void ChatDialog::slot_add_auth_friend(std::shared_ptr<AuthInfo> auth_info)
{
	qDebug() << "receive slot_add_auth__friend uid is " << auth_info->_uid
		<< " name is " << auth_info->_name << " nick is " << auth_info->_nick;

	//判断如果已经是好友则跳过
	auto bfriend = UserMgr::Getinstance()->CheckFriendById(auth_info->_uid);
	if (bfriend) {
		return;
	}

	UserMgr::Getinstance()->AddFriend(auth_info);

	int randomValue = QRandomGenerator::global()->bounded(100); // 生成0到99之间的随机整数
	int str_i = randomValue % strs.size();
	int head_i = randomValue % heads.size();
	int name_i = randomValue % names.size();

	auto* chat_user_wid = new ChatUserWid();
	auto user_info = std::make_shared<UserInfo>(auth_info);
	chat_user_wid->SetInfo(user_info);
	QListWidgetItem* item = new QListWidgetItem;
	//qDebug()<<"chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
	item->setSizeHint(chat_user_wid->sizeHint());
	ui->chat_search_list->insertItem(0, item);
	ui->chat_search_list->setItemWidget(item, chat_user_wid);
	_chat_items_added.insert(auth_info->_uid, item);
}

void ChatDialog::slot_auth_rsp(std::shared_ptr<AuthRsp> auth_rsp)
{
	qDebug() << "receive slot_auth_rsp uid is " << auth_rsp->_uid
		<< " name is " << auth_rsp->_name << " nick is " << auth_rsp->_nick;
	qDebug() << "icon is " << auth_rsp->_icon;

	//判断如果已经是好友则跳过
	auto bfriend = UserMgr::Getinstance()->CheckFriendById(auth_rsp->_uid);
	if (bfriend) {
		return;
	}

	UserMgr::Getinstance()->AddFriend(auth_rsp);
	int randomValue = QRandomGenerator::global()->bounded(100); // 生成0到99之间的随机整数
	int str_i = randomValue % strs.size();
	int head_i = randomValue % heads.size();
	int name_i = randomValue % names.size();

	auto* chat_user_wid = new ChatUserWid();
	auto user_info = std::make_shared<UserInfo>(auth_rsp);
	chat_user_wid->SetInfo(user_info);
	QListWidgetItem* item = new QListWidgetItem;
	item->setSizeHint(chat_user_wid->sizeHint());
	ui->chat_search_list->insertItem(0, item);
	ui->chat_search_list->setItemWidget(item, chat_user_wid);
	_chat_items_added.insert(auth_rsp->_uid, item);
}

void ChatDialog::slot_jump_chat(std::shared_ptr<SearchInfo> si)
{
	qDebug() << "slot jump chat item";
	auto find_iter = _chat_items_added.find(si->_uid);
	if (find_iter != _chat_items_added.end()) {
		qDebug() << "jump to chat item , uid is " << si->_uid;
		ui->chat_search_list->scrollToItem(find_iter.value());
		ui->chat_bar->SetSelected(true);
		SetSelectChatItem(si->_uid);
		//更新聊天界面信息
		SetSelectChatPage(si->_uid);
		slot_side_chat();
		return;
	}

	//如果没找到，则创建新的插入listwidget

	auto* chat_user_wid = new ChatUserWid();
	auto user_info = std::make_shared<UserInfo>(si);
	chat_user_wid->SetInfo(user_info);
	QListWidgetItem* item = new QListWidgetItem;
	//qDebug()<<"chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
	item->setSizeHint(chat_user_wid->sizeHint());
	ui->chat_search_list->insertItem(0, item);
	ui->chat_search_list->setItemWidget(item, chat_user_wid);

	_chat_items_added.insert(si->_uid, item);

	ui->chat_bar->SetSelected(true);
	SetSelectChatItem(si->_uid);
	//更新聊天界面信息
	SetSelectChatPage(si->_uid);
	slot_side_chat();
}

void ChatDialog::slot_loading_contact_user()
{
	qDebug() << "slot loading contact user";
	if (_b_loading) {
		return;
	}

	_b_loading = true;
	Loadingdlg* loadingDialog = new Loadingdlg(this);
	loadingDialog->setModal(true);
	loadingDialog->show();
	qDebug() << "add new data to list.....";
	LoadMoreConWid();
	// 加载完成后关闭对话框
	loadingDialog->deleteLater();

	_b_loading = false;
}

void ChatDialog::slot_switch_friend_info_page(std::shared_ptr<UserInfo> user_info)
{
	qDebug() << "receive switch friend info page sig";
	_last_widget = ui->friend_info_page;
	ui->chat_data_stacked->setCurrentWidget(ui->friend_info_page);
	ui->friend_info_page->SetInfo(user_info);
}

void ChatDialog::slot_switch_apply_friend_page()
{
	qDebug() << "receive switch apply friend page sig";
	_last_widget = ui->friend_apply_page;
	ui->chat_data_stacked->setCurrentWidget(ui->friend_apply_page);
}

void ChatDialog::slot_jump_chat_again(std::shared_ptr<UserInfo> user_info)
{
	qDebug() << "slot jump chat item";
	auto find_iter = _chat_items_added.find(user_info->_uid);
	if (find_iter != _chat_items_added.end()) {
		qDebug() << "jump to chat item , uid is " << user_info->_uid;
		ui->chat_search_list->scrollToItem(find_iter.value());
		ui->chat_bar->SetSelected(true);
		SetSelectChatItem(user_info->_uid);
		//更新聊天界面信息
		SetSelectChatPage(user_info->_uid);
		slot_side_chat();
		return;
	}

	//如果没找到，则创建新的插入listwidget

	auto* chat_user_wid = new ChatUserWid();
	chat_user_wid->SetInfo(user_info);
	QListWidgetItem* item = new QListWidgetItem;
	//qDebug()<<"chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
	item->setSizeHint(chat_user_wid->sizeHint());
	ui->chat_search_list->insertItem(0, item);
	ui->chat_search_list->setItemWidget(item, chat_user_wid);

	_chat_items_added.insert(user_info->_uid, item);

	ui->chat_bar->SetSelected(true);
	SetSelectChatItem(user_info->_uid);
	//更新聊天界面信息
	SetSelectChatPage(user_info->_uid);
	slot_side_chat();
}

void ChatDialog::slot_append_send_chat_msg(std::shared_ptr<TextChatData> msgdata)
{
	if (_cur_chat_uid == 0) {
		return;
	}

	auto find_iter = _chat_items_added.find(_cur_chat_uid);
	if (find_iter == _chat_items_added.end()) {
		return;
	}

	//转为widget
	QWidget* widget = ui->chat_search_list->itemWidget(find_iter.value());
	if (!widget) {
		return;
	}

	//判断转化为自定义的widget
	// 对自定义widget进行操作， 将item 转化为基类ListItemBase
	ListItemBase* customItem = qobject_cast<ListItemBase*>(widget);
	if (!customItem) {
		qDebug() << "qobject_cast<ListItemBase*>(widget) is nullptr";
		return;
	}

	auto itemType = customItem->GetItemType();
	if (itemType == CHAT_USER_ITEM) {
		auto con_item = qobject_cast<ChatUserWid*>(customItem);
		if (!con_item) {
			return;
		}

		//设置信息
		auto user_info = con_item->GetUserInfo();
		user_info->_chat_msgs.push_back(msgdata);
		std::vector<std::shared_ptr<TextChatData>> msg_vec;
		msg_vec.push_back(msgdata);
		UserMgr::Getinstance()->AppendFriendChatMsg(_cur_chat_uid, msg_vec);
		return;
	}

}

void ChatDialog::slot_item_clicked(QListWidgetItem* item)
{
	QWidget* widget = ui->chat_search_list->itemWidget(item); // 获取自定义widget对象
	if (!widget) {
		qDebug() << "slot item clicked widget is nullptr";
		return;
	}

	// 对自定义widget进行操作， 将item 转化为基类ListItemBase
	ListItemBase* customItem = qobject_cast<ListItemBase*>(widget);
	if (!customItem) {
		qDebug() << "slot item clicked widget is nullptr";
		return;
	}

	auto itemType = customItem->GetItemType();
	if (itemType == ListItemType::INVALID_ITEM
		|| itemType == ListItemType::GROUP_TIP_ITEM) {
		qDebug() << "slot invalid item clicked ";
		return;
	}


	if (itemType == ListItemType::CHAT_USER_ITEM) {
		// 创建对话框，提示用户
		qDebug() << "contact user item clicked ";

		auto chat_wid = qobject_cast<ChatUserWid*>(customItem);
		auto user_info = chat_wid->GetUserInfo();
		//跳转到聊天界面
		ui->Chat_Page->SetUserInfo(user_info);
		_cur_chat_uid = user_info->_uid;
		return;
	}
}

void ChatDialog::slot_chat_msg_changed(std::shared_ptr<TextChatMsg> msg)
{
	auto find_iter = _chat_items_added.find(msg->_from_uid);
	if (find_iter != _chat_items_added.end()) {
		qDebug() << "set chat item msg, uid is " << msg->_from_uid;
		QWidget* widget = ui->chat_search_list->itemWidget(find_iter.value());
		auto chat_wid = qobject_cast<ChatUserWid*>(widget);
		if (!chat_wid) {
			return;
		}
		chat_wid->updateLastMsg(msg->_chat_msgs);
		//更新当前聊天页面记录
		UpdateChatMsg(msg->_chat_msgs);
		UserMgr::Getinstance()->AppendFriendChatMsg(msg->_from_uid, msg->_chat_msgs);
		return;
	}
	//如果没找到，则创建新的插入listwidget

	auto* chat_user_wid = new ChatUserWid();
	//查询好友信息
	auto fi_ptr = UserMgr::Getinstance()->GetFriendById(msg->_from_uid);
	chat_user_wid->SetInfo(fi_ptr);
	QListWidgetItem* item = new QListWidgetItem;
	item->setSizeHint(chat_user_wid->sizeHint());
	chat_user_wid->updateLastMsg(msg->_chat_msgs);
	UserMgr::Getinstance()->AppendFriendChatMsg(msg->_from_uid, msg->_chat_msgs);
	ui->chat_search_list->insertItem(0, item);
	ui->chat_search_list->setItemWidget(item, chat_user_wid);
	_chat_items_added.insert(msg->_from_uid, item);

}

void ChatDialog::UpdateChatMsg(std::vector<std::shared_ptr<TextChatData> > msgdata)
{
	for (auto& msg : msgdata) {
		if (msg->_from_uid != _cur_chat_uid) {
			break;
		}

		ui->Chat_Page->AppendChatMsg(msg);
	}
}

void ChatDialog::setExternalNavigationEnabled(bool enabled)
{
	ui->bar_list->setVisible(!enabled);
}
