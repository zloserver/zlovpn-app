#include "firstSetupController.h"

#include <QDebug>
#include <string>

#ifdef Q_OS_MACOS
#include <AmneziaVPN-Swift.h>
#include <core/ipcclient.h>

// Local builds without the team's signing certificate can't install the launcher (SMJobBless needs a signed app).
// With ZLOVPN_DEV_MANUAL_SERVICE=1 the app skips the launcher: run Contents/MacOS/com.zloserver.vpn-service as root yourself.
static bool isManualServiceMode() { return qEnvironmentVariableIsSet("ZLOVPN_DEV_MANUAL_SERVICE"); }
#endif

FirstSetupController::FirstSetupController(QObject *parent) : QObject{parent} {}

bool FirstSetupController::firstSetupNeeded() {
#ifdef Q_OS_MACOS
  if (isManualServiceMode())
    return false;
  return ZloVPN::firstSetupNeeded();
#endif

  return false;
}

void FirstSetupController::doFirstSetup() {
#ifdef Q_OS_MACOS
  QString firstSetupString = tr("To install the VPN service");
  ZloVPN::FirstSetupResponse response = ZloVPN::doFirstSetup(firstSetupString.toStdString());

  if (response.isError()) {
    std::string errorString = response.getErrorString();
    emit firstSetupFailed(response.requiresApproval(), QString::fromStdString(errorString));
    return;
  } else {
    IpcClient::Reinitialize();
  }
#endif

  emit firstSetupFinished();
}
  
void FirstSetupController::restartService() { startService(); }

bool FirstSetupController::startService() {
#ifdef Q_OS_MACOS
  if (isManualServiceMode()) {
    qDebug() << "ZLOVPN_DEV_MANUAL_SERVICE is set, the service has to be started manually";
    return false;
  }

  auto restartResult = ZloVPN::restartService();
  if (restartResult.getErrorMessage()) {
    qDebug() << "Failed to start service, reason: " << restartResult.getErrorMessage().get();
    return false;
  } else if (restartResult.getLauncherResponse()) {
    qDebug() << "Failed to start service, launcher response: " << restartResult.getLauncherResponse().get().getRawValue();
    return false;
  }
  return true;
#endif

  return false;
}
