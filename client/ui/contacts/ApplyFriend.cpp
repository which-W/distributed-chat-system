#include "ApplyFriend.h"

ApplyFriend::ApplyFriend(QWidget* parent) :
    QDialog(),
    ui(new Ui::ApplyFriendClass), _label_point(2, 6)
{
    ui->setupUi(this);
    // 隐藏对话框标题栏
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    this->setObjectName("ApplyFriend");
    this->setModal(true);


    ui->name_ed->setPlaceholderText(tr("添加人名称"));
    ui->lb_edit->setPlaceholderText(tr("搜索、添加标签"));
    ui->other_name_ed->setPlaceholderText(tr("请添加好友名"));

    connect(ui->more_lb, &ClickOnceLabel::clicked, this, &ApplyFriend::ShowMoreLabel);
    ui->input_widght->hide();

    ui->lb_edit->SetMaxLength(21);
    ui->lb_edit->move(2, 2);
    ui->lb_edit->setFixedHeight(20);
    ui->lb_edit->setMaxLength(10);
    _tip_cur_point = QPoint(5, 5);

    _tip_data = { tr("情人"), tr("仇人"), tr("死人"), tr("python好友"),
    tr("C++大师"), tr("好基友"), tr("一生不分离的最好的朋友"),
    tr("好基友") };
    InitTipLbs();
    //链接输入标签回车事件
    connect(ui->lb_edit, &CustomizeEdit::returnPressed, this, &ApplyFriend::SlotLabelEnter);
    connect(ui->lb_edit, &CustomizeEdit::textChanged, this, &ApplyFriend::SlotLabelTextChange);
    connect(ui->lb_edit, &CustomizeEdit::editingFinished, this, &ApplyFriend::SlotLabelEditFinished);
    connect(ui->tip_label, &ClickOnceLabel::clicked, this, &ApplyFriend::SlotAddFirendLabelByClickTip);

    ui->scrollArea->horizontalScrollBar()->setHidden(true);
    ui->scrollArea->verticalScrollBar()->setHidden(true);
    ui->scrollArea->installEventFilter(this);
    ui->sure_btn->SetState("normal", "hover", "press");
    ui->cancel_btn->SetState("normal", "hover", "press");
    //连接确认和取消按钮的槽函数
    connect(ui->cancel_btn, &QPushButton::clicked, this, &ApplyFriend::SlotApplyCancel);
    connect(ui->sure_btn, &QPushButton::clicked, this, &ApplyFriend::SlotApplySure);
}

ApplyFriend::~ApplyFriend()
{
    qDebug() << "ApplyFriend destruct";
    delete ui;
}

void ApplyFriend::InitTipLbs()
{
    int lines = 1;
    for (int i = 0; i < _tip_data.size(); i++) {

        auto* lb = new ClickLabel(ui->lb_list);
        lb->SetState("normal", "hover", "pressed", "selected_normal",
            "selected_hover", "selected_pressed");
        lb->setObjectName("tip_label");
        lb->setText(_tip_data[i]);
        connect(lb, &ClickLabel::clicked, this, &ApplyFriend::SlotChangeFriendLabelByTip);

        QFontMetrics fontMetrics(lb->font()); // 获取QLabel控件的字体信息
        int textWidth = fontMetrics.horizontalAdvance(lb->text()); // 获取文本的宽度
        int textHeight = fontMetrics.height(); // 获取文本的高度

        if (_tip_cur_point.x() + textWidth + tip_offset > ui->lb_list->width()) {
            lines++;
            if (lines > 2) {
                delete lb;
                return;
            }

            _tip_cur_point.setX(tip_offset);
            _tip_cur_point.setY(_tip_cur_point.y() + textHeight + 15);

        }

        auto next_point = _tip_cur_point;

        AddTipLbs(lb, _tip_cur_point, next_point, textWidth, textHeight);

        _tip_cur_point = next_point;
    }

}

