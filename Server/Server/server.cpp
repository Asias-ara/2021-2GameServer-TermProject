#include"stdafx.h"

#include <MSWSock.h>
#include <thread>
#include <array>
#include <vector>
#include <chrono>
#include <concurrent_priority_queue.h>      //lock�� ���� �ʰ� ������ ť�� ��� ����, atomic�� ->peak�� ����, pop�� ���� trypop�� ����

#include "Player.h"

#pragma comment (lib, "WS2_32.LIB")
#pragma comment (lib, "MSWSock.LIB")

HANDLE g_h_iocp;
SOCKET g_s_socket;
array <Npc*, MAX_USER + MAX_NPC> players;
concurrency::concurrent_priority_queue<timer_event> timer_queue;

void do_npc_move(int npc_id);

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
        clients[i].state_lock.lock();
        if (ST_FREE == clients[i]._state) {
            clients[i]._state = ST_ACCEPT;
            clients[i].state_lock.unlock();
            return i;
        }
        clients[i].state_lock.unlock();
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
    packet.x = clients[c_id].x;
    packet.y = clients[c_id].y;
    clients[c_id].do_send(sizeof(packet), &packet);
}

void send_move_packet(int c_id, int mover)
{
    sc_packet_move packet;
    packet.id = mover;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_MOVE;
    packet.x = clients[mover].x;
    packet.y = clients[mover].y;
    packet.move_time = clients[mover].last_move_time;
    clients[c_id].do_send(sizeof(packet), &packet);
}

void send_remove_object(int c_id, int victim)
{
    sc_packet_remove_object packet;
    packet.id = victim;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_REMOVE_OBJECT;
    clients[c_id].do_send(sizeof(packet), &packet);
}

void send_put_object(int c_id, int target)
{
    sc_packet_put_object packet;
    packet.id = target;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_PUT_OBJECT;
    packet.x = clients[target].x;
    packet.y = clients[target].y;
    strcpy_s(packet.name, clients[target].name);
    packet.object_type = 0;
    clients[c_id].do_send(sizeof(packet), &packet);
}

bool is_npc(int id)
{
    return (id >= NPC_ID_START) && (id <= NPC_ID_END);
}

void Disconnect(int c_id)
{
    Player& pl = reinterpret_cast<Player&>(players[c_id]);
    pl.vl.lock();
    unordered_set <int> my_vl = pl.viewlist;
    pl.vl.unlock();
    for (auto& other : my_vl) {
        Player& target = reinterpret_cast<Player&>(players[other]);
        if (true == is_npc(target._id)) break;   // npc�� ��� ������ �ʴ´�
        if (ST_INGAME != target._state)
            continue;
        target.vl.lock();
        if (0 != target.viewlist.count(c_id)) {
            target.viewlist.erase(c_id);
            target.vl.unlock();
            send_remove_object(other, c_id);
        }
        else target.vl.unlock();
    }
    players[c_id].state_lock.lock();
    closesocket(reinterpret_cast<Player*>(players[c_id])->_socket);
    clients[c_id]._state = ST_FREE;
    clients[c_id].state_lock.unlock();
}

