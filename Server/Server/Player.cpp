#include "Player.h"

Player::Player() : Npc()
{
}

Player::~Player()
{
	closesocket(_socket);
}
