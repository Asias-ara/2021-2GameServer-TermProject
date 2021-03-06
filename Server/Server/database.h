#pragma once
#include <WS2tcpip.h>
#include <sqlext.h>
#include <string>
#include "Player.h"

void	HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode);
void	Initialise_DB();
bool	Search_Id(Player* pl, char* login_id);
void	Save_position(Player* pl);
void	Disconnect_DB();