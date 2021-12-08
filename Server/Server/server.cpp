#include "stdafx.h"
#include "Player.h"
#include "database.h"
#include "send.h"

class obstacle {
public:
    int id;
    TRIBE tribe;
    short x;
    short y;
    obstacle() {
        tribe = OBSTACLE;
    }
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
    /*     target_id
    스킬 관련 쿨타임의 경우 : 어떤 스킬인지 넣어줌
    */
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
            cout << "장애물 : " << ob.x << ob.y << endl;
            cout << "이동 에상 : " << x << y << endl;
            return false;
        }
    }
    return true;
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
            send_remove_object_packet(target, players[c_id]);
        }
        else target->vl.unlock();
    }
    Save_position(pl);
    players[c_id]->state_lock.lock();
    closesocket(reinterpret_cast<Player*>(players[c_id])->_socket);
    players[c_id]->set_state(ST_FREE);
    players[c_id]->state_lock.unlock();
}

// 스크립트 추가
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
    cout << "데미지 : " << damage << endl;
    cout << "맞기전 hp : " << players[target]->get_hp() << endl;
    int target_hp = players[target]->get_hp() - damage;
    players[target]->set_hp(target_hp);
    cout << players[target]->get_name() << "이 공격받음 남은 hp : " << players[target]->get_hp() << endl;
    if (target_hp <= 0) {
        players[target]->state_lock.lock();
        players[target]->set_state(ST_DEAD);
        players[target]->state_lock.unlock();
        if (target < NPC_ID_START) {    // 죽은게 플레이어이다
            players[p_id]->set_active(false);
            // 죽은것이 플레이어라면 죽었다는 패킷을 보내준다
            sc_packet_dead packet;
            packet.size = sizeof(packet);
            packet.type = SC_PACKET_DEAD;
            packet.attacker_id = p_id;
            reinterpret_cast<Player*>(players[target])->do_send(sizeof(packet), &packet);
            // 3초후 부활하며 부활과 동시에 위치 좌표를 수정해준다
            timer_event ev;
            ev.obj_id = target;
            ev.start_time = chrono::system_clock::now() + 3s;
            ev.ev = EVENT_PLAYER_REVIVE;
            ev.target_id = 0;
            timer_queue.push(ev);
        }
        else {  // NPC라면 30초 후에 부활할 수 있도록 하자
            players[target]->set_active(false);
            timer_event ev;
            ev.obj_id = target;
            ev.start_time = chrono::system_clock::now() + 30s;
            ev.ev = EVENT_NPC_REVIVE;
            ev.target_id = 0;
            timer_queue.push(ev);

            // 플레이어에게 경험치 제공, 그리고 바뀐 경험치와 레벨을 보내주자
            int get_exp = players[target]->get_lv() * players[target]->get_lv() * 2;
            if (players[target]->get_tribe() == BOSS)
                get_exp = get_exp * 2;
            char mess[MAX_CHAT_SIZE];
            sprintf_s(mess, MAX_CHAT_SIZE, "%s를 무찔러서 %d의 경험치를 얻었습니다",
                 players[target]->get_name(), get_exp);
            send_chat_packet(reinterpret_cast<Player*>(players[p_id]), p_id, mess);
            send_status_change_packet(reinterpret_cast<Player*>(players[p_id]));
            int max_exp = 100 * pow(2, (players[p_id]->get_lv() - 1));
            if (players[p_id]->get_exp() + get_exp >= max_exp) {
                players[p_id]->set_lv(players[p_id]->get_lv() + 1);
                players[p_id]->set_exp(players[p_id]->get_exp()+ get_exp - max_exp);
                sprintf_s(mess, MAX_CHAT_SIZE, "%d레벨로 레벨업 하였습니다",
                    players[p_id]->get_lv());
                send_chat_packet(reinterpret_cast<Player*>(players[p_id]), p_id, mess);
                send_status_change_packet(reinterpret_cast<Player*>(players[p_id]));
            }
            else {
                players[p_id]->set_exp(players[p_id]->get_exp() + get_exp);
            }
            send_status_change_packet(reinterpret_cast<Player*>(players[p_id]));
        }
        // 죽은 target 주위의 플레이어에게 사라지게 해주자
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
                send_remove_object_packet(other_player, players[target]);
            }
            else other_player->vl.unlock();
        }
    }
    else if(p_id >= NPC_ID_START){
        // 플레이어가 공격을 당한 것이므로 hp정보가 바뀌었으므로 그것을 보내주자
        send_status_change_packet(reinterpret_cast<Player*>(players[target]));
        char mess[MAX_CHAT_SIZE];
        sprintf_s(mess, MAX_CHAT_SIZE, "%s가 %s를 공격으로 %d의 데미지를 입었습니다",
            players[p_id]->get_name(), players[target]->get_name(), damage);
        send_chat_packet(reinterpret_cast<Player*>(players[target]), target, mess);


        // hp가 깎이였으므로 hp자동회복을 해주도록 하자
        timer_event ev;
        ev.obj_id = target;
        ev.start_time = chrono::system_clock::now() + 5s;
        ev.ev = EVENT_AUTO_PLAYER_HP;
        ev.target_id = 0;
        timer_queue.push(ev);

        // npc공격이면 타이머 큐에 다시 넣어주자
        ev;
        ev.obj_id = p_id;
        ev.start_time = chrono::system_clock::now() + 3s;
        ev.ev = EVENT_NPC_ATTACK;
        ev.target_id = target;
        timer_queue.push(ev);
    }
    else {  // 플레이어가 공격을 입힘
        char mess[MAX_CHAT_SIZE];
        sprintf_s(mess, MAX_CHAT_SIZE, "%s가 %s를 공격으로 %d의 데미지를 입혔습니다",
            players[p_id]->get_name(), players[target]->get_name(), damage);
        send_chat_packet(reinterpret_cast<Player*>(players[p_id]), p_id, mess);
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
            send_login_fail_packet(pl, 0);   // 아이디 없음
            Disconnect(client_id);
            return;
        }

        // 중복 아이디 검사
        for (auto* p : players) {
            if (p->get_tribe() != HUMAN) break;
            if (p->get_state() == ST_FREE) continue;
            if (p->get_Id() == client_id) continue;
            if (reinterpret_cast<Player*>(p)->get_login_id() == pl->get_login_id()) {
                send_login_fail_packet(pl, 1);   // 중복 로그인
                Disconnect(client_id);
                return;
            }
        }

        if (pl->get_hp() <= pl->get_maxhp()) {
            // hp가 깎이였으므로 hp자동회복을 해주도록 하자
            timer_event ev;
            ev.obj_id = client_id;
            ev.start_time = chrono::system_clock::now() + 5s;
            ev.ev = EVENT_AUTO_PLAYER_HP;
            ev.target_id = 0;
            timer_queue.push(ev);
        }

        send_login_ok_packet(pl);
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

            // 여기는 플레이어 처리
            Player* other_player = reinterpret_cast<Player*>(other);
            other_player->vl.lock();
            other_player->viewlist.insert(client_id);
            other_player->vl.unlock();
            sc_packet_put_object packet;
            packet.id = client_id;
            strcpy_s(packet.name, pl->get_name());
            packet.object_type = pl->get_tribe();
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
                if (is_agro_near(client_id, other->get_Id())) {
                    if (other->get_active() == false) {
                        other->set_active(true);
                        timer_event ev;
                        ev.obj_id = other->get_Id();
                        ev.start_time = chrono::system_clock::now() + 1s;
                        ev.ev = EVENT_NPC_ATTACK;
                        ev.target_id = client_id;
                        timer_queue.push(ev);
                        Activate_Npc_Move_Event(other->get_Id(), pl->get_Id());
                    }
                }
            }

            pl->vl.lock();
            pl->viewlist.insert(other->get_Id());
            pl->vl.unlock();

            sc_packet_put_object packet;
            packet.id = other->get_Id();
            strcpy_s(packet.name, other->get_name());
            packet.object_type = other->get_tribe();
            packet.size = sizeof(packet);
            packet.type = SC_PACKET_PUT_OBJECT;
            packet.x = other->get_x();
            packet.y = other->get_y();
            pl->do_send(sizeof(packet), &packet);
        }
        // 장애물 정보
        for (auto& ob : obstacles) {
            if (RANGE < abs(pl->get_x() - ob.x)) continue;
            if (RANGE < abs(pl->get_y() - ob.y)) continue;

            pl->ob_vl.lock();
            pl->ob_viewlist.insert(ob.id);
            pl->ob_vl.unlock();

            sc_packet_put_object packet;
            packet.id = ob.id;
            strcpy_s(packet.name, "");
            packet.object_type = ob.tribe;
            packet.size = sizeof(packet);
            packet.type = SC_PACKET_PUT_OBJECT;
            packet.x = ob.x;
            packet.y = ob.y;
            pl->do_send(sizeof(packet), &packet);
        }

        break;
    }
    case CS_PACKET_MOVE: {
        cs_packet_move* packet = reinterpret_cast<cs_packet_move*>(p);
        // pl.last_move_time = packet->move_time;
        int x = pl->get_x();
        int y = pl->get_y();
        pl->last_move_time = packet->move_time;
        switch (packet->direction) {
        case 0: if (y > 0) y--; break;
        case 1: if (y < (WORLD_HEIGHT - 1)) y++; break;
        case 2: if (x > 0) x--; break;
        case 3: if (x < (WORLD_WIDTH - 1)) x++; break;
        default:
            cout << "Invalid move in client " << client_id << endl;
            exit(-1);
        }
        pl->direction = packet->direction;
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
            //스크립트 추가
            if (true == is_npc(other->get_Id())) {
                if (is_agro_near(client_id, other->get_Id())) {
                    if (other->get_active() == false) {
                        other->set_active(true);
                        timer_event ev;
                        ev.obj_id = other->get_Id();
                        ev.start_time = chrono::system_clock::now() + 1s;
                        ev.ev = EVENT_NPC_ATTACK;
                        ev.target_id = client_id;
                        timer_queue.push(ev);
                        Activate_Npc_Move_Event(other->get_Id(), pl->get_Id());
                    }
                }
            }
            nearlist.insert(other->get_Id());
        }
        nearlist.erase(client_id);  // 내 아이디는 무조건 들어가니 그것을 지워주자


        send_move_packet(pl, pl); // 내 자신의 움직임을 먼저 보내주자

        pl->vl.lock();
        unordered_set <int> my_vl{ pl->viewlist };
        pl->vl.unlock();

        // 새로시야에 들어온 플레이어 처리
        for (auto other : nearlist) {
            if (0 == my_vl.count(other)) {   // 원래 없던 플레이어/npc
                pl->vl.lock();
                pl->viewlist.insert(other);
                pl->vl.unlock();
                send_put_object_packet(pl, players[other]);

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
                    send_put_object_packet(other_player, pl);
                }
                else {
                    other_player->vl.unlock();
                    send_move_packet(other_player, pl);
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
                    send_put_object_packet(other_player, pl);
                }
                else {
                    other_player->vl.unlock();
                    send_move_packet(other_player, pl);
                }
            }
        }
        // 시야에서 사라진 플레이어 처리
        for (auto other : my_vl) {
            if (0 == nearlist.count(other)) {
                pl->vl.lock();
                pl->viewlist.erase(other);
                pl->vl.unlock();
                send_remove_object_packet(pl, players[other]);

                // if (true == is_npc(other)) continue;
                if (true == is_npc(other)) break;
                Player* other_player = reinterpret_cast<Player*>(players[other]);
                other_player->vl.lock();
                if (0 != other_player->viewlist.count(pl->get_Id())) {
                    other_player->viewlist.erase(pl->get_Id());
                    other_player->vl.unlock();
                    send_remove_object_packet(other_player, pl);
                }
                else other_player->vl.unlock();
            }
        }
        // 새로 생긴 장애물이 존재 가능
        // 장애물 정보

        for (auto& ob : obstacles) {
            if ((RANGE < abs(pl->get_x() - ob.x)) &&
                (RANGE < abs(pl->get_y() - ob.y))) {
                // 범위 벗어난거임(존재하던게 있으면 없애자)
                pl->ob_vl.lock();
                if (pl->ob_viewlist.count(ob.id) != 0) {
                    pl->ob_viewlist.erase(ob.id);
                    pl->ob_vl.unlock();
                    sc_packet_remove_object packet;
                    packet.size = sizeof(packet);
                    packet.type = SC_PACKET_REMOVE_OBJECT;
                    packet.id = ob.id;
                    packet.object_type = ob.tribe;
                    pl->do_send(sizeof(packet), &packet);
                    continue;
                }
                pl->ob_vl.unlock();
                continue;
            }
            // 이미 존재하는가
            pl->ob_vl.lock();
            if (pl->ob_viewlist.count(ob.id) != 0) {
                pl->ob_vl.unlock();
                continue;
            }
            pl->ob_viewlist.insert(ob.id);
            pl->ob_vl.unlock();

            sc_packet_put_object packet;
            packet.id = ob.id;
            strcpy_s(packet.name, "");
            packet.object_type = ob.tribe;
            packet.size = sizeof(packet);
            packet.type = SC_PACKET_PUT_OBJECT;
            packet.x = ob.x;
            packet.y = ob.y;
            pl->do_send(sizeof(packet), &packet);
        }

        break;
    }
    case CS_PACKET_ATTACK: {
        // cs_packet_attack* packet = reinterpret_cast<cs_packet_attack*>(p);
        // 플레이어가 공격하고 반경 1칸 이내에 몬스터가 있다면 전투
        if (pl->get_attack_active()) break;
        cout << "공격" << endl;
        pl->set_attack_active(true);

        for (int i = NPC_ID_START; i <= NPC_ID_END; ++i) {
            players[i]->state_lock.lock();
            if (players[i]->get_state() != ST_INGAME) {
                players[i]->state_lock.unlock();
                continue;
            }
            players[i]->state_lock.unlock();
            if (players[i]->get_x() == pl->get_x()) {
                if (players[i]->get_y() >= pl->get_y() - 1 && players[i]->get_y() <= pl->get_y() + 1) {
                    cout << "범위 1" << endl;
                    attack_success(client_id, players[i]->get_Id());    // 데미지 계산
                    // 몬스터의 자동공격을 넣어주자
                    if (players[i]->get_active() == false && players[i]->get_tribe() == MONSTER) {
                        players[i]->set_active(true);
                        timer_event ev;
                        ev.obj_id = i;
                        ev.start_time = chrono::system_clock::now() + 1s;
                        ev.ev = EVENT_NPC_ATTACK;
                        ev.target_id = client_id;
                        timer_queue.push(ev);
                        // 몬스터의 이동도 넣어주자
                        Activate_Npc_Move_Event(i, pl->get_Id());
                    }
                }
            }
            else if (players[i]->get_y() == pl->get_y()) {
                if (players[i]->get_x() >= pl->get_x() - 1 && players[i]->get_x() <= pl->get_x() + 1) {
                    cout << "범위 2" << endl;
                    attack_success(client_id, players[i]->get_Id());    // 데미지 계산
                    // 몬스터의 자동공격을 넣어주자
                    if (players[i]->get_active() == false && players[i]->get_tribe() == MONSTER) {
                        players[i]->set_active(true);
                        timer_event ev;
                        ev.obj_id = i;
                        ev.start_time = chrono::system_clock::now() + 1s;
                        ev.ev = EVENT_NPC_ATTACK;
                        ev.target_id = client_id;
                        timer_queue.push(ev);
                        // 몬스터의 이동도 넣어주자
                        Activate_Npc_Move_Event(i, pl->get_Id());
                    }
                }
            }
        }
        break;
    }
    case CS_PACKET_TELEPORT: {
        pl->direction = 1;
        pl->set_x(rand() % WORLD_WIDTH);
        pl->set_y(rand() % WORLD_HEIGHT);
        sc_packet_move packet;
        packet.size = sizeof(packet);
        packet.type = SC_PACKET_MOVE;
        packet.id = pl->get_Id();
        packet.x = pl->get_x();
        packet.y = pl->get_y();
        packet.move_time = pl->last_move_time;
        break;
    }
    case CS_PACKET_SKILL:{
        cs_packet_skill* packet = reinterpret_cast<cs_packet_skill*>(p);
        
        // 스킬 사용 쿨타임 판단
        if (pl->get_skill_active(packet->skill_type) == true) return;
        pl->set_skill_active(packet->skill_type, true);
        // 이벤트 추가는 각 타입에서 하자 -> 스킬마다 쿨타임이 다르다
        switch (packet->skill_type)
        {
        case 0: {
            timer_event ev;
            ev.obj_id = client_id;
            ev.start_time = chrono::system_clock::now() + 3s;
            ev.ev = EVENT_SKILL_COOLTIME;
            ev.target_id = 0;
            timer_queue.push(ev);

            for (int i = NPC_ID_START; i <= NPC_ID_END; ++i) {
                players[i]->state_lock.lock();
                if (players[i]->get_state() != ST_INGAME) {
                    players[i]->state_lock.unlock();
                    continue;
                }
                players[i]->state_lock.unlock();
                if (players[i]->get_x() >= pl->get_x() - 1 && players[i]->get_x() <= pl->get_x() + 1) {
                    if (players[i]->get_y() >= pl->get_y() - 1 && players[i]->get_y() <= pl->get_y() + 1) {
                        attack_success(client_id, players[i]->get_Id());    // 데미지 계산
                        // 몬스터의 자동공격을 넣어주자
                        if (players[i]->get_active() == false && players[i]->get_tribe() == MONSTER) {
                            players[i]->set_active(true);
                            timer_event ev;
                            ev.obj_id = i;
                            ev.start_time = chrono::system_clock::now() + 1s;
                            ev.ev = EVENT_NPC_ATTACK;
                            ev.target_id = client_id;
                            timer_queue.push(ev);
                            // 몬스터의 이동도 넣어주자
                            Activate_Npc_Move_Event(i, pl->get_Id());
                        }
                    }
                }
            }
            break;
        }
        case 1: {
            // 플레이어의 방향을 만들어 줘야 겠네;;
            timer_event ev;
            int m_x = pl->get_x();
            int m_y = pl->get_y();
            switch (pl->direction) {
            case 0:
                m_y -= 5;
                break;
            case 1:
                m_y += 5;
                break;
            case 2:
                m_x -= 5;
                break;
            case 3:
                m_x += 5;
                break;
            }

            for (int i = NPC_ID_START; i <= NPC_ID_END; ++i) {
                players[i]->state_lock.lock();
                if (players[i]->get_state() != ST_INGAME) {
                    players[i]->state_lock.unlock();
                    continue;
                }
                players[i]->state_lock.unlock();
                if (!(players[i]->get_x() == m_x || players[i]->get_y() == m_y)) continue;
                bool attack_bool = false;

                switch (pl->direction)
                {
                case 0: {
                    if (players[i]->get_x() != m_x) continue;
                    if (m_y <= players[i]->get_y() && players[i]->get_y() <= pl->get_y())
                        attack_bool = true;
                    else continue;
                    break;
                }
                case 1:
                    if (players[i]->get_x() != m_x) continue;
                    if (pl->get_y() <= players[i]->get_y() && players[i]->get_y() <= m_y)
                        attack_bool = true;
                    else continue;
                    break;
                case 2:
                    if (players[i]->get_y() != m_y) continue;
                    if (m_x <= players[i]->get_x() && players[i]->get_x() <= pl->get_x())
                        attack_bool = true;
                    else continue;
                    break;
                case 3:
                    if (players[i]->get_y() != m_y) continue;
                    if (pl->get_x() <= players[i]->get_x() && players[i]->get_x() <= m_x)
                        attack_bool = true;
                    else continue;
                    break;
                default:
                    cout << "스킬2 오류" << endl;
                    break;
                }
                // 공격범위 판단
                if (attack_bool){
                    attack_success(client_id, players[i]->get_Id());    // 데미지 계산
                    // 몬스터의 자동공격을 넣어주자
                    if (players[i]->get_active() == false && players[i]->get_tribe() == MONSTER) {
                        players[i]->set_active(true);
                        timer_event ev;
                        ev.obj_id = i;
                        ev.start_time = chrono::system_clock::now() + 1s;
                        ev.ev = EVENT_NPC_ATTACK;
                        ev.target_id = client_id;
                        timer_queue.push(ev);
                        // 몬스터의 이동도 넣어주자
                        Activate_Npc_Move_Event(i, pl->get_Id());
                    }
                }
            }
            pl->set_x(m_x);
            pl->set_y(m_y);
            send_move_packet(pl, pl);
            // 스킬 쿨타임
            ev.obj_id = client_id;
            ev.start_time = chrono::system_clock::now() + 5s;
            ev.ev = EVENT_SKILL_COOLTIME;
            ev.target_id = 1;
            timer_queue.push(ev);
            break;
        }
        case 2: {
            break;
        }
        default:
            break;
        }
        break;
    }
    }
}

