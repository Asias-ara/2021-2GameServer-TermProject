#include"stdafx.h"

#include <MSWSock.h>
#include <thread>
#include <array>
#include <vector>
#include <chrono>
#include <concurrent_priority_queue.h>      //lock를 쓰지 않고 열심히 큐를 사용 가능, atomic함 ->peak가 없음, pop도 없고 trypop만 있음

#include "Player.h"

#pragma comment (lib, "WS2_32.LIB")
#pragma comment (lib, "MSWSock.LIB")

HANDLE g_h_iocp;
SOCKET g_s_socket;
array <Npc*, MAX_USER + MAX_NPC> players;

void do_npc_move(int npc_id);

struct timer_event {
    int obj_id;
    chrono::system_clock::time_point start_time;
    EVENT_TYPE ev;
    int target_id;

    constexpr bool operator < (const timer_event& _left) const
    {
        return (start_time > _left.start_time);
    }

};
concurrency::concurrent_priority_queue<timer_event> timer_queue;

void error_display(int err_no)
{
    WCHAR* lpMsgBuf;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, err_no,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf, 0, 0);
    wcout << lpMsgBuf << endl;
    //while (true);
    LocalFree(lpMsgBuf);
}

bool is_near(int a, int b)
{
    if (RANGE < abs(players[a]->get_x() - players[b]->get_x())) return false;
    if (RANGE < abs(players[a]->get_y() - players[b]->get_y())) return false;
    return true;
}

int get_new_id()
{
    static int g_id = 0;

    for (int i = 0; i < MAX_USER; ++i) {
        players[i]->state_lock.lock();
        if (ST_FREE == players[i]->get_state()) {
            players[i]->set_state(ST_ACCEPT);
            players[i]->state_lock.unlock();
            return i;
        }
        players[i]->state_lock.unlock();
    }
    cout << "Maximum Number of Clients Overflow!!\n";
    return -1;
}

void send_login_ok_packet(int c_id)
{
    sc_packet_login_ok packet;
    packet.id = c_id;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_LOGIN_OK;
    packet.x = players[c_id]->get_x();
    packet.y = players[c_id]->get_y();
    reinterpret_cast<Player*>(players[c_id])->do_send(sizeof(packet), &packet);
}

void send_move_packet(int c_id, int mover)
{
    sc_packet_move packet;
    packet.id = mover;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_MOVE;
    packet.x = players[mover]->get_x();
    packet.y = players[mover]->get_y();
    //packet.move_time =  clients[mover].last_move_time;
    packet.move_time = 0;
    reinterpret_cast<Player*>(players[c_id])->do_send(sizeof(packet), &packet);
}

void send_remove_object(int c_id, int victim)
{
    sc_packet_remove_object packet;
    packet.id = victim;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_REMOVE_OBJECT;
    reinterpret_cast<Player*>(players[c_id])->do_send(sizeof(packet), &packet);
}

void send_put_object(int c_id, int target)
{
    sc_packet_put_object packet;
    packet.id = target;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_PUT_OBJECT;
    packet.x = players[target]->get_x();
    packet.y = players[target]->get_y();
    strcpy_s(packet.name, players[target]->get_name());
    packet.object_type = 0;
    reinterpret_cast<Player*>(players[c_id])->do_send(sizeof(packet), &packet);
}

void send_chat_packet(int user_id, int my_id, char* mess)
{
    sc_packet_chat packet;
    packet.id = my_id;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_CHAT;
    strcpy_s(packet.message, mess);
    reinterpret_cast<Player*>(players[user_id])->do_send(sizeof(packet), &packet);
}

bool is_npc(int id)
{
    return (id >= NPC_ID_START) && (id <= NPC_ID_END);
}

bool is_player(int id)
{
    return (id >= 0) && (id < MAX_USER);
}