void ApplyFriend::AddTipLbs(ClickLabel* lb, QPoint cur_point, QPoint& next_point, int text_width, int text_height)
{
    lb->move(cur_point);
    lb->show();
    _add_labels.insert(lb->text(), lb);
    _add_label_keys.push_back(lb->text());
    next_point.setX(lb->pos().x() + text_width + 15);
    next_point.setY(lb->pos().y());
}

bool ApplyFriend::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == ui->scrollArea && event->type() == QEvent::Enter)
    {
        ui->scrollArea->verticalScrollBar()->setHidden(false);
    }
    else if (obj == ui->scrollArea && event->type() == QEvent::Leave)
    {
        ui->scrollArea->verticalScrollBar()->setHidden(true);
    }
    return QObject::eventFilter(obj, event);
}

void ApplyFriend::ShowMoreLabel()
{
    qDebug() << "receive more label clicked";
    ui->need_more_lb->hide();

    ui->lb_list->setFixedWidth(325);
    _tip_cur_point = QPoint(5, 5);
    auto next_point = _tip_cur_point;
    int textWidth = 0;
    int textHeight = QFontMetrics(ui->lb_list->font()).height();
    //重拍现有的label
    for (auto& added_key : _add_label_keys) {
        auto added_lb = _add_labels[added_key];

        QFontMetrics fontMetrics(added_lb->font()); // 获取QLabel控件的字体信息
        textWidth = fontMetrics.horizontalAdvance(added_lb->text()); // 获取文本的宽度
        textHeight = fontMetrics.height(); // 获取文本的高度

        if (_tip_cur_point.x() + textWidth + tip_offset > ui->lb_list->width()) {
            _tip_cur_point.setX(tip_offset);
            _tip_cur_point.setY(_tip_cur_point.y() + textHeight + 15);
        }
        added_lb->move(_tip_cur_point);

        next_point.setX(added_lb->pos().x() + textWidth + 15);
        next_point.setY(_tip_cur_point.y());

        _tip_cur_point = next_point;

    }

    //添加未添加的
    for (int i = 0; i < _tip_data.size(); i++) {
        auto iter = _add_labels.find(_tip_data[i]);
        if (iter != _add_labels.end()) {
            continue;
        }

        auto* lb = new ClickLabel(ui->lb_list);
        lb->SetState("normal", "hover", "pressed", "selected_normal",
            "selected_hover", "selected_pressed");
        lb->setObjectName("tip_label");
        lb->setText(_tip_data[i]);
        connect(lb, &ClickLabel::clicked, this, &ApplyFriend::SlotChangeFriendLabelByTip);

        QFontMetrics fontMetrics(lb->font()); // 获取QLabel控件的字体信息
        int textWidth = fontMetrics.horizontalAdvance(lb->text()); // 获取文本的宽度
        int textHeight = fontMetrics.height(); // 获取文本的高度

        if (_tip_cur_point.x() + textWidth + tip_offset > ui->lb_list->width()) {

            _tip_cur_point.setX(tip_offset);
            _tip_cur_point.setY(_tip_cur_point.y() + textHeight + 15);

        }

        next_point = _tip_cur_point;

        AddTipLbs(lb, _tip_cur_point, next_point, textWidth, textHeight);

        _tip_cur_point = next_point;

    }

    int diff_height = next_point.y() + textHeight + tip_offset - ui->lb_list->height();
    ui->lb_list->setFixedHeight(next_point.y() + textHeight + tip_offset);

    //qDebug()<<"after resize ui->lb_list size is " <<  ui->lb_list->size();
    ui->scrollAreaWidgetContents->setFixedHeight(ui->scrollAreaWidgetContents->height() + diff_height);

}

void ApplyFriend::SetSearchInfo(std::shared_ptr<SearchInfo> si)
{
    _si = si;
    auto applyname = UserMgr::Getinstance()->GetName();
    auto bakname = si->_name;
    ui->name_ed->setText(applyname);
    ui->other_name_ed->setText(bakname);
}

