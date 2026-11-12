#include "window.h"
#include "core.h"
#ifdef EVEREYE_WEBENGINE
#include "terminalview.h"
#endif
#include <QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QUrl>
#include <QBoxLayout>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QStackedWidget>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <QtConcurrent/QtConcurrentRun>

namespace insomnia {
namespace {
// Let wheel events reach the scroll area, including when an input has focus.
// Changing session duration must require typing or an explicit keyboard action.
class NumberField final : public QSpinBox {
public:
    explicit NumberField(QWidget *parent = nullptr) : QSpinBox(parent) {
        setButtonSymbols(QAbstractSpinBox::NoButtons);
        setAlignment(Qt::AlignCenter); setKeyboardTracking(false);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        lineEdit()->installEventFilter(this);
    }
protected:
    void wheelEvent(QWheelEvent *event) override {
        for (auto *parent = parentWidget(); parent; parent = parent->parentWidget()) {
            if (auto *area = qobject_cast<QScrollArea *>(parent)) {
                QWheelEvent forwarded(area->viewport()->mapFromGlobal(event->globalPosition().toPoint()), event->globalPosition(), event->pixelDelta(), event->angleDelta(), event->buttons(), event->modifiers(), event->phase(), event->inverted());
                QApplication::sendEvent(area->viewport(), &forwarded); event->accept(); return;
            }
        }
        event->ignore();
    }
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == lineEdit() && event->type() == QEvent::Wheel) { wheelEvent(static_cast<QWheelEvent *>(event)); return true; }
        return QSpinBox::eventFilter(object, event);
    }
    void resizeEvent(QResizeEvent *event) override {
        QSpinBox::resizeEvent(event);
        // Some Qt stylesheet styles give NoButtons an edit field one pixel wide.
        lineEdit()->setGeometry(rect().adjusted(10, 8, -10, -8));
    }
};
class Switch final : public QCheckBox {
public:
    explicit Switch(bool large = false) { setFixedSize(large ? 80 : 44, large ? 38 : 26); setCursor(Qt::PointingHandCursor); }
protected:
    bool hitButton(const QPoint &point) const override { return rect().contains(point); }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        p.setOpacity(isEnabled() ? 1.0 : 0.42);
        const bool large=width()>50; const QRectF track=large?QRectF(0,0,80,38):QRectF(2,3,40,20);
        p.setPen(Qt::NoPen); p.setBrush(QColor(isChecked()?"#65bda7":"#48504f")); p.drawRoundedRect(track,track.height()/2,track.height()/2);
        const qreal diameter=large?28:14; const qreal inset=large?5:3;
        p.setBrush(QColor("#fcfbff")); p.drawEllipse(QRectF(isChecked()?track.right()-diameter-inset:track.left()+inset,track.top()+inset,diameter,diameter));
        if(hasFocus()){ p.setBrush(Qt::NoBrush); p.setPen(QPen(QColor("#91bca9"),1.5)); p.drawRoundedRect(rect().adjusted(1,1,-1,-1),height()/2,height()/2); }
    }
};
class TimerFace final : public QFrame {
protected:
    void paintEvent(QPaintEvent *event) override {
        QFrame::paintEvent(event); QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const QRectF track(28,height()-26,width()-56,3);
        p.setPen(Qt::NoPen); p.setBrush(QColor("#343b3b")); p.drawRoundedRect(track,1.5,1.5);
        if(property("active").toBool()) {
            p.setBrush(QColor("#65bda7"));
            p.drawRoundedRect(QRectF(track.left(),track.top(),track.width()*qBound(0.0,property("progress").toDouble(),1.0),track.height()),1.5,1.5);
        }
    }
};
class QuickPanelSurface final : public QWidget {
public:
    QuickPanelSurface() : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint) {}
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#171d22"));
        painter.drawRoundedRect(QRectF(rect()),16,16);
    }
};
// A stacked page should not inherit the tallest sibling's size hint, or a
// terminal page would force the dashboard to scroll.
class PageStack final : public QStackedWidget {
public:
    using QStackedWidget::QStackedWidget;
    QSize sizeHint() const override { return currentWidget() ? currentWidget()->sizeHint() : QStackedWidget::sizeHint(); }
    QSize minimumSizeHint() const override { return currentWidget() ? currentWidget()->minimumSizeHint() : QStackedWidget::minimumSizeHint(); }
};
// Hidden pages must not impose their own minimum height on the shared canvas;
// the enclosing scroll area supplies scrolling when a page is taller.
class PageWidget final : public QWidget {
public:
    using QWidget::QWidget;
    QSize minimumSizeHint() const override { return QSize(0, 0); }
};
class Choice final : public QComboBox {
protected:
    void paintEvent(QPaintEvent *event) override {
        QComboBox::paintEvent(event); QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#91bca9"),1.5)); QPainterPath arrow;
        arrow.moveTo(width()-18,height()/2.0-2); arrow.lineTo(width()-14,height()/2.0+2); arrow.lineTo(width()-10,height()/2.0-2); p.drawPath(arrow);
    }
};
QLabel *textLabel(const QString &text, const QString &name, bool wrap = false) {
    auto *label = new QLabel(text); label->setObjectName(name); label->setTextFormat(Qt::PlainText);
    label->setWordWrap(wrap); return label;
}
QFrame *card(const QString &name) {
    auto *frame = new QFrame; frame->setObjectName(name); frame->setFrameShape(QFrame::StyledPanel);
    frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum); return frame;
}
QWidget *rule(const QString &title, const QString &description, QCheckBox *control) {
    auto *row = new QWidget; auto *layout = new QHBoxLayout(row); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(14);
    auto *words = new QVBoxLayout; words->setSpacing(5);
    words->addWidget(textLabel(title, "ruleTitle", true)); words->addWidget(textLabel(description, "muted", true));
    layout->addLayout(words, 1); layout->addWidget(control, 0, Qt::AlignVCenter);
    control->setAccessibleName(title); control->setAccessibleDescription(description); return row;
}
}
QIcon eyeIcon(bool active) {
    QPixmap image(128,128); image.fill(Qt::transparent); QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen); p.setBrush(QColor(active?"#377e6e":"#495351")); p.drawRoundedRect(QRectF(4,4,120,120),26,26);
    p.setBrush(Qt::NoBrush); p.setPen(QPen(QColor("#ffffff"),5)); p.drawEllipse(QRectF(35,35,58,58));
    p.setPen(QPen(QColor(255,255,255,175),3)); p.drawEllipse(QRectF(45,45,38,38)); return QIcon(image);
}
Window::Window(Client c, bool demonstration, bool background)
    : client(std::move(c)), demo(demonstration), backgroundRequests(background) {
    setObjectName("evereyeWindow"); setWindowTitle(demo ? "EverEye · Preview" : "EverEye"); setWindowIcon(eyeIcon(true));
    // Only the area outside the rounded surface is transparent. paintEvent
    // fills the entire content surface with an opaque background.
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMinimumSize(360, 340); resize(1118, 650);
    setStyleSheet(R"(
        QWidget { color: #f4f6ff; font-family: "Inter", "Noto Sans", sans-serif; font-size: 13px; }
        QWidget#evereyeWindow { background: transparent; }
        QFrame#shell { background: #191d1e; border: 1px solid #343b3c; border-radius: 12px; }
        QFrame#sidebar { background: #161a1b; border: 0; border-right: 1px solid #303738; border-top-left-radius: 12px; border-bottom-left-radius: 12px; }
        QFrame#dashboardCard, QFrame#timerFace, QFrame#protectionCard { background: #202627; border: 1px solid #343c3d; border-radius: 8px; }
        QLabel, QStackedWidget, QWidget#scrollCanvas, QWidget#page { background: transparent; border: 0; }
        QLabel#appName { font-size: 19px; font-weight: 600; }
        QLabel#linux { color: #8c9996; font-size: 9px; font-weight: 500; }
        QLabel#headline { font-size: 29px; font-weight: 700; letter-spacing: -0.8px; }
        QLabel#sectionTitle { font-size: 18px; font-weight: 600; }
        QLabel#eyebrow { color: #9baaa5; font-size: 11px; font-weight: 500; letter-spacing: 1px; }
        QLabel#muted, QLabel#detail, QLabel#selectionLabel { color: #a5b1ae; font-size: 13px; }
        QLabel#small { color: #929f9c; font-size: 11px; }
        QLabel#ruleTitle, QLabel#protectionValue { font-size: 14px; font-weight: 600; }
        QLabel#countdown { font-family: "DejaVu Sans Mono", monospace; font-size: 46px; font-weight: 500; letter-spacing: -1px; }
        QLabel#endsAt { color: #8dcab8; font-size: 12px; }
        QLabel#badge { color: #a8cfbf; background: #24332e; border: 1px solid #3c554a; border-radius: 5px; padding: 6px 10px; font-size: 11px; }
        QLabel#badge[offline="true"] { color: #ffd6ad; border-color: #98765c; }
        QLabel#error { background: #483c36; color: #ffe0bf; border: 1px solid #a68165; border-radius: 10px; padding: 12px; }
        QSpinBox { background: #202627; color: #f4f6ff; border: 1px solid #495550; border-radius: 10px; padding: 9px 4px; min-height: 24px; font-size: 20px; selection-background-color: #377e6e; }
        QSpinBox:focus { border: 2px solid #83c7b1; padding: 8px 3px; }
        QSpinBox:disabled { color: #98a39f; border-color: #39433f; }
        QSpinBox QLineEdit { color: #f4f6ff; background: transparent; border: 0; padding: 0; font-size: 20px; }
        QPushButton { background: rgba(255,255,255,12); color: #eef2fc; border: 1px solid #48534f; border-radius: 6px; padding: 11px 14px; font-weight: 600; }
        QPushButton:hover { background: rgba(255,255,255,28); border-color: #71887d; }
        QPushButton:focus { border: 2px solid #83c7b1; padding: 10px 13px; }
        QPushButton:disabled { color: #929e99; background: rgba(255,255,255,5); border-color: #39433f; }
        QPushButton#primary, QPushButton#overviewStart, QPushButton#newTerminal { background: #83c7b1; color: #10251e; border: 1px solid #83c7b1; padding: 12px 14px; border-radius: 6px; }
        QPushButton#primary:hover, QPushButton#overviewStart:hover, QPushButton#newTerminal:hover { background: #a0d8c5; }
        QPushButton#end { background: #382828; color: #e3b6af; border: 1px solid #74504b; padding: 12px 14px; border-radius: 6px; }
        QPushButton#end:hover { background: #4b302d; }
        QPushButton#primary:disabled, QPushButton#end:disabled, QPushButton#overviewStart:disabled { background: #2c3432; color: #98a39f; border-color: #39423f; }
        QPushButton[nav="true"] { background: transparent; border: 1px solid transparent; color: #a5b1ae; text-align: left; font-weight: 400; padding: 15px 16px; }
        QPushButton[nav="true"]:checked { background: #283330; color: #c2e0d3; border-color: #3b4b44; font-weight: 600; }
        QPushButton[nav="true"]:hover { background: rgba(255,255,255,14); }
        QPushButton[quiet="true"] { color: #a1ccba; background: transparent; border-color: transparent; padding: 7px; font-size: 12px; }
        QComboBox { color: #f7f8ff; background: transparent; border: 0; font-size: 19px; font-weight: 600; padding: 0 24px 0 0; min-height: 29px; }
        QComboBox::drop-down { border: 0; width: 22px; }
        QComboBox::down-arrow { image: none; }
        QComboBox QAbstractItemView { background: #252e2b; color: #f4f6ff; selection-background-color: #3c574b; padding: 6px; font-size: 14px; }
        QToolButton { background: transparent; color: #a6b8b0; border: 0; padding: 3px 8px; font-size: 20px; }
        QToolButton::menu-indicator { image: none; }
        QMenu { background: #252e2b; border: 1px solid #48534f; padding: 6px; }
        QMenu::item { padding: 9px 14px; } QMenu::item:selected { background: #3c574b; }
        QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: 0; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 3px 1px; }
        QScrollBar::handle:vertical { background: #52675d; min-height: 38px; border-radius: 4px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        QTextEdit, QPlainTextEdit { background: #202627; color: #eef2fc; border: 1px solid #48534f; padding: 12px; font-family: "DejaVu Sans Mono", monospace; }
        QListWidget { background: #202627; color: #eef2fc; border: 1px solid #48534f; }
        QLineEdit { background: #202627; color: #f4f6ff; border: 1px solid #495550; border-radius: 8px; padding: 8px; }
    )");
    auto *outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0);
    auto *shell = card("shell"); outer->addWidget(shell); auto *split = new QHBoxLayout(shell); split->setContentsMargins(0,0,0,0); split->setSpacing(0);
    sidebar = card("sidebar"); sidebar->setFixedWidth(205); auto *side = new QVBoxLayout(sidebar); side->setContentsMargins(16,28,16,24); side->setSpacing(6);
    auto *brand = new QHBoxLayout; brand->setSpacing(12); auto *mark = new QLabel; mark->setPixmap(eyeIcon(true).pixmap(37,37)); brand->addWidget(mark);
    brand->addWidget(textLabel("EverEye", "appName")); auto *platformTag = textLabel("LINUX", "linux"); brand->addWidget(platformTag,0,Qt::AlignTop); brand->addStretch(); side->addLayout(brand); side->addSpacing(26);
    const QStringList names{"Overview", "Sessions", "Lid actions", "Preferences", "Terminal"};
    for (int i=0; i<names.size(); ++i) {
        auto *button = new QPushButton(names[i]); button->setObjectName("nav" + QString::number(i)); button->setProperty("nav",true); button->setCheckable(true); button->setCursor(Qt::PointingHandCursor); side->addWidget(button);
        connect(button,&QPushButton::clicked,this,[this,i]{ selectPage(i); });
    }
    side->addStretch(); auto *line = new QFrame; line->setFixedHeight(1); line->setStyleSheet("background: #343c3d;"); side->addWidget(line); side->addSpacing(10);
    serviceStatus = textLabel("● Connecting to service", "muted", true); side->addWidget(serviceStatus);
    powerLabel = textLabel("Reading power status…", "small", true); side->addWidget(powerLabel);
    const auto session = qEnvironmentVariable("XDG_SESSION_TYPE"); side->addWidget(textLabel((session.isEmpty() ? "Desktop" : session.left(1).toUpper()+session.mid(1)) + " · Linux", "muted"));
    side->addSpacing(12); side->addWidget(textLabel("EVEREYE 0.4.0", "small")); split->addWidget(sidebar);
    auto *main = new QVBoxLayout; main->setContentsMargins(0,0,0,0); main->setSpacing(0); split->addLayout(main,1);
    auto *header = new QHBoxLayout; header->setObjectName("topBar"); header->setContentsMargins(36,26,28,12);
    breadcrumb = textLabel("Workspace / Overview", "muted"); header->addWidget(breadcrumb); header->addStretch();
    pagePicker = new Choice; pagePicker->setObjectName("pagePicker"); pagePicker->addItems(names); pagePicker->setAccessibleName("Navigation"); pagePicker->setFixedWidth(125); pagePicker->setStyleSheet("font-size: 13px;"); header->addWidget(pagePicker); pagePicker->hide();
    connect(pagePicker,&QComboBox::activated,this,&Window::selectPage);
    badge = textLabel("Connecting", "badge"); header->addWidget(badge);
    auto *more = new QToolButton; more->setText("⋯"); more->setAccessibleName("Window menu"); more->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(more); menu->addAction("Hide window",this,&QWidget::hide); menu->addAction("Close window · keep session running",this,&Window::QuitWindow);
    menu->addSeparator(); menu->addAction("End session and quit",this,[this]{ perform("shutdown"); }); more->setMenu(menu); header->addWidget(more); main->addLayout(header);
    scroll = new QScrollArea; scroll->setObjectName("contentScroll"); scroll->setWidgetResizable(true); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setFocusPolicy(Qt::StrongFocus); scroll->setFrameShape(QFrame::NoFrame); main->addWidget(scroll,1);
    auto *canvas = new QWidget; canvas->setObjectName("scrollCanvas"); auto *content = new QVBoxLayout(canvas); content->setObjectName("contentLayout"); content->setContentsMargins(36,12,28,8); content->setSpacing(14); content->setSizeConstraint(QLayout::SetMinAndMaxSize);
    pages = new PageStack; pages->setObjectName("pages"); content->addWidget(pages);
    const auto page = [&] { auto *p = new PageWidget; p->setObjectName("page"); auto *layout = new QVBoxLayout(p); layout->setContentsMargins(0,0,0,0); layout->setSpacing(18); pages->addWidget(p); return layout; };
    auto *overview = page(); overview->setSpacing(15);
    auto *intro = new QHBoxLayout; auto *words = new QVBoxLayout; words->setSpacing(6); words->addWidget(textLabel("CURRENT SESSION","eyebrow"));
    headline = textLabel("Ready when you are.","headline",true); words->addWidget(headline);
    subtitle = textLabel("Give your long-running tasks more time.","muted",true); words->addWidget(subtitle); intro->addLayout(words,1);
    sessionToggle = new Switch(true); sessionToggle->setObjectName("sessionToggle"); sessionToggle->setAccessibleName("Keep the system awake"); intro->addWidget(sessionToggle,0,Qt::AlignBottom); overview->addLayout(intro);
    auto *hero = new QWidget; cards = new QBoxLayout(QBoxLayout::LeftToRight,hero); cards->setContentsMargins(0,0,0,0); cards->setSpacing(22);
    auto *face = new TimerFace; face->setObjectName("timerFace"); face->setMinimumHeight(230); auto *timerLayout = new QVBoxLayout(face); timerLayout->setContentsMargins(28,28,28,48); timerLayout->setSpacing(12);
    auto *timeCaption = textLabel("TIME REMAINING","small"); timerLayout->addWidget(timeCaption);
    countdown = textLabel("04:00:00","countdown"); countdown->setAccessibleName("Time remaining"); timerLayout->addWidget(countdown);
    endsAt = textLabel("Choose a session length","endsAt",true); timerLayout->addWidget(endsAt); cards->addWidget(face,6);
    auto *actions = new QVBoxLayout; actions->setSpacing(12);
    auto *extendCard = card("dashboardCard"); auto *extendLayout = new QVBoxLayout(extendCard); extendLayout->setContentsMargins(20,15,18,15); extendLayout->setSpacing(8);
    durationLabel = textLabel("SESSION LENGTH","muted"); extendLayout->addWidget(durationLabel);
    extension = new Choice; extension->setObjectName("durationPreset"); extension->setAccessibleName("Session duration or extension");
    for (int n : {15,30,60,120,240}) extension->addItem(n<60 ? QString("%1 minutes").arg(n) : QString("%1 hour%2").arg(n/60).arg(n==60?"":"s"), n*60);
    extension->addItem("Custom duration…",0); extension->setCurrentIndex(4); extendLayout->addWidget(extension); actions->addWidget(extendCard,1);
    auto *sleepCard = card("dashboardCard"); auto *sleepLayout = new QVBoxLayout(sleepCard); sleepLayout->setContentsMargins(20,15,18,15); sleepLayout->setSpacing(8);
    sleepLayout->addWidget(textLabel("SLEEP BEHAVIOR","muted")); auto *sleepRow = new QHBoxLayout; sleepSummary = textLabel("Prevent suspend","sectionTitle"); sleepRow->addWidget(sleepSummary,1); sleepLayout->addLayout(sleepRow); actions->addWidget(sleepCard,1);
    overviewStart = new QPushButton("Start session"); overviewStart->setObjectName("overviewStart"); actions->addWidget(overviewStart);
    end = new QPushButton("End session"); end->setObjectName("end"); actions->addWidget(end); cards->addLayout(actions,4); overview->addWidget(hero);
    auto *protectionTitle = new QHBoxLayout; protectionTitle->addWidget(textLabel("System protection","ruleTitle")); protectionTitle->addStretch(); protectionTitle->addWidget(textLabel("Configured safeguards","small")); overview->addLayout(protectionTitle);
    auto *protect = new QWidget; protections = new QBoxLayout(QBoxLayout::LeftToRight,protect); protections->setContentsMargins(0,0,0,0); protections->setSpacing(14);
    const auto protectCard = [&](const QString &title, QLabel *&value, int destination) {
        auto *panel = card("protectionCard"); auto *layout = new QVBoxLayout(panel); layout->setContentsMargins(17,14,16,14); layout->setSpacing(7);
        auto *row = new QHBoxLayout; row->addWidget(textLabel(title,"muted"),1); row->addWidget(textLabel("●","endsAt")); layout->addLayout(row);
        value = textLabel("", "protectionValue",true); layout->addWidget(value); protections->addWidget(panel,1);
        panel->setAccessibleName(title); panel->setToolTip("Configure in " + names[destination]);
    };
    protectCard("Battery cutoff",batterySummary,3); protectCard("Lid policy",lidSummary,2); protectCard("Crash recovery",recoverySummary,3); overview->addWidget(protect);
    overview->addStretch();

    auto *sessions = page(); sessions->addWidget(textLabel("Sessions","sectionTitle")); sessions->addWidget(textLabel("Choose a duration or add time to your current session.","muted",true));
    auto *sessionCard = card("dashboardCard"); auto *sessionLayout = new QVBoxLayout(sessionCard); sessionLayout->setContentsMargins(24,24,24,24); sessionLayout->setSpacing(18);
    auto *duration = new QHBoxLayout; duration->setSpacing(10);
    const auto field = [&](const QString &name,int maximum,int initial) {
        auto *column = new QVBoxLayout; auto *spin = new NumberField; spin->setObjectName(name.toLower()); spin->setRange(0,maximum); spin->setValue(initial); spin->setAccessibleName(name); column->addWidget(spin); column->addWidget(textLabel(name,"muted")); duration->addLayout(column,1);
        connect(spin,&QSpinBox::valueChanged,this,[this]{ localError.clear(); updateSelection(); }); return spin;
    };
    days=field("Days",30,0); hours=field("Hours",23,4); minutes=field("Minutes",59,0); sessionLayout->addLayout(duration);
    auto *presets = new QGridLayout; int presetIndex=0; for (int n : {30,60,120,240}) { auto *button = new QPushButton(n<60?"30 min":QString("%1h").arg(n/60)); presets->addWidget(button,presetIndex/2,presetIndex%2); ++presetIndex; connect(button,&QPushButton::clicked,this,[this,n]{ days->setValue(0); hours->setValue(n/60); minutes->setValue(n%60); }); } sessionLayout->addLayout(presets);
    selectionLabel = textLabel("","selectionLabel",true); sessionLayout->addWidget(selectionLabel);
    start=new QPushButton("Start session"); start->setObjectName("primary"); sessionLayout->addWidget(start);
    auto *quick = new QHBoxLayout; extend15=new QPushButton("+15 minutes"); extendHour=new QPushButton("+1 hour"); quick->addWidget(extend15); quick->addWidget(extendHour); sessionLayout->addLayout(quick); sessions->addWidget(sessionCard);
    detail=textLabel("","detail",true); sessions->addWidget(detail); sessions->addStretch();

    auto *lidPage=page(); lidPage->addWidget(textLabel("Lid actions","sectionTitle")); lidPage->addWidget(textLabel("Choose how your next session handles a closed laptop lid.","muted",true));
    auto *lidCard=card("dashboardCard"); auto *lidLayout=new QVBoxLayout(lidCard); lidLayout->setContentsMargins(24,24,24,24); lidLayout->setSpacing(24);
    lid=new Switch; lid->setObjectName("lidSwitch"); lid->setChecked(true); lidLayout->addWidget(rule("Keep awake with lid closed","Let your tasks continue when you close your laptop.",lid));
    lidLayout->addWidget(textLabel("Changes apply to the next session. Your screen can still lock. Lid behavior depends on your desktop and hardware.","muted",true)); lidPage->addWidget(lidCard); lidPage->addStretch();
    auto *preferences=page(); preferences->addWidget(textLabel("Preferences","sectionTitle")); preferences->addWidget(textLabel("Configure safeguards before starting a session.","muted",true));
    auto *settings=card("dashboardCard"); auto *settingsLayout=new QVBoxLayout(settings); settingsLayout->setContentsMargins(24,24,24,24); settingsLayout->setSpacing(20);
    saver=new Switch; saver->setObjectName("saverSwitch"); saver->setChecked(true); settingsLayout->addWidget(rule("Use power saver","Request power saver on low battery or with the lid closed.",saver));
    auto *thresholds=new QGridLayout; thresholds->setHorizontalSpacing(16); thresholds->setVerticalSpacing(10);
    cutoff=new NumberField; cutoff->setObjectName("cutoff"); cutoff->setRange(0,99); cutoff->setValue(10); cutoff->setSuffix("%"); cutoff->setAccessibleName("Battery cutoff percentage; zero disables protection");
    lowPower=new NumberField; lowPower->setObjectName("lowPower"); lowPower->setRange(1,100); lowPower->setValue(40); lowPower->setSuffix("%"); lowPower->setAccessibleName("Power saver battery threshold");
    thresholds->addWidget(textLabel("End session below","muted",true),0,0); thresholds->addWidget(textLabel("Power saver below","muted",true),0,1); thresholds->addWidget(cutoff,1,0); thresholds->addWidget(lowPower,1,1); thresholds->setColumnStretch(0,1); thresholds->setColumnStretch(1,1); settingsLayout->addLayout(thresholds);
    settingsLayout->addWidget(textLabel("Set battery cutoff to 0% to disable it. Crash recovery releases interrupted sessions; it never silently resumes them.","muted",true)); preferences->addWidget(settings);
    auto *diagnostics=new QPushButton("Open diagnostics"); diagnostics->setObjectName("diagnostics"); preferences->addWidget(diagnostics); preferences->addWidget(textLabel("Closing the window keeps a running session alive. End session and quit is available in the window menu.","muted",true)); preferences->addStretch();
    auto *aiPage=page();
    aiPage->addWidget(textLabel("Terminal","sectionTitle"));
    aiPage->addWidget(textLabel("Open a real terminal attached to a session. It keeps running when you close this window.","muted",true));
    auto *newTerminal=new QPushButton("New terminal"); newTerminal->setObjectName("newTerminal");
    aiPage->addWidget(newTerminal);
    terminalHost=new QWidget; terminalHost->setObjectName("terminalHost");
    auto *terminalLayout=new QVBoxLayout(terminalHost); terminalLayout->setContentsMargins(0,0,0,0); terminalLayout->setSpacing(0);
    terminalHost->setMinimumHeight(380);
    aiPage->addWidget(terminalHost,1);
    connect(newTerminal,&QPushButton::clicked,this,[this]{ perform("jobs.start", {{"shell", true}}); });
    error=textLabel("","error",true); error->setAccessibleName("Session error or warning"); error->hide(); content->addWidget(error);
    reconnect=new QPushButton("Reconnect to service"); reconnect->hide(); content->addWidget(reconnect); content->addStretch(); scroll->setWidget(canvas);
    connect(reconnect,&QPushButton::clicked,this,[this]{ perform("connect"); }); connect(diagnostics,&QPushButton::clicked,this,[this]{ dispatch("doctor"); });
    connect(start,&QPushButton::clicked,this,&Window::startSelected); connect(overviewStart,&QPushButton::clicked,this,&Window::startSelected);
    connect(end,&QPushButton::clicked,this,[this]{ perform("end"); });
    connect(sessionToggle,&QCheckBox::clicked,this,[this](bool checked){ if(checked) startSelected(); else perform("end"); });
    connect(extend15,&QPushButton::clicked,this,[this]{ perform("extend",{{"seconds",900}}); }); connect(extendHour,&QPushButton::clicked,this,[this]{ perform("extend",{{"seconds",3600}}); });
    connect(extension,&QComboBox::activated,this,[this](int index){ const int seconds=extension->itemData(index).toInt(); if(!seconds){ selectPage(1); return; } if(current.value("active").toBool()) perform("extend",{{"seconds",seconds}}); else { days->setValue(0); hours->setValue(seconds/3600); minutes->setValue(seconds/60%60); } });
    for(auto *spin : {cutoff,lowPower}) connect(spin,&QSpinBox::valueChanged,this,[this]{ localError.clear(); if(!current.isEmpty()) render(current); });
    connect(lid,&QCheckBox::toggled,this,[this]{ if(!current.isEmpty()) render(current); });
    if(!demo && QSystemTrayIcon::isSystemTrayAvailable()) {
        tray=new QSystemTrayIcon(eyeIcon(),this); auto *trayMenu=new QMenu(this); trayMenu->addAction("Open EverEye",this,&Window::Raise); trayMenu->addAction("Add 15 minutes",this,[this]{ perform("extend",{{"seconds",900}}); }); trayMenu->addAction("End session",this,[this]{ perform("end"); }); trayMenu->addSeparator(); trayMenu->addAction("End session and quit",this,[this]{ perform("shutdown"); }); tray->setContextMenu(trayMenu); tray->show();
        connect(tray,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason reason){
            if(reason==QSystemTrayIcon::Trigger || reason==QSystemTrayIcon::DoubleClick) toggleQuickPanel();
        });
    }
    auto *timer=new QTimer(this); timer->setInterval(1000); connect(timer,&QTimer::timeout,this,&Window::refresh); timer->start();
    selectPage(0); render({{"connected",false}}); refresh();
}
Window::~Window() { delete quickPanel; }
void Window::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#191d1e"));
    painter.drawRoundedRect(QRectF(rect()),12,12);
}
void Window::createQuickPanel() {
    if (quickPanel || demo) return;
    // Keep the tray panel as a real top-level tool window. Qt::Popup requires
    // an input-grabbing transient parent on Wayland, which a hidden tray app
    // does not have; a tool window keeps the same compact presentation without
    // triggering a compositor grab failure.
    quickPanel = new QuickPanelSurface;
    quickPanel->setObjectName("quickPanel");
    // Transparency is only for the pixels outside the rounded panel. The
    // styled background paints the content area as a fully opaque surface.
    quickPanel->setAttribute(Qt::WA_TranslucentBackground, true);
    quickPanel->setAttribute(Qt::WA_StyledBackground, true);
    auto panelPalette = quickPanel->palette();
    panelPalette.setColor(QPalette::Window, QColor("#171d22"));
    quickPanel->setPalette(panelPalette);
    quickPanel->setAutoFillBackground(false);
    quickPanel->setFixedSize(330, 530); quickPanel->setWindowTitle("EverEye"); quickPanel->setWindowIcon(eyeIcon(true));
    quickPanel->setStyleSheet(R"(
        QWidget#quickPanel { background: #171d22; border: 0; border-radius: 16px; color: #f4f6ef; }
        QLabel#quickBrand { color: #f4f6ef; font-size: 12px; font-weight: 700; }
        QLabel#quickMuted { color: #9ca9a5; font-size: 10px; }
        QLabel#quickStatus { color: #f4f6ef; font-size: 13px; font-weight: 700; }
        QLabel#quickCountdown { color: #f5f6ee; font-size: 34px; font-weight: 700; letter-spacing: -1px; }
        QLabel#quickDetail { color: #a4b0ab; font-size: 10px; }
        QFrame#quickSafeguards { background: #1e2729; border: 1px solid #344043; border-radius: 8px; }
        QLabel#quickSafetyLabel { color: #a4b2ae; font-size: 11px; background: transparent; }
        QLabel#quickSafetyValue { color: #e2e9e4; font-size: 11px; background: transparent; }
        QPushButton { background: #252d31; color: #e7ebe5; border: 1px solid #394347; border-radius: 8px; padding: 7px 8px; font-size: 10px; }
        QPushButton:hover { background: #334044; border-color: #66787a; }
        QPushButton:focus { border: 2px solid #86d7a4; padding: 6px 7px; }
        QPushButton#quickEnd { background: #8d5147; border-color: #b27164; color: #fff4ef; font-weight: 700; }
        QPushButton#quickEnd:disabled { background: #4b3936; color: #bba9a4; }
        QFrame#quickRule { background: #313b3d; border: 0; }
        QSpinBox { background: #252d31; color: #e7ebe5; border: 1px solid #4b595a; border-radius: 5px; padding: 4px; min-height: 24px; font-size: 14px; }
        QSpinBox:focus { border-color: #86d7a4; }
        QLabel#quickPointer { color: #86b9a7; font-size: 11px; }
    )");
    auto *root = new QVBoxLayout(quickPanel); root->setContentsMargins(14,12,14,13); root->setSpacing(9);
    auto *pointer = textLabel("▴", "quickPointer"); pointer->setAlignment(Qt::AlignHCenter); root->addWidget(pointer);
    auto *brand = new QHBoxLayout; brand->setSpacing(7); auto *mark = new QLabel; mark->setPixmap(eyeIcon(true).pixmap(18,18)); brand->addWidget(mark);
    brand->addWidget(textLabel("EverEye", "quickBrand")); brand->addStretch(); auto *settings = new QToolButton; settings->setText("⚙"); settings->setAccessibleName("Open EverEye settings"); settings->setStyleSheet("QToolButton{color:#8f9d98;font-size:13px;padding:1px 3px;border:0;} QToolButton:hover{color:#e8eee9;}"); brand->addWidget(settings); root->addLayout(brand);
    auto *ruleLine = new QFrame; ruleLine->setObjectName("quickRule"); ruleLine->setFixedHeight(1); root->addWidget(ruleLine);
    quickStatus = textLabel("Ready", "quickStatus"); root->addWidget(quickStatus);
    quickDetail = textLabel("Your system will sleep normally.", "quickDetail"); root->addWidget(quickDetail);
    quickCountdown = textLabel("00:00:00", "quickCountdown"); quickCountdown->setAlignment(Qt::AlignCenter); root->addWidget(quickCountdown);
    auto *remaining = textLabel("Remaining", "quickMuted"); remaining->setAlignment(Qt::AlignCenter); root->addWidget(remaining);
    auto *quick = new QGridLayout; quick->setHorizontalSpacing(6); quick->setVerticalSpacing(6);
    const QList<QPair<QString,int>> choices{{"15m",900},{"30m",1800},{"1h",3600},{"2h",7200}};
    for (int i=0;i<choices.size();++i) { auto *button = new QPushButton(choices[i].first); button->setAccessibleName("Use " + choices[i].first); quick->addWidget(button,i/4,i%4); connect(button,&QPushButton::clicked,this,[this,seconds=choices[i].second]{ startQuickDuration(seconds); }); }
    root->addLayout(quick);
    root->addWidget(textLabel("Custom duration", "quickMuted"));
    auto *customFields = new QHBoxLayout; customFields->setSpacing(8);
    const auto customField = [&](const QString &name, int maximum, int initial) {
        auto *column = new QVBoxLayout; column->setSpacing(4);
        auto *field = new NumberField; field->setObjectName("quick" + name); field->setRange(0,maximum); field->setValue(initial);
        field->setAccessibleName("Custom duration " + name.toLower()); field->setKeyboardTracking(false);
        column->addWidget(field); column->addWidget(textLabel(name,"quickMuted")); customFields->addLayout(column,1); return field;
    };
    quickDays = customField("Days",30,0); quickHours = customField("Hours",23,0); quickMinutes = customField("Minutes",59,30);
    root->addLayout(customFields);
    quickCustom = new QPushButton("Start custom session"); quickCustom->setObjectName("quickCustom"); root->addWidget(quickCustom);
    connect(quickCustom,&QPushButton::clicked,this,[this]{
        for (auto *field : {quickDays,quickHours,quickMinutes}) field->interpretText();
        startQuickDuration(qint64(quickDays->value())*86400 + quickHours->value()*3600 + quickMinutes->value()*60);
    });
    quickEnd = new QPushButton("End session"); quickEnd->setObjectName("quickEnd"); quickEnd->setAccessibleName("End session"); root->addWidget(quickEnd); connect(quickEnd,&QPushButton::clicked,this,[this]{ perform("end"); });
    root->addSpacing(3);
    auto *safety = new QFrame; safety->setObjectName("quickSafeguards");
    auto *safetyLayout = new QGridLayout(safety); safetyLayout->setContentsMargins(12,10,12,10); safetyLayout->setHorizontalSpacing(12); safetyLayout->setVerticalSpacing(8);
    safetyLayout->setColumnStretch(1,1);
    const auto safetyRow = [&](int row,const QString &name) {
        safetyLayout->addWidget(textLabel(name,"quickSafetyLabel"),row,0);
        auto *value=textLabel("—","quickSafetyValue",true); value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        safetyLayout->addWidget(value,row,1); return value;
    };
    quickSafety=safetyRow(0,"Battery"); quickLid=safetyRow(1,"Lid policy"); quickRecovery=safetyRow(2,"Crash recovery");
    root->addWidget(safety);
    connect(settings,&QToolButton::clicked,this,[this]{ if(quickPanel) quickPanel->hide(); Raise(); selectPage(3); });
}
void Window::toggleQuickPanel() {
    createQuickPanel(); if (!quickPanel) return;
    if (quickPanel->isVisible()) { quickPanel->hide(); return; }
    renderQuickPanel();
    const QRect anchor = tray ? tray->geometry() : QRect();
    const QPoint source = anchor.isValid() ? anchor.center() : QCursor::pos();
    auto *screen = QGuiApplication::screenAt(source); if (!screen) screen=QGuiApplication::primaryScreen();
    int x=source.x()-quickPanel->width()/2, y=source.y()+12;
    if (screen) {
        const auto bounds=screen->availableGeometry();
        // Place beneath a top bar (above a bottom bar), on the icon's monitor.
        y=anchor.isValid() ? (anchor.center().y()<bounds.center().y() ? anchor.bottom()+5 : anchor.top()-quickPanel->height()-5) : bounds.top()+5;
        x=qBound(bounds.left(),x,qMax(bounds.left(),bounds.right()-quickPanel->width()+1));
        y=qBound(bounds.top(),y,qMax(bounds.top(),bounds.bottom()-quickPanel->height()+1));
    }
    quickPanel->move(x,y); quickPanel->show(); quickPanel->raise(); quickPanel->activateWindow();
}
void Window::OpenQuickPanel() { toggleQuickPanel(); }
void Window::startQuickDuration(qint64 seconds) {
    if(seconds<MinSeconds || seconds>MaxSeconds) {
        localError="Choose a duration from 1 minute to 30 days."; render(current); return;
    }
    days->setValue(int(seconds/86400)); hours->setValue(int(seconds/3600%24)); minutes->setValue(int(seconds/60%60));
    startSelected();
}
void Window::renderQuickPanel() {
    if (!quickPanel) return;
    const bool active=current.value("active").toBool(); const bool connected=current.value("connected").toBool(true);
    quickStatus->setText(!connected ? "Service unavailable" : active ? "System is awake" : "Ready to protect");
    quickDetail->setText(!localError.isEmpty() ? localError : !current.value("error").toString().isEmpty() ? current.value("error").toString() : !connected ? "Reconnect to check your session." : active ? (lid->isChecked()?"Sleep and lid suspend are disabled.":"Automatic sleep is disabled.") : "Choose a duration to protect your work.");
    quickDetail->setWordWrap(true);
    quickCountdown->setText(connected ? formatDuration(active ? current.value("remainingSeconds").toInteger() : selectedSeconds()) : "--:--:--");
    quickEnd->setEnabled(connected && active && !operationInFlight);
    for (auto *button : quickPanel->findChildren<QPushButton*>()) {
        if (button != quickEnd) button->setEnabled(connected && !operationInFlight);
    }
    quickCustom->setText(active?"Add custom time":"Start custom session");
    for(auto *field : {quickDays,quickHours,quickMinutes}) field->setEnabled(connected && !operationInFlight);
    const bool batteryKnown=current.value("batteryKnown").toBool();
    QString battery="Unavailable";
    if(connected && batteryKnown) {
        if(current.contains("batteryPresent") && !current.value("batteryPresent").toBool()) battery="No battery";
        else {
            battery=QString("%1%").arg(current.value("batteryPercent").toDouble(),0,'f',0);
            if(current.value("acKnown").toBool()) battery+=current.value("onBattery").toBool()?" · On battery":" · Plugged in";
        }
    }
    quickSafety->setText(battery);
    quickSafety->setToolTip(battery=="Unavailable"?"Battery data is currently unavailable.":battery);
    quickLid->setText(lid->isChecked()?"Keep awake":"System default");
    quickLid->setToolTip("Configured lid policy during awake sessions.");
    quickRecovery->setText(current.value("recoveryBlocked").toBool()?"Needs attention":"Release on interruption");
}
void Window::selectPage(int index) {
    if(!pages || index<0 || index>=pages->count()) return;
    pages->setCurrentIndex(index); pagePicker->setCurrentIndex(index);
    for(int i=0;i<pages->count();++i) if(auto *button=findChild<QPushButton *>("nav"+QString::number(i))) button->setChecked(i==index);
    breadcrumb->setText(pagePicker->itemText(index));
    // Hidden pages must not impose their minimum height on the current page.
    for(int i=0;i<pages->count();++i) pages->widget(i)->setSizePolicy(i==index?QSizePolicy::Expanding:QSizePolicy::Ignored,i==index?QSizePolicy::Minimum:QSizePolicy::Ignored);
    pages->adjustSize(); scroll->verticalScrollBar()->setValue(0);
}
void Window::startSelected() {
    for(auto *spin : {days,hours,minutes,cutoff,lowPower}) spin->interpretText();
    const auto seconds=selectedSeconds();
    if(seconds<MinSeconds || seconds>MaxSeconds) { localError="Choose a duration from 1 minute to 30 days."; render(current); scroll->ensureWidgetVisible(error); return; }
    if(current.value("active").toBool()) perform("extend",{{"seconds",seconds}});
    else {
        const QJsonObject args{{"seconds",seconds},{"lid",lid->isChecked()},{"powerSaver",saver->isChecked()},{"endFloor",cutoff->value()},{"lowPowerFloor",lowPower->value()}};
        Options checked; QString validation; if(!Options::parse(args,checked,validation)){ localError=validation; render(current); scroll->ensureWidgetVisible(error); return; }
        perform("start",args);
    }
}
qint64 Window::selectedSeconds() const { return qint64(days->value()) * 86400 + hours->value() * 3600 + minutes->value() * 60; }
void Window::Raise() { showNormal(); raise(); activateWindow(); }
void Window::QuitWindow() { QApplication::quit(); }
void Window::closeEvent(QCloseEvent *event) { if (tray && tray->isVisible()) { hide(); event->ignore(); } else QWidget::closeEvent(event); }
void Window::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    const bool narrow=width()<850;
    if(sidebar) sidebar->setVisible(!narrow);
    if(badge) badge->setVisible(width()>=500);
    if(auto *layout=findChild<QHBoxLayout *>("topBar")) layout->setContentsMargins(narrow?18:36,26,narrow?18:28,12);
    if(auto *layout=findChild<QVBoxLayout *>("contentLayout")) layout->setContentsMargins(narrow?18:36,12,narrow?18:28,8);
    if(pagePicker) pagePicker->setVisible(narrow);
    if(breadcrumb) breadcrumb->setVisible(!narrow);
    if(cards) cards->setDirection(width()<690?QBoxLayout::TopToBottom:QBoxLayout::LeftToRight);
    if(protections) protections->setDirection(width()<690?QBoxLayout::TopToBottom:QBoxLayout::LeftToRight);
    if(countdown) countdown->setStyleSheet(width()<450?"font-size: 38px;":"font-size: 53px;");
    if(headline) headline->setStyleSheet(width()<450?"font-size: 22px;":"font-size: 26px;");
}
void Window::refresh() { dispatch("status"); }
void Window::perform(const QString &action, const QJsonObject &args) { localError.clear(); dispatch(action, args); }
void Window::dispatch(const QString &action, const QJsonObject &args) {
    if (inFlight) {
        if (action != "status" && pendingAction.isEmpty()) { pendingAction = action; pendingArgs = args; pendingIsOperation = true; updateButtons(); }
        return;
    }
    inFlight = true; operationInFlight = action != "status"; updateButtons();
    if (!backgroundRequests) { acceptReply(action, client(action, args)); return; }
    auto *watcher = new QFutureWatcher<QJsonObject>(this);
    connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, action] {
        const auto result = watcher->result(); watcher->deleteLater(); acceptReply(action, result);
    });
    watcher->setFuture(QtConcurrent::run([fn = client, action, args] { return fn(action, args); }));
}
void Window::acceptReply(const QString &action, const QJsonObject &reply) {
    inFlight = false; operationInFlight = false;
    if (action == "doctor") { updateButtons(); showDiagnostics(reply); }
    else if (action == "jobs.start") {
        if (reply.value("ok").toBool()) attachTerminal(reply.value("id").toString());
        else {
            const auto message = reply.value("error").toString();
            if (!message.isEmpty()) { localError = message; render(current); }
        }
        updateButtons();
    } else {
        render(reply);
        if (action == "start" && reply.value("ok").toBool()) selectPage(0);
        if (action == "shutdown" && reply.value("ok").toBool()) { QApplication::quit(); return; }
        if (action != "status" && !error->isHidden()) scroll->ensureWidgetVisible(error);
    }
    if (!pendingAction.isEmpty()) {
        const auto next = pendingAction; const auto args = pendingArgs;
        pendingAction.clear(); pendingArgs = {}; pendingIsOperation = false;
        dispatch(next, args);
    }
}
void Window::attachTerminal(const QString &id) {
    if (id.isEmpty()) return;
#ifdef EVEREYE_WEBENGINE
    if (!terminalView) {
        terminalView = new TerminalView(terminalHost);
        if (auto *existing = terminalHost->layout()) existing->addWidget(terminalView);
        else { auto *layout = new QVBoxLayout(terminalHost); layout->addWidget(terminalView); }
    }
    terminalView->attach(id);
    terminalView->setFocus();
#else
    localError = "This build has no Qt WebEngine, so the embedded terminal is unavailable.";
    render(current);
#endif
}
void Window::showDiagnostics(const QJsonObject &reply) {
    QDialog dialog(this); dialog.setWindowTitle("EverEye diagnostics"); dialog.resize(560, 440);
    auto *layout = new QVBoxLayout(&dialog); auto *text = new QTextEdit; text->setReadOnly(true); text->setPlainText(QString::fromUtf8(QJsonDocument(reply).toJson())); layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); layout->addWidget(buttons); dialog.exec();
}
void Window::updateSelection() {
    if (!selectionLabel || !countdown) return;
    const auto selected = selectedSeconds();
    selectionLabel->setText(current.value("active").toBool() ? "Add " + formatDuration(selected) + " to this session" : "Your session will run for " + formatDuration(selected));
    if (!current.value("active").toBool() && current.value("connected").toBool(true)) countdown->setText(formatDuration(selected));
    if (!current.value("active").toBool()) {
        const QSignalBlocker block(extension); const int preset = extension->findData(int(selected));
        extension->setCurrentIndex(preset >= 0 ? preset : extension->count()-1);
    }
    if (!localError.isEmpty()) return;
    if (!current.isEmpty()) { const auto serverError = current.value("error").toString(); if (serverError.isEmpty() && current.value("warnings").toArray().isEmpty()) error->hide(); }
}
void Window::updateButtons() {
    if (!start) return;
    const bool connected = current.value("connected").toBool(true); const bool active = current.value("active").toBool();
    const bool working = operationInFlight || pendingIsOperation;
    start->setEnabled(connected && !working && !current.value("recoveryBlocked").toBool());
    end->setEnabled(connected && active && !working); extend15->setEnabled(connected && active && !working); extendHour->setEnabled(connected && active && !working);
    lid->setEnabled(connected && !active && !working); saver->setEnabled(connected && !active && !working); cutoff->setEnabled(connected && !active && !working); lowPower->setEnabled(connected && !active && !working);
    for (auto *spin : {days, hours, minutes}) spin->setEnabled(!working);
    start->setText(working ? "Working…" : active ? "Add selected time" : "Start session");
    overviewStart->setEnabled(start->isEnabled()); overviewStart->setVisible(!active); end->setVisible(active);
    sessionToggle->setEnabled(connected && !working && !current.value("recoveryBlocked").toBool());
    sessionToggle->setChecked(active); extension->setEnabled(connected && !working);
    overviewStart->setText(working ? "Working…" : "Start session");
}
void Window::render(const QJsonObject &result) {
    const bool wasActive = current.value("active").toBool(); current = result;
    const bool active = result.value("active").toBool(); const bool connected = result.value("connected").toBool(true);
    badge->setText(!connected ? "●  Offline" : active ? "●  Protection active" : "●  Ready");
    badge->setToolTip(demo ? "Preview only. No system power settings are changed." : "Session protection status");
    headline->setText(!connected ? "Service unavailable" : active ? "Session active" : "Keep your system awake");
    subtitle->setText(!connected ? "Reconnect to check your current session." : active ? "Automatic sleep is blocked until this session ends." : "Set a duration to prevent automatic sleep.");
    serviceStatus->setText(demo ? "● Offline preview" : connected ? "● Session service running" : "● Service unavailable");
    auto *face = findChild<QFrame *>("timerFace");
    const auto remaining=result.value("remainingSeconds").toInteger();
    const auto total=qMax(remaining,face->property("total").toLongLong());
    face->setProperty("total",active?total:0); face->setProperty("active",active);
    face->setProperty("progress",total>0?double(remaining)/total:0.0); face->update();
    endsAt->setText(active ? "●  Automatically ends at " + QDateTime::currentDateTime().addSecs(remaining).toString("h:mm AP") : "●  Choose a session length");
    const bool previousOffline = badge->property("offline").toBool(); badge->setProperty("offline", !connected);
    if (previousOffline != !connected) { badge->style()->unpolish(badge); badge->style()->polish(badge); }
    countdown->setText(connected ? formatDuration(active ? result.value("remainingSeconds").toInteger() : selectedSeconds()) : "--:--:--");
    QString reason = result.value("reason").toString();
    if (reason == "Ready" || reason == "Ended by user" || reason.isEmpty()) reason = "Choose a duration to get started.";
    if (active && reason == "Session running") reason = "Your computer is staying awake.";
    if (reason == "Timer expired") reason = "Your timer finished. Ready for another session?";
    detail->setText(connected ? reason : "Connection unavailable. Reconnect to check your session.");
    durationLabel->setText(active ? "EXTEND SESSION" : "SESSION LENGTH");
    extension->setToolTip(active ? "Choose an amount to add immediately" : "Choose your session duration");
    if(active != wasActive) { const QSignalBlocker block(extension); extension->setCurrentIndex(active ? 1 : 4); }
    for(int i=0;i<5;++i) {
        const int n=extension->itemData(i).toInt()/60;
        extension->setItemText(i,(active?"+ ":"")+(n<60?QString("%1 minutes").arg(n):QString("%1 hour%2").arg(n/60).arg(n==60?"":"s")));
    }
    if ((!initialized || active || wasActive) && result.value("options").isObject()) {
        const auto options = result.value("options").toObject();
        const QSignalBlocker blockCutoff(cutoff), blockPower(lowPower), blockLid(lid), blockSaver(saver);
        lid->setChecked(options.value("lid").toBool(true)); saver->setChecked(options.value("powerSaver").toBool(true));
        cutoff->setValue(options.value("endFloor").toInt(10)); lowPower->setValue(options.value("lowPowerFloor").toInt(40)); initialized = true;
    }
    QString message = localError.isEmpty() ? result.value("error").toString() : localError;
    for (const auto &warning : result.value("warnings").toArray()) message += (message.isEmpty() ? "" : "\n") + warning.toString();
    error->setText(message); error->setVisible(!message.isEmpty());
    const double percent = result.value("batteryPercent").toDouble(-1);
    QString power = result.value("batteryKnown").toBool() ? (result.value("batteryPresent").toBool() ? (percent >= 0 ? QString("Battery %1%").arg(percent, 0, 'f', 0) : "Battery unreadable") : "No battery") : "Power status unavailable";
    if (result.value("acKnown").toBool()) power += result.value("onBattery").toBool() ? "  ·  On battery" : "  ·  Plugged in";
    if (result.value("powerSaverHeld").toBool()) power += "  ·  Power saver";
    batterySummary->setText(cutoff->value()>0?QString("Ends below %1%").arg(cutoff->value()):"Cutoff disabled");
    lidSummary->setText(lid->isChecked()?"Keep tasks alive":"Follow system policy");
    recoverySummary->setText(result.value("recoveryBlocked").toBool()?"Needs attention":"Release on interruption");
    sleepSummary->setText(active?"Prevent suspend":"Ready to protect");
    powerLabel->setText(power); reconnect->setVisible(!connected); updateButtons(); updateSelection();
    if (tray) { tray->setIcon(eyeIcon(active)); tray->setToolTip(active ? "EverEye · " + countdown->text() + " remaining" : "EverEye · Ready"); }
    renderQuickPanel();
}
}
