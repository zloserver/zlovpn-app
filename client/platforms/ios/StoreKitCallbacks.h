#pragma once
#include <string>

using NotifyTransactionCallback = void(void*, bool);
void notifyTransaction(std::string signedPayload, void* userData, NotifyTransactionCallback* callback);