void ApplyFriend::resetLabels()
{
    auto max_width = ui->grid_widght->width();
    auto label_height = 0;
    for (auto iter = _friend_labels.begin(); iter != _friend_labels.end(); iter++) {
        //todo... 添加宽度统计
        if (_label_point.x() + iter.value()->width() > max_width) {
            _label_point.setY(_label_point.y() + iter.value()->height() + 6);
            _label_point.setX(2);
        }

        iter.value()->move(_label_point);
        iter.value()->show();

        _label_point.setX(_label_point.x() + iter.value()->width() + 2);
        _label_point.setY(_label_point.y());
        label_height = iter.value()->height();
    }

    if (_friend_labels.isEmpty()) {
        ui->lb_edit->move(_label_point);
        return;
    }

    if (_label_point.x() + MIN_APPLY_LABEL_ED_LEN > ui->grid_widght->width()) {
        ui->lb_edit->move(2, _label_point.y() + label_height + 6);
    }
    else {
        ui->lb_edit->move(_label_point);
    }
}

void ApplyFriend::addLabel(QString name)
{
    if (_friend_labels.find(name) != _friend_labels.end()) {
        ui->lb_edit->clear();
        return;
    }

    auto tmplabel = new FriendLabel(ui->grid_widght);
    tmplabel->SetText(name);
    tmplabel->setObjectName("FriendLabel");

    auto max_width = ui->grid_widght->width();
    //todo... 添加宽度统计
    if (_label_point.x() + tmplabel->width() > max_width) {
        _label_point.setY(_label_point.y() + tmplabel->height() + 6);
        _label_point.setX(2);
    }
    else {

    }


    tmplabel->move(_label_point);
    tmplabel->show();
    _friend_labels[tmplabel->Text()] = tmplabel;
    _friend_label_keys.push_back(tmplabel->Text());

    connect(tmplabel, &FriendLabel::sig_close, this, &ApplyFriend::SlotRemoveFriendLabel);

    _label_point.setX(_label_point.x() + tmplabel->width() + 2);

    if (_label_point.x() + MIN_APPLY_LABEL_ED_LEN > ui->grid_widght->width()) {
        ui->lb_edit->move(2, _label_point.y() + tmplabel->height() + 2);
    }
    else {
        ui->lb_edit->move(_label_point);
    }

    ui->lb_edit->clear();

    if (ui->grid_widght->height() < _label_point.y() + tmplabel->height() + 2) {
        ui->grid_widght->setFixedHeight(_label_point.y() + tmplabel->height() * 2 + 2);
    }
}

void ApplyFriend::SlotLabelEnter()
{
    if (ui->lb_edit->text().isEmpty()) {
        return;
    }

    auto text = ui->lb_edit->text();
    addLabel(text);

    ui->input_widght->hide();

    auto find_it = std::find(_tip_data.begin(), _tip_data.end(), text);
    //找到了就只需设置状态为选中即可
    if (find_it == _tip_data.end()) {
        _tip_data.push_back(text);
    }

    //判断标签展示栏是否有该标签
    auto find_add = _add_labels.find(text);
    if (find_add != _add_labels.end()) {
        find_add.value()->SetCurState(ClickLbState::Selected);
        return;
    }

    //标签展示栏也增加一个标签, 并设置绿色选中
    auto* lb = new ClickLabel(ui->lb_list);
    lb->SetState("normal", "hover", "pressed", "selected_normal",
        "selected_hover", "selected_pressed");
    lb->setObjectName("tip_label");
    lb->setText(text);
    connect(lb, &ClickLabel::clicked, this, &ApplyFriend::SlotChangeFriendLabelByTip);
    qDebug() << "ui->lb_list->width() is " << ui->lb_list->width();
    qDebug() << "_tip_cur_point.x() is " << _tip_cur_point.x();

    QFontMetrics fontMetrics(lb->font()); // 获取QLabel控件的字体信息
    int textWidth = fontMetrics.horizontalAdvance(lb->text()); // 获取文本的宽度
    int textHeight = fontMetrics.height(); // 获取文本的高度
    qDebug() << "textWidth is " << textWidth;

    if (_tip_cur_point.x() + textWidth + tip_offset + 3 > ui->lb_list->width()) {

        _tip_cur_point.setX(5);
        _tip_cur_point.setY(_tip_cur_point.y() + textHeight + 15);

    }

    auto next_point = _tip_cur_point;

    AddTipLbs(lb, _tip_cur_point, next_point, textWidth, textHeight);
    _tip_cur_point = next_point;

    int diff_height = next_point.y() + textHeight + tip_offset - ui->lb_list->height();
    ui->lb_list->setFixedHeight(next_point.y() + textHeight + tip_offset);

    lb->SetCurState(ClickLbState::Selected);

    ui->scrollAreaWidgetContents->setFixedHeight(ui->scrollAreaWidgetContents->height() + diff_height);
}