void player_revive(int client_id)
{
    // 플레이어 죽은 후 초기화 설정
    Player* pl = reinterpret_cast<Player*>(players[client_id]);
    pl->state_lock.lock();
    pl->set_state(ST_INGAME);
    pl->state_lock.unlock();
    pl->set_hp(players[client_id]->get_maxhp());
    pl->set_x(500);
    pl->set_y(500);
    pl->set_exp(pl->get_exp() / 2);
    send_status_change_packet(pl);

    sc_packet_revive packet;
    packet.size = sizeof(packet);
    packet.type = SC_PACKET_REVIVE;
    packet.x = pl->get_x();
    packet.y = pl->get_y();
    packet.hp = pl->get_hp();
    packet.exp = pl->get_exp();
    pl->do_send(sizeof(packet), &packet);

    // 주변에 있는 얘들에게 시야처리 해주어야함
    pl->vl.lock();
    pl->viewlist.clear();
    pl->vl.unlock();
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
            if (is_agro_near(client_id, other->get_Id())) {
                if (other->get_active() == false) {
                    other->set_active(true);
                    timer_event ev;
                    ev.obj_id = other->get_Id();
                    ev.start_time = chrono::system_clock::now() + 1s;
                    ev.ev = EVENT_NPC_ATTACK;
                    ev.target_id = client_id;
                    timer_queue.push(ev);
                    Activate_Npc_Move_Event(other->get_Id(), pl->get_Id());
                }
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
    // 장애물 정보
    pl->ob_vl.lock();
    pl->ob_viewlist.clear();
    pl->ob_vl.unlock();
    for (auto& ob : obstacles) {
        if (RANGE < abs(pl->get_x() - ob.x)) continue;
        if (RANGE < abs(pl->get_y() - ob.y)) continue;

        pl->ob_vl.lock();
        pl->ob_viewlist.insert(ob.id);
        pl->ob_vl.unlock();

        sc_packet_put_object packet;
        packet.id = ob.id;
        strcpy_s(packet.name, "");
        packet.object_type = ob.tribe;
        packet.size = sizeof(packet);
        packet.type = SC_PACKET_PUT_OBJECT;
        packet.x = ob.x;
        packet.y = ob.y;
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
            players[client_id]->state_lock.lock();
            if ((players[client_id]->get_state() != ST_INGAME)) {
                players[client_id]->state_lock.unlock();
                players[client_id]->set_active(false);
                delete exp_over;
                break;
            }
            players[client_id]->state_lock.unlock();
            // 제자리로 돌아가는 것인가
            if (exp_over->_target == -1) {
                return_npc_position(client_id);
                delete exp_over;
                break;
            }
            int target_id = exp_over->_target;
            players[target_id]->state_lock.lock();
            //쫒아가던 타겟이 살아있는가
            if (players[target_id]->get_state() != ST_INGAME) {
                players[target_id]->state_lock.unlock();
                players[client_id]->set_active(false);
                return_npc_position(client_id);
                delete exp_over;
                break;
            }
            players[target_id]->state_lock.unlock();

            players[client_id]->lua_lock.lock();
            lua_State* L = players[client_id]->L;
            lua_getglobal(L, "event_npc_move");
            lua_pushnumber(L, exp_over->_target);
            int error = lua_pcall(L, 1, 1, 0);
            if (error != 0) {
                cout << "LUA_NPC_MOVE ERROR" << endl;
            }
            // bool값도 리턴을 해주자 
            // true면 쫒아간다 
            bool m = lua_toboolean(L, -1);
            lua_pop(L, 2);
            players[client_id]->lua_lock.unlock();
            if (m) {
                do_npc_move(client_id, exp_over->_target);
            }
            else {
                // 원래 자리로 돌아가자
                players[client_id]->set_active(false);
                return_npc_position(client_id);
            }
            delete exp_over;
            break;
        }
        case OP_NPC_ATTACK: {
            cout << "여긴 되냐??" << endl;
            // 죽은 상태나 공격하는 상태인지 아닌지 확인
            players[client_id]->state_lock.lock();
            if ((players[client_id]->get_state() != ST_INGAME) || (false == players[client_id]->get_active())) {
                players[client_id]->state_lock.unlock();
                delete exp_over;
                break;
            }
            players[client_id]->state_lock.unlock();

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
            if (m) {
                // 공격처리
                //cout << "NPC 공격" << endl;
                attack_success(client_id, exp_over->_target);
            }
            else {
                //cout << "공격실패" << endl;
                if (players[client_id]->get_active()) {
                    // 공격은 실패했지만 계속(그렇지만 1초후) 공격시도
                    timer_event ev;
                    ev.obj_id = client_id;
                    ev.start_time = chrono::system_clock::now() + 1s;
                    ev.ev = EVENT_NPC_ATTACK;
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
            send_status_change_packet(pl);
            break;
        }
        case OP_PLAYER_REVIVE: {
            player_revive(client_id);
            delete exp_over;
            break;
        }
        case OP_NPC_REVIVE: {
            //cout << "NPC 부활" << endl;
            // 상태 바꿔주고
            players[client_id]->state_lock.lock();
            players[client_id]->set_state(ST_INGAME);
            players[client_id]->state_lock.unlock();
            // NPC의 정보 가져오기
            players[client_id]->lua_lock.lock();
            lua_State* L = players[client_id]->L;
            lua_getglobal(L, "set_uid");
            lua_pushnumber(L, client_id);
            int error = lua_pcall(L, 1, 3, 0);
            if (error != 0) {
                //cout << "ERROR : " << lua_tostring(L, -1);
                cout << "초기화 오류" << endl;
            }

            players[client_id]->set_lv(lua_tointeger(L, -3));
            players[client_id]->set_hp(lua_tointeger(L, -2));
            players[client_id]->set_name(lua_tostring(L, -1));
            lua_pop(L, 4);// eliminate set_uid from stack after call
            players[client_id]->lua_lock.unlock();
            // 부활하는 NPC주변 얘들에게 보이게 해주자
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
                send_put_object_packet(other_player, players[client_id]);
            }
            delete exp_over;
            break;
        }
        }
    }
}

