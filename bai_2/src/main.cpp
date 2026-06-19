#include <QtWidgets>
#include <QtNetwork>
#include <QtConcurrent>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <filesystem>
#include <memory>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

namespace fs = std::filesystem;

static QString readText(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll());
}

static QString humanBytes(qulonglong value) {
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double size = static_cast<double>(value);
    int unit = 0;
    while (size >= 1024.0 && unit < 4) { size /= 1024.0; ++unit; }
    return QString::number(size, unit == 0 ? 'f' : 'f', unit == 0 ? 0 : 1) + " " + units[unit];
}

static QString currentUserName(uid_t uid) {
    if (passwd *entry = getpwuid(uid)) return QString::fromLocal8Bit(entry->pw_name);
    return QString::number(uid);
}

static void configureModel(QStandardItemModel *model, const QStringList &headers) {
    model->clear();
    model->setHorizontalHeaderLabels(headers);
}

class TreeBranchStyle final : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget = nullptr) const override {
        if (element != PE_IndicatorBranch) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }
        if (!(option->state & State_Children)) return;

        const QPoint center = option->rect.center();
        QPolygon triangle;
        if (option->state & State_Open) {
            triangle << QPoint(center.x() - 5, center.y() - 3)
                     << QPoint(center.x() + 5, center.y() - 3)
                     << QPoint(center.x(), center.y() + 5);
        } else {
            triangle << QPoint(center.x() - 3, center.y() - 5)
                     << QPoint(center.x() - 3, center.y() + 5)
                     << QPoint(center.x() + 5, center.y());
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor("#a99bff"));
        painter->drawPolygon(triangle);
        painter->restore();
    }
};

class MainWindow final : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle("Linux Control Center");
        resize(1360, 840);
        setMinimumSize(1080, 680);
        buildUi();
        applyStyle();
        refreshAll();

        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] { refreshDashboard(); });
        timer->start(5000);
    }

    ~MainWindow() override { ++(*fileSizeGeneration_); }

