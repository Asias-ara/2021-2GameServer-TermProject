#pragma once
#include "stdafx.h"
class Npc
{
protected:
	char	_name[MAX_NAME_SIZE];
	int		_id;
	short	_x, _y;
	TRIBE	_tribe;

	STATE	_state;
	atomic_bool	_active;

public:
	mutex	state_lock;
	Npc(int id);
	Npc(int id, const char* name);
	~Npc();
	
	void set_pos(int x, int y);
	void set_x(int x);
	void set_y(int y);

	int get_x();
	int get_y();
	int get_Id();
	STATE get_state();
	bool get_active();
	TRIBE get_tribe();
};

