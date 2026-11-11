#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QIcon>
#include <functional>

class QLabel; class QPushButton; class QSpinBox; class QCheckBox; class QSystemTrayIcon;
class QScrollArea; class QBoxLayout; class QResizeEvent;
class QStackedWidget; class QComboBox;
class QDialog; class QToolButton; class QPlainTextEdit; class QListWidget; class QLineEdit;
namespace insomnia {
QIcon eyeIcon(bool active = false);
class Window final : public QWidget {
    Q_OBJECT
public:
    using Client = std::function<QJsonObject(const QString &, const QJsonObject &)>;
    explicit Window(Client client, bool demo = false, bool backgroundRequests = false);
    ~Window() override;
    void refresh();
public slots:
    Q_SCRIPTABLE void Raise();
    Q_SCRIPTABLE void QuitWindow();
    void OpenQuickPanel();
protected:
    void paintEvent(QPaintEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void resizeEvent(QResizeEvent *) override;
private:
    void perform(const QString &, const QJsonObject &args = {});
    void dispatch(const QString &, const QJsonObject &args = {});
    void acceptReply(const QString &, const QJsonObject &);
    void render(const QJsonObject &);
    void updateButtons();
    void updateSelection();
    void showDiagnostics(const QJsonObject &);
    void selectPage(int);
    void toggleQuickPanel();
    void createQuickPanel();
    void renderQuickPanel();
    void startQuickDuration(qint64 seconds);
    void startSelected();
    qint64 selectedSeconds() const;
    Client client;
    bool demo;
    bool initialized = false;
    bool backgroundRequests;
    bool inFlight = false;
    bool operationInFlight = false;
    QString pendingAction;
    QJsonObject pendingArgs;
    QString localError;
    QJsonObject current;
    QLabel *countdown = nullptr; QLabel *badge = nullptr; QLabel *detail = nullptr; QLabel *error = nullptr; QLabel *powerLabel = nullptr;
    QSpinBox *days; QSpinBox *hours; QSpinBox *minutes; QSpinBox *cutoff; QSpinBox *lowPower;
    QCheckBox *lid; QCheckBox *saver;
    QPushButton *start = nullptr; QPushButton *end = nullptr; QPushButton *extend15 = nullptr; QPushButton *extendHour = nullptr;
    QPushButton *reconnect;
    QLabel *selectionLabel = nullptr; QLabel *durationLabel = nullptr;
    QScrollArea *scroll = nullptr;
    QBoxLayout *cards = nullptr;
    QBoxLayout *protections = nullptr;
    QWidget *sidebar = nullptr;
    QStackedWidget *pages = nullptr;
    QComboBox *pagePicker = nullptr; QComboBox *extension = nullptr;
    QLabel *breadcrumb = nullptr; QLabel *headline = nullptr; QLabel *subtitle = nullptr;
    QLabel *endsAt = nullptr; QLabel *batterySummary = nullptr; QLabel *lidSummary = nullptr; QLabel *recoverySummary = nullptr;
    QLabel *serviceStatus = nullptr; QLabel *sleepSummary = nullptr;
    QCheckBox *sessionToggle = nullptr;
    QPushButton *overviewStart = nullptr;
    QSystemTrayIcon *tray = nullptr;
    QWidget *quickPanel = nullptr;
    QLabel *quickStatus = nullptr; QLabel *quickCountdown = nullptr; QLabel *quickDetail = nullptr;
    QLabel *quickSafety = nullptr; QPushButton *quickEnd = nullptr;
    QLabel *quickLid = nullptr; QLabel *quickRecovery = nullptr;
    QSpinBox *quickDays = nullptr; QSpinBox *quickHours = nullptr; QSpinBox *quickMinutes = nullptr;
    QPushButton *quickCustom = nullptr;
    QListWidget *jobList = nullptr;
    QPlainTextEdit *jobLog = nullptr;
    QComboBox *jobPreset = nullptr;
    QLineEdit *jobCommand = nullptr;
    QPushButton *jobStart = nullptr;
    QPushButton *jobStop = nullptr;
    QPushButton *jobRefresh = nullptr;
    QString selectedJob;
    qint64 jobLogOffset = 0;
};
}
