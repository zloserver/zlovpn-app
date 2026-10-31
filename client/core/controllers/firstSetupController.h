#ifndef FIRSTSETUPCONTROLLER_H
#define FIRSTSETUPCONTROLLER_H

#include <QObject>
#include <QString>

class FirstSetupController : public QObject {
  Q_OBJECT

public:
  explicit FirstSetupController(QObject *parent = nullptr);

  // Asks the launcher to (re)start the VPN service, returns false if it failed
  static bool startService();

public slots:
  bool firstSetupNeeded();
  void doFirstSetup();
  void restartService();

signals:
  void firstSetupFinished();
  void firstSetupFailed(bool requiresApproval, QString string);
};

#endif // FIRSTSETUPCONTROLLER_H