// 스크립트 API
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
    for (int i = NPC_ID_START; i < NPC_ID_END-7000; ++i) {
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
            cout << "초기화 오류" << endl;
        }

        players[i]->set_tribe(MONSTER);
        players[i]->set_lv(lua_tointeger(L, -3));
        players[i]->set_hp(lua_tointeger(L, -2));
        players[i]->set_name(lua_tostring(L, -1));
        lua_pop(L, 4);// eliminate set_uid from stack after call

        lua_register(L, "API_get_x", API_get_x);
        lua_register(L, "API_get_y", API_get_y);

    }
    for (int i = NPC_ID_END - 7000; i <= NPC_ID_END; ++i) {
        sprintf_s(name, "NPC %d", i);
        players[i] = new Npc(i, name);

        lua_State* L = players[i]->L = luaL_newstate();
        luaL_openlibs(L);
        int error = luaL_loadfile(L, "monster2.lua") ||
            lua_pcall(L, 0, 0, 0);
        lua_getglobal(L, "set_uid");
        lua_pushnumber(L, i);
        lua_pushnumber(L, players[i]->get_x());
        lua_pushnumber(L, players[i]->get_y());
        error = lua_pcall(L, 3, 3, 0);

        cout << " 어그로 몬스터 : " << players[i]->get_x() << "," << players[i]->get_y() << endl;

        if (error != 0) {
            //cout << "ERROR : " << lua_tostring(L, -1);
            cout << "초기화 오류" << endl;
        }
        players[i]->set_tribe(BOSS);
        players[i]->set_lv(lua_tointeger(L, -3));
        players[i]->set_hp(lua_tointeger(L, -2));
        players[i]->set_name(lua_tostring(L, -1));
        lua_pop(L, 4);// eliminate set_uid from stack after call

        lua_register(L, "API_get_x", API_get_x);
        lua_register(L, "API_get_y", API_get_y);

    }

    cout << "NPC로딩 완료" << endl;
}