void Disconnect(int c_id)
{
    Player* pl = reinterpret_cast<Player*>(players[c_id]);
    pl->vl.lock();
    unordered_set <int> my_vl = pl->viewlist;
    pl->vl.unlock();
    for (auto& other : my_vl) {
        Player* target = reinterpret_cast<Player*>(players[other]);
        if (true == is_npc(target->get_Id())) break;   // npc일 경우 보내지 않는다
        if (ST_INGAME != target->get_state()) continue;
        target->vl.lock();
        if (0 != target->viewlist.count(c_id)) {
            target->viewlist.erase(c_id);
            target->vl.unlock();
            send_remove_object(other, c_id);
        }
        else target->vl.unlock();
    }
    players[c_id]->state_lock.lock();
    closesocket(reinterpret_cast<Player*>(players[c_id])->_socket);
    players[c_id]->set_state(ST_FREE);
    players[c_id]->state_lock.unlock();
}

// 스크립트 추가
void Activate_Player_Move_Event(int target, int player_id)
{
    EXP_OVER* exp_over = new EXP_OVER;
    exp_over->_comp_op = OP_PLAYER_MOVE;
    exp_over->_target = player_id;
    PostQueuedCompletionStatus(g_h_iocp, 1, target, &exp_over->_wsa_over);
}

