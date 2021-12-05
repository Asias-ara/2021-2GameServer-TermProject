#include "stdafx.h"
#include "Player.h"
#include "database.h"

struct obstacle {
    TRIBE trive = OBSTACLE;
    short x;
    short y;
};

HANDLE g_h_iocp;
SOCKET g_s_socket;
array <Npc*, MAX_USER + MAX_NPC> players;
array <obstacle, MAX_OBSTACLE> obstacles;

void do_npc_move(int npc_id, int target);
void return_npc_position(int npc_id);

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

bool is_agro_near(int a, int b)
{
    if (players[b]->get_tribe() != BOSS) return false;
    if (AGRORANGE < abs(players[a]->get_x() - players[b]->get_x())) return false;
    if (AGRORANGE < abs(players[a]->get_y() - players[b]->get_y())) return false;
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

bool check_move_alright(int x, int y)
{
    for (auto& ob : obstacles) {
        if (ob.x == x && ob.y == y) {
            cout << "��ֹ� : " << ob.x << ob.y << endl;
            cout << "�̵� ���� : " << x << y << endl;
            return false;
        }
    }
    return true;
}

void send_login_ok_packet(int c_id)
{
    sc_packet_login_ok packet;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_LOGIN_OK;
    packet.id = c_id;
    strcpy_s(packet.name, players[c_id]->get_name());
    packet.x = players[c_id]->get_x();
    packet.y = players[c_id]->get_y();
    packet.level = players[c_id]->get_lv();
    packet.hp = players[c_id]->get_hp();
    packet.maxhp = players[c_id]->get_maxhp();
    packet.exp = players[c_id]->get_exp();
    packet.tribe = players[c_id]->get_tribe();
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

void send_login_fail_packet(int c_id, int reason)
{
    sc_packet_login_fail packet;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_LOGIN_FAIL;
    packet.reason = reason;
    reinterpret_cast<Player*>(players[c_id])->do_send(sizeof(packet), &packet);
}

void send_status_change_packet(int c_id)
{
    sc_packet_status_change packet;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_STATUS_CHANGE;
    packet.level = players[c_id]->get_lv();
    packet.hp = players[c_id]->get_hp();
    packet.maxhp = players[c_id]->get_maxhp();
    packet.exp = players[c_id]->get_exp();
    reinterpret_cast<Player*>(players[c_id])->do_send(sizeof(packet), &packet);
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
        if (true == is_npc(target->get_Id())) break;   // npc�� ��� ������ �ʴ´�
        if (ST_INGAME != target->get_state()) continue;
        target->vl.lock();
        if (0 != target->viewlist.count(c_id)) {
            target->viewlist.erase(c_id);
            target->vl.unlock();
            send_remove_object(other, c_id);
        }
        else target->vl.unlock();
    }
    Save_position(pl);
    players[c_id]->state_lock.lock();
    closesocket(reinterpret_cast<Player*>(players[c_id])->_socket);
    players[c_id]->set_state(ST_FREE);
    players[c_id]->state_lock.unlock();
}

// ��ũ��Ʈ �߰�
void Activate_Npc_Move_Event(int target, int player_id)
{
    EXP_OVER* exp_over = new EXP_OVER;
    exp_over->_comp_op = OP_NPC_MOVE;
    exp_over->_target = player_id;
    PostQueuedCompletionStatus(g_h_iocp, 1, target, &exp_over->_wsa_over);
}

void attack_success(int p_id, int target)
{
    int damage = (players[p_id]->get_lv() * 10);
    int target_hp = players[target]->get_hp() - damage;
    players[target]->set_hp(target_hp);
    cout << target << "�� ���ݹ��� ���� hp : " << players[target]->get_hp() << endl;
    if (target_hp <= 0) {
        players[target]->state_lock.lock();
        players[target]->set_state(ST_DEAD);
        players[target]->state_lock.unlock();
        if (target < NPC_ID_START) { 
            players[p_id]->set_active(false);
            players[p_id]->state_lock.lock();
            players[p_id]->set_state(ST_DEAD);
            players[p_id]->state_lock.unlock();
            players[p_id]->set_exp(players[p_id]->get_exp() / 2);
            // �������� �÷��̾��� �׾��ٴ� ��Ŷ�� �����ش�
            sc_packet_dead packet;
            packet.size = sizeof(packet);
            packet.type = SC_PACKET_DEAD;
            packet.attacker_id = p_id;
            reinterpret_cast<Player*>(players[target])->do_send(sizeof(packet), &packet);
            // 3���� ��Ȱ�ϸ� ��Ȱ�� ���ÿ� ��ġ ��ǥ�� �������ش�
            timer_event ev;
            ev.obj_id = target;
            ev.start_time = chrono::system_clock::now() + 3s;
            ev.ev = EVENT_PLAYER_REVIVE;
            ev.target_id = 0;
            timer_queue.push(ev);
        }
        else {  // NPC��� 30�� �Ŀ� ��Ȱ�� �� �ֵ��� ����
            players[target]->set_active(false);
            timer_event ev;
            ev.obj_id = target;
            ev.start_time = chrono::system_clock::now() + 30s;
            ev.ev = EVENT_NPC_REVIVE;
            ev.target_id = 0;
            timer_queue.push(ev);

            // �÷��̾�� ����ġ ����, �׸��� �ٲ� ����ġ�� ������ ��������
            int get_exp = players[target]->get_lv() * players[target]->get_lv() * 2;
            char mess[MAX_CHAT_SIZE];
            sprintf_s(mess, MAX_CHAT_SIZE, "%s�� ���񷯼� %d�� ����ġ�� ������ϴ�",
                 players[target]->get_name(), get_exp);
            send_chat_packet(p_id, p_id, mess);
            int max_exp = 100 * pow(2, (players[p_id]->get_lv() - 1));
            if (players[p_id]->get_exp() + get_exp >= max_exp) {
                players[p_id]->set_lv(players[p_id]->get_lv() + 1);
                players[p_id]->set_exp(get_exp - max_exp);
                sprintf_s(mess, MAX_CHAT_SIZE, "%d������ ������ �Ͽ����ϴ�",
                    players[p_id]->get_lv());
                send_chat_packet(p_id, p_id, mess);
            }
            else {
                players[p_id]->set_exp(get_exp);
            }
            send_status_change_packet(p_id);
        }
        // ���� target ������ �÷��̾�� ������� ������
        unordered_set <int> nearlist;
        for (auto& other : players) {
            // if (other._id == client_id) continue;
            if (false == is_near(players[target]->get_Id(), other->get_Id()))
                continue;
            if (ST_INGAME != other->get_state())
                continue;
            if (other->get_tribe() != HUMAN) break;
            nearlist.insert(other->get_Id());
        }
        nearlist.erase(target);
        for (auto other : nearlist) {
            Player* other_player = reinterpret_cast<Player*>(players[other]);
            other_player->vl.lock();
            if (0 != other_player->viewlist.count(target)) {
                other_player->viewlist.erase(target);
                other_player->vl.unlock();
                send_remove_object(other, target);
            }
            else other_player->vl.unlock();
        }
    }
    else if(p_id >= NPC_ID_START){
        // �÷��̾ ������ ���� ���̹Ƿ� hp������ �ٲ�����Ƿ� �װ��� ��������
        send_status_change_packet(target);
        char mess[MAX_CHAT_SIZE];
        sprintf_s(mess, MAX_CHAT_SIZE, "%s�� %s�� �������� %d�� �������� �Ծ����ϴ�",
            players[p_id]->get_name(), players[target]->get_name(), damage);
        send_chat_packet(target, target, mess);


        // hp�� ���̿����Ƿ� hp�ڵ�ȸ���� ���ֵ��� ����
        timer_event ev;
        ev.obj_id = target;
        ev.start_time = chrono::system_clock::now() + 5s;
        ev.ev = EVENT_AUTO_PLAYER_HP;
        ev.target_id = 0;
        timer_queue.push(ev);

        // npc�����̸� Ÿ�̸� ť�� �ٽ� �־�����
        ev;
        ev.obj_id = p_id;
        ev.start_time = chrono::system_clock::now() + 3s;
        ev.ev = EVENT_NPC_ATTACK;
        ev.target_id = target;
        timer_queue.push(ev);
    }
    else {  // �÷��̾ ������ ����
        char mess[MAX_CHAT_SIZE];
        sprintf_s(mess, MAX_CHAT_SIZE, "%s�� %s�� �������� %d�� �������� �������ϴ�",
            players[p_id]->get_name(), players[target]->get_name(), damage);
        send_chat_packet(p_id, p_id, mess);
    }
}

void process_packet(int client_id, unsigned char* p)
{
    unsigned char packet_type = p[1];
    Player* pl = reinterpret_cast<Player*>(players[client_id]);
    switch (packet_type) {
    case CS_PACKET_LOGIN: {
        cs_packet_login* packet = reinterpret_cast<cs_packet_login*>(p);

        // pl->set_name(packet->name);
        if (!(Search_Id(pl, packet->name))) {
            send_login_fail_packet(client_id, 0);   // ���̵� ����
            Disconnect(client_id);
            return;
        }

        // �ߺ� ���̵� �˻�
        for (auto* p : players) {
            if (p->get_tribe() != HUMAN) break;
            if (p->get_state() == ST_FREE) continue;  
            if (p->get_Id() == client_id) continue;
            if (reinterpret_cast<Player*>(p)->get_login_id() == pl->get_login_id()) {
                send_login_fail_packet(client_id, 1);   // �ߺ� �α���
                Disconnect(client_id);
                return;
            }
        }

        if (pl->get_hp() <= pl->get_maxhp()) {
            // hp�� ���̿����Ƿ� hp�ڵ�ȸ���� ���ֵ��� ����
            timer_event ev;
            ev.obj_id = client_id;
            ev.start_time = chrono::system_clock::now() + 5s;
            ev.ev = EVENT_AUTO_PLAYER_HP;
            ev.target_id = 0;
            timer_queue.push(ev);
        }

        send_login_ok_packet(client_id);
        pl->state_lock.lock();
        pl->set_state(ST_INGAME);
        pl->state_lock.unlock();

        // ���� ������ ������ �ٸ��̿��� ������
        for (auto& other : players) {
            if (other->get_Id() == client_id) continue;   // ����

            if (true == is_npc(other->get_Id())) break;// ���� ���� �ִ� ���� NPC�� �ִٸ�

            other->state_lock.lock();
            if (ST_INGAME != other->get_state()) {
                other->state_lock.unlock();
                continue;
            }
            other->state_lock.unlock();

            if (false == is_near(other->get_Id(), client_id)) continue;

            // ����� �÷��̾� ó��
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

        // ���� ������ �÷��̾�� ���� ������ ������
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

            // ��ũ��Ʈ�� �Բ� �߰��� �κ�
            if (true == is_npc(other->get_Id())) {	// �þ߿� npc�� �ִٸ� 
                if (is_agro_near(client_id, other->get_Id())) {
                    Activate_Npc_Move_Event(other->get_Id(), pl->get_Id());
                }
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
        if (check_move_alright(x, y) == false) {
            break;
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
            //��ũ��Ʈ �߰�
            if (true == is_npc(other->get_Id())) {
                if (is_agro_near(client_id, other->get_Id())) {
                    Activate_Npc_Move_Event(other->get_Id(), pl->get_Id());
                }
            }
            nearlist.insert(other->get_Id());
        }
        nearlist.erase(client_id);  // �� ���̵�� ������ ���� �װ��� ��������


        send_move_packet(pl->get_Id(), pl->get_Id()); // �� �ڽ��� �������� ���� ��������

        pl->vl.lock();
        unordered_set <int> my_vl{ pl->viewlist };
        pl->vl.unlock();

        // ���νþ߿� ���� �÷��̾� ó��
        for (auto other : nearlist) {
            if (0 == my_vl.count(other)) {   // ���� ���� �÷��̾�/npc
                pl->vl.lock();
                pl->viewlist.insert(other);
                pl->vl.unlock();
                send_put_object(pl->get_Id(), other);

                //if (true == is_npc(other)) {   // ���� ���� npc�� �����̱� ����
                //    timer_event ev;
                //    ev.obj_id = other;
                //    ev.start_time = chrono::system_clock::now() + 1s;
                //    ev.ev = EVENT_NPC_MOVE;
                //    ev.target_id = client_id;
                //    timer_queue.push(ev);
                //    continue;
                //}
                // ��ũ��Ʈ �߰�
                if (true == is_npc(other)) break;

                // ����� �÷��̾� ó���̴�.
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
            // ��� �þ߿� �����ϴ� �÷��̾� ó��
            else {
                if (true == is_npc(other)) continue;   // ���� �ִ� npc�� npc_move���� ó��

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
        // �þ߿��� ����� �÷��̾� ó��
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
        break;
    }
    case CS_PACKET_ATTACK: {
        // cs_packet_attack* packet = reinterpret_cast<cs_packet_attack*>(p);
        // �÷��̾ �����ϰ� �ݰ� 1ĭ �̳��� ���Ͱ� �ִٸ� ����
        if (pl->get_attack_active()) break;
        cout << "����" << endl;
        pl->set_attack_active(true);
        int range_max_x = pl->get_x() + 1;
        int range_min_x = pl->get_x() - 1;
        int range_max_y = pl->get_y() + 1;
        int range_min_y = pl->get_y() - 1;
        for (int i = NPC_ID_START; i <= NPC_ID_END; ++i) {
            if (players[i]->get_state() != ST_INGAME) continue;
            if (players[i]->get_x() == pl->get_x()) {
                if (players[i]->get_y() >= range_min_y && players[i]->get_y() <= range_max_y) {
                    attack_success(client_id, players[i]->get_Id());    // ������ ���
                    // ������ �ڵ������� �־�����
                    if (players[i]->get_active() == false) {
                        players[i]->set_active(true);
                        timer_event ev;
                        ev.obj_id = i;
                        ev.start_time = chrono::system_clock::now() + 3s;
                        ev.ev = EVENT_NPC_ATTACK;
                        ev.target_id = client_id;
                        timer_queue.push(ev);
                    }
                    // ������ �̵��� �־�����
                    Activate_Npc_Move_Event(i, pl->get_Id());
                }
            }
            else if (players[i]->get_y() == pl->get_y()) {
                if (players[i]->get_x() >= range_min_y && players[i]->get_x() <= range_max_y) {
                    attack_success(client_id, players[i]->get_Id());    // ������ ���
                    // ������ �ڵ������� �־�����
                    if (players[i]->get_active() == false) {
                        players[i]->set_active(true);
                        timer_event ev;
                        ev.obj_id = i;
                        ev.start_time = chrono::system_clock::now() + 3s;
                        ev.ev = EVENT_NPC_ATTACK;
                        ev.target_id = client_id;
                        timer_queue.push(ev);
                    }
                    // ������ �̵��� �־�����
                    Activate_Npc_Move_Event(i, pl->get_Id());
                }
            }
        }
        timer_event ev;
        ev.obj_id = client_id;
        ev.start_time = chrono::system_clock::now() + 1s;
        ev.ev = EVENT_PLAYER_ATTACK;
        ev.target_id = 0;
        timer_queue.push(ev);
        break;
    }
    }
}

void player_revive(int client_id)
{
    Player* pl = reinterpret_cast<Player*>(players[client_id]);
    pl->state_lock.lock();
    pl->set_state(ST_INGAME);
    pl->state_lock.unlock();
    pl->set_hp(players[client_id]->get_maxhp());
    pl->set_x(500);
    pl->set_y(500);

    sc_packet_revive packet;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_REVIVE;
    packet.x = pl->get_x();
    packet.y = pl->get_y();
    packet.hp = pl->get_hp();
    packet.exp = pl->get_exp();
    pl->do_send(sizeof(packet), &packet);

    // �ֺ��� �ִ� ��鿡�� �þ�ó�� ���־����
    pl->vl.lock();
    pl->viewlist.clear();
    pl->vl.unlock();
    for (auto& other : players) {
        if (other->get_Id() == client_id) continue;   // ����
        if (true == is_npc(other->get_Id())) break;// ���� ���� �ִ� ���� NPC�� �ִٸ�
        other->state_lock.lock();
        if (ST_INGAME != other->get_state()) {
            other->state_lock.unlock();
            continue;
        }
        other->state_lock.unlock();
        if (false == is_near(other->get_Id(), client_id)) continue;

        // ����� �÷��̾� ó��
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

    // ���� ������ �÷��̾�� ���� ������ ������
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

        // ��ũ��Ʈ�� �Բ� �߰��� �κ�
        if (true == is_npc(other->get_Id())) {	// �þ߿� npc�� �ִٸ� 
            if (is_agro_near(client_id, other->get_Id())) {
                Activate_Npc_Move_Event(other->get_Id(), pl->get_Id());
            }
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
            if (pl->get_state() == ST_FREE) continue;
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
            // ��� �̵��ϴ��� �ƴ����� �־��־���Ѵ�
            if (exp_over->_target == -1) {
                return_npc_position(client_id);
                delete exp_over;
                break;
            }
            players[client_id]->lua_lock.lock();
            lua_State* L = players[client_id]->L;
            lua_getglobal(L, "event_npc_move");
            lua_pushnumber(L, exp_over->_target);
            int error = lua_pcall(L, 1, 1, 0);
            if (error != 0) {
                cout << "LUA_NPC_MOVE ERROR" << endl;
            }
            // bool���� ������ ������ 
            // true�� �i�ư��� 
            bool m = lua_toboolean(L, -1);
            lua_pop(L, 2);
            if (m) {
                do_npc_move(client_id, exp_over->_target);
            }
            else {
                // ���� �ڸ��� ���ư���
                return_npc_position(client_id);
            }
            players[client_id]->lua_lock.unlock();
            delete exp_over;
            break;
        }
        case OP_NPC_ATTACK: {
            players[client_id]->lua_lock.lock();
            lua_State* L = players[client_id]->L;
            lua_getglobal(L, "attack_range");
            cout << exp_over->_target << endl;
            lua_pushnumber(L, exp_over->_target);
            int error = lua_pcall(L, 1, 1, 0);
            if (error != 0) {
                cout << "LUA ATTACK RANGE ERROR" << endl;
                cout << lua_tostring(L, -1) << endl;
            }
            bool m = false;
            m = lua_toboolean(L, -1);
            lua_pop(L, 2);
            cout << m << endl;
            if (m) {
                // ����ó��
                cout << "NPC ����" << endl;
                attack_success(client_id, exp_over->_target);
            }
            else {
                cout << "���ݽ���" << endl;
                if (players[client_id]->get_active()) {
                    // ������ ���������� ���(�׷����� 1����) ���ݽõ�
                    timer_event ev;
                    ev.obj_id = client_id;
                    ev.start_time = chrono::system_clock::now() + 1s;
                    ev.ev = EVENT_AUTO_PLAYER_HP;
                    ev.target_id = exp_over->_target;
                    timer_queue.push(ev);
                }
            }
            players[client_id]->lua_lock.unlock();
            delete exp_over;
            break;
        }
        case OP_AUTO_PLAYER_HP: {
            Player* pl = reinterpret_cast<Player*>(players[client_id]);
            pl->set_hp(pl->get_hp() + (pl->get_maxhp()*0.1));
            if (pl->get_hp() >= pl->get_maxhp())
                pl->set_hp(pl->get_maxhp());
            else {
                timer_event ev;
                ev.obj_id = client_id;
                ev.start_time = chrono::system_clock::now() + 5s;
                ev.ev = EVENT_AUTO_PLAYER_HP;
                ev.target_id = 0;
                timer_queue.push(ev);
            }
            send_status_change_packet(client_id);
            break;
        }
        case OP_PLAYER_REVIVE: {
            player_revive(client_id);
            break;
        }
        case OP_NPC_REVIVE: {
            cout << "NPC ��Ȱ" << endl;
            // ���� �ٲ��ְ�
            players[client_id]->state_lock.lock();
            players[client_id]->set_state(ST_INGAME);
            players[client_id]->state_lock.unlock();
            // NPC�� ���� ��������
            players[client_id]->lua_lock.lock();
            lua_State* L = players[client_id]->L;
            lua_getglobal(L, "set_uid");
            lua_pushnumber(L, client_id);
            int error = lua_pcall(L, 1, 3, 0);
            if (error != 0) {
                //cout << "ERROR : " << lua_tostring(L, -1);
                cout << "�ʱ�ȭ ����" << endl;
            }

            players[client_id]->set_lv(lua_tointeger(L, -3));
            players[client_id]->set_hp(lua_tointeger(L, -2));
            players[client_id]->set_name(lua_tostring(L, -1));
            lua_pop(L, 4);// eliminate set_uid from stack after call
            players[client_id]->lua_lock.unlock();
            // ��Ȱ�ϴ� NPC�ֺ� ��鿡�� ���̰� ������
            unordered_set <int> nearlist;
            for (auto& other : players) {
                // if (other._id == client_id) continue;
                if (false == is_near(players[client_id]->get_Id(), other->get_Id()))
                    continue;
                if (ST_INGAME != other->get_state())
                    continue;
                if (other->get_tribe() != HUMAN) break;
                nearlist.insert(other->get_Id());
            }
            for (auto other : nearlist) {
                Player* other_player = reinterpret_cast<Player*>(players[other]);
                other_player->vl.lock();
                other_player->viewlist.insert(client_id);
                other_player->vl.unlock();
                send_put_object(other, client_id);
            }
            break;
        }
        }
    }
}

// ��ũ��Ʈ API
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
    cout << "NPC �ε���" << endl;
    char name[MAX_NAME_SIZE];
    for (int i = NPC_ID_START; i < NPC_ID_END-10; ++i) {
        sprintf_s(name, "NPC %d", i);
        players[i] = new Npc(i, name);
        lua_State* L = players[i]->L = luaL_newstate();
        luaL_openlibs(L);
        int error = luaL_loadfile(L, "monster.lua") ||
            lua_pcall(L, 0, 0, 0);

        lua_getglobal(L, "set_uid");
        lua_pushnumber(L, i);
        lua_pushnumber(L, players[i]->get_x());
        lua_pushnumber(L, players[i]->get_y());
        error = lua_pcall(L, 3, 3, 0);

        if (error != 0) {
            //cout << "ERROR : " << lua_tostring(L, -1);
            cout << "�ʱ�ȭ ����" << endl;
        }

        players[i]->set_tribe(MONSTER);
        players[i]->set_lv(lua_tointeger(L, -3));
        players[i]->set_hp(lua_tointeger(L, -2));
        players[i]->set_name(lua_tostring(L, -1));
        lua_pop(L, 4);// eliminate set_uid from stack after call

        lua_register(L, "API_SendMessage", API_SendMessage);
        lua_register(L, "API_get_x", API_get_x);
        lua_register(L, "API_get_y", API_get_y);

    }
    for (int i = NPC_ID_END - 10; i <= NPC_ID_END; ++i) {
        sprintf_s(name, "NPC %d", i);
        players[i] = new Npc(i, name);

        lua_State* L = players[i]->L = luaL_newstate();
        luaL_openlibs(L);
        int error = luaL_loadfile(L, "monster2.lua") ||
            lua_pcall(L, 0, 0, 0);
        lua_getglobal(L, "set_uid");
        lua_pushnumber(L, i);
        error = lua_pcall(L, 1, 3, 0);

        if (error != 0) {
            //cout << "ERROR : " << lua_tostring(L, -1);
            cout << "�ʱ�ȭ ����" << endl;
        }
        players[i]->set_tribe(BOSS);
        players[i]->set_lv(lua_tointeger(L, -3));
        players[i]->set_hp(lua_tointeger(L, -2));
        players[i]->set_name(lua_tostring(L, -1));
        lua_pop(L, 4);// eliminate set_uid from stack after call

        lua_register(L, "API_SendMessage", API_SendMessage);
        lua_register(L, "API_get_x", API_get_x);
        lua_register(L, "API_get_y", API_get_y);

    }

    cout << "NPC�ε� �Ϸ�" << endl;
}

void return_npc_position(int npc_id)
{
    cout << "���ư���" << endl;
    unordered_set<int> old_viewlist;
    unordered_set<int> new_viewlist;
    for (auto& obj : players) {
        if (obj->get_state() != ST_INGAME) continue;
        // if (true == is_npc(obj._id)) continue;   // npc�� �ƴҶ�
        if (true == is_npc(obj->get_Id())) break;   // npc�� �ƴҶ�
        if (true == is_near(npc_id, obj->get_Id())) {      // ��ó�� ������
            old_viewlist.insert(obj->get_Id());         // npc��ó�� �÷��̾ ������ old_viewlist�� �÷��̾� id�� �ִ´�
        }
    }

    if (old_viewlist.size() == 0) return;


    // ���� �ڸ��� ���ư���
    lua_State* L = players[npc_id]->L;
    lua_getglobal(L, "return_my_position");
    int error = lua_pcall(L, 0, 2, 0);
    if (error != 0) {
        cout << "LUA_RETURN_MY_POSITION ERROR" << endl;
    }
    int my_x = lua_tointeger(L, -2);
    int my_y = lua_tointeger(L, -1);
    lua_pop(L, 3);

    int now_x = players[npc_id]->get_x();
    int now_y = players[npc_id]->get_y();
    bool my_pos_fail = true;
    if (my_x != now_x) {
        if (my_x >= now_x) now_x = now_x + 1;
        else now_x = now_x - 1;
    }
    else if (my_y != now_y) {
        if (my_y >= now_y) now_y = now_y + 1;
        else now_y = now_y - 1;
    }
    else my_pos_fail = false;

    if (false == check_move_alright(now_x, now_y)) {
        return;
    }
    cout << "���� �ڸ� (" << players[npc_id]->get_x() << ", " <<
        players[npc_id]->get_y() << ")" << endl;
    cout << "�̵� �ڸ� (" << now_x << ", " <<
        now_y << ")" << endl;
    players[npc_id]->set_x(now_x);
    players[npc_id]->set_y(now_y);


    for (auto& obj : players) {
        if (obj->get_state() != ST_INGAME) continue;   // in game�� �ƴҶ�
        //if (true == is_npc(obj._id)) continue;   // npc�� �ƴҶ� -> ingame���� �÷��̾� ã��
        if (true == is_npc(obj->get_Id())) break;   // npc�� �ƴҶ� -> ingame���� �÷��̾� ã��
        if (true == is_near(npc_id, obj->get_Id())) {
            new_viewlist.insert(obj->get_Id());
        }
    }

    int temp = 0;
    for (auto pl : new_viewlist) {
        // ���� �þ߿� ���� �÷��̾�
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

    // �þ߿� ����� ���
    for (auto pl : old_viewlist) {
        if (0 == new_viewlist.count(pl)) {
            reinterpret_cast<Player*>(players[pl])->vl.lock();
            reinterpret_cast<Player*>(players[pl])->viewlist.erase(npc_id);
            reinterpret_cast<Player*>(players[pl])->vl.unlock();
            send_remove_object(pl, npc_id);
        }
    }

    if (my_pos_fail) {    // �� �������ߵ�
        timer_event ev;
        ev.obj_id = npc_id;
        ev.start_time = chrono::system_clock::now() + 1s;
        ev.ev = EVENT_NPC_MOVE;
        ev.target_id = -1;
        timer_queue.push(ev);
    }
}

void do_npc_move(int npc_id, int target)
{
    unordered_set<int> old_viewlist;
    unordered_set<int> new_viewlist;
    for (auto& obj : players) {
        if (obj->get_state() != ST_INGAME) continue;
        // if (true == is_npc(obj._id)) continue;   // npc�� �ƴҶ�
        if (true == is_npc(obj->get_Id())) break;   // npc�� �ƴҶ�
        if (true == is_near(npc_id, obj->get_Id())) {      // ��ó�� ������
            old_viewlist.insert(obj->get_Id());         // npc��ó�� �÷��̾ ������ old_viewlist�� �÷��̾� id�� �ִ´�
        }
    }

    if (old_viewlist.size() == 0) return;


    int x = players[npc_id]->get_x();
    int y = players[npc_id]->get_y();
    int t_x = players[target]->get_x();
    int t_y = players[target]->get_y();
    
    // ������ ���⿡ A*�˰����� �־�� �Ѵ�
    if (t_x != x) {
        if (t_x > x) x++;
        else x--;
    }
    else if(t_y != y){
        if (t_y > y) y++;
        else y--;
    }
    
    if (false == check_move_alright(x, y)) {
        return;
    }
    players[npc_id]->set_x(x);
    players[npc_id]->set_y(y);


    for (auto& obj : players) {
        if (obj->get_state() != ST_INGAME) continue;   // in game�� �ƴҶ�
        //if (true == is_npc(obj._id)) continue;   // npc�� �ƴҶ� -> ingame���� �÷��̾� ã��
        if (true == is_npc(obj->get_Id())) break;   // npc�� �ƴҶ� -> ingame���� �÷��̾� ã��
        if (true == is_near(npc_id, obj->get_Id())) {
            new_viewlist.insert(obj->get_Id());
        }
    }

    int temp = 0;
    for (auto pl : new_viewlist) {
        // ���� �þ߿� ���� �÷��̾�
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

    // �þ߿� ����� ���
    for (auto pl : old_viewlist) {
        if (0 == new_viewlist.count(pl)) {
            reinterpret_cast<Player*>(players[pl])->vl.lock();
            reinterpret_cast<Player*>(players[pl])->viewlist.erase(npc_id);
            reinterpret_cast<Player*>(players[pl])->vl.unlock();
            send_remove_object(pl, npc_id);
        }
    }

    timer_event ev;
    ev.obj_id = npc_id;
    ev.start_time = chrono::system_clock::now() + 1s;
    ev.ev = EVENT_NPC_MOVE;
    ev.target_id = temp;
    timer_queue.push(ev);
}

COMP_OP EVtoOP(EVENT_TYPE ev) {
    switch (ev)
    {
    case EVENT_NPC_MOVE:
        return OP_NPC_MOVE;
        break;
    case EVENT_NPC_ATTACK:
        return OP_NPC_ATTACK;
        break;
    case EVENT_AUTO_PLAYER_HP:
        return OP_AUTO_PLAYER_HP;
        break;
    case EVENT_PLAYER_REVIVE:
        return OP_PLAYER_REVIVE;
        break;
    case EVENT_NPC_REVIVE:
        return OP_NPC_REVIVE;
        break;
    }
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
            if (temp.ev == EVENT_PLAYER_ATTACK) {
                cout << "ó����" << endl;
                reinterpret_cast<Player*>(players[temp.obj_id])->set_attack_active(false);
                continue;
            }
            EXP_OVER* ex_over = new EXP_OVER;
            ex_over->_comp_op = EVtoOP(temp.ev);
            ex_over->_target = temp.target_id;
            PostQueuedCompletionStatus(g_h_iocp, 1, temp.obj_id, &ex_over->_wsa_over);   //0�� ��������� ����
        }

        while (true) {
            timer_event ev;
            if (timer_queue.size() == 0) break;
            timer_queue.try_pop(ev);

            dura = ev.start_time - chrono::system_clock::now();
            if (dura <= 0ms) {
                EXP_OVER* ex_over = new EXP_OVER;
                if (ev.ev == EVENT_PLAYER_ATTACK) {
                    cout << "ó����" << endl;
                    reinterpret_cast<Player*>(players[ev.obj_id])->set_attack_active(false);
                    continue;
                }
                ex_over->_comp_op = EVtoOP(ev.ev);
                ex_over->_target = ev.target_id;
                PostQueuedCompletionStatus(g_h_iocp, 1, ev.obj_id, &ex_over->_wsa_over);   //0�� ��������� ����
            }
            else if (dura <= waittime) {
                temp = ev;
                temp_bool = true;
                break;
            }
            else {
                timer_queue.push(ev);   // Ÿ�̸� ť�� ���� �ʰ� ����ȭ �ʿ�
            }
        }
        this_thread::sleep_for(dura);
        // �� �翩�־ ��� ó���� �ϵ��� �ؾ���
    }
}

int main()
{
    setlocale(LC_ALL, "korean");
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
        players[i] = new Player(i);
    }

    Initialise_DB();
    initialise_NPC();

    for (auto& ob : obstacles) {
        ob.x = rand() % WORLD_WIDTH;
        ob.y = rand() % WORLD_HEIGHT;
    }

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
    Disconnect_DB();
}
