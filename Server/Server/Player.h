#pragma once
#include "EXP_OVER.h"
#include "Npc.h"
class Player : public Npc
{
public:
	SOCKET				_socket;
	EXP_OVER			_recv_over;
	int					_prev_size;

	mutex		        vl;
	unordered_set <int>	viewlist;

public:
    Player(int id) : Npc(id)
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
            if (ERROR_IO_PENDING != error_num) {
                WCHAR* lpMsgBuf;
                FormatMessage(
                    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                    NULL, error_num,
                    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                    (LPTSTR)&lpMsgBuf, 0, 0);
                wcout << lpMsgBuf << endl;
                //while (true);
                LocalFree(lpMsgBuf);
            }
        }
    }

    void do_send(int num_bytes, void* mess)
    {
        EXP_OVER* ex_over = new EXP_OVER(OP_SEND, num_bytes, mess);
        int ret = WSASend(_socket, &ex_over->_wsa_buf, 1, 0, 0, &ex_over->_wsa_over, NULL);
        if (SOCKET_ERROR == ret) {
            int error_num = WSAGetLastError();
            if (ERROR_IO_PENDING != error_num) {
                WCHAR* lpMsgBuf;
                FormatMessage(
                    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                    NULL, error_num,
                    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                    (LPTSTR)&lpMsgBuf, 0, 0);
                wcout << lpMsgBuf << endl;
                //while (true);
                LocalFree(lpMsgBuf);
            }
        }
    }
};