void process_packet(int client_id, unsigned char* p)
{
    unsigned char packet_type = p[1];
    Player* pl = reinterpret_cast<Player*>(players[client_id]);
    switch (packet_type) {
    case CS_PACKET_LOGIN: {
        cs_packet_login* packet = reinterpret_cast<cs_packet_login*>(p);
        pl->set_name(packet->name);
        send_login_ok_packet(client_id);
        pl->state_lock.lock();
        pl->set_state(ST_INGAME);
        pl->state_lock.unlock();

        // 새로 접속한 정보를 다른이에게 보내줌
        for (auto& other : players) {
            if (other->get_Id() == client_id) continue;   // 나다

            if (true == is_npc(other->get_Id())) break;// 만약 내가 있는 곳에 NPC가 있다면

            other->state_lock.lock();
            if (ST_INGAME != other->get_state()) {
                other->state_lock.unlock();
                continue;
            }
            other->state_lock.unlock();

            if (false == is_near(other->get_Id(), client_id)) continue;

            //if (true == is_npc(other->get_Id())) {   // 근처에 있는 npc
            //   // timer 큐에 넣어주자
            //    timer_event ev;
            //    ev.obj_id = other->get_Id();
            //    ev.start_time = chrono::system_clock::now() + 1s;
            //    ev.ev = EVENT_NPC_MOVE;
            //    ev.target_id = client_id;
            //    timer_queue.push(ev);
            //    continue;
            //}

            // 여기는 플레이어 처리
            Player* other_player = reinterpret_cast<Player*>(other);
            other_player->vl.lock();
            other_player->viewlist.insert(client_id);
            other_player->vl.unlock();
            sc_packet_put_object packet;
            packet.id = client_id;
            strcpy_s(packet.name, pl->get_name());
            packet.object_type = 0;
            packet.size = sizeof(packet);
            packet.type = SC_PACKET_PUT_OBJECT;
            packet.x = pl->get_x();
            packet.y = pl->get_y();
            other_player->do_send(sizeof(packet), &packet);
        }

        // 새로 접속한 플레이어에게 기존 정보를 보내중
        for (auto& other : players) {
            if (other->get_Id() == client_id) continue;
            other->state_lock.lock();
            if (ST_INGAME != other->get_state()) {
                other->state_lock.unlock();
                continue;
            }
            other->state_lock.unlock();

            if (false == is_near(other->get_Id(), client_id))
                continue;

            // 스크립트와 함께 추가된 부분
            if (true == is_npc(other->get_Id())) {	// 시야에 npc가 있다면 
                Activate_Player_Move_Event(other->get_Id(), pl->get_Id());
            }

            pl->vl.lock();
            pl->viewlist.insert(other->get_Id());
            pl->vl.unlock();

            sc_packet_put_object packet;
            packet.id = other->get_Id();
            strcpy_s(packet.name, other->get_name());
            packet.object_type = 0;
            packet.size = sizeof(packet);
            packet.type = SC_PACKET_PUT_OBJECT;
            packet.x = other->get_x();
            packet.y = other->get_y();
            pl->do_send(sizeof(packet), &packet);
        }
        break;
    }
    case CS_PACKET_MOVE: {
        cs_packet_move* packet = reinterpret_cast<cs_packet_move*>(p);
        // pl.last_move_time = packet->move_time;
        int x = pl->get_x();
        int y = pl->get_y();
        switch (packet->direction) {
        case 0: if (y > 0) y--; break;
        case 1: if (y < (WORLD_HEIGHT - 1)) y++; break;
        case 2: if (x > 0) x--; break;
        case 3: if (x < (WORLD_WIDTH - 1)) x++; break;
        default:
            cout << "Invalid move in client " << client_id << endl;
            exit(-1);
        }
        pl->set_x(x);
        pl->set_y(y);

        unordered_set <int> nearlist;
        for (auto& other : players) {
            // if (other._id == client_id) continue;
            if (false == is_near(client_id, other->get_Id()))
                continue;
            if (ST_INGAME != other->get_state())
                continue;
            //스크립트 추가
            if (true == is_npc(other->get_Id())) {
                Activate_Player_Move_Event(other->get_Id(), pl->get_Id());
            }
            nearlist.insert(other->get_Id());
        }
        nearlist.erase(client_id);  // 내 아이디는 무조건 들어가니 그것을 지워주자


        send_move_packet(pl->get_Id(), pl->get_Id()); // 내 자신의 움직임을 먼저 보내주자

        pl->vl.lock();
        unordered_set <int> my_vl{ pl->viewlist };
        pl->vl.unlock();

        // 새로시야에 들어온 플레이어 처리
        for (auto other : nearlist) {
            if (0 == my_vl.count(other)) {   // 원래 없던 플레이어/npc
                pl->vl.lock();
                pl->viewlist.insert(other);
                pl->vl.unlock();
                send_put_object(pl->get_Id(), other);

                //if (true == is_npc(other)) {   // 원래 없던 npc는 움직이기 시작
                //    timer_event ev;
                //    ev.obj_id = other;
                //    ev.start_time = chrono::system_clock::now() + 1s;
                //    ev.ev = EVENT_NPC_MOVE;
                //    ev.target_id = client_id;
                //    timer_queue.push(ev);
                //    continue;
                //}
                // 스크립트 추가
                if (true == is_npc(other)) break;

                // 여기는 플레이어 처리이다.
                Player* other_player = reinterpret_cast<Player*>(players[other]);
                other_player->vl.lock();
                if (0 == other_player->viewlist.count(pl->get_Id())) {
                    other_player->viewlist.insert(pl->get_Id());
                    other_player->vl.unlock();
                    send_put_object(other, pl->get_Id());
                }
                else {
                    other_player->vl.unlock();
                    send_move_packet(other, pl->get_Id());
                }
            }
            // 계속 시야에 존재하는 플레이어 처리
            else {
                if (true == is_npc(other)) continue;   // 원래 있던 npc는 npc_move에서 처리

                Player* other_player = reinterpret_cast<Player*>(players[other]);
                other_player->vl.lock();
                if (0 == other_player->viewlist.count(pl->get_Id())) {
                    other_player->viewlist.insert(pl->get_Id());
                    other_player->vl.unlock();
                    send_put_object(other, pl->get_Id());
                }
                else {
                    other_player->vl.unlock();
                    send_move_packet(other, pl->get_Id());
                }
            }
        }
        // 시야에서 사라진 플레이어 처리
        for (auto other : my_vl) {
            if (0 == nearlist.count(other)) {
                pl->vl.lock();
                pl->viewlist.erase(other);
                pl->vl.unlock();
                send_remove_object(pl->get_Id(), other);

                // if (true == is_npc(other)) continue;
                if (true == is_npc(other)) break;
                Player* other_player = reinterpret_cast<Player*>(players[other]);
                other_player->vl.lock();
                if (0 != other_player->viewlist.count(pl->get_Id())) {
                    other_player->viewlist.erase(pl->get_Id());
                    other_player->vl.unlock();
                    send_remove_object(other, pl->get_Id());
                }
                else other_player->vl.unlock();
            }
        }
    }
    }
}

