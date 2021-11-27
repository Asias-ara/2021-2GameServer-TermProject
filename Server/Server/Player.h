#pragma once
#include "EXP_OVER.h"
#include "Npc.h"
#include "stdafx.h"
class Player : public Npc
{
public:
	SOCKET				_socket;
	EXP_OVER			_recv_over;
	int					_prev_size;

	mutex		        vl;
	unordered_set <int>	viewlist;

public:
    Player() 
    {
        _state = ST_FREE;
        _prev_size = 0;
        _x = 0;
        _y = 0;
    }
    ~Player()
    {
        closesocket(_socket);
    }

    void do_recv()
    {
        DWORD recv_flag = 0;
        ZeroMemory(&_recv_over._wsa_over, sizeof(_recv_over._wsa_over));
        _recv_over._wsa_buf.buf = reinterpret_cast<char*>(_recv_over._net_buf + _prev_size);
        _recv_over._wsa_buf.len = sizeof(_recv_over._net_buf) - _prev_size;
        int ret = WSARecv(_socket, &_recv_over._wsa_buf, 1, 0, &recv_flag, &_recv_over._wsa_over, NULL);
        if (SOCKET_ERROR == ret) {
            int error_num = WSAGetLastError();
            if (ERROR_IO_PENDING != error_num)
                error_display(error_num);
        }
    }

    void do_send(int num_bytes, void* mess)
    {
        EXP_OVER* ex_over = new EXP_OVER(OP_SEND, num_bytes, mess);
        WSASend(_socket, &ex_over->_wsa_buf, 1, 0, 0, &ex_over->_wsa_over, NULL);
    }
};

