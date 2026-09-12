#include "ApplyFriendPage.h"

ApplyFriendPage::ApplyFriendPage(QWidget* parent)
    : QWidget(parent), ui(new Ui::ApplyFriendPageClass()) {
    ui->setupUi(this);
    ui->friend_apply_wid->setMinimumHeight(72);
    auto* add = new QPushButton(tr("添加朋友"), this);
    add->setObjectName("addFriendButton");
    ui->horizontalLayout->addWidget(add);
    ui->horizontalLayout->setContentsMargins(20, 12, 20, 12);
    connect(add, &QPushButton::clicked, this, [this]() { emit sig_show_search(true); });
    auto* empty = new QLabel(tr("暂无好友申请\n点击“添加朋友”，通过 UID 或用户名找到对方"), this);
    empty->setObjectName("friendEmptyState");
    empty->setAlignment(Qt::AlignCenter);
    ui->verticalLayout->addWidget(empty, 1);
    auto updateEmpty = [this, empty]() {
        const bool isEmpty = ui->apply_friend_list->count() == 0;
        empty->setVisible(isEmpty);
        ui->apply_friend_list->parentWidget()->setVisible(!isEmpty);
    };
    connect(ui->apply_friend_list->model(), &QAbstractItemModel::rowsInserted, this, updateEmpty);
    connect(ui->apply_friend_list->model(), &QAbstractItemModel::rowsRemoved, this, updateEmpty);
    connect(ui->apply_friend_list, &ApplyFriendList::sig_show_search, this,
            &ApplyFriendPage::sig_show_search);
    loadApplyList();
    updateEmpty();
    // 接受tcp传递的authrsp信号处理
    connect(TcpMgr::Getinstance().get(), &TcpMgr::sig_auth_rsp, this,
            &ApplyFriendPage::slot_auth_rsp);
    connect(TcpMgr::Getinstance().get(), &TcpMgr::sig_friend_snapshot, this, [this, updateEmpty]() {
        _unauth_items.clear();
        ui->apply_friend_list->clear();
        loadApplyList();
        updateEmpty();
    });
}

ApplyFriendPage::~ApplyFriendPage() {
    delete ui;
}

void ApplyFriendPage::AddNewApply(std::shared_ptr<AddFriendApply> apply) {
    auto* apply_item = new ApplyFriendItem();
    auto apply_info = std::make_shared<ApplyInfo>(apply->_from_uid, apply->_name, apply->_desc,
                                                  apply->_icon, apply->_nick, apply->_sex, 0);
    apply_item->SetInfo(apply_info);
    QListWidgetItem* item = new QListWidgetItem;
    // qDebug()<<"chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
    item->setSizeHint(apply_item->sizeHint());
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    ui->apply_friend_list->insertItem(0, item);
    ui->apply_friend_list->setItemWidget(item, apply_item);
    apply_item->ShowAddBtn(true);
    _unauth_items[apply->_from_uid] = apply_item;
    // 收到审核好友信号
    connect(apply_item, &ApplyFriendItem::sig_auth_friend,
            [this](std::shared_ptr<ApplyInfo> apply_info) {
                auto* authFriend = new AuthenFriend(this);
                authFriend->setAttribute(Qt::WA_DeleteOnClose);
                authFriend->setModal(true);
                authFriend->SetApplyInfo(apply_info);
                authFriend->show();
            });
}

void ApplyFriendPage::paintEvent(QPaintEvent* event) {
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void ApplyFriendPage::loadApplyList() {
    // 添加好友申请
    auto apply_list = UserMgr::Getinstance()->GetApplyList();
    for (auto& apply : apply_list) {
        auto* apply_item = new ApplyFriendItem();
        apply_item->SetInfo(apply);
        QListWidgetItem* item = new QListWidgetItem;
        // qDebug()<<"chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
        item->setSizeHint(apply_item->sizeHint());
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        ui->apply_friend_list->addItem(item);
        ui->apply_friend_list->setItemWidget(item, apply_item);
        if (apply->_status) {
            apply_item->ShowAddBtn(false);
        } else {
            apply_item->ShowAddBtn(true);
            auto uid = apply_item->GetUid();
            _unauth_items[uid] = apply_item;
        }

        // 收到审核好友信号
        connect(apply_item, &ApplyFriendItem::sig_auth_friend,
                [this](std::shared_ptr<ApplyInfo> apply_info) {
                    auto* authFriend = new AuthenFriend(this);
                    authFriend->setAttribute(Qt::WA_DeleteOnClose);
                    authFriend->setModal(true);
                    authFriend->SetApplyInfo(apply_info);
                    authFriend->show();
                });
    }

    // 模拟假数据，创建QListWidgetItem，并设置自定义的widget
    // for (int i = 0; i < 13; i++) {
    //    int randomValue = QRandomGenerator::global()->bounded(100); // 生成0到99之间的随机整数
    //    int str_i = randomValue % strs.size();
    //    int head_i = randomValue % heads.size();
    //    int name_i = randomValue % names.size();

    //    auto* apply_item = new ApplyFriendItem();
    //    auto apply = std::make_shared<ApplyInfo>(0, names[name_i], strs[str_i],
    //        heads[head_i], names[name_i], 0, 1);
    //    apply_item->SetInfo(apply);
    //    QListWidgetItem* item = new QListWidgetItem;
    //    //qDebug()<<"chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
    //    item->setSizeHint(apply_item->sizeHint());
    //    item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
    //    ui->apply_friend_list->addItem(item);
    //    ui->apply_friend_list->setItemWidget(item, apply_item);
    //    //收到审核好友信号
    //    connect(apply_item, &ApplyFriendItem::sig_auth_friend, [this](std::shared_ptr<ApplyInfo>
    //    apply_info) {
    //                    auto *authFriend =  new AuthenFriend(this);
    //                    authFriend->setModal(true);
    //                    authFriend->SetApplyInfo(apply_info);
    //                    authFriend->show();
    //        });
    //}
}

void ApplyFriendPage::slot_auth_rsp(std::shared_ptr<AuthRsp> auth_rsp) {
    auto uid = auth_rsp->_uid;
    for (auto& apply : UserMgr::Getinstance()->GetApplyList())
        if (apply->_uid == uid) apply->_status = 1;
    auto find_iter = _unauth_items.find(uid);
    if (find_iter == _unauth_items.end()) {
        return;
    }

    find_iter->second->ShowAddBtn(false);
}
