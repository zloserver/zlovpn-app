#ifndef ERRORPARSER_H
#define ERRORPARSER_H

#include <QMap>
#include <QObject>

struct Errors {
  Q_GADGET

  Q_PROPERTY(QString errorMessage MEMBER errorMessage)
  Q_PROPERTY(QMap<QString, QString> fieldErrors MEMBER fieldErrors)

public:
  // What went wrong decides what the app may do (docs: design.md, section 4):
  // only Unauthorized ends the session, Network and Server errors leave it alone
  enum class Kind {
    Network,      // no HTTP answer: no connection, DNS, TLS, timeout
    Server,       // 5xx
    Unauthorized, // 401
    Business,     // other 4xx with an error code
  };

  QString errorMessage;
  bool isFormError{};
  QMap<QString, QString> fieldErrors;
  int statusCode{};
  Kind kind{Kind::Business};
};

class ErrorParser {
public:
  // statusCode is the HTTP status, 0 when there was no HTTP answer at all
  static Errors parse(QByteArray body, int statusCode, bool isFormError = false);
};

#endif // ERRORPARSER_H
