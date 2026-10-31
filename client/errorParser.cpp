#include "errorParser.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "amnezia_application.h"

static Errors::Kind kindOf(int statusCode) {
  if (statusCode == 0)
    return Errors::Kind::Network;
  if (statusCode == 401)
    return Errors::Kind::Unauthorized;
  if (statusCode >= 500)
    return Errors::Kind::Server;
  return Errors::Kind::Business;
}

Errors ErrorParser::parse(QByteArray body, int statusCode, bool isFormError) {
  Errors err{};
  err.isFormError = isFormError;
  err.statusCode = statusCode;
  err.kind = kindOf(statusCode);

  if (err.kind == Errors::Kind::Network) {
    err.errorMessage = QCoreApplication::translate("ServerLocale", "No connection to the ZloVPN server");
    return err;
  }
  if (err.kind == Errors::Kind::Server) {
    err.errorMessage = QCoreApplication::translate("ServerLocale", "The ZloVPN server is temporarily unavailable");
    return err;
  }

  QJsonDocument document = QJsonDocument::fromJson(body);
  if (document.isNull()) {
    err.errorMessage =
        QCoreApplication::translate("ServerLocale", "Server error");
    return err;
  }

  auto *jsonTranslation = amnApp->jsonTranslation();

  QJsonObject obj = document.object();
  err.errorMessage = jsonTranslation->translate(obj["error"].toString());
  if (obj.contains("errors")) {
    QJsonObject errors = obj["errors"].toObject();

    if (errors.contains("fieldErrors")) {
      QJsonObject fieldErrors = errors["fieldErrors"].toObject();

      for (QString key : fieldErrors.keys()) {
        QJsonArray keyErrors = fieldErrors[key].toArray();
        QString keyErrorsJoined{};

        for (qsizetype i = 0; i < keyErrors.size(); i++) {
          if (i != 0)
            keyErrorsJoined += "\n";
          keyErrorsJoined +=
              jsonTranslation->translate(keyErrors[i].toString());
        }

        err.fieldErrors.insert(key, keyErrorsJoined);
      }
    }
  }

  return err;
}