void worker()
{
    for (;;) {
        DWORD num_byte;
        LONG64 iocp_key;
        WSAOVERLAPPED* p_over;
        BOOL ret = GetQueuedCompletionStatus(g_h_iocp, &num_byte, (PULONG_PTR)&iocp_key, &p_over, INFINITE);
        //cout << "GQCS returned.\n";
        int client_id = static_cast<int>(iocp_key);
        EXP_OVER* exp_over = reinterpret_cast<EXP_OVER*>(p_over);
        if (FALSE == ret) {
            int err_no = WSAGetLastError();
            cout << "GQCS Error : ";
            error_display(err_no);
            cout << endl;
            Disconnect(client_id);
            if (exp_over->_comp_op == OP_SEND)
                delete exp_over;
            continue;
        }

        switch (exp_over->_comp_op) {
        case OP_RECV: {
            if (num_byte == 0) {
                Disconnect(client_id);
                continue;
            }
            Player* pl = reinterpret_cast<Player*>(players[client_id]);
            int remain_data = num_byte + pl->_prev_size;
            unsigned char* packet_start = exp_over->_net_buf;
            int packet_size = packet_start[0];

            while (packet_size <= remain_data) {
                process_packet(client_id, packet_start);
                remain_data -= packet_size;
                packet_start += packet_size;
                if (remain_data > 0) packet_size = packet_start[0];
                else break;
            }

            if (0 < remain_data) {
                pl->_prev_size = remain_data;
                memcpy(&exp_over->_net_buf, packet_start, remain_data);
            }
            pl->do_recv();
            break;
        }
        case OP_SEND: {
            if (num_byte != exp_over->_wsa_buf.len) {
                Disconnect(client_id);
            }
            delete exp_over;
            break;
        }
        case OP_ACCEPT: {
            cout << "Accept Completed.\n";
            SOCKET c_socket = *(reinterpret_cast<SOCKET*>(exp_over->_net_buf));
            int new_id = get_new_id();
            if (-1 == new_id) {
                cout << "Maxmum user overflow. Accept aborted.\n";
            }
            else {
                //players[new_id] = new Player(new_id);
                Player* pl = reinterpret_cast<Player*>(players[new_id]);
                pl->set_x(rand() % WORLD_WIDTH);
                pl->set_y(rand() % WORLD_HEIGHT);
                pl->set_id(new_id);
                pl->_prev_size = 0;
                pl->_recv_over._comp_op = OP_RECV;
                pl->_recv_over._wsa_buf.buf = reinterpret_cast<char*>(pl->_recv_over._net_buf);
                pl->_recv_over._wsa_buf.len = sizeof(pl->_recv_over._net_buf);
                pl->set_tribe(HUMAN);
                ZeroMemory(&pl->_recv_over._wsa_over, sizeof(pl->_recv_over._wsa_over));
                pl->_socket = c_socket;

                CreateIoCompletionPort(reinterpret_cast<HANDLE>(c_socket), g_h_iocp, new_id, 0);
                pl->do_recv();
            }

            ZeroMemory(&exp_over->_wsa_over, sizeof(exp_over->_wsa_over));
            c_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
            *(reinterpret_cast<SOCKET*>(exp_over->_net_buf)) = c_socket;
            AcceptEx(g_s_socket, c_socket, exp_over->_net_buf + 8, 0, sizeof(SOCKADDR_IN) + 16,
                sizeof(SOCKADDR_IN) + 16, NULL, &exp_over->_wsa_over);
            break;
        }
        case OP_NPC_MOVE: {
            players[client_id]->lua_lock.lock();
            lua_State* L = players[client_id]->L;
            lua_getglobal(L, "event_npc_move");
            lua_pushnumber(L, exp_over->_target);
            lua_pcall(L, 1, 1, 0);

            // bool값도 리턴을 해주자 
            // true면 움직이고 
            // false면 lua안에서 send_chat_packet으로 bye를 보낸다6
            bool m = lua_toboolean(L, -1);
            lua_pop(L, 1);
            if (m) do_npc_move(client_id);
            else players[client_id]->set_active(true);
            players[client_id]->lua_lock.unlock();
            delete exp_over;
            break;
        }
        case OP_PLAYER_MOVE: {
            players[client_id]->lua_lock.lock();
            lua_State* L = players[client_id]->L;
            lua_getglobal(L, "event_player_move");
            lua_pushnumber(L, exp_over->_target);
            int error = lua_pcall(L, 1, 0, 0);
            if (error != 0) {
                cout << lua_tostring(L, -1) << endl;
            }
            players[client_id]->lua_lock.unlock();
            delete exp_over;
            break;
        }
        }
    
    }
}

