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
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QConicalGradient>
#include <QProgressBar>
#include <QSettings>
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
#include <QTabBar>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
        p.setPen(Qt::NoPen); p.setBrush(QColor(isChecked()?"#9fb8ad":"#4a4e53")); p.drawRoundedRect(track,track.height()/2,track.height()/2);
        const qreal diameter=large?28:14; const qreal inset=large?5:3;
        p.setBrush(QColor("#fcfbff")); p.drawEllipse(QRectF(isChecked()?track.right()-diameter-inset:track.left()+inset,track.top()+inset,diameter,diameter));
        if(hasFocus()){ p.setBrush(Qt::NoBrush); p.setPen(QPen(QColor("#9fb8ad"),1.5)); p.drawRoundedRect(rect().adjusted(1,1,-1,-1),height()/2,height()/2); }
    }
};
class TimerFace final : public QFrame {
protected:
    void paintEvent(QPaintEvent *event) override {
        QFrame::paintEvent(event);
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const QRectF area = rect().adjusted(14, 14, -14, -14);
        const qreal side = qMin(area.width(), area.height());
        if (side <= 0) return;
        const QRectF ring(area.center().x() - side / 2, area.center().y() - side / 2, side, side);
        const qreal stroke = qMax(9.0, side * 0.055);
        const QRectF track = ring.adjusted(stroke / 2, stroke / 2, -stroke / 2, -stroke / 2);
        const bool active = property("active").toBool();
        const qreal progress = active ? qBound(0.0, property("progress").toDouble(), 1.0) : 0.86;
        QConicalGradient gradient(ring.center(), 90);
        gradient.setColorAt(0.0, QColor("#f2f3f5"));
        gradient.setColorAt(0.45, QColor("#9fb8ad"));
        gradient.setColorAt(0.75, QColor("#e6e7e9"));
        gradient.setColorAt(1.0, QColor("#f2f3f5"));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor("#1c1f22"), stroke, Qt::SolidLine, Qt::RoundCap));
        p.drawEllipse(track);
        p.setPen(QPen(QBrush(gradient), stroke + 10, Qt::SolidLine, Qt::RoundCap));
        p.setOpacity(0.10);
        p.drawArc(track, 90 * 16, -int(progress * 360 * 16));
        p.setOpacity(1.0);
        p.setPen(QPen(QBrush(gradient), stroke, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(track, 90 * 16, -int(progress * 360 * 16));
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
        painter.setBrush(QColor("#17191c"));
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
        p.setPen(QPen(QColor("#9fb8ad"),1.5)); QPainterPath arrow;
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
namespace {
QPixmap glyph(const QString &kind, const QColor &color, int size = 18) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 18.0, size / 18.0);
    QPen pen(color, 1.7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    if (kind == "home") {
        QPainterPath roof; roof.moveTo(2.5, 9); roof.lineTo(9, 3); roof.lineTo(15.5, 9);
        p.drawPath(roof); p.drawRect(QRectF(4.5, 9, 9, 6.5));
    } else if (kind == "clock") {
        p.drawEllipse(QRectF(3, 3, 12, 12)); p.drawLine(QPointF(9, 5.5), QPointF(9, 9)); p.drawLine(QPointF(9, 9), QPointF(12, 10.5));
    } else if (kind == "lid") {
        p.drawRoundedRect(QRectF(3, 4, 12, 8.5), 1.5, 1.5); p.drawLine(QPointF(2.5, 13), QPointF(15.5, 13));
    } else if (kind == "gear") {
        p.drawEllipse(QRectF(6.3, 6.3, 5.4, 5.4));
        for (int i = 0; i < 8; ++i) {
            const double angle = i * M_PI / 4;
            p.drawLine(QPointF(9 + std::cos(angle) * 6.4, 9 + std::sin(angle) * 6.4),
                       QPointF(9 + std::cos(angle) * 7.8, 9 + std::sin(angle) * 7.8));
        }
    } else if (kind == "terminal") {
        p.drawRoundedRect(QRectF(2.5, 3.5, 13, 11), 2, 2);
        p.drawLine(QPointF(5, 6.5), QPointF(7.5, 9)); p.drawLine(QPointF(7.5, 9), QPointF(5, 11.5));
        p.drawLine(QPointF(9.5, 11.5), QPointF(12.5, 11.5));
    } else if (kind == "spark") {
        QPainterPath star;
        star.moveTo(9, 2); star.lineTo(10.7, 7.3); star.lineTo(16, 9); star.lineTo(10.7, 10.7);
        star.lineTo(9, 16); star.lineTo(7.3, 10.7); star.lineTo(2, 9); star.lineTo(7.3, 7.3);
        star.closeSubpath(); p.drawPath(star);
    } else if (kind == "info") {
        p.drawEllipse(QRectF(3, 3, 12, 12)); p.drawLine(QPointF(9, 8), QPointF(9, 12.5));
        p.setBrush(color); p.drawEllipse(QPointF(9, 5.8), 0.7, 0.7);
    } else if (kind == "battery") {
        p.drawRoundedRect(QRectF(2, 6, 11.5, 6.5), 1, 1); p.drawLine(QPointF(14.5, 8), QPointF(14.5, 10.5));
        p.drawLine(QPointF(4, 9.2), QPointF(9, 9.2));
    } else if (kind == "temp") {
        QPainterPath tube; tube.moveTo(7.5, 3.5); tube.lineTo(7.5, 10.5);
        tube.arcTo(QRectF(5.5, 10, 5, 5), 180, 180); tube.lineTo(10.5, 3.5);
        tube.arcTo(QRectF(7.5, 2, 3, 3), 0, 180); p.drawPath(tube);
        p.setBrush(color); p.drawEllipse(QPointF(9, 12.7), 1.4, 1.4);
    } else if (kind == "shield") {
        QPainterPath shield; shield.moveTo(9, 2.2); shield.lineTo(15, 4.8); shield.lineTo(15, 8.5);
        shield.cubicTo(15, 12.2, 12.4, 14.8, 9, 16); shield.cubicTo(5.6, 14.8, 3, 12.2, 3, 8.5);
        shield.lineTo(3, 4.8); shield.closeSubpath(); p.drawPath(shield);
    } else if (kind == "pause") {
        p.drawRoundedRect(QRectF(5, 4, 2.6, 10), 1, 1); p.drawRoundedRect(QRectF(10.4, 4, 2.6, 10), 1, 1);
    } else if (kind == "stop") {
        p.setPen(Qt::NoPen); p.setBrush(color); p.drawRoundedRect(QRectF(5, 5, 8, 8), 1.5, 1.5);
    } else if (kind == "power") {
        p.drawArc(QRectF(3, 3.5, 12, 12), 120 * 16, 300 * 16); p.drawLine(QPointF(9, 2), QPointF(9, 8.5));
    }
    return pixmap;
}
QIcon navIcon(const QString &kind) {
    QIcon icon;
    icon.addPixmap(glyph(kind, QColor("#8a8f94")), QIcon::Normal, QIcon::Off);
    icon.addPixmap(glyph(kind, QColor("#f2f3f5")), QIcon::Normal, QIcon::On);
    icon.addPixmap(glyph(kind, QColor("#f2f3f5")), QIcon::Active, QIcon::On);
    return icon;
}
QWidget *iconBox(const QString &kind) {
    auto *box = new QFrame;
    box->setObjectName("iconBox");
    box->setFixedSize(30, 30);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *label = new QLabel;
    label->setPixmap(glyph(kind, QColor("#e6e7e9"), 17));
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);
    return box;
}
QString temperatureText() {
    double hottest = -1;
    QDir thermal("/sys/class/thermal");
    for (const auto &zone : thermal.entryList({"thermal_zone*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile file("/sys/class/thermal/" + zone + "/temp");
        if (!file.open(QIODevice::ReadOnly)) continue;
        bool ok = false;
        const double milli = file.readAll().trimmed().toDouble(&ok);
        if (!ok) continue;
        const double celsius = milli / 1000.0;
        if (celsius > 0 && celsius < 150) hottest = qMax(hottest, celsius);
    }
    if (hottest < 0) {
        QDir hwmon("/sys/class/hwmon");
        for (const auto &dir : hwmon.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            for (int index = 1; index <= 6; ++index) {
                QFile file("/sys/class/hwmon/" + dir + QString("/temp%1_input").arg(index));
                if (!file.open(QIODevice::ReadOnly)) continue;
                bool ok = false;
                const double milli = file.readAll().trimmed().toDouble(&ok);
                if (!ok) continue;
                const double celsius = milli / 1000.0;
                if (celsius > 0 && celsius < 150) hottest = qMax(hottest, celsius);
            }
        }
    }
    return hottest > 0 ? QString("%1°C").arg(hottest, 0, 'f', 0) : QString("—");
}
}
QIcon eyeIcon(bool active) {
    QPixmap image(128,128); image.fill(Qt::transparent); QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen); p.setBrush(QColor(active?"#5c6066":"#495351")); p.drawRoundedRect(QRectF(4,4,120,120),26,26);
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
        QWidget { color: #ecedef; font-family: "Inter", "Noto Sans", sans-serif; font-size: 13px; }
        QWidget#evereyeWindow { background: transparent; }
        QFrame#shell { background: #0e0f10; border: 1px solid #222529; border-radius: 6px; }
        QFrame#sidebar { background: #101113; border: 0; border-right: 1px solid #222529; border-top-left-radius: 14px; border-bottom-left-radius: 14px; }
        QFrame#dashboardCard, QFrame#timerFace, QFrame#protectionCard, QFrame#navStatus { background: #15171a; border: 1px solid #25282c; border-radius: 6px; }
        QFrame#iconBox { background: #1a1d20; border: 1px solid #33373c; border-radius: 6px; }
        QLabel, QStackedWidget, QWidget#scrollCanvas, QWidget#page { background: transparent; border: 0; }
        QLabel#appName { font-size: 19px; font-weight: 600; }
        QLabel#linux { color: #7a7f85; font-size: 9px; font-weight: 500; }
        QLabel#headline { font-size: 26px; font-weight: 650; letter-spacing: -0.6px; }
        QLabel#sectionTitle { font-size: 20px; font-weight: 600; }
        QLabel#eyebrow { color: #8a8f94; font-size: 10px; font-weight: 500; letter-spacing: 1.4px; }
        QLabel#muted, QLabel#detail, QLabel#selectionLabel { color: #9a9ea3; font-size: 13px; }
        QLabel#small { color: #84898e; font-size: 11px; }
        QLabel#ruleTitle, QLabel#protectionValue { font-size: 14px; font-weight: 600; }
        QLabel#countdown { font-family: "Inter", "Noto Sans", sans-serif; font-size: 46px; font-weight: 500; letter-spacing: 1px; }
        QLabel#endsAt { color: #9fb8ad; font-size: 12px; }
        QLabel#badge { color: #d6d9dd; background: #1a1d20; border: 1px solid #33373c; border-radius: 6px; padding: 5px 10px; font-size: 11px; }
        QLabel#badge[offline="true"] { color: #ffd6ad; border-color: #7a5c45; }
        QLabel#error { background: #2a1c17; color: #ffd8c2; border: 1px solid #6f4634; border-radius: 6px; padding: 12px; }
        QLabel#sessionTimer { font-family: "Inter", "Noto Sans", sans-serif; font-size: 30px; font-weight: 500; letter-spacing: 0.5px; }
        QLabel#statusPill { color: #f2f3f5; background: #1a1d20; border: 1px solid #33373c; border-radius: 6px; padding: 3px 8px; font-size: 10px; font-weight: 600; }
        QSpinBox { background: #131518; color: #ecedef; border: 1px solid #2a2d31; border-radius: 6px; padding: 8px 6px; min-height: 22px; font-size: 16px; selection-background-color: #44484f; }
        QSpinBox:focus { border: 1px solid #9fb8ad; }
        QSpinBox:disabled { color: #6b7075; border-color: #202326; }
        QSpinBox QLineEdit { color: #ecedef; background: transparent; border: 0; padding: 0; font-size: 16px; }
        QPushButton { background: #16181b; color: #dfe1e4; border: 1px solid #2a2d31; border-radius: 6px; padding: 10px 14px; font-weight: 600; }
        QPushButton:hover { background: #1c1f22; border-color: #3c4046; }
        QPushButton:focus { border: 2px solid #9fb8ad; padding: 9px 13px; }
        QPushButton:disabled { color: #6b7075; background: #131518; border-color: #202326; }
        QPushButton#primary, QPushButton#overviewStart, QPushButton#newTerminal { background: #e6e7e9; color: #101112; border: 1px solid #e6e7e9; }
        QPushButton#primary:hover, QPushButton#overviewStart:hover, QPushButton#newTerminal:hover { background: #f2f3f5; }
        QPushButton#end { background: #16181b; color: #dfe1e4; border: 1px solid #33373c; }
        QPushButton#end:hover { background: #1c1f22; }
        QPushButton#primary:disabled, QPushButton#end:disabled, QPushButton#overviewStart:disabled, QPushButton#newTerminal:disabled { background: #131518; color: #62676c; border-color: #202326; }
        QPushButton[nav="true"] { background: transparent; border: 0; color: #9a9ea3; text-align: left; font-weight: 500; padding: 10px 12px; border-radius: 6px; }
        QPushButton[nav="true"]:checked { background: #1c1f22; color: #e6e8eb; border: 0; font-weight: 600; }
        QPushButton[nav="true"]:hover { background: #191c1f; }
        QPushButton[quiet="true"] { color: #b9cfc6; background: transparent; border-color: transparent; padding: 7px; font-size: 12px; }
        QComboBox { color: #ecedef; background: #131518; border: 1px solid #2a2d31; border-radius: 6px; font-size: 13px; padding: 8px 10px; min-height: 22px; }
        QComboBox::drop-down { border: 0; width: 22px; }
        QComboBox::down-arrow { image: none; }
        QComboBox QAbstractItemView { background: #17191c; color: #ecedef; selection-background-color: #33373c; padding: 6px; font-size: 13px; border: 1px solid #2a2d31; }
        QToolButton { background: transparent; color: #9a9ea3; border: 0; padding: 4px 8px; font-size: 17px; }
        QToolButton::menu-indicator { image: none; }
        QTabBar::tab { background: #16181b; color: #9a9ea3; border: 1px solid #2a2d31; border-radius: 6px; padding: 7px 12px; margin-right: 6px; }
        QTabBar::tab:selected { background: #1c1f22; color: #e6e8eb; border-color: #383c42; }
        QTabBar::tab:hover { background: #1a1d20; }
        QMenu { background: #17191c; border: 1px solid #2a2d31; padding: 6px; }
        QMenu::item { padding: 9px 14px; } QMenu::item:selected { background: #33373c; }
        QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: 0; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 3px 1px; }
        QScrollBar::handle:vertical { background: #383c42; min-height: 38px; border-radius: 4px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        QTextEdit, QPlainTextEdit { background: #111315; color: #dfe1e4; border: 1px solid #2a2d31; padding: 12px; font-family: "DejaVu Sans Mono", monospace; border-radius: 6px; }
        QListWidget { background: #111315; color: #dfe1e4; border: 1px solid #2a2d31; border-radius: 6px; }
        QListWidget::item { padding: 4px; }
        QListWidget::item:selected { background: #1c1f22; color: #e6e8eb; }
        QLineEdit { background: #131518; color: #ecedef; border: 1px solid #2a2d31; border-radius: 6px; padding: 9px; }
        QProgressBar { background: #1a1d20; border: 0; border-radius: 3px; }
        QProgressBar::chunk { background: #e6e7e9; border-radius: 3px; }
    )");
    auto *outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0);
    auto *shell = card("shell"); outer->addWidget(shell); auto *split = new QHBoxLayout(shell); split->setContentsMargins(0,0,0,0); split->setSpacing(0);
    sidebar = card("sidebar"); sidebar->setFixedWidth(205);
    auto *side = new QVBoxLayout(sidebar); side->setContentsMargins(14,22,14,18); side->setSpacing(5);
    auto *brand = new QHBoxLayout; brand->setSpacing(10);
    auto *mark = new QLabel; mark->setPixmap(eyeIcon(true).pixmap(32,32)); brand->addWidget(mark);
    brand->addWidget(textLabel("EverEye", "appName"));
    auto *platformTag = textLabel("LINUX", "linux"); brand->addWidget(platformTag, 0, Qt::AlignTop); brand->addStretch(); side->addLayout(brand); side->addSpacing(18);
    const QStringList names{"Overview", "Sessions", "Lid Actions", "Preferences", "Terminal", "AI Sessions", "About"};
    const QStringList kinds{"home", "clock", "lid", "gear", "terminal", "spark", "info"};
    for (int i=0; i<names.size(); ++i) {
        auto *button = new QPushButton(names[i]); button->setObjectName("nav" + QString::number(i)); button->setProperty("nav",true); button->setCheckable(true); button->setCursor(Qt::PointingHandCursor);
        button->setIcon(navIcon(kinds.value(i))); button->setIconSize(QSize(17,17));
        button->setAccessibleName(names[i]); side->addWidget(button);
        connect(button,&QPushButton::clicked,this,[this,i]{ selectPage(i); });
    }
    side->addStretch();
    auto *statusCard = card("navStatus");
    auto *statusLayout = new QVBoxLayout(statusCard); statusLayout->setContentsMargins(12,11,12,11); statusLayout->setSpacing(5);
    auto *serviceRow = new QHBoxLayout; serviceRow->setSpacing(7);
    serviceRow->addWidget(textLabel("●", "endsAt"));
    serviceStatus = textLabel("Connecting…", "muted", true); serviceRow->addWidget(serviceStatus, 1); statusLayout->addLayout(serviceRow);
    powerLabel = textLabel("Reading power status…", "small", true); statusLayout->addWidget(powerLabel);
    const auto session = qEnvironmentVariable("XDG_SESSION_TYPE");
    statusLayout->addWidget(textLabel((session.isEmpty() ? "Desktop" : session.left(1).toUpper()+session.mid(1)) + " · Linux", "small"));
    side->addWidget(statusCard); side->addSpacing(8); side->addWidget(textLabel("EVEREYE 0.4.0", "small")); split->addWidget(sidebar);
    auto *main = new QVBoxLayout; main->setContentsMargins(0,0,0,0); main->setSpacing(0); split->addLayout(main,1);
    auto *header = new QHBoxLayout; header->setObjectName("topBar"); header->setContentsMargins(36,26,28,12);
    breadcrumb = textLabel("Workspace / Overview", "muted"); header->addWidget(breadcrumb); header->addStretch();
    pagePicker = new Choice; pagePicker->setObjectName("pagePicker"); pagePicker->addItems(names); pagePicker->setAccessibleName("Navigation"); pagePicker->setFixedWidth(125); pagePicker->setStyleSheet("font-size: 13px;"); header->addWidget(pagePicker); pagePicker->hide();
    connect(pagePicker,&QComboBox::activated,this,&Window::selectPage);
    badge = textLabel("Connecting", "badge"); header->addWidget(badge); badge->hide();
    auto *gear = new QToolButton; gear->setText("⚙"); gear->setAccessibleName("Preferences"); gear->setCursor(Qt::PointingHandCursor);
    connect(gear, &QToolButton::clicked, this, [this] { selectPage(3); }); header->addWidget(gear);
    auto *more = new QToolButton; more->setText("⋯"); more->setAccessibleName("Window menu"); more->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(more); menu->addAction("Hide window",this,&QWidget::hide); menu->addAction("Close window · keep session running",this,&Window::QuitWindow);
    menu->addSeparator(); menu->addAction("End session and quit",this,[this]{ perform("shutdown"); }); more->setMenu(menu); header->addWidget(more); main->addLayout(header);
    scroll = new QScrollArea; scroll->setObjectName("contentScroll"); scroll->setWidgetResizable(true); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setFocusPolicy(Qt::StrongFocus); scroll->setFrameShape(QFrame::NoFrame); main->addWidget(scroll,1);
    auto *canvas = new QWidget; canvas->setObjectName("scrollCanvas");
    auto *canvasRow = new QHBoxLayout(canvas); canvasRow->setContentsMargins(0,0,0,0); canvasRow->setSpacing(0);
    canvasRow->addStretch(1);
    auto *column = new QWidget; column->setObjectName("scrollColumn"); column->setMaximumWidth(1040);
    auto *content = new QVBoxLayout(column); content->setObjectName("contentLayout"); content->setContentsMargins(36,12,28,8); content->setSpacing(14);
    canvasRow->addWidget(column, 8); canvasRow->addStretch(1);
    pages = new PageStack; pages->setObjectName("pages"); content->addWidget(pages);
    const auto page = [&] { auto *p = new PageWidget; p->setObjectName("page"); auto *layout = new QVBoxLayout(p); layout->setContentsMargins(0,0,0,0); layout->setSpacing(18); pages->addWidget(p); return layout; };
    auto *overview = page(); overview->setSpacing(14);
    auto *intro = new QHBoxLayout; intro->addStretch();
    intro->addWidget(textLabel("Keep awake", "small"), 0, Qt::AlignVCenter);
    sessionToggle = new Switch(true); sessionToggle->setObjectName("sessionToggle"); sessionToggle->setAccessibleName("Keep the system awake"); intro->addWidget(sessionToggle, 0, Qt::AlignVCenter);
    overview->addLayout(intro);
    auto *face = new TimerFace; face->setObjectName("timerFace"); face->setMinimumHeight(300); face->setMaximumHeight(340);
    auto *timerLayout = new QVBoxLayout(face); timerLayout->setContentsMargins(60,18,60,20); timerLayout->setSpacing(6);
    timerLayout->addStretch();
    auto *orb = new QLabel; orb->setPixmap(eyeIcon(true).pixmap(50,50)); orb->setAlignment(Qt::AlignHCenter); timerLayout->addWidget(orb);
    countdown = textLabel("04:00:00","countdown"); countdown->setAlignment(Qt::AlignHCenter); countdown->setAccessibleName("Time remaining"); timerLayout->addWidget(countdown);
    auto *remaining = textLabel("Remaining","small"); remaining->setAlignment(Qt::AlignHCenter); timerLayout->addWidget(remaining);
    endsAt = textLabel("Choose a session length","endsAt",true); endsAt->setAlignment(Qt::AlignHCenter); timerLayout->addWidget(endsAt); endsAt->hide();
    timerLayout->addStretch();
    overview->addWidget(face);
    auto *primaryRow = new QHBoxLayout; primaryRow->addStretch();
    overviewStart = new QPushButton("Start Session"); overviewStart->setObjectName("overviewStart"); overviewStart->setMinimumWidth(176);
    end = new QPushButton("End Session"); end->setObjectName("end"); end->setMinimumWidth(176); end->setIcon(QIcon(glyph("stop", QColor("#dfe1e4"), 14)));
    primaryRow->addWidget(overviewStart); primaryRow->addWidget(end); primaryRow->addStretch(); overview->addLayout(primaryRow);
    auto *protect = new QWidget; protections = new QBoxLayout(QBoxLayout::LeftToRight,protect); protections->setContentsMargins(0,0,0,0); protections->setSpacing(12);
    const auto protectCard = [&](const QString &kind, const QString &title, QLabel *&value) {
        auto *panel = card("protectionCard");
        auto *layout = new QHBoxLayout(panel); layout->setContentsMargins(14,12,14,12); layout->setSpacing(12);
        layout->addWidget(iconBox(kind), 0, Qt::AlignTop);
        auto *column = new QVBoxLayout; column->setSpacing(4);
        column->addWidget(textLabel(title,"muted")); value = textLabel("", "protectionValue", true); column->addWidget(value);
        layout->addLayout(column, 1); protections->addWidget(panel, 1); panel->setAccessibleName(title);
    };
    protectCard("battery","Battery",batterySummary); protectCard("temp","Temperature",temperatureSummary); protectCard("shield","System Protected",sleepSummary);
    overview->addWidget(protect);
    overview->addStretch();

    auto *sessions = page();
    auto *sessionsScroll=new QScrollArea; sessionsScroll->setObjectName("sessionsScroll"); sessionsScroll->setWidgetResizable(true); sessionsScroll->setFrameShape(QFrame::NoFrame); sessionsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); sessionsScroll->setFixedHeight(500);
    auto *sessionsCanvas=new QWidget; auto *sessionsLayout=new QVBoxLayout(sessionsCanvas); sessionsLayout->setContentsMargins(0,0,0,0); sessionsLayout->setSpacing(14);
    sessionsLayout->addWidget(textLabel("Sessions","sectionTitle")); sessionsLayout->addWidget(textLabel("Choose a duration or add time to your current session.","muted",true));
    auto *currentCard = card("dashboardCard"); auto *currentLayout = new QVBoxLayout(currentCard); currentLayout->setContentsMargins(20,16,20,16); currentLayout->setSpacing(8);
    auto *currentHead = new QHBoxLayout; currentHead->addWidget(textLabel("Current session","ruleTitle")); currentHead->addStretch();
    sessionStatus = textLabel("Idle","statusPill"); currentHead->addWidget(sessionStatus); currentLayout->addLayout(currentHead);
    sessionStarted = textLabel("No active session","small"); currentLayout->addWidget(sessionStarted);
    sessionTimer = textLabel("00:00:00","sessionTimer"); currentLayout->addWidget(sessionTimer);
    sessionProgress = new QProgressBar; sessionProgress->setRange(0,1000); sessionProgress->setValue(0); sessionProgress->setTextVisible(false); sessionProgress->setFixedHeight(6); currentLayout->addWidget(sessionProgress);
    auto *currentRow = new QHBoxLayout; currentRow->addStretch();
    currentEnd = new QPushButton("End session"); currentEnd->setObjectName("currentEnd"); currentRow->addWidget(currentEnd); currentLayout->addLayout(currentRow);
    connect(currentEnd,&QPushButton::clicked,this,[this]{ perform("end"); });
    sessionsLayout->addWidget(currentCard);
    auto *sessionCard = card("dashboardCard"); auto *sessionLayout = new QVBoxLayout(sessionCard); sessionLayout->setContentsMargins(20,16,20,16); sessionLayout->setSpacing(12);
    durationLabel = textLabel("SESSION LENGTH","muted"); sessionLayout->addWidget(durationLabel);
    auto *presetRow = new QHBoxLayout; presetRow->setSpacing(8);
    for (int n : {15,30,60,120}) {
        auto *button = new QPushButton(n<60?QString("%1m").arg(n):QString("%1h").arg(n/60));
        presetRow->addWidget(button, 1);
        connect(button,&QPushButton::clicked,this,[this,n]{ days->setValue(0); hours->setValue(n/60); minutes->setValue(n%60); });
    }
    auto *customButton = new QPushButton("Custom"); presetRow->addWidget(customButton, 1);
    connect(customButton,&QPushButton::clicked,this,[this]{ days->setFocus(); });
    sessionLayout->addLayout(presetRow);
    start=new QPushButton("Start session"); start->setObjectName("primary"); sessionLayout->addWidget(start);
    extension = new Choice; extension->setObjectName("durationPreset"); extension->setAccessibleName("Session duration or extension");
    for (int n : {15,30,60,120,240}) extension->addItem(n<60 ? QString("%1 minutes").arg(n) : QString("%1 hour%2").arg(n/60).arg(n==60?"":"s"), n*60);
    extension->addItem("Custom duration…",0); extension->setCurrentIndex(4); sessionLayout->addWidget(extension);
    auto *duration = new QHBoxLayout; duration->setSpacing(10);
    const auto field = [&](const QString &name,int maximum,int initial) {
        auto *column = new QVBoxLayout; auto *spin = new NumberField; spin->setObjectName(name.toLower()); spin->setRange(0,maximum); spin->setValue(initial); spin->setAccessibleName(name); column->addWidget(spin); column->addWidget(textLabel(name,"muted")); duration->addLayout(column,1);
        connect(spin,&QSpinBox::valueChanged,this,[this]{ localError.clear(); updateSelection(); }); return spin;
    };
    days=field("Days",30,0); hours=field("Hours",23,4); minutes=field("Minutes",59,0); sessionLayout->addLayout(duration);
    selectionLabel = textLabel("","selectionLabel",true); sessionLayout->addWidget(selectionLabel);
    auto *quick = new QHBoxLayout; extend15=new QPushButton("+15 minutes"); extendHour=new QPushButton("+1 hour"); quick->addWidget(extend15); quick->addWidget(extendHour); sessionLayout->addLayout(quick); sessionsLayout->addWidget(sessionCard);
    detail=textLabel("","detail",true); sessionsLayout->addWidget(detail); sessionsLayout->addStretch();
    sessionsScroll->setWidget(sessionsCanvas); sessions->addWidget(sessionsScroll);

    auto *lidPage=page(); lidPage->addWidget(textLabel("Lid actions","sectionTitle")); lidPage->addWidget(textLabel("Choose how your next session handles a closed laptop lid.","muted",true));
    auto *lidCard=card("dashboardCard"); auto *lidLayout=new QVBoxLayout(lidCard); lidLayout->setContentsMargins(20,16,20,16); lidLayout->setSpacing(16);
    auto *lidRow = new QHBoxLayout; lidRow->setSpacing(12); lidRow->addWidget(iconBox("lid"));
    auto *lidWords = new QVBoxLayout; lidWords->setSpacing(3); lidWords->addWidget(textLabel("Keep awake with lid closed","ruleTitle")); lidWords->addWidget(textLabel("Let your tasks continue when you close your laptop.","muted",true));
    lidRow->addLayout(lidWords,1);
    lid=new Switch; lid->setObjectName("lidSwitch"); lid->setChecked(true); lidRow->addWidget(lid); lidLayout->addLayout(lidRow);
    auto *policyRow = new QHBoxLayout; policyRow->addWidget(textLabel("What happens when I close the lid?","muted"),1);
    lidPolicy = new QComboBox; lidPolicy->setObjectName("lidPolicy"); lidPolicy->addItems({"Keep session running","Use system policy"}); policyRow->addWidget(lidPolicy); lidLayout->addLayout(policyRow);
    connect(lidPolicy,&QComboBox::currentIndexChanged,this,[this](int index){ if (lid && lid->isChecked() != (index==0)) { const QSignalBlocker block(lid); lid->setChecked(index==0); if(!current.isEmpty()) render(current); } });
    lidSummary = textLabel("","small",true); lidLayout->addWidget(lidSummary);
    auto *advanced=new QPushButton("Advanced options"); advanced->setObjectName("advancedOptions"); connect(advanced,&QPushButton::clicked,this,[this]{ selectPage(3); }); lidLayout->addWidget(advanced);
    lidLayout->addWidget(textLabel("Changes apply to the next session. Your screen can still lock. Lid behavior depends on your desktop and hardware.","muted",true)); lidPage->addWidget(lidCard); lidPage->addStretch();
    auto *preferences=page(); preferences->addWidget(textLabel("Preferences","sectionTitle")); preferences->addWidget(textLabel("Configure safeguards before starting a session.","muted",true));
    auto *settings=card("dashboardCard"); auto *settingsLayout=new QVBoxLayout(settings); settingsLayout->setContentsMargins(20,16,20,16); settingsLayout->setSpacing(18);
    auto *saverRow = new QHBoxLayout; saverRow->setSpacing(12); saverRow->addWidget(iconBox("power"));
    saver=new Switch; saver->setObjectName("saverSwitch"); saver->setChecked(true);
    saverRow->addWidget(rule("Use power saver","Request power saver on low battery or with the lid closed.",saver),1); settingsLayout->addLayout(saverRow);
    auto *thresholds=new QGridLayout; thresholds->setHorizontalSpacing(16); thresholds->setVerticalSpacing(10);
    cutoff=new NumberField; cutoff->setObjectName("cutoff"); cutoff->setRange(0,99); cutoff->setValue(10); cutoff->setSuffix("%"); cutoff->setAccessibleName("Battery cutoff percentage; zero disables protection");
    lowPower=new NumberField; lowPower->setObjectName("lowPower"); lowPower->setRange(1,100); lowPower->setValue(40); lowPower->setSuffix("%"); lowPower->setAccessibleName("Power saver battery threshold");
    thresholds->addWidget(textLabel("End session below","muted",true),0,0); thresholds->addWidget(textLabel("Power saver below","muted",true),0,1); thresholds->addWidget(cutoff,1,0); thresholds->addWidget(lowPower,1,1); thresholds->setColumnStretch(0,1); thresholds->setColumnStretch(1,1); settingsLayout->addLayout(thresholds);
    settingsLayout->addWidget(textLabel("Set battery cutoff to 0% to disable it. Crash recovery releases interrupted sessions; it never silently resumes them.","muted",true));
    auto *diagnostics=new QPushButton("Open diagnostics"); diagnostics->setObjectName("diagnostics"); settingsLayout->addWidget(diagnostics);
    preferences->addWidget(settings);
    auto *tip=card("protectionCard"); auto *tipLayout=new QHBoxLayout(tip); tipLayout->setContentsMargins(14,12,14,12); tipLayout->setSpacing(12); tipLayout->addWidget(iconBox("info"),0,Qt::AlignTop);
    auto *tipWords=new QVBoxLayout; tipWords->setSpacing(3); tipWords->addWidget(textLabel("Tip","ruleTitle")); tipWords->addWidget(textLabel("Keep your battery above 10% for uninterrupted work.","muted",true)); recoverySummary = textLabel("Release on interruption","small",true); tipWords->addWidget(recoverySummary);
    tipLayout->addLayout(tipWords,1); preferences->addWidget(tip);
    preferences->addWidget(textLabel("Closing the window keeps a running session alive. End session and quit is available in the window menu.","muted",true)); preferences->addStretch();
    auto *terminalPage=page();
    terminalPage->addWidget(textLabel("Terminal","sectionTitle"));
    terminalPage->addWidget(textLabel("Open up to four terminals. They keep running when you close this window.","muted",true));
    auto *terminalBar=new QHBoxLayout; terminalBar->setSpacing(10);
    newTerminal=new QPushButton("New terminal"); newTerminal->setObjectName("newTerminal");
    terminalCount=textLabel("0 of 4 terminals","muted");
    terminalBar->addWidget(newTerminal); terminalBar->addWidget(terminalCount,1);
    terminalPage->addLayout(terminalBar);
    terminalTabs=new QTabBar; terminalTabs->setObjectName("terminalTabs"); terminalTabs->setExpanding(false); terminalTabs->setTabsClosable(true); terminalTabs->setDrawBase(false); terminalTabs->hide();
    terminalPage->addWidget(terminalTabs);
    terminalStack=new QStackedWidget; terminalStack->setObjectName("terminalStack");
    terminalPage->addWidget(terminalStack,1);
    connect(newTerminal,&QPushButton::clicked,this,[this]{ if(terminalViews.size()>=4) return; perform("jobs.start", {{"shell", true}}); });
    connect(terminalTabs,&QTabBar::currentChanged,this,[this](int index){ if(index>=0 && index<terminalStack->count()) terminalStack->setCurrentIndex(index); });
    connect(terminalTabs,&QTabBar::tabCloseRequested,this,&Window::removeTerminal);

    auto *aiPage=page();
    // Two panes: a list of terminal agents on the left, the selected one on the right.
    auto *aiSplit=new QHBoxLayout; aiSplit->setSpacing(18);
    auto *aiSide=new QVBoxLayout; aiSide->setSpacing(10);
    aiSide->addWidget(textLabel("Terminal agents","sectionTitle"));
    aiSide->addWidget(textLabel("CLI agents such as opencode and freebuff running on this PC.","muted",true));
    jobList=new QListWidget; jobList->setObjectName("jobList"); jobList->setMinimumWidth(320);
    auto *startRow=new QHBoxLayout; startRow->setSpacing(8);
    agentPreset=new QComboBox; agentPreset->setObjectName("agentPreset"); agentPreset->setAccessibleName("Agent preset");
    auto *startAgent=new QPushButton("Start agent"); startAgent->setObjectName("startAgent");
    startRow->addWidget(agentPreset,1); startRow->addWidget(startAgent);
    aiSide->addLayout(startRow);
    connect(startAgent,&QPushButton::clicked,this,[this]{ if(agentPreset->currentText().isEmpty()) return; perform("jobs.start", {{"preset", agentPreset->currentText()}}); });
    dispatch("tasks.list");
    aiSide->addWidget(jobList,1);
    jobHint=textLabel("Scanning for agents…","small",true); aiSide->addWidget(jobHint);
    agentKeepSwitch=new Switch; agentKeepSwitch->setObjectName("agentKeepSwitch"); agentKeepSwitch->setChecked(QSettings("evereye","window").value("keepAgentsAlive", true).toBool());
    auto *keepRow=new QHBoxLayout; keepRow->addWidget(textLabel("Keep agents alive with the lid closed","small",true),1); keepRow->addWidget(agentKeepSwitch);
    aiSide->addLayout(keepRow);
    connect(agentKeepSwitch,&QCheckBox::toggled,this,[this](bool on){ QSettings("evereye","window").setValue("keepAgentsAlive", on); });
    jobTerminalHost=new QWidget; jobTerminalHost->setObjectName("jobTerminalHost");
    auto *jobTerminalLayout=new QVBoxLayout(jobTerminalHost); jobTerminalLayout->setContentsMargins(0,0,0,0);
    agentPane=new QWidget; agentPane->setObjectName("agentPane");
    auto *agentLayout=new QVBoxLayout(agentPane); agentLayout->setContentsMargins(0,0,0,0); agentLayout->setSpacing(8);
    agentLog=new QPlainTextEdit; agentLog->setObjectName("agentLog"); agentLog->setReadOnly(true); agentLog->setMaximumBlockCount(2000); agentLog->setAccessibleName("AI session output");
    agentLog->setPlaceholderText("Live output is available for sessions running inside tmux. Other processes still accept a task through their terminal.");
    auto *agentRow=new QHBoxLayout; agentRow->setSpacing(8);
    agentTask=new QLineEdit; agentTask->setObjectName("agentTask"); agentTask->setPlaceholderText("Type a task and press Enter");
    agentSend=new QPushButton("Send task"); agentSend->setObjectName("agentSend");
    agentRow->addWidget(agentTask,1); agentRow->addWidget(agentSend);
    agentLayout->addWidget(agentLog,1); agentLayout->addLayout(agentRow);
    aiDetail=new QStackedWidget; aiDetail->setObjectName("aiDetail"); aiDetail->setMinimumHeight(260);
    aiDetail->addWidget(jobTerminalHost); aiDetail->addWidget(agentPane);
    aiSplit->addLayout(aiSide);
    aiSplit->addWidget(aiDetail,1);
    aiPage->addLayout(aiSplit,1);
    connect(jobList,&QListWidget::currentRowChanged,this,[this](int row){
        if(row<0 || !jobList->currentItem()) return;
        const auto item=jobList->currentItem();
        if(item->data(Qt::UserRole).toString()=="job") {
            const auto id=item->data(Qt::UserRole+1).toString();
            const auto status=item->data(Qt::UserRole+2).toString();
            selectedKind="job"; selectedAgentPid.clear(); selectedJob=id;
            if(aiDetail) aiDetail->setCurrentWidget(jobTerminalHost);
            attachAiJob(id,status);
        } else {
            selectedKind="agent"; selectedJob.clear();
            selectedAgentPid=item->data(Qt::UserRole+1).toString();
            selectedAgentPane=item->data(Qt::UserRole+2).toString();
            selectedAgentTty=item->data(Qt::UserRole+3).toString();
            if(aiDetail) aiDetail->setCurrentWidget(agentPane);
            if(agentLog) agentLog->clear();
            const auto command=item->data(Qt::UserRole+4).toString();
            const auto cwd=item->data(Qt::UserRole+5).toString();
            if(!selectedAgentPane.isEmpty()) dispatch("agents.capture", {{"pid", selectedAgentPid.toLongLong()}});
            else if(agentLog) agentLog->setPlainText(QString("No live output is available for this process.\n\n%1\n%2\n\nRun agents inside tmux, or start them with EverEye for a full terminal.").arg(command, cwd));
            if(agentTask) agentTask->setFocus();
        }
    });
    connect(agentSend,&QPushButton::clicked,this,&Window::sendAgentTask);
    connect(agentTask,&QLineEdit::returnPressed,this,&Window::sendAgentTask);

    auto *aboutPage=page();
    auto *aboutCard=card("dashboardCard"); auto *aboutLayout=new QVBoxLayout(aboutCard); aboutLayout->setContentsMargins(30,24,30,24); aboutLayout->setSpacing(10);
    auto *logo=new QLabel; logo->setPixmap(eyeIcon(true).pixmap(72,72)); logo->setAlignment(Qt::AlignHCenter); aboutLayout->addWidget(logo);
    auto *appName=textLabel("EverEye","headline"); appName->setAlignment(Qt::AlignHCenter); aboutLayout->addWidget(appName);
    auto *tagline=textLabel("Keep your system awake. Keep working.","muted",true); tagline->setAlignment(Qt::AlignHCenter); aboutLayout->addWidget(tagline);
    aboutLayout->addSpacing(8);
    auto *aboutGrid=new QGridLayout; aboutGrid->setHorizontalSpacing(18); aboutGrid->setVerticalSpacing(8);
    int aboutRow=0;
    const auto aboutLine=[&](const QString &key, QWidget *value){ aboutGrid->addWidget(textLabel(key,"muted"),aboutRow,0); aboutGrid->addWidget(value,aboutRow,1); ++aboutRow; };
    aboutLine("Version", textLabel("0.4.0","small"));
    aboutLine("Author", textLabel("Prof-bilal","small"));
    aboutLine("License", textLabel("MIT","small"));
    auto *link=new QLabel("<a href=\"https://github.com/Prof-bilal/evereye-linux\" style=\"color:#f2f3f5;text-decoration:none;\">github.com/Prof-bilal/evereye-linux</a>");
    link->setTextFormat(Qt::RichText); link->setOpenExternalLinks(true); link->setObjectName("small");
    aboutLine("GitHub", link);
    aboutLayout->addLayout(aboutGrid);
    aboutPage->addWidget(aboutCard); aboutPage->addStretch();

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
    auto *agentTimer=new QTimer(this); agentTimer->setInterval(2000); connect(agentTimer,&QTimer::timeout,this,[this]{ dispatch("agents.list"); }); agentTimer->start();
    selectPage(0); render({{"connected",false}}); refresh(); dispatch("agents.list");
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
    panelPalette.setColor(QPalette::Window, QColor("#17191c"));
    quickPanel->setPalette(panelPalette);
    quickPanel->setAutoFillBackground(false);
    quickPanel->setFixedSize(330, 530); quickPanel->setWindowTitle("EverEye"); quickPanel->setWindowIcon(eyeIcon(true));
    quickPanel->setStyleSheet(R"(
        QWidget#quickPanel { background: #17191c; border: 0; border-radius: 6px; color: #f4f6ef; }
        QLabel#quickBrand { color: #f4f6ef; font-size: 12px; font-weight: 700; }
        QLabel#quickMuted { color: #9ca9a5; font-size: 10px; }
        QLabel#quickStatus { color: #f4f6ef; font-size: 13px; font-weight: 700; }
        QLabel#quickCountdown { color: #f5f6ee; font-size: 34px; font-weight: 700; letter-spacing: -1px; }
        QLabel#quickDetail { color: #a4b0ab; font-size: 10px; }
        QFrame#quickSafeguards { background: #1e2729; border: 1px solid #344043; border-radius: 6px; }
        QLabel#quickSafetyLabel { color: #b0b4b8; font-size: 11px; background: transparent; }
        QLabel#quickSafetyValue { color: #e2e9e4; font-size: 11px; background: transparent; }
        QPushButton { background: #252d31; color: #e7ebe5; border: 1px solid #394347; border-radius: 6px; padding: 7px 8px; font-size: 10px; }
        QPushButton:hover { background: #334044; border-color: #66787a; }
        QPushButton:focus { border: 2px solid #9fb8ad; padding: 6px 7px; }
        QPushButton#quickEnd { background: #8d5147; border-color: #b27164; color: #fff4ef; font-weight: 700; }
        QPushButton#quickEnd:disabled { background: #4b3936; color: #bba9a4; }
        QFrame#quickRule { background: #313b3d; border: 0; }
        QSpinBox { background: #252d31; color: #e7ebe5; border: 1px solid #4b595a; border-radius: 5px; padding: 4px; min-height: 24px; font-size: 14px; }
        QSpinBox:focus { border-color: #9fb8ad; }
        QLabel#quickPointer { color: #9fb8ad; font-size: 11px; }
    )");
    auto *root = new QVBoxLayout(quickPanel); root->setContentsMargins(14,12,14,13); root->setSpacing(9);
    auto *pointer = textLabel("▴", "quickPointer"); pointer->setAlignment(Qt::AlignHCenter); root->addWidget(pointer);
    auto *brand = new QHBoxLayout; brand->setSpacing(7); auto *mark = new QLabel; mark->setPixmap(eyeIcon(true).pixmap(18,18)); brand->addWidget(mark);
    brand->addWidget(textLabel("EverEye", "quickBrand")); brand->addStretch(); auto *settings = new QToolButton; settings->setText("⚙"); settings->setAccessibleName("Open EverEye settings"); settings->setStyleSheet("QToolButton{color:#9a9ea3;font-size:13px;padding:1px 3px;border:0;} QToolButton:hover{color:#ecedef;}"); brand->addWidget(settings); root->addLayout(brand);
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
    if(breadcrumb) breadcrumb->setVisible(false); // the page title already names the page
    if(cards) cards->setDirection(width()<690?QBoxLayout::TopToBottom:QBoxLayout::LeftToRight);
    if(protections) protections->setDirection(width()<690?QBoxLayout::TopToBottom:QBoxLayout::LeftToRight);
    if(countdown) countdown->setStyleSheet(width()<450?"font-size: 38px;":"font-size: 53px;");
}
namespace {
bool pollAction(const QString &action) {
    return action == "status" || action == "jobs.list" || action == "agents.list" || action == "agents.capture";
}
}
void Window::refresh() {
    dispatch("status");
    if (jobList) dispatch("jobs.list");
    if (selectedKind == "agent" && !selectedAgentPid.isEmpty() && !selectedAgentPane.isEmpty())
        dispatch("agents.capture", {{"pid", selectedAgentPid.toLongLong()}});
}
void Window::perform(const QString &action, const QJsonObject &args) { localError.clear(); dispatch(action, args); }
void Window::dispatch(const QString &action, const QJsonObject &args) {
    const bool ambient = pollAction(action);
    if (inFlight) {
        if (!ambient) { pendingAction = action; pendingArgs = args; pendingIsOperation = true; }
        else if (pendingAction.isEmpty()) { pendingAction = action; pendingArgs = args; pendingIsOperation = false; }
        updateButtons();
        return;
    }
    inFlight = true; operationInFlight = !ambient; updateButtons();
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
        if (reply.value("ok").toBool()) addTerminal(reply.value("id").toString());
        else {
            const auto message = reply.value("error").toString();
            if (!message.isEmpty()) { localError = message; render(current); }
        }
        updateButtons();
    } else if (action == "tasks.list") {
        if (agentPreset) {
            agentPreset->clear();
            for (const auto &value : reply.value("tasks").toArray()) agentPreset->addItem(value.toObject().value("name").toString());
        }
    } else if (action == "jobs.stop") {
        updateButtons();
    } else if (action == "jobs.list") {
        jobsCache = reply.value("jobs").toArray();
        rebuildAiList();
        updateButtons();
    } else if (action == "agents.list") {
        agentsCache = reply.value("agents").toArray();
        rebuildAiList();
        if (!demo && agentKeepSwitch && agentKeepSwitch->isChecked() && !agentsCache.isEmpty()
            && current.value("connected").toBool(true) && !current.value("active").toBool() && !agentKeepStarted) {
            agentKeepStarted = true;
            perform("start", {{"seconds", 8 * 3600}, {"lid", lid->isChecked()}, {"powerSaver", saver->isChecked()},
                              {"endFloor", cutoff->value()}, {"lowPowerFloor", lowPower->value()}});
        }
        if (agentsCache.isEmpty()) agentKeepStarted = false;
        updateButtons();
    } else if (action == "agents.capture") {
        if (reply.value("ok").toBool() && selectedKind == "agent" && agentLog
            && reply.value("pid").toString() == selectedAgentPid) {
            agentLog->setPlainText(reply.value("text").toString());
            if (auto *bar = agentLog->verticalScrollBar()) bar->setValue(bar->maximum());
        }
    } else if (action == "agents.send") {
        if (!reply.value("ok").toBool()) {
            const auto message = reply.value("error").toString();
            if (!message.isEmpty()) { localError = message; render(current); }
        } else if (jobHint) {
            jobHint->setText("Task sent to the selected AI process.");
        }
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
void Window::addTerminal(const QString &jobId) {
    if (jobId.isEmpty()) return;
#ifdef EVEREYE_WEBENGINE
    if (!terminalStack || terminalViews.size() >= 4) return;
    auto *view = new TerminalView(terminalStack);
    terminalViews.append(view);
    terminalJobs.append(jobId);
    const int index = terminalStack->addWidget(view);
    terminalTabs->addTab(QString("Terminal %1").arg(terminalViews.size()));
    terminalTabs->setCurrentIndex(index);
    terminalTabs->setVisible(true);
    terminalStack->setCurrentIndex(index);
    view->attach(jobId);
    view->setFocus();
    updateButtons();
#else
    localError = "This build has no Qt WebEngine, so the embedded terminal is unavailable.";
    render(current);
#endif
}

void Window::removeTerminal(int index) {
    if (index < 0 || index >= terminalViews.size()) return;
#ifdef EVEREYE_WEBENGINE
    const auto jobId = terminalJobs.value(index);
    if (!jobId.isEmpty()) perform("jobs.stop", {{"id", jobId}});
    auto *view = terminalViews.takeAt(index);
    terminalJobs.removeAt(index);
    terminalStack->removeWidget(view);
    view->deleteLater();
    terminalTabs->removeTab(index);
    terminalTabs->setVisible(!terminalViews.isEmpty());
    if (!terminalViews.isEmpty()) terminalStack->setCurrentIndex(qMin(index, terminalViews.size() - 1));
    updateButtons();
#else
    Q_UNUSED(index);
#endif
}

void Window::attachAiJob(const QString &jobId, const QString &status) {
    if (jobId.isEmpty()) return;
    if (status != "Running" && status != "Starting") {
        if (jobHint) jobHint->setText("That session has ended. Pick a running session.");
        return;
    }
#ifdef EVEREYE_WEBENGINE
    if (!jobTerminal) {
        jobTerminal = new TerminalView(jobTerminalHost);
        if (auto *existing = jobTerminalHost->layout()) existing->addWidget(jobTerminal);
        else { auto *layout = new QVBoxLayout(jobTerminalHost); layout->addWidget(jobTerminal); }
    }
    jobTerminal->attach(jobId);
    jobTerminal->setFocus();
    if (jobHint) jobHint->setText("Attached to " + jobId + ". Type in the terminal to give it a task.");
#else
    localError = "This build has no Qt WebEngine, so the embedded terminal is unavailable.";
    render(current);
#endif
}

void Window::rebuildAiList() {
    if (!jobList) return;
    QString signature;
    for (const auto &value : agentsCache) {
        const auto agent = value.toObject();
        signature += "a" + agent.value("pid").toString() + ':' + agent.value("name").toString() + ';';
    }
    for (const auto &value : jobsCache) {
        const auto job = value.toObject();
        signature += "j" + job.value("id").toString() + ':' + job.value("status").toString() + ';';
    }
    int runningJobs = 0;
    for (const auto &value : jobsCache) {
        const auto status = value.toObject().value("status").toString();
        if (status == "Running" || status == "Starting") ++runningJobs;
    }
    if (jobHint) {
        jobHint->setText(QString("%1 AI process(es) · %2 EverEye job(s) running. Select one to watch it and type a task.")
            .arg(int(agentsCache.size())).arg(runningJobs));
    }
    if (signature == jobSignature) return;
    jobSignature = signature;
    const QSignalBlocker block(jobList);
    jobList->clear();
    jobList->setSpacing(4);
    int restore = -1;
    const auto makeRow = [&](const QString &kind, const QString &title, const QString &detail,
                             const QString &status, bool live, const QString &copyText, bool isAgent) {
        auto *row = new QWidget;
        row->setObjectName("aiRow");
        row->setAttribute(Qt::WA_TranslucentBackground);
        row->setStyleSheet("background: transparent;");
        auto *layout = new QHBoxLayout(row); layout->setContentsMargins(10,6,10,6); layout->setSpacing(10);
        layout->addWidget(iconBox(kind));
        auto *words = new QVBoxLayout; words->setSpacing(2);
        auto *titleLabel = textLabel(title, "ruleTitle"); titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents); words->addWidget(titleLabel);
        auto *detailLabel = textLabel(detail, "small"); detailLabel->setAttribute(Qt::WA_TransparentForMouseEvents); detailLabel->setToolTip(detail); words->addWidget(detailLabel);
        layout->addLayout(words, 1);
        auto *pill = textLabel(status, live ? "statusPill" : "small"); pill->setAttribute(Qt::WA_TransparentForMouseEvents); pill->setVisible(!status.isEmpty()); layout->addWidget(pill);
        auto *more = new QToolButton; more->setText("⋯"); more->setAccessibleName("Session actions"); more->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(more);
        menu->addAction("Copy " + QString(isAgent ? "command" : "session id"), [copyText] { QApplication::clipboard()->setText(copyText); });
        if (isAgent) menu->addAction("Focus task", [this] { if (agentTask) agentTask->setFocus(); });
        more->setMenu(menu); layout->addWidget(more);
        return row;
    };
    for (const auto &value : agentsCache) {
        const auto agent = value.toObject();
        const auto pid = QString::number(agent.value("pid").toInteger());
        auto *item = new QListWidgetItem; item->setSizeHint(QSize(0, 54));
        item->setData(Qt::UserRole, "agent");
        item->setData(Qt::UserRole + 1, pid);
        item->setData(Qt::UserRole + 2, agent.value("pane").toString());
        item->setData(Qt::UserRole + 3, agent.value("tty").toString());
        item->setData(Qt::UserRole + 4, agent.value("command").toString());
        item->setData(Qt::UserRole + 5, agent.value("cwd").toString());
        item->setToolTip(agent.value("command").toString());
        jobList->addItem(item);
        jobList->setItemWidget(item, makeRow("spark", agent.value("name").toString(),
                                            "pid " + pid + "  ·  " + agent.value("cwd").toString(), "", true, agent.value("command").toString(), true));
        if (selectedKind == "agent" && pid == selectedAgentPid) restore = jobList->count() - 1;
    }
    for (const auto &value : jobsCache) {
        const auto job = value.toObject();
        const auto status = job.value("status").toString();
        const bool live = status == "Running" || status == "Starting";
        auto *item = new QListWidgetItem; item->setSizeHint(QSize(0, 54));
        item->setData(Qt::UserRole, "job");
        item->setData(Qt::UserRole + 1, job.value("id").toString());
        item->setData(Qt::UserRole + 2, status);
        jobList->addItem(item);
        jobList->setItemWidget(item, makeRow("terminal", job.value("name").toString(),
                                            "EverEye session  ·  " + job.value("id").toString(), live ? "Running" : status, live, job.value("id").toString(), false));
        if (selectedKind == "job" && job.value("id").toString() == selectedJob) restore = jobList->count() - 1;
    }
    if (restore >= 0) jobList->setCurrentRow(restore);
}

void Window::sendAgentTask() {
    if (selectedKind != "agent" || selectedAgentPid.isEmpty() || !agentTask) return;
    const auto text = agentTask->text().trimmed();
    if (text.isEmpty()) return;
    agentTask->clear();
    perform("agents.send", {{"pid", selectedAgentPid.toLongLong()}, {"pane", selectedAgentPane}, {"tty", selectedAgentTty}, {"text", text}});
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
    if (newTerminal) {
        newTerminal->setEnabled(terminalViews.size() < 4 && !working);
        terminalCount->setText(QString("%1 of 4 terminals").arg(terminalViews.size()));
    }
    if (currentEnd) currentEnd->setEnabled(connected && active && !working);
}
void Window::render(const QJsonObject &result) {
    const bool wasActive = current.value("active").toBool(); current = result;
    const bool active = result.value("active").toBool(); const bool connected = result.value("connected").toBool(true);
    badge->setText(!connected ? "●  Offline" : active ? "●  Protection active" : "●  Ready");
    badge->setToolTip(demo ? "Preview only. No system power settings are changed." : "Session protection status");
    serviceStatus->setText(demo ? "Offline preview" : connected ? "Service running" : "Service unavailable");
    auto *face = findChild<QFrame *>("timerFace");
    const auto remaining=result.value("remainingSeconds").toInteger();
    const auto total=qMax(remaining,face->property("total").toLongLong());
    face->setProperty("total",active?total:0); face->setProperty("active",active);
    face->setProperty("progress",total>0?double(remaining)/total:0.0); face->update();
    endsAt->setText(active ? "Auto-ends at " + QDateTime::currentDateTime().addSecs(remaining).toString("h:mm AP") : "Choose a session length");
    const bool previousOffline = badge->property("offline").toBool(); badge->setProperty("offline", !connected);
    if (previousOffline != !connected) { badge->style()->unpolish(badge); badge->style()->polish(badge); }
    countdown->setText(connected ? formatDuration(active ? result.value("remainingSeconds").toInteger() : selectedSeconds()) : "--:--:--");
    if (sessionStatus) sessionStatus->setText(active ? "Active" : "Idle");
    if (sessionTimer) sessionTimer->setText(connected ? (active ? formatDuration(remaining) : "00:00:00") : "--:--:--");
    if (sessionProgress) sessionProgress->setValue(active && total > 0 ? int((double(remaining) / double(total)) * 1000.0) : 0);
    if (sessionStarted) {
        const auto startedOffset = qMax<qint64>(0, total - remaining);
        sessionStarted->setText(active ? "Started Today, " + QDateTime::currentDateTime().addSecs(-startedOffset).toString("HH:mm") : "No active session");
    }
    if (temperatureSummary) temperatureSummary->setText(temperatureText());
    if (lidPolicy) { const QSignalBlocker block(lidPolicy); lidPolicy->setCurrentIndex(lid->isChecked() ? 0 : 1); }
    if (currentEnd) currentEnd->setEnabled(connected && active && !operationInFlight && !pendingIsOperation);
    QString reason = result.value("reason").toString();
    if (reason == "Ready" || reason == "Ended by user" || reason.isEmpty()) reason = "Choose a duration to get started.";
    if (active && reason == "Session running") reason = "Your computer is staying awake.";
    if (reason == "Timer expired") reason = "Your timer finished. Ready for another session?";
    detail->setText(connected ? reason : "Connection unavailable. Reconnect to check your session.");
    durationLabel->setText(active ? "Extend session" : "Session length");
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
    const double batteryPercent = result.value("batteryPercent").toDouble(-1);
    batterySummary->setText(batteryPercent >= 0 ? QString("%1%").arg(batteryPercent, 0, 'f', 0) : "Unavailable");
    lidSummary->setText(lid->isChecked()?"Keep tasks alive":"Follow system policy");
    recoverySummary->setText(result.value("recoveryBlocked").toBool()?"Needs attention":"Release on interruption");
    sleepSummary->setText(active?"Protected":"Ready");
    powerLabel->setText(power); reconnect->setVisible(!connected); updateButtons(); updateSelection();
    if (tray) { tray->setIcon(eyeIcon(active)); tray->setToolTip(active ? "EverEye · " + countdown->text() + " remaining" : "EverEye · Ready"); }
    renderQuickPanel();
}
}
