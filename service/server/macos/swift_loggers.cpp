#include "swift_loggers.h"

#include <QCoreApplication>
#include <QDebug>

void logError(std::string message) {
  qCritical() << message;
}

void logDebug(std::string message) {
  qDebug() << message;
}

void requestServiceShutdown() {
  // called from a Swift background queue, quit has to run on the main thread
  QMetaObject::invokeMethod(QCoreApplication::instance(), []() { QCoreApplication::quit(); }, Qt::QueuedConnection);
}