void process_packet(int client_id, unsigned char* p)
{
    unsigned char packet_type = p[1];
    CLIENT& cl = clients[client_id];

    switch (packet_type) {
    case CS_PACKET_LOGIN: {
        cs_packet_login* packet = reinterpret_cast<cs_packet_login*>(p);
        strcpy_s(cl.name, packet->name);
        send_login_ok_packet(client_id);

        CLIENT& cl = clients[client_id];
        cl.state_lock.lock();
        cl._state = ST_INGAME;
        cl.state_lock.unlock();

        // ���� ������ ������ �ٸ��̿��� ������
        for (auto& other : clients) {
            if (other._id == client_id) continue;   // ����
            other.state_lock.lock();
            if (ST_INGAME != other._state) {
                other.state_lock.unlock();
                continue;
            }
            other.state_lock.unlock();

            if (false == is_near(other._id, client_id)) continue;

            if (true == is_npc(other._id)) {   // ��ó�� �ִ� npc
               // timer ť�� �־�����
                timer_event ev;
                ev.obj_id = other._id;
                ev.start_time = chrono::system_clock::now() + 1s;
                ev.ev = EVENT_NPC_MOVE;
                ev.target_id = client_id;
                timer_queue.push(ev);
                continue;
            }

            other.vl.lock();
            other.viewlist.insert(client_id);
            other.vl.unlock();
            sc_packet_put_object packet;
            packet.id = client_id;
            strcpy_s(packet.name, cl.name);
            packet.object_type = 0;
            packet.size = sizeof(packet);
            packet.type = SC_PACKET_PUT_OBJECT;
            packet.x = cl.x;
            packet.y = cl.y;
            other.do_send(sizeof(packet), &packet);
        }

        // ���� ������ �÷��̾�� ���� ������ ������
        for (auto& other : clients) {
            if (other._id == client_id) continue;
            other.state_lock.lock();
            if (ST_INGAME != other._state) {
                other.state_lock.unlock();
                continue;
            }
            other.state_lock.unlock();

            if (false == is_near(other._id, client_id))
                continue;

            clients[client_id].vl.lock();
            clients[client_id].viewlist.insert(other._id);
            clients[client_id].vl.unlock();

            sc_packet_put_object packet;
            packet.id = other._id;
            strcpy_s(packet.name, other.name);
            packet.object_type = 0;
            packet.size = sizeof(packet);
            packet.type = SC_PACKET_PUT_OBJECT;
            packet.x = other.x;
            packet.y = other.y;
            cl.do_send(sizeof(packet), &packet);
        }
        break;
    }
    case CS_PACKET_MOVE: {
        cs_packet_move* packet = reinterpret_cast<cs_packet_move*>(p);
        cl.last_move_time = packet->move_time;
        int x = cl.x;
        int y = cl.y;
        switch (packet->direction) {
        case 0: if (y > 0) y--; break;
        case 1: if (y < (WORLD_HEIGHT - 1)) y++; break;
        case 2: if (x > 0) x--; break;
        case 3: if (x < (WORLD_WIDTH - 1)) x++; break;
        default:
            cout << "Invalid move in client " << client_id << endl;
            exit(-1);
        }
        cl.x = x;
        cl.y = y;

        unordered_set <int> nearlist;
        for (auto& other : clients) {
            // if (other._id == client_id) continue;
            if (false == is_near(client_id, other._id))
                continue;
            if (ST_INGAME != other._state)
                continue;
            nearlist.insert(other._id);
        }
        nearlist.erase(client_id);  // �� ���̵�� ������ ���� �װ��� ��������


        send_move_packet(cl._id, cl._id);

        cl.vl.lock();
        unordered_set <int> my_vl{ cl.viewlist };
        cl.vl.unlock();

        // ���νþ߿� ���� �÷��̾� ó��
        for (auto other : nearlist) {
            if (0 == my_vl.count(other)) {   // ���� ���� �÷��̾�/npc
                cl.vl.lock();
                cl.viewlist.insert(other);
                cl.vl.unlock();
                send_put_object(cl._id, other);

                if (true == is_npc(other)) {   // ���� ���� npc�� �����̱� ����
                    timer_event ev;
                    ev.obj_id = other;
                    ev.start_time = chrono::system_clock::now() + 1s;
                    ev.ev = EVENT_NPC_MOVE;
                    ev.target_id = client_id;
                    timer_queue.push(ev);
                    continue;
                }

                clients[other].vl.lock();
                if (0 == clients[other].viewlist.count(cl._id)) {
                    clients[other].viewlist.insert(cl._id);
                    clients[other].vl.unlock();
                    send_put_object(other, cl._id);
                }
                else {
                    clients[other].vl.unlock();
                    send_move_packet(other, cl._id);
                }
            }
            // ��� �þ߿� �����ϴ� �÷��̾� ó��
            else {
                if (true == is_npc(other)) continue;   // ���� �ִ� npc�� npc_move���� ó��

                clients[other].vl.lock();
                if (0 != clients[other].viewlist.count(cl._id)) {
                    clients[other].vl.unlock();
                    send_move_packet(other, cl._id);
                }
                else {
                    clients[other].viewlist.insert(cl._id);
                    clients[other].vl.unlock();
                    send_put_object(other, cl._id);
                }
            }
        }
        // �þ߿��� ����� �÷��̾� ó��
        for (auto other : my_vl) {
            if (0 == nearlist.count(other)) {
                cl.vl.lock();
                cl.viewlist.erase(other);
                cl.vl.unlock();
                send_remove_object(cl._id, other);

                // if (true == is_npc(other)) continue;
                if (true == is_npc(other)) break;
                clients[other].vl.lock();
                if (0 != clients[other].viewlist.count(cl._id)) {
                    clients[other].viewlist.erase(cl._id);
                    clients[other].vl.unlock();
                    send_remove_object(other, cl._id);
                }
                else clients[other].vl.unlock();
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
            CLIENT& cl = clients[client_id];
            int remain_data = num_byte + cl._prev_size;
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
                cl._prev_size = remain_data;
                memcpy(&exp_over->_net_buf, packet_start, remain_data);
            }
            cl.do_recv();
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
                CLIENT& cl = clients[new_id];
                cl.x = rand() % WORLD_WIDTH;
                cl.y = rand() % WORLD_HEIGHT;
                cl._id = new_id;
                cl._prev_size = 0;
                cl._recv_over._comp_op = OP_RECV;
                cl._recv_over._wsa_buf.buf = reinterpret_cast<char*>(cl._recv_over._net_buf);
                cl._recv_over._wsa_buf.len = sizeof(cl._recv_over._net_buf);
                cl._type = 1;
                ZeroMemory(&cl._recv_over._wsa_over, sizeof(cl._recv_over._wsa_over));
                cl._socket = c_socket;

                CreateIoCompletionPort(reinterpret_cast<HANDLE>(c_socket), g_h_iocp, new_id, 0);
                cl.do_recv();
            }

            ZeroMemory(&exp_over->_wsa_over, sizeof(exp_over->_wsa_over));
            c_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
            *(reinterpret_cast<SOCKET*>(exp_over->_net_buf)) = c_socket;
            AcceptEx(g_s_socket, c_socket, exp_over->_net_buf + 8, 0, sizeof(SOCKADDR_IN) + 16,
                sizeof(SOCKADDR_IN) + 16, NULL, &exp_over->_wsa_over);
            break;
        }
        case OP_NPC_MOVE:
            delete exp_over;
            do_npc_move(client_id);
            break;
        }
    }
}

