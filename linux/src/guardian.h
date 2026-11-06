#pragma once
#include "core.h"
#include <QProcess>
#include <optional>
#include <time.h>

namespace insomnia {
class ProcessGuardian final : public Guardian {
public:
    explicit ProcessGuardian(bool forceKernel = false);
    ~ProcessGuardian() override;
    QString arm(qint64 deadlineMs) override;
    QString heartbeat(qint64 deadlineMs) override;
    void stop() override;
private:
    QProcess process;
    bool forceKernel;
    std::optional<timer_t> kernelTimer;
};
int runGuardian();
QString guardianAvailabilityError();
QString guardianBackend();
}