// 스크립트 API
int API_SendMessage(lua_State* L)
{
    int my_id = (int)lua_tointeger(L, -3);
    int user_id = (int)lua_tointeger(L, -2);
    char* mess = (char*)lua_tostring(L, -1);
    lua_pop(L, 4);

    send_chat_packet(user_id, my_id, mess);
    if (0 == strcmp(mess, "HELLO")) {
        if (players[my_id]->get_active()) return 0;
        timer_event ev;
        ev.obj_id = my_id;
        ev.start_time = chrono::system_clock::now() + 1s;
        ev.ev = EVENT_NPC_MOVE;
        ev.target_id = user_id;
        timer_queue.push(ev);
        players[my_id]->set_active(true);
    }
    return 0;
}

int API_get_x(lua_State* L)
{
    int user_id = (int)lua_tointeger(L, -1);
    lua_pop(L, 2);
    int x = players[user_id]->get_x();
    lua_pushnumber(L, x);
    return 1;
}

int API_get_y(lua_State* L)
{
    int user_id =
        (int)lua_tointeger(L, -1);
    lua_pop(L, 2);
    int y = players[user_id]->get_y();
    lua_pushnumber(L, y);
    return 1;
}

void initialise_NPC()
{
    cout << "NPC 로딩중" << endl;
    char name[MAX_NAME_SIZE];
    for (int i = NPC_ID_START; i <= NPC_ID_END; ++i) {
        sprintf_s(name, "NPC %d", i);
        players[i] = new Npc(i, name);

        lua_State* L = players[i]->L = luaL_newstate();
        luaL_openlibs(L);
        int error = luaL_loadfile(L, "monster.lua") ||
            lua_pcall(L, 0, 0, 0);
        lua_getglobal(L, "set_uid");
        lua_pushnumber(L, i);
        error = lua_pcall(L, 1, 1, 0);
        if (error != 0)
            cout << "초기화 오류" << endl;
        lua_pop(L, 1);// eliminate set_uid from stack after call

        lua_register(L, "API_SendMessage", API_SendMessage);
        lua_register(L, "API_get_x", API_get_x);
        lua_register(L, "API_get_y", API_get_y);
    }
    cout << "NPC로딩 완료" << endl;
}

void do_npc_move(int npc_id)
{
    unordered_set<int> old_viewlist;
    unordered_set<int> new_viewlist;
    for (auto& obj : players) {
        if (obj->get_state() != ST_INGAME) continue;
        // if (true == is_npc(obj._id)) continue;   // npc가 아닐때
        if (true == is_npc(obj->get_Id())) break;   // npc가 아닐때
        if (true == is_near(npc_id, obj->get_Id())) {      // 근처에 있을때
            old_viewlist.insert(obj->get_Id());         // npc근처에 플레이어가 있으면 old_viewlist에 플레이어 id를 넣는다
        }
    }

    if (old_viewlist.size() == 0) return;


    int x = players[npc_id]->get_x();
    int y = players[npc_id]->get_y();
    //auto& x = players[npc_id]->get_x();
    //auto& y = clients[npc_id].y;
    switch (rand() % 4)
    {
    case 0: if (y > 0) y--; break;
    case 1: if (y < WORLD_HEIGHT) y++; break;
    case 2: if (x > 0) x--; break;
    case 3: if (x < WORLD_WIDTH) x++; break;
    default:
        break;
    }
    players[npc_id]->set_x(x);
    players[npc_id]->set_y(y);


    for (auto& obj : players) {
        if (obj->get_state() != ST_INGAME) continue;   // in game이 아닐때
        //if (true == is_npc(obj._id)) continue;   // npc가 아닐때 -> ingame중인 플레이어 찾기
        if (true == is_npc(obj->get_Id())) break;   // npc가 아닐때 -> ingame중인 플레이어 찾기
        if (true == is_near(npc_id, obj->get_Id())) {
            new_viewlist.insert(obj->get_Id());
        }
    }

    int temp = 0;
    for (auto pl : new_viewlist) {
        // 새로 시야에 들어온 플레이어
        if (0 == old_viewlist.count(pl)) {
            reinterpret_cast<Player*>(players[pl])->vl.lock();
            reinterpret_cast<Player*>(players[pl])->viewlist.insert(npc_id);
            reinterpret_cast<Player*>(players[pl])->vl.unlock();
            send_put_object(pl, npc_id);
        }
        else {
            send_move_packet(pl, npc_id);
        }
        temp = pl;
    }

    // 시야에 사라진 경우
    for (auto pl : old_viewlist) {
        if (0 == new_viewlist.count(pl)) {
            reinterpret_cast<Player*>(players[pl])->vl.lock();
            reinterpret_cast<Player*>(players[pl])->viewlist.erase(npc_id);
            reinterpret_cast<Player*>(players[pl])->vl.unlock();
            send_remove_object(pl, npc_id);
        }
    }

    if (new_viewlist.size() == 0) {   // 움직인 후 주위에 플레이어가 없다
        return;
    }

    timer_event ev;
    ev.obj_id = npc_id;
    ev.start_time = chrono::system_clock::now() + 1s;
    ev.ev = EVENT_NPC_MOVE;
    ev.target_id = temp;
    timer_queue.push(ev);
}

