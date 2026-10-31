#pragma once
#include <string>

void logError(std::string message);
void logDebug(std::string message);

// Asks the Qt event loop to quit, so the service cleans up before exiting
void requestServiceShutdown();