void return_npc_position(int npc_id)
{
    if (players[npc_id]->get_active() == true) {
        return;
    }
    // players[npc_id]->set_active(false); // 공격을 멈춤
    //cout << "돌아가자" << endl;
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

    // 원래 자리로 돌아가자
    players[npc_id]->lua_lock.lock();
    lua_State* L = players[npc_id]->L;
    lua_getglobal(L, "return_my_position");
    int error = lua_pcall(L, 0, 2, 0);
    if (error != 0) {
        players[npc_id]->lua_lock.unlock();
        cout << "LUA_RETURN_MY_POSITION ERROR" << endl;
        return;
    }
    int my_x = lua_tointeger(L, -2);
    int my_y = lua_tointeger(L, -1);
    lua_pop(L, 3);
    players[npc_id]->lua_lock.unlock();
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
    players[npc_id]->set_x(now_x);
    players[npc_id]->set_y(now_y);


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
            send_put_object_packet(reinterpret_cast<Player*>(players[pl]), players[npc_id]);
        }
        else {
            send_move_packet(reinterpret_cast<Player*>(players[pl]), players[npc_id]);
        }
        temp = pl;
    }

    // 시야에 사라진 경우
    for (auto pl : old_viewlist) {
        if (0 == new_viewlist.count(pl)) {
            reinterpret_cast<Player*>(players[pl])->vl.lock();
            reinterpret_cast<Player*>(players[pl])->viewlist.erase(npc_id);
            reinterpret_cast<Player*>(players[pl])->vl.unlock();
            send_remove_object_packet(reinterpret_cast<Player*>(players[pl]), players[npc_id]);
        }
    }

    players[npc_id]->state_lock.lock();
    if (players[npc_id]->get_state() != ST_INGAME) {
        players[npc_id]->state_lock.unlock();
        return;
    }
    players[npc_id]->state_lock.unlock();

    if (my_pos_fail) {    // 더 움직여야돼
        cout << "움직여" << endl;
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
    cout << "타겟 : " << target << endl;
    unordered_set<int> old_viewlist;
    unordered_set<int> new_viewlist;
    for (auto& obj : players) {
        if (obj->get_state() != ST_INGAME) continue;
        if (true == is_npc(obj->get_Id())) break;   // npc가 아닐때
        if (true == is_near(npc_id, obj->get_Id())) {      // 근처에 있을때
            old_viewlist.insert(obj->get_Id());         // npc근처에 플레이어가 있으면 old_viewlist에 플레이어 id를 넣는다
        }
    }

    if (old_viewlist.size() == 0) {
        players[npc_id]->set_active(false);
        return_npc_position(npc_id);
        return;
    }


    int x = players[npc_id]->get_x();
    int y = players[npc_id]->get_y();
    int t_x = players[target]->get_x();
    int t_y = players[target]->get_y();
    
    // 원래는 여기에 A*알고리즘을 넣어야 한다
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
            send_put_object_packet(reinterpret_cast<Player*>(players[pl]), players[npc_id]);
        }
        else {
            send_move_packet(reinterpret_cast<Player*>(players[pl]), players[npc_id]);
        }
    }

    // 시야에 사라진 경우
    for (auto pl : old_viewlist) {
        if (0 == new_viewlist.count(pl)) {
            reinterpret_cast<Player*>(players[pl])->vl.lock();
            reinterpret_cast<Player*>(players[pl])->viewlist.erase(npc_id);
            reinterpret_cast<Player*>(players[pl])->vl.unlock();
            send_remove_object_packet(reinterpret_cast<Player*>(players[pl]), players[npc_id]);
        }
    }

    players[npc_id]->state_lock.lock();
    if (players[npc_id]->get_state() != ST_INGAME) {
        players[npc_id]->state_lock.unlock();
        return;
    }
    players[npc_id]->state_lock.unlock();

    timer_event ev;
    ev.obj_id = npc_id;
    ev.start_time = chrono::system_clock::now() + 1s;
    ev.ev = EVENT_NPC_MOVE;
    ev.target_id = target;
    timer_queue.push(ev);
}

