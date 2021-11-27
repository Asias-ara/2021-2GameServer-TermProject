#include "Npc.h"

Npc::Npc(int id) : _id(id)
{
	_x = rand() % WORLD_WIDTH;
	_y = rand() % WORLD_HEIGHT;
	_tribe = MONSTER;
	_state = ST_INGAME;
	_active = false;
}

Npc::Npc(int id, const char* name) : _id(id)
{
	sprintf_s(_name, MAX_NAME_SIZE, name);
	_x = rand() % WORLD_WIDTH;
	_y = rand() % WORLD_HEIGHT;
	_tribe = MONSTER;
	_state = ST_INGAME;
	_active = false;
}

Npc::~Npc() 
{
}

void Npc::set_pos(int x, int y)
{
	_x = x;
	_y = y;
}

void Npc::set_x(int x)
{
	_x = x;
}

void Npc::set_y(int y)
{
	_y = y;
}

int Npc::get_x()
{
	return _x;
}

int Npc::get_y()
{
	return _y;
}

int Npc::get_Id()
{
	return _id;
}

STATE Npc::get_state()
{
	return _state;
}

bool Npc::get_active()
{
	return _active;
}

TRIBE Npc::get_tribe()
{
	return _tribe;
}