void ApplyFriend::SlotRemoveFriendLabel(QString name)
{
    qDebug() << "receive close signal";

    _label_point.setX(2);
    _label_point.setY(6);

    auto find_iter = _friend_labels.find(name);

    if (find_iter == _friend_labels.end()) {
        return;
    }

    auto find_key = _friend_label_keys.end();
    for (auto iter = _friend_label_keys.begin(); iter != _friend_label_keys.end();
        iter++) {
        if (*iter == name) {
            find_key = iter;
            break;
        }
    }

    if (find_key != _friend_label_keys.end()) {
        _friend_label_keys.erase(find_key);
    }


    delete find_iter.value();

    _friend_labels.erase(find_iter);

    resetLabels();

    auto find_add = _add_labels.find(name);
    if (find_add == _add_labels.end()) {
        return;
    }

    find_add.value()->ResetNormalState();
}

//点击标已有签添加或删除新联系人的标签
void ApplyFriend::SlotChangeFriendLabelByTip(QString lbtext, ClickLbState state)
{
    auto find_iter = _add_labels.find(lbtext);
    if (find_iter == _add_labels.end()) {
        return;
    }

    if (state == ClickLbState::Selected) {
        //编写添加逻辑
        addLabel(lbtext);
        return;
    }

    if (state == ClickLbState::Normal) {
        //编写删除逻辑
        SlotRemoveFriendLabel(lbtext);
        return;
    }

}

void ApplyFriend::SlotLabelTextChange(const QString& text)
{
    QString add_prefix = tr("添加标签: ");
    if (text.isEmpty()) {
        ui->tip_label->setText("");
        ui->input_widght->hide();
        return;
    }

    auto iter = std::find(_tip_data.begin(), _tip_data.end(), text);
    if (iter == _tip_data.end()) {
        auto new_text = add_prefix + text;
        ui->tip_label->setText(new_text);
        ui->input_widght->show();
        return;
    }
    ui->tip_label->setText(text);
    ui->input_widght->show();
}

void ApplyFriend::SlotLabelEditFinished()
{
    ui->input_widght->hide();
}