COMP_OP EVtoOP(EVENT_TYPE ev) {
    switch (ev)
    {
    case EVENT_NPC_MOVE:
        return OP_NPC_MOVE;
        break;
    case EVENT_NPC_ATTACK: {
        return OP_NPC_ATTACK;
        break;
    }
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
                reinterpret_cast<Player*>(players[temp.obj_id])->set_attack_active(false);
            }
            else if (temp.ev == EVENT_SKILL_COOLTIME) {
                reinterpret_cast<Player*>(players[temp.obj_id])
                    ->set_skill_active(temp.target_id, false);
            }
            else {
                EXP_OVER* ex_over = new EXP_OVER;
                ex_over->_comp_op = EVtoOP(temp.ev);
                ex_over->_target = temp.target_id;
                PostQueuedCompletionStatus(g_h_iocp, 1, temp.obj_id, &ex_over->_wsa_over);   //0은 소켓취급을 받음
            }
        }

        while (true) {
            timer_event ev;
            if (timer_queue.size() == 0) break;
            timer_queue.try_pop(ev);

            dura = ev.start_time - chrono::system_clock::now();
            if (dura <= 0ms) {
                EXP_OVER* ex_over = new EXP_OVER;
                if (ev.ev == EVENT_PLAYER_ATTACK) {
                    cout << "처리좀" << endl;
                    reinterpret_cast<Player*>(players[ev.obj_id])->set_attack_active(false);
                    continue;
                }
                else if (temp.ev == EVENT_SKILL_COOLTIME) {
                    reinterpret_cast<Player*>(players[temp.obj_id])
                        ->set_skill_active(temp.target_id, false);
                    continue;
                }
                ex_over->_comp_op = EVtoOP(ev.ev);
                ex_over->_target = ev.target_id;
                PostQueuedCompletionStatus(g_h_iocp, 1, ev.obj_id, &ex_over->_wsa_over);   //0은 소켓취급을 받음
            }
            else if (dura <= waittime) {
                temp = ev;
                temp_bool = true;
                break;
            }
            else {
                timer_queue.push(ev);   // 타이머 큐에 넣지 않고 최적화 필요
            }
        }
        this_thread::sleep_for(dura);
        // 쭉 사여있어서 계속 처리를 하도록 해야함
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

    // 초기화 실행
    for (int i = 0; i < MAX_USER; ++i) {
        players[i] = new Player(i);
    }

    Initialise_DB();
    initialise_NPC();

    for (int i = 0; i < MAX_OBSTACLE; i++) {
        obstacles[i].id = i;
        obstacles[i].x = rand() % WORLD_WIDTH;
        obstacles[i].y = rand() % WORLD_HEIGHT;
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
