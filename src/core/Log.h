// StreamX — log em arquivo (%APPDATA%/StreamX/logs) e no console
#pragma once
#include <QString>

namespace sx {
void initLog();
void logInfo(const QString &msg);
void logWarn(const QString &msg);
void logError(const QString &msg);
QString logFilePath();
} // namespace sx