private:
    QStackedWidget *pages_{};
    QLabel *pageTitle_{};
    QLabel *pageSubtitle_{};
    QLabel *processCount_{};
    QLabel *socketCount_{};
    QLabel *interfaceCount_{};
    QLabel *memoryValue_{};
    QLabel *systemInfo_{};

    QStandardItemModel *processModel_{};
    QStandardItemModel *fileModel_{};
    QStandardItemModel *socketModel_{};
    QStandardItemModel *networkModel_{};
    QSortFilterProxyModel *processProxy_{};
    QSortFilterProxyModel *fileProxy_{};
    QSortFilterProxyModel *socketProxy_{};
    QSortFilterProxyModel *networkProxy_{};
    QTableView *processTable_{};
    QTreeView *fileTable_{};
    QTableView *socketTable_{};
    QTableView *networkTable_{};
    QLineEdit *pathEdit_{};
    QTextEdit *pingOutput_{};
    QLineEdit *pingHost_{};
    QProcess *pingProcess_{};
    QString currentPath_;
    std::shared_ptr<std::atomic_int> fileSizeGeneration_ = std::make_shared<std::atomic_int>(0);

    const QStringList titles_ = {"Tổng quan", "Tiến trình", "Quản lý file", "Socket", "Network"};
    const QStringList subtitles_ = {
        "Trạng thái Ubuntu theo thời gian thực",
        "Theo dõi và điều khiển các tiến trình đang chạy",
        "Duyệt và thao tác an toàn với hệ thống file",
        "Theo dõi kết nối TCP/UDP và tiến trình sở hữu",
        "Giao diện mạng, lưu lượng và công cụ chẩn đoán"
    };

    static QPushButton *button(const QString &text, const QString &kind = {}) {
        auto *b = new QPushButton(text);
        b->setCursor(Qt::PointingHandCursor);
        b->setMinimumHeight(38);
        if (!kind.isEmpty()) b->setProperty("kind", kind);
        return b;
    }

    static QLineEdit *searchBox(const QString &placeholder) {
        auto *edit = new QLineEdit;
        edit->setPlaceholderText(placeholder);
        edit->setClearButtonEnabled(true);
        edit->setMinimumHeight(40);
        return edit;
    }

    static QSortFilterProxyModel *proxyFor(QStandardItemModel *source, QObject *parent) {
        auto *proxy = new QSortFilterProxyModel(parent);
        proxy->setSourceModel(source);
        proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
        proxy->setFilterKeyColumn(-1);
        proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
        proxy->setDynamicSortFilter(true);
        proxy->setRecursiveFilteringEnabled(true);
        proxy->setAutoAcceptChildRows(true);
        return proxy;
    }

    static QTableView *tableFor(QAbstractItemModel *model) {
        auto *table = new QTableView;
        table->setModel(model);
        table->setAlternatingRowColors(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSortingEnabled(true);
        table->verticalHeader()->hide();
        table->verticalHeader()->setDefaultSectionSize(38);
        table->horizontalHeader()->setStretchLastSection(true);
        table->setShowGrid(false);
        return table;
    }

    static QTreeView *treeFor(QAbstractItemModel *model) {
        auto *tree = new QTreeView;
        tree->setModel(model);
        tree->setAlternatingRowColors(true);
        tree->setSelectionBehavior(QAbstractItemView::SelectRows);
        tree->setSelectionMode(QAbstractItemView::SingleSelection);
        tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tree->setSortingEnabled(true);
        tree->setRootIsDecorated(true);
        tree->setItemsExpandable(true);
        tree->setExpandsOnDoubleClick(false);
        tree->setIndentation(22);
        tree->header()->setStretchLastSection(true);
        auto *branchStyle = new TreeBranchStyle;
        branchStyle->setParent(tree);
        tree->setStyle(branchStyle);
        return tree;
    }

    QWidget *card(const QString &caption, QLabel **value, const QString &accent) {
        auto *frame = new QFrame;
        frame->setProperty("card", true);
        auto *layout = new QVBoxLayout(frame);
        layout->setContentsMargins(20, 18, 20, 18);
        auto *stripe = new QFrame;
        stripe->setFixedHeight(4);
        stripe->setStyleSheet("background:" + accent + "; border-radius:2px;");
        auto *label = new QLabel(caption);
        label->setProperty("muted", true);
        *value = new QLabel("—");
        (*value)->setProperty("metric", true);
        layout->addWidget(stripe);
        layout->addSpacing(8);
        layout->addWidget(label);
        layout->addWidget(*value);
        return frame;
    }

    QWidget *pageShell(QWidget *toolbar, QTableView *table) {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(14);
        layout->addWidget(toolbar);
        auto *frame = new QFrame;
        frame->setProperty("card", true);
        auto *inside = new QVBoxLayout(frame);
        inside->setContentsMargins(1, 1, 1, 1);
        inside->addWidget(table);
        layout->addWidget(frame, 1);
        return page;
    }

    void buildUi() {
        auto *root = new QWidget;
        setCentralWidget(root);
        auto *rootLayout = new QHBoxLayout(root);
        rootLayout->setContentsMargins(0, 0, 0, 0);
        rootLayout->setSpacing(0);

        auto *sidebar = new QFrame;
        sidebar->setObjectName("sidebar");
        sidebar->setFixedWidth(238);
        auto *sideLayout = new QVBoxLayout(sidebar);
        sideLayout->setContentsMargins(20, 26, 20, 22);
        sideLayout->setSpacing(8);
        auto *brand = new QLabel("⬡  LINUX\n     CONTROL");
        brand->setObjectName("brand");
        sideLayout->addWidget(brand);
        sideLayout->addSpacing(28);

        auto *navGroup = new QButtonGroup(this);
        navGroup->setExclusive(true);
        const QStringList nav = {"▦   Tổng quan", "◉   Tiến trình", "□   Quản lý file", "⇄   Socket", "⌁   Network"};
        for (int i = 0; i < nav.size(); ++i) {
            auto *b = new QPushButton(nav[i]);
            b->setProperty("nav", true);
            b->setCheckable(true);
            b->setCursor(Qt::PointingHandCursor);
            b->setMinimumHeight(46);
            navGroup->addButton(b, i);
            sideLayout->addWidget(b);
            connect(b, &QPushButton::clicked, this, [this, i] { switchPage(i); });
            if (i == 0) b->setChecked(true);
        }
        sideLayout->addStretch();
        auto *hint = new QLabel("Ubuntu Admin\nC++17 • Qt 6");
        hint->setProperty("muted", true);
        sideLayout->addWidget(hint);

        auto *content = new QWidget;
        auto *contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(30, 24, 30, 24);
        contentLayout->setSpacing(20);
        auto *header = new QHBoxLayout;
        auto *headText = new QVBoxLayout;
        pageTitle_ = new QLabel(titles_[0]);
        pageTitle_->setObjectName("pageTitle");
        pageSubtitle_ = new QLabel(subtitles_[0]);
        pageSubtitle_->setProperty("muted", true);
        headText->addWidget(pageTitle_);
        headText->addWidget(pageSubtitle_);
        header->addLayout(headText);
        header->addStretch();
        auto *refresh = button("↻  Làm mới", "primary");
        connect(refresh, &QPushButton::clicked, this, [this] { refreshCurrent(); });
        header->addWidget(refresh);
        contentLayout->addLayout(header);

        pages_ = new QStackedWidget;
        pages_->addWidget(buildDashboard());
        pages_->addWidget(buildProcesses());
        pages_->addWidget(buildFiles());
        pages_->addWidget(buildSockets());
        pages_->addWidget(buildNetwork());
        contentLayout->addWidget(pages_, 1);
        rootLayout->addWidget(sidebar);
        rootLayout->addWidget(content, 1);
        statusBar()->showMessage("Sẵn sàng");
    }

    QWidget *buildDashboard() {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(18);
        auto *cards = new QGridLayout;
        cards->setSpacing(16);
        cards->addWidget(card("TIẾN TRÌNH", &processCount_, "#7c5cff"), 0, 0);
        cards->addWidget(card("SOCKET", &socketCount_, "#21c7a8"), 0, 1);
        cards->addWidget(card("INTERFACE", &interfaceCount_, "#4da3ff"), 0, 2);
        cards->addWidget(card("BỘ NHỚ", &memoryValue_, "#ffb454"), 0, 3);
        for (int i = 0; i < 4; ++i) cards->setColumnStretch(i, 1);
        layout->addLayout(cards);
        auto *systemCard = new QFrame;
        systemCard->setProperty("card", true);
        auto *sysLayout = new QVBoxLayout(systemCard);
        sysLayout->setContentsMargins(24, 22, 24, 22);
        auto *title = new QLabel("Thông tin hệ thống");
        title->setProperty("section", true);
        systemInfo_ = new QLabel;
        systemInfo_->setWordWrap(true);
        systemInfo_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        sysLayout->addWidget(title);
        sysLayout->addSpacing(10);
        sysLayout->addWidget(systemInfo_);
        sysLayout->addStretch();
        layout->addWidget(systemCard, 1);
        return page;
    }

    QWidget *buildProcesses() {
        processModel_ = new QStandardItemModel(this);
        configureModel(processModel_, {"PID", "User", "Trạng thái", "RAM", "CPU time", "Lệnh"});
        processProxy_ = proxyFor(processModel_, this);
        processTable_ = tableFor(processProxy_);
        processTable_->setColumnWidth(0, 80);
        processTable_->setColumnWidth(1, 120);
        processTable_->setColumnWidth(2, 110);
        processTable_->setColumnWidth(3, 100);
        processTable_->setColumnWidth(4, 100);
        auto *toolbar = new QWidget;
        auto *bar = new QHBoxLayout(toolbar);
        bar->setContentsMargins(0, 0, 0, 0);
        auto *search = searchBox("Tìm PID, user hoặc tên tiến trình...");
        connect(search, &QLineEdit::textChanged, processProxy_, &QSortFilterProxyModel::setFilterFixedString);
        auto *term = button("Dừng (SIGTERM)", "warning");
        auto *killButton = button("Buộc dừng", "danger");
        connect(term, &QPushButton::clicked, this, [this] { signalSelectedProcess(SIGTERM); });
        connect(killButton, &QPushButton::clicked, this, [this] { signalSelectedProcess(SIGKILL); });
        bar->addWidget(search, 1); bar->addWidget(term); bar->addWidget(killButton);
        return pageShell(toolbar, processTable_);
    }

    QWidget *buildFiles() {
        fileModel_ = new QStandardItemModel(this);
        configureModel(fileModel_, {"Tên", "Loại", "Kích thước", "Quyền", "Cập nhật"});
        fileProxy_ = proxyFor(fileModel_, this);
        fileTable_ = treeFor(fileProxy_);
        fileTable_->setColumnWidth(0, 330);
        fileTable_->setColumnWidth(1, 100);
        fileTable_->setColumnWidth(2, 110);
        fileTable_->setColumnWidth(3, 100);
        fileTable_->setColumnWidth(4, 170);
        currentPath_ = QDir::homePath();
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(12);
        auto *pathBar = new QHBoxLayout;
        auto *up = button("↑", "ghost"); up->setFixedWidth(44);
        pathEdit_ = searchBox("Đường dẫn thư mục"); pathEdit_->setText(currentPath_);
        auto *go = button("Đi đến", "primary");
        connect(up, &QPushButton::clicked, this, [this] { navigateTo(QFileInfo(currentPath_).dir().absolutePath()); });
        connect(go, &QPushButton::clicked, this, [this] { navigateTo(pathEdit_->text()); });
        connect(pathEdit_, &QLineEdit::returnPressed, this, [this] { navigateTo(pathEdit_->text()); });
        pathBar->addWidget(up); pathBar->addWidget(pathEdit_, 1); pathBar->addWidget(go);
        layout->addLayout(pathBar);
        auto *actions = new QHBoxLayout;
        auto *filter = searchBox("Lọc file trong thư mục...");
        connect(filter, &QLineEdit::textChanged, fileProxy_, &QSortFilterProxyModel::setFilterFixedString);
        auto *open = button("Mở", "ghost");
        auto *create = button("Thư mục mới", "primary");
        auto *rename = button("Đổi tên", "warning");
        auto *remove = button("Xóa", "danger");
        connect(open, &QPushButton::clicked, this, [this] { openSelectedFile(); });
        connect(create, &QPushButton::clicked, this, [this] { createDirectory(); });
        connect(rename, &QPushButton::clicked, this, [this] { renameSelectedFile(); });
        connect(remove, &QPushButton::clicked, this, [this] { deleteSelectedFile(); });
        connect(fileTable_, &QTreeView::doubleClicked, this, [this](const QModelIndex &) { openSelectedFile(); });
        connect(fileTable_, &QTreeView::expanded, this, [this](const QModelIndex &proxyIndex) {
            const QModelIndex sourceIndex = fileProxy_->mapToSource(proxyIndex.siblingAtColumn(0));
            QStandardItem *item = fileModel_->itemFromIndex(sourceIndex);
            if (!item || item->data(Qt::UserRole + 1).toBool()) return;
            item->setData(true, Qt::UserRole + 1);
            item->removeRows(0, item->rowCount());
            populateFileItems(item, item->data(Qt::UserRole).toString());
        });
        actions->addWidget(filter, 1); actions->addWidget(open); actions->addWidget(create); actions->addWidget(rename); actions->addWidget(remove);
        layout->addLayout(actions);
        auto *frame = new QFrame; frame->setProperty("card", true);
        auto *inside = new QVBoxLayout(frame); inside->setContentsMargins(1, 1, 1, 1); inside->addWidget(fileTable_);
        layout->addWidget(frame, 1);
        return page;
    }

    QWidget *buildSockets() {
        socketModel_ = new QStandardItemModel(this);
        configureModel(socketModel_, {"Protocol", "Trạng thái", "Local", "Peer", "Tiến trình"});
        socketProxy_ = proxyFor(socketModel_, this);
        socketTable_ = tableFor(socketProxy_);
        socketTable_->setColumnWidth(0, 90); socketTable_->setColumnWidth(1, 110);
        socketTable_->setColumnWidth(2, 240); socketTable_->setColumnWidth(3, 240);
        auto *toolbar = new QWidget;
        auto *bar = new QHBoxLayout(toolbar); bar->setContentsMargins(0, 0, 0, 0);
        auto *search = searchBox("Lọc protocol, địa chỉ, port hoặc tiến trình...");
        connect(search, &QLineEdit::textChanged, socketProxy_, &QSortFilterProxyModel::setFilterFixedString);
        auto *terminate = button("Dừng tiến trình sở hữu", "danger");
        connect(terminate, &QPushButton::clicked, this, [this] { terminateSocketOwner(); });
        bar->addWidget(search, 1); bar->addWidget(terminate);
        return pageShell(toolbar, socketTable_);
    }

    QWidget *buildNetwork() {
        networkModel_ = new QStandardItemModel(this);
        configureModel(networkModel_, {"Interface", "Trạng thái", "Địa chỉ", "MAC", "RX", "TX"});
        networkProxy_ = proxyFor(networkModel_, this);
        networkTable_ = tableFor(networkProxy_);
        networkTable_->setColumnWidth(0, 130); networkTable_->setColumnWidth(1, 110);
        networkTable_->setColumnWidth(2, 320); networkTable_->setColumnWidth(3, 160);
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(14);
        auto *actions = new QHBoxLayout;
        auto *filter = searchBox("Lọc interface hoặc địa chỉ...");
        connect(filter, &QLineEdit::textChanged, networkProxy_, &QSortFilterProxyModel::setFilterFixedString);
        auto *enable = button("Bật interface", "primary");
        auto *disable = button("Tắt interface", "danger");
        connect(enable, &QPushButton::clicked, this, [this] { setSelectedInterface(true); });
        connect(disable, &QPushButton::clicked, this, [this] { setSelectedInterface(false); });
        actions->addWidget(filter, 1); actions->addWidget(enable); actions->addWidget(disable);
        layout->addLayout(actions);
        auto *frame = new QFrame; frame->setProperty("card", true);
        auto *inside = new QVBoxLayout(frame); inside->setContentsMargins(1, 1, 1, 1); inside->addWidget(networkTable_);
        layout->addWidget(frame, 2);

        auto *pingCard = new QFrame; pingCard->setProperty("card", true);
        auto *pingLayout = new QVBoxLayout(pingCard); pingLayout->setContentsMargins(18, 16, 18, 16);
        auto *pingTitle = new QLabel("Chẩn đoán kết nối"); pingTitle->setProperty("section", true);
        auto *pingBar = new QHBoxLayout;
        pingHost_ = searchBox("Host hoặc IP, ví dụ 8.8.8.8"); pingHost_->setText("8.8.8.8");
        auto *pingButton = button("Ping", "primary");
        connect(pingButton, &QPushButton::clicked, this, [this, pingButton] { startPing(pingButton); });
        pingBar->addWidget(pingHost_, 1); pingBar->addWidget(pingButton);
        pingOutput_ = new QTextEdit; pingOutput_->setReadOnly(true); pingOutput_->setMaximumHeight(130);
        pingLayout->addWidget(pingTitle); pingLayout->addLayout(pingBar); pingLayout->addWidget(pingOutput_);
        layout->addWidget(pingCard, 1);
        return page;
    }

    void applyStyle() {
        qApp->setStyle("Fusion");
        qApp->setFont(QFont("Noto Sans", 10));
        qApp->setStyleSheet(R"CSS(
            * { color:#e8ebf5; }
            QMainWindow, QWidget { background:#0f1220; }
            QLabel { background-color:transparent; }
            #sidebar { background:#171a2b; border-right:1px solid #272b43; }
            #brand { color:#a99bff; font-size:18px; font-weight:800; letter-spacing:2px; }
            #pageTitle { font-size:27px; font-weight:750; }
            QLabel[muted="true"] { color:#9298ad; }
            QLabel[metric="true"] { font-size:27px; font-weight:750; }
            QLabel[section="true"] { font-size:16px; font-weight:700; }
            QFrame[card="true"] { background:#191d30; border:1px solid #292e48; border-radius:14px; }
            QPushButton { background:#292e48; border:1px solid #373d5b; border-radius:10px; padding:8px 15px; font-weight:600; }
            QPushButton:hover { background:#343a59; }
            QPushButton:pressed { background:#22263c; }
            QPushButton[kind="primary"] { background:#7057e8; border-color:#806aff; color:white; }
            QPushButton[kind="primary"]:hover { background:#806aff; }
            QPushButton[kind="danger"] { background:#402434; border-color:#74354d; color:#ff9eb5; }
            QPushButton[kind="danger"]:hover { background:#5a2a40; }
            QPushButton[kind="warning"] { background:#3d3225; border-color:#6c5131; color:#ffc77d; }
            QPushButton[kind="ghost"] { background:#20243a; }
            QPushButton[nav="true"] { text-align:left; padding-left:16px; border:0; background:transparent; color:#aeb3c5; }
            QPushButton[nav="true"]:hover { background:#21253b; color:white; }
            QPushButton[nav="true"]:checked { background:#2d2851; color:#b9adff; border-left:3px solid #8c75ff; }
            QLineEdit, QTextEdit { background:#151829; border:1px solid #303651; border-radius:10px; padding:8px 12px; selection-background-color:#7057e8; }
            QLineEdit:focus, QTextEdit:focus { border:1px solid #7863eb; }
            QTableView, QTreeView { background:#191d30; alternate-background-color:#1d2135; border:0; border-radius:14px; selection-background-color:#393466; gridline-color:#292e48; }
            QTableView::item, QTreeView::item { padding:7px; border-bottom:1px solid #252a42; }
            QHeaderView::section { background:#20243a; color:#aeb4c8; padding:10px; border:0; border-right:1px solid #2c314b; font-weight:700; }
            QScrollBar:vertical { width:10px; background:#171a2b; }
            QScrollBar::handle:vertical { background:#3b405b; border-radius:5px; min-height:25px; }
            QStatusBar { background:#171a2b; color:#8e95aa; border-top:1px solid #292e48; }
            QMessageBox, QInputDialog { background:#191d30; }
        )CSS");
    }

    void switchPage(int index) {
        pages_->setCurrentIndex(index);
        pageTitle_->setText(titles_.value(index));
        pageSubtitle_->setText(subtitles_.value(index));
        refreshCurrent();
    }

    void refreshCurrent() {
        switch (pages_->currentIndex()) {
            case 0: refreshDashboard(); break;
            case 1: refreshProcesses(); break;
            case 2: refreshFiles(); break;
            case 3: refreshSockets(); break;
            case 4: refreshNetwork(); break;
        }
    }

    void refreshAll() {
        refreshProcesses(); refreshFiles(); refreshSockets(); refreshNetwork(); refreshDashboard();
    }

    void refreshDashboard() {
        QDir proc("/proc");
        int processes = 0;
        for (const QString &name : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            bool ok = false; name.toInt(&ok); if (ok) ++processes;
        }
        processCount_->setText(QString::number(processes));
        socketCount_->setText(QString::number(socketModel_ ? socketModel_->rowCount() : 0));
        interfaceCount_->setText(QString::number(QNetworkInterface::allInterfaces().size()));

        qulonglong total = 0, available = 0;
        const auto memLines = readText("/proc/meminfo").split('\n');
        for (const QString &line : memLines) {
            if (line.startsWith("MemTotal:")) total = line.simplified().split(' ').value(1).toULongLong() * 1024;
            if (line.startsWith("MemAvailable:")) available = line.simplified().split(' ').value(1).toULongLong() * 1024;
        }
        memoryValue_->setText(total ? QString::number((total - available) * 100.0 / total, 'f', 0) + "%" : "—");
        const QString os = readText("/etc/os-release");
        QString pretty = "Ubuntu/Linux";
        QRegularExpressionMatch match = QRegularExpression("PRETTY_NAME=\"?([^\n\"]+)").match(os);
        if (match.hasMatch()) pretty = match.captured(1);
        systemInfo_->setText(
            "Hệ điều hành     " + pretty + "\n\n" +
            "Kernel             " + QSysInfo::kernelType() + " " + QSysInfo::kernelVersion() + "\n\n" +
            "Kiến trúc          " + QSysInfo::currentCpuArchitecture() + "\n\n" +
            "Hostname          " + QSysInfo::machineHostName() + "\n\n" +
            "Người dùng      " + currentUserName(getuid()) + "\n\n" +
            "RAM                  " + humanBytes(total - available) + " / " + humanBytes(total));
    }

    void refreshProcesses() {
        configureModel(processModel_, {"PID", "User", "Trạng thái", "RAM", "CPU time", "Lệnh"});
        QDir proc("/proc");
        const long ticks = sysconf(_SC_CLK_TCK);
        const long pageSize = sysconf(_SC_PAGESIZE);
        for (const QString &name : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            bool numeric = false; const int pid = name.toInt(&numeric); if (!numeric) continue;
            const QString base = "/proc/" + name + "/";
            QFileInfo info(base);
            QString stat = readText(base + "stat");
            const int close = stat.lastIndexOf(')');
            if (close < 0) continue;
            const QStringList fields = stat.mid(close + 2).simplified().split(' ');
            if (fields.size() < 22) continue;
            QString command = readText(base + "cmdline");
            command.replace(QChar('\0'), ' '); command = command.trimmed();
            if (command.isEmpty()) command = stat.mid(stat.indexOf('(') + 1, close - stat.indexOf('(') - 1);
            const qulonglong cpuTicks = fields[11].toULongLong() + fields[12].toULongLong();
            const qlonglong rss = fields[21].toLongLong() * pageSize;
            QList<QStandardItem*> row;
            auto *pidItem = new QStandardItem; pidItem->setData(pid, Qt::DisplayRole); pidItem->setData(pid, Qt::UserRole);
            row << pidItem << new QStandardItem(currentUserName(info.ownerId()))
                << new QStandardItem(fields[0]) << new QStandardItem(humanBytes(std::max<qlonglong>(0, rss)))
                << new QStandardItem(QString::number(cpuTicks / static_cast<double>(ticks), 'f', 1) + " s")
                << new QStandardItem(command);
            processModel_->appendRow(row);
        }
        processTable_->sortByColumn(0, Qt::AscendingOrder);
        statusBar()->showMessage(QString("Đã nạp %1 tiến trình").arg(processModel_->rowCount()), 3000);
    }

    int selectedPid(QTableView *table) const {
        QModelIndex index = table->currentIndex();
        if (!index.isValid()) return -1;
        return index.sibling(index.row(), 0).data(Qt::UserRole).toInt();
    }

    void signalSelectedProcess(int signal) {
        const int pid = selectedPid(processTable_);
        if (pid <= 1) { QMessageBox::information(this, "Chọn tiến trình", "Hãy chọn một tiến trình hợp lệ."); return; }
        const QString action = signal == SIGKILL ? "buộc dừng" : "dừng";
        if (QMessageBox::question(this, "Xác nhận", QString("Bạn có chắc muốn %1 tiến trình PID %2?").arg(action).arg(pid)) != QMessageBox::Yes) return;
        if (::kill(pid, signal) != 0) QMessageBox::critical(this, "Không thể dừng", QString::fromLocal8Bit(strerror(errno)));
        else { statusBar()->showMessage(QString("Đã gửi signal tới PID %1").arg(pid), 4000); QTimer::singleShot(350, this, [this] { refreshProcesses(); }); }
    }

    static QString permissionString(QFile::Permissions p) {
        QString s;
        const QFile::Permission bits[] = {QFile::ReadOwner, QFile::WriteOwner, QFile::ExeOwner,
                                          QFile::ReadGroup, QFile::WriteGroup, QFile::ExeGroup,
                                          QFile::ReadOther, QFile::WriteOther, QFile::ExeOther};
        const char chars[] = {'r','w','x','r','w','x','r','w','x'};
        for (int i = 0; i < 9; ++i) s += (p & bits[i]) ? QChar(chars[i]) : QChar('-');
        return s;
    }

    void navigateTo(QString path) {
        path = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
        QFileInfo info(path);
        if (!info.exists() || !info.isDir()) { QMessageBox::warning(this, "Đường dẫn không hợp lệ", "Thư mục không tồn tại hoặc không thể truy cập."); return; }
        currentPath_ = info.absoluteFilePath(); pathEdit_->setText(currentPath_); refreshFiles();
    }

    void refreshFiles() {
        ++(*fileSizeGeneration_);
        configureModel(fileModel_, {"Tên", "Loại", "Kích thước", "Quyền", "Cập nhật"});
        const int count = populateFileItems(nullptr, currentPath_);
        statusBar()->showMessage(QString("%1 mục trong %2").arg(count).arg(currentPath_), 3000);
    }

    int populateFileItems(QStandardItem *parent, const QString &path) {
        QDir dir(path);
        const QFileInfoList entries = dir.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
            QDir::DirsFirst | QDir::IgnoreCase | QDir::Name);
        QStringList directories;
        for (const QFileInfo &info : entries) {
            auto *name = new QStandardItem(info.fileName());
            name->setData(info.absoluteFilePath(), Qt::UserRole);
            const bool realDirectory = info.isDir() && !info.isSymLink();
            name->setData(!realDirectory, Qt::UserRole + 1);
            QList<QStandardItem*> row{name,
                new QStandardItem(info.isSymLink() ? "Liên kết" : info.isDir() ? "Thư mục" : "File"),
                new QStandardItem(realDirectory ? "Đang tính…" : humanBytes(info.size())),
                new QStandardItem(permissionString(info.permissions())),
                new QStandardItem(info.lastModified().toString("dd/MM/yyyy HH:mm"))};
            if (parent) parent->appendRow(row); else fileModel_->appendRow(row);
            if (realDirectory) {
                directories << info.absoluteFilePath();
                QList<QStandardItem*> placeholder{new QStandardItem("Đang tải…")};
                while (placeholder.size() < fileModel_->columnCount()) placeholder << new QStandardItem;
                name->appendRow(placeholder);
            }
        }
        startDirectorySizeCalculation(directories);
        return entries.size();
    }

    void startDirectorySizeCalculation(const QStringList &directories) {
        if (directories.isEmpty() || qApp->property("selfTest").toBool()) return;
        const int generation = fileSizeGeneration_->load();
        auto *watcher = new QFutureWatcher<QHash<QString, qulonglong>>(this);
        const auto generationState = fileSizeGeneration_;
        connect(watcher, &QFutureWatcher<QHash<QString, qulonglong>>::finished, this,
                [this, watcher, generation, generationState] {
            const QHash<QString, qulonglong> sizes = watcher->result();
            watcher->deleteLater();
            if (generationState->load() != generation) return;
            for (auto it = sizes.cbegin(); it != sizes.cend(); ++it) {
                const QModelIndexList matches = fileModel_->match(
                    fileModel_->index(0, 0), Qt::UserRole, it.key(), -1,
                    Qt::MatchExactly | Qt::MatchRecursive);
                for (const QModelIndex &index : matches) {
                    QStandardItem *nameItem = fileModel_->itemFromIndex(index);
                    QStandardItem *sizeItem = nameItem->parent()
                        ? nameItem->parent()->child(nameItem->row(), 2)
                        : fileModel_->item(nameItem->row(), 2);
                    if (sizeItem) sizeItem->setText(humanBytes(it.value()));
                }
            }
            statusBar()->showMessage("Đã tính xong kích thước thư mục", 3000);
        });
        watcher->setFuture(QtConcurrent::run([directories, generation, generationState] {
            QHash<QString, qulonglong> sizes;
            for (const QString &path : directories) {
                if (generationState->load() != generation) return sizes;
                qulonglong total = 0;
                QDirIterator iterator(path, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks,
                                      QDirIterator::Subdirectories);
                while (iterator.hasNext()) {
                    if (generationState->load() != generation) return sizes;
                    iterator.next();
                    total += static_cast<qulonglong>(std::max<qint64>(0, iterator.fileInfo().size()));
                }
                sizes.insert(path, total);
            }
            return sizes;
        }));
    }

    QString selectedFilePath() const {
        QModelIndex index = fileTable_->currentIndex();
        return index.isValid() ? index.sibling(index.row(), 0).data(Qt::UserRole).toString() : QString();
    }

    void openSelectedFile() {
        const QString path = selectedFilePath();
        if (path.isEmpty()) { QMessageBox::information(this, "Chọn file", "Hãy chọn file hoặc thư mục."); return; }
        if (QFileInfo(path).isDir()) navigateTo(path);
        else if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) QMessageBox::warning(this, "Không thể mở", "Không tìm thấy ứng dụng phù hợp.");
    }

    void createDirectory() {
        bool ok = false;
        QString name = QInputDialog::getText(this, "Tạo thư mục", "Tên thư mục:", QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        if (name.contains('/') || name == "." || name == "..") { QMessageBox::warning(this, "Tên không hợp lệ", "Tên thư mục không được chứa '/'."); return; }
        if (!QDir(currentPath_).mkdir(name)) QMessageBox::critical(this, "Không thể tạo", "Kiểm tra quyền hoặc tên đã tồn tại.");
        else refreshFiles();
    }

    void renameSelectedFile() {
        const QString path = selectedFilePath(); if (path.isEmpty()) { QMessageBox::information(this, "Chọn mục", "Hãy chọn mục cần đổi tên."); return; }
        QFileInfo info(path); bool ok = false;
        QString name = QInputDialog::getText(this, "Đổi tên", "Tên mới:", QLineEdit::Normal, info.fileName(), &ok).trimmed();
        if (!ok || name.isEmpty() || name == info.fileName()) return;
        if (name.contains('/') || name == "." || name == "..") { QMessageBox::warning(this, "Tên không hợp lệ", "Tên không được chứa '/'."); return; }
        if (!QFile::rename(path, info.dir().absoluteFilePath(name))) QMessageBox::critical(this, "Không thể đổi tên", "Kiểm tra quyền hoặc tên đích đã tồn tại.");
        else refreshFiles();
    }

    void deleteSelectedFile() {
        const QString path = selectedFilePath(); if (path.isEmpty()) { QMessageBox::information(this, "Chọn mục", "Hãy chọn mục cần xóa."); return; }
        QFileInfo info(path);
        const QString detail = info.isDir() && !info.isSymLink() ? "Thư mục và toàn bộ nội dung sẽ bị xóa vĩnh viễn." : "File sẽ bị xóa vĩnh viễn.";
        if (QMessageBox::warning(this, "Xác nhận xóa", detail + "\n\n" + path, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
        bool ok = (info.isDir() && !info.isSymLink()) ? QDir(path).removeRecursively() : QFile::remove(path);
        if (!ok) QMessageBox::critical(this, "Không thể xóa", "Kiểm tra quyền truy cập."); else refreshFiles();
    }

    void refreshSockets() {
        configureModel(socketModel_, {"Protocol", "Trạng thái", "Local", "Peer", "Tiến trình"});
        QProcess proc;
        proc.start("ss", {"-H", "-t", "-u", "-n", "-a", "-p"});
        if (!proc.waitForStarted(1500) || !proc.waitForFinished(4000)) {
            proc.kill(); statusBar()->showMessage("Không chạy được lệnh ss. Hãy cài iproute2.", 5000); return;
        }
        const QString output = QString::fromLocal8Bit(proc.readAllStandardOutput());
        for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
            const QStringList fields = line.simplified().split(' ');
            if (fields.size() < 6) continue;
            QString owner;
            for (int i = 6; i < fields.size(); ++i) owner += (owner.isEmpty() ? "" : " ") + fields[i];
            auto *proto = new QStandardItem(fields[0].toUpper());
            QRegularExpressionMatch pidMatch = QRegularExpression("pid=(\\d+)").match(owner);
            proto->setData(pidMatch.hasMatch() ? pidMatch.captured(1).toInt() : -1, Qt::UserRole);
            socketModel_->appendRow({proto, new QStandardItem(fields[1]), new QStandardItem(fields[4]),
                                     new QStandardItem(fields[5]), new QStandardItem(owner.isEmpty() ? "—" : owner)});
        }
        socketCount_->setText(QString::number(socketModel_->rowCount()));
        statusBar()->showMessage(QString("Đã nạp %1 socket").arg(socketModel_->rowCount()), 3000);
    }

    void terminateSocketOwner() {
        const int pid = selectedPid(socketTable_);
        if (pid <= 1) { QMessageBox::information(this, "Không có PID", "Socket này không hiển thị tiến trình sở hữu. Một số thông tin cần quyền cao hơn."); return; }
        if (QMessageBox::question(this, "Xác nhận", QString("Gửi SIGTERM tới tiến trình PID %1 đang sở hữu socket?").arg(pid)) != QMessageBox::Yes) return;
        if (::kill(pid, SIGTERM) != 0) QMessageBox::critical(this, "Không thể dừng", QString::fromLocal8Bit(strerror(errno)));
        else QTimer::singleShot(400, this, [this] { refreshSockets(); });
    }

    static qulonglong sysNumber(const QString &path) { return readText(path).trimmed().toULongLong(); }

    void refreshNetwork() {
        configureModel(networkModel_, {"Interface", "Trạng thái", "Địa chỉ", "MAC", "RX", "TX"});
        const auto interfaces = QNetworkInterface::allInterfaces();
        for (const QNetworkInterface &iface : interfaces) {
            QStringList addresses;
            for (const QNetworkAddressEntry &entry : iface.addressEntries()) addresses << entry.ip().toString();
            const bool up = iface.flags().testFlag(QNetworkInterface::IsUp);
            auto *name = new QStandardItem(iface.humanReadableName()); name->setData(iface.name(), Qt::UserRole);
            const QString sys = "/sys/class/net/" + iface.name() + "/statistics/";
            networkModel_->appendRow({name, new QStandardItem(up ? "● Đang bật" : "○ Đã tắt"),
                new QStandardItem(addresses.join(", ")), new QStandardItem(iface.hardwareAddress()),
                new QStandardItem(humanBytes(sysNumber(sys + "rx_bytes"))), new QStandardItem(humanBytes(sysNumber(sys + "tx_bytes")))});
        }
        interfaceCount_->setText(QString::number(interfaces.size()));
        statusBar()->showMessage(QString("Đã nạp %1 interface").arg(interfaces.size()), 3000);
    }

    QString selectedInterface() const {
        QModelIndex index = networkTable_->currentIndex();
        return index.isValid() ? index.sibling(index.row(), 0).data(Qt::UserRole).toString() : QString();
    }

    void setSelectedInterface(bool enable) {
        const QString iface = selectedInterface();
        if (iface.isEmpty()) { QMessageBox::information(this, "Chọn interface", "Hãy chọn một interface mạng."); return; }
        if (!enable && iface == "lo") { QMessageBox::warning(this, "Không được phép", "Không nên tắt loopback interface."); return; }
        const QString action = enable ? "bật" : "tắt";
        if (QMessageBox::question(this, "Xác nhận", QString("%1 interface '%2'? Kết nối mạng có thể bị gián đoạn.").arg(action, iface)) != QMessageBox::Yes) return;
        const bool started = QProcess::startDetached("pkexec", {"ip", "link", "set", "dev", iface, enable ? "up" : "down"});
        if (!started) QMessageBox::critical(this, "Không thể thực hiện", "Không khởi chạy được pkexec. Hãy kiểm tra PolicyKit và iproute2.");
        else { statusBar()->showMessage("Đã gửi yêu cầu thay đổi interface", 4000); QTimer::singleShot(1800, this, [this] { refreshNetwork(); }); }
    }

    void startPing(QPushButton *trigger) {
        QString host = pingHost_->text().trimmed();
        QHostAddress address;
        static const QRegularExpression hostname("^[A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?$");
        if (!address.setAddress(host) && !hostname.match(host).hasMatch()) { QMessageBox::warning(this, "Host không hợp lệ", "Chỉ nhập hostname hoặc địa chỉ IP."); return; }
        if (pingProcess_) { pingProcess_->kill(); pingProcess_->deleteLater(); }
        pingProcess_ = new QProcess(this);
        trigger->setEnabled(false); pingOutput_->setPlainText("Đang kiểm tra " + host + " ...");
        connect(pingProcess_, &QProcess::readyReadStandardOutput, this, [this] { pingOutput_->append(QString::fromLocal8Bit(pingProcess_->readAllStandardOutput()).trimmed()); });
        connect(pingProcess_, &QProcess::readyReadStandardError, this, [this] { pingOutput_->append(QString::fromLocal8Bit(pingProcess_->readAllStandardError()).trimmed()); });
        connect(pingProcess_, &QProcess::errorOccurred, this, [this, trigger](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart) return;
            trigger->setEnabled(true);
            pingOutput_->append("Không khởi chạy được lệnh ping. Hãy cài gói iputils-ping.");
            statusBar()->showMessage("Không tìm thấy lệnh ping", 5000);
            pingProcess_->deleteLater();
            pingProcess_ = nullptr;
        });
        connect(pingProcess_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, trigger](int code, QProcess::ExitStatus) { trigger->setEnabled(true); statusBar()->showMessage(code == 0 ? "Ping thành công" : "Ping thất bại", 4000); pingProcess_->deleteLater(); pingProcess_ = nullptr; });
        pingProcess_->start("ping", {"-c", "4", "-W", "2", host});
    }
};

int main(int argc, char *argv[]) {
    const QString argument = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (argument == "--version") {
        QTextStream(stdout) << "linux-control-center 1.0.0\n";
        return 0;
    }
    if (argument == "--help") {
        QTextStream(stdout) << "Linux Control Center\n"
                            << "  --help       Hiện hướng dẫn\n"
                            << "  --version    Hiện phiên bản\n"
                            << "  --self-test  Kiểm tra ứng dụng ở chế độ offscreen\n";
        return 0;
    }
    const bool selfTest = argument == "--self-test";
    if (selfTest) qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setApplicationName("Linux Control Center");
    QApplication::setApplicationVersion("1.0.0");
    app.setProperty("selfTest", selfTest);
    MainWindow window;
    if (selfTest) {
        QTimer::singleShot(250, &app, [&app] { qInfo() << "SELF-TEST PASS"; app.quit(); });
    } else {
        window.show();
    }
    return app.exec();
}