void ApplyFriend::SlotAddFirendLabelByClickTip(QString text)
{
    int index = text.indexOf(add_prefix);
    if (index != -1) {
        text = text.mid(index + add_prefix.length());
    }
    addLabel(text);

    auto find_it = std::find(_tip_data.begin(), _tip_data.end(), text);
    //找到了就只需设置状态为选中即可
    if (find_it == _tip_data.end()) {
        _tip_data.push_back(text);
    }

    //判断标签展示栏是否有该标签
    auto find_add = _add_labels.find(text);
    if (find_add != _add_labels.end()) {
        find_add.value()->SetCurState(ClickLbState::Selected);
        return;
    }

    //标签展示栏也增加一个标签, 并设置绿色选中
    auto* lb = new ClickLabel(ui->lb_list);
    lb->SetState("normal", "hover", "pressed", "selected_normal",
        "selected_hover", "selected_pressed");
    lb->setObjectName("tipslb");
    lb->setText(text);
    connect(lb, &ClickLabel::clicked, this, &ApplyFriend::SlotChangeFriendLabelByTip);
    qDebug() << "ui->lb_list->width() is " << ui->lb_list->width();
    qDebug() << "_tip_cur_point.x() is " << _tip_cur_point.x();

    QFontMetrics fontMetrics(lb->font()); // 获取QLabel控件的字体信息
    int textWidth = fontMetrics.horizontalAdvance(lb->text()); // 获取文本的宽度
    int textHeight = fontMetrics.height(); // 获取文本的高度
    qDebug() << "textWidth is " << textWidth;

    if (_tip_cur_point.x() + textWidth + tip_offset + 3 > ui->lb_list->width()) {

        _tip_cur_point.setX(5);
        _tip_cur_point.setY(_tip_cur_point.y() + textHeight + 15);

    }

    auto next_point = _tip_cur_point;

    AddTipLbs(lb, _tip_cur_point, next_point, textWidth, textHeight);
    _tip_cur_point = next_point;

    int diff_height = next_point.y() + textHeight + tip_offset - ui->lb_list->height();
    ui->lb_list->setFixedHeight(next_point.y() + textHeight + tip_offset);

    lb->SetCurState(ClickLbState::Selected);

    ui->scrollAreaWidgetContents->setFixedHeight(ui->scrollAreaWidgetContents->height() + diff_height);
}

void ApplyFriend::SlotApplySure()
{
    qDebug() << "Slot Apply Sure called";
    //发送请求逻辑
    QJsonObject jsonObj;
    auto uid = UserMgr::Getinstance()->GetUid();
    jsonObj["uid"] = uid;
    auto name = ui->name_ed->text();
    if (name.isEmpty()) {
        name = ui->name_ed->placeholderText();
    }

    jsonObj["applyname"] = name;

    auto bakname = ui->other_name_ed->text();
    if (bakname.isEmpty()) {
        bakname = ui->other_name_ed->placeholderText();
    }

    jsonObj["bakname"] = bakname;
    jsonObj["touid"] = _si->_uid;

    QJsonDocument doc(jsonObj);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    //发送tcp请求给chat server
    emit TcpMgr::Getinstance()->sig_send_data(Req::ID_ADD_FRIEND_REQ, jsonData);
    this->hide();
    deleteLater();
}

void ApplyFriend::SlotApplyCancel()
{
    qDebug() << "Slot Apply Cancel";
    this->hide();
    deleteLater();
}

void ApplyFriend::centerOnParent()
{
    if (parentWidget()) {
        QWidget* parent = parentWidget();
        // 获取父窗口的几何信息
        QRect parentGeometry = parent->geometry();

        // 计算居中位置
        int x = parentGeometry.x() + (parentGeometry.width() - this->width()) / 2;
        int y = parentGeometry.y() + (parentGeometry.height() - this->height()) / 2;

        // 确保不会超出屏幕边界
        QScreen* screen = QGuiApplication::screenAt(parent->mapToGlobal(parent->rect().center()));
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
        QRect screenRect = screen->availableGeometry();

        x = qBound(screenRect.left(), x, screenRect.right() - this->width() + 1);
        y = qBound(screenRect.top(), y, screenRect.bottom() - this->height() + 1);

        this->move(x, y);
    }
    else {
        // 屏幕居中
        QScreen* screen = QGuiApplication::primaryScreen();
        QRect screenRect = screen->availableGeometry();
        int x = screenRect.left() + (screenRect.width() - this->width()) / 2;
        int y = screenRect.top() + (screenRect.height() - this->height()) / 2;
        this->move(x, y);
    }
}
