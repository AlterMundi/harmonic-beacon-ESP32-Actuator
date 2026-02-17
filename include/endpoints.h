#ifndef ENDPOINTS_H
#define ENDPOINTS_H

#include <WebServer.h>

void setupEndpoints(WebServer &server);
void handleHome();
void handleConfig();
void handlePostConfig();
void handleSettings();
void handlePlay();
void handlePluck();
void handleStop();
void handleMelody();
void handleStatus();
void handleRestart();
void handleSetFundamental();

#endif // ENDPOINTS_H