void initialise_NPC()
{
    char name[MAX_NAME_SIZE];
    for (int i = NPC_ID_START; i <= NPC_ID_END; ++i) {
        sprintf_s(name, "NPC %d", i);
        players[i] = new Npc(i, name);
    }
}

void do_npc_move(int npc_id)
{
    unordered_set<int> old_viewlist;
    unordered_set<int> new_viewlist;
    for (auto& obj : clients) {
        if (obj._state != ST_INGAME) continue;
        // if (true == is_npc(obj._id)) continue;   // npc�� �ƴҶ�
        if (true == is_npc(obj._id)) break;   // npc�� �ƴҶ�
        if (true == is_near(npc_id, obj._id)) {      // ��ó�� ������
            old_viewlist.insert(obj._id);         // npc��ó�� �÷��̾ ������ old_viewlist�� �÷��̾� id�� �ִ´�
        }
    }

    if (old_viewlist.size() == 0) return;

    auto& x = clients[npc_id].x;
    auto& y = clients[npc_id].y;
    switch (rand() % 4)
    {
    case 0: if (y > 0) y--; break;
    case 1: if (y < WORLD_HEIGHT) y++; break;
    case 2: if (x > 0) x--; break;
    case 3: if (x < WORLD_WIDTH) x++; break;
    default:
        break;
    }

    for (auto& obj : clients) {
        if (obj._state != ST_INGAME) continue;   // in game�� �ƴҶ�
        //if (true == is_npc(obj._id)) continue;   // npc�� �ƴҶ� -> ingame���� �÷��̾� ã��
        if (true == is_npc(obj._id)) break;   // npc�� �ƴҶ� -> ingame���� �÷��̾� ã��
        if (true == is_near(npc_id, obj._id)) {
            new_viewlist.insert(obj._id);
        }
    }

    int temp = 0;
    for (auto pl : new_viewlist) {
        // ���� �þ߿� ���� �÷��̾�
        if (0 == old_viewlist.count(pl)) {
            clients[pl].vl.lock();
            clients[pl].viewlist.insert(npc_id);
            clients[pl].vl.unlock();
            send_put_object(pl, npc_id);
        }
        else {
            send_move_packet(pl, npc_id);
        }
        temp = pl;
    }

    // �þ߿� ����� ���
    for (auto pl : old_viewlist) {
        if (0 == new_viewlist.count(pl)) {
            clients[pl].vl.lock();
            clients[pl].viewlist.erase(npc_id);
            clients[pl].vl.unlock();
            send_remove_object(pl, npc_id);
        }
    }

    if (new_viewlist.size() == 0) {   // ������ �� ������ �÷��̾ ����
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
            PostQueuedCompletionStatus(g_h_iocp, 1, temp.obj_id, &ex_over->_wsa_over);   //0�� ��������� ����
        }

        while (true) {
            timer_event ev;
            if (timer_queue.size() == 0) break;
            timer_queue.try_pop(ev);

            dura = ev.start_time - chrono::system_clock::now();
            if (dura <= 0ms) {
                EXP_OVER* ex_over = new EXP_OVER;
                ex_over->_comp_op = OP_NPC_MOVE;
                PostQueuedCompletionStatus(g_h_iocp, 1, ev.obj_id, &ex_over->_wsa_over);   //0�� ��������� ����
            }
            else if (dura <= waittime) {
                temp = ev;
                temp_bool = true;
                break;
            }
            else {
                timer_queue.push(ev);   // Ÿ�̸� ť�� ���� �ʰ� ����ȭ �ʿ�
                break;
            }
        }
        this_thread::sleep_for(dura);
        // �� �翩�־ ��� ó���� �ϵ��� ������
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

    // �ʱ�ȭ ����
    for (int i = 0; i < MAX_USER; ++i) {
        players[i] = new Player;
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