void do_timer()
{
    chrono::system_clock::duration dura;
    const chrono::milliseconds waittime = 10ms;
    timer_event temp;
    bool temp_bool = false;
    while (true) {
        if (temp_bool) {
            temp_bool = false;
            EXP_OVER* ex_over = new EXP_OVER;
            ex_over->_comp_op = OP_NPC_MOVE;
            PostQueuedCompletionStatus(g_h_iocp, 1, temp.obj_id, &ex_over->_wsa_over);   //0은 소켓취급을 받음
        }

        while (true) {
            timer_event ev;
            if (timer_queue.size() == 0) break;
            timer_queue.try_pop(ev);

            dura = ev.start_time - chrono::system_clock::now();
            if (dura <= 0ms) {
                EXP_OVER* ex_over = new EXP_OVER;
                ex_over->_comp_op = OP_NPC_MOVE;
                PostQueuedCompletionStatus(g_h_iocp, 1, ev.obj_id, &ex_over->_wsa_over);   //0은 소켓취급을 받음
            }
            else if (dura <= waittime) {
                temp = ev;
                temp_bool = true;
                break;
            }
            else {
                timer_queue.push(ev);   // 타이머 큐에 넣지 않고 최적화 필요
                break;
            }
        }
        this_thread::sleep_for(dura);
        // 쭉 사여있어서 계속 처리를 하도록 ㅎ야함
    }
}

int main()
{
    wcout.imbue(locale("korean"));
    WSADATA WSAData;
    WSAStartup(MAKEWORD(2, 2), &WSAData);
    g_s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
    SOCKADDR_IN server_addr;
    ZeroMemory(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(g_s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    listen(g_s_socket, SOMAXCONN);

    g_h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
    CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_s_socket), g_h_iocp, 0, 0);

    SOCKET c_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
    char   accept_buf[sizeof(SOCKADDR_IN) * 2 + 32 + 100];
    EXP_OVER   accept_ex;
    *(reinterpret_cast<SOCKET*>(&accept_ex._net_buf)) = c_socket;
    ZeroMemory(&accept_ex._wsa_over, sizeof(accept_ex._wsa_over));
    accept_ex._comp_op = OP_ACCEPT;

    AcceptEx(g_s_socket, c_socket, accept_buf, 0, sizeof(SOCKADDR_IN) + 16,
        sizeof(SOCKADDR_IN) + 16, NULL, &accept_ex._wsa_over);
    cout << "Accept Called\n";

    // 초기화 실행
    for (int i = 0; i < MAX_USER; ++i) {
        players[i] = new Player(i);
    }
    initialise_NPC();

    vector <thread> worker_threads;
    thread timer_thread{ do_timer };
    for (int i = 0; i < 16; ++i)
        worker_threads.emplace_back(worker);
    for (auto& th : worker_threads)
        th.join();

    timer_thread.join();
    for (auto& pl : players) {
        if (pl->get_tribe() != HUMAN) break;
        if (ST_INGAME == pl->get_state())
            Disconnect(pl->get_Id());
    }
    closesocket(g_s_socket);
    WSACleanup();
}
