#define SFML_STATIC 1
#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <iostream>
#include <chrono>
#include <vector>
using namespace std;

#ifdef _DEBUG
#pragma comment (lib, "lib/sfml-graphics-s-d.lib")
#pragma comment (lib, "lib/sfml-window-s-d.lib")
#pragma comment (lib, "lib/sfml-system-s-d.lib")
#pragma comment (lib, "lib/sfml-network-s-d.lib")
#else
#pragma comment (lib, "lib/sfml-graphics-s.lib")
#pragma comment (lib, "lib/sfml-window-s.lib")
#pragma comment (lib, "lib/sfml-system-s.lib")
#pragma comment (lib, "lib/sfml-network-s.lib")
#endif
#pragma comment (lib, "opengl32.lib")
#pragma comment (lib, "winmm.lib")
#pragma comment (lib, "ws2_32.lib")

#include "..\..\Server\Server\2021_가을_protocol.h"

sf::TcpSocket socket;

constexpr auto BUF_SIZE = 256;
constexpr auto SCREEN_WIDTH = 20;
constexpr auto SCREEN_HEIGHT = 20;

constexpr auto TILE_WIDTH = 45;
constexpr auto WINDOW_WIDTH = TILE_WIDTH * SCREEN_WIDTH + 10;   // size of window
constexpr auto WINDOW_HEIGHT = TILE_WIDTH * SCREEN_WIDTH + 10;
//constexpr auto BUF_SIZE = MAX_BUFFER;
const int BAR_SIZE_WIDTH = 400;
const int BAR_SIZE_HEIGHT = 40;
const int CHAT_WIDTH = 400;
const int CHAT_HEIGHT = 120;


int g_myid;
int g_x_origin;
int g_y_origin;
bool dead_screen = false;

sf::RenderWindow* g_window;

vector<string> c_vector;

//-----------------------------------------------------------------

class TextField : public sf::Transformable {
private:
	unsigned int m_size;
	sf::Font m_font;
	std::wstring m_text;
	sf::RectangleShape m_rect;
	sf::RectangleShape log_rect;
	bool m_hasfocus;
public:
	TextField(unsigned int maxChars) : m_size(maxChars),
		m_rect(sf::Vector2f(400, 20)), // 15 pixels per char, 20 pixels height, you can tweak
		m_hasfocus(false),
		log_rect(sf::Vector2f(400, 200))
	{
		m_font.loadFromFile("C:/Windows/Fonts/Arial.ttf"); // I'm working on Windows, you can put your own font instead
		m_rect.setOutlineThickness(2);
		m_rect.setFillColor(sf::Color::White);
		m_rect.setOutlineColor(sf::Color(127, 127, 127));
		m_rect.setPosition(this->getPosition());

		log_rect.setFillColor(sf::Color(250, 250, 255, 100));
		log_rect.setPosition(this->getPosition().x, this->getPosition().y - 180);

	}

	void setPosition(float x, float y) {
		sf::Transformable::setPosition(x, y);
		m_rect.setPosition(x, y);
		log_rect.setPosition(x, y - 180);
	}

	bool contains(sf::Vector2f point) const {
		return m_rect.getGlobalBounds().contains(point);
	}

	void draw() {
		sf::Text temp_text;
		temp_text.setFont(m_font);
		temp_text.setString(m_text);
		temp_text.setPosition(this->getPosition());
		temp_text.scale(0.5f, 0.5f);
		temp_text.setFillColor(sf::Color::Black);

		g_window->draw(log_rect);
		g_window->draw(m_rect);
		g_window->draw(temp_text);


		for (int i = 0; i < c_vector.size(); i++) {
			string m_string = c_vector[i];
			wstring w_string;
			w_string.assign(m_string.begin(), m_string.end());
			temp_text.setString(w_string);
			temp_text.setPosition(this->getPosition().x, this->getPosition().y - 15*(11-i) - 10);
			g_window->draw(temp_text);
		}
	}

	void setFocus(bool focus) {
		m_hasfocus = focus;
		if (focus) {
			m_rect.setOutlineColor(sf::Color::Blue);
		}
		else {
			m_rect.setOutlineColor(sf::Color(127, 127, 127)); // Gray color
		}
	}

	bool getFocus() {
		return m_hasfocus;
	}

	void handleInput(sf::Event e) {
		if (!m_hasfocus || e.type != sf::Event::TextEntered)
			return;

		if (e.text.unicode == 8) {   // Delete key
			m_text = m_text.substr(0, m_text.size() - 1);
		}
		else if (e.text.unicode == 13) {
			cout << "패킷보내기" << endl;
			string m_string;
			m_string.assign(m_text.begin(), m_text.end());
			cs_packet_chat packet;
			packet.size = sizeof(packet);
			packet.type = CS_PACKET_CHAT;
			strncpy_s(packet.message, m_string.c_str(), MAX_CHAT_SIZE);
			size_t sent = 0;
			socket.send(&packet, sizeof(packet), sent);
			m_text.erase();
		}
		else if (m_text.size() < m_size) {
			wcout << e.text.unicode << endl;
			m_text += e.text.unicode;
		}
	}
	~TextField() {}
};

//---------------------------------------------------------------
sf::Font g_font;



class OBJECT {
private:
	bool m_showing;
	sf::Sprite m_sprite;
	sf::Text m_name;
	sf::Text m_chat;
	chrono::system_clock::time_point m_mess_end_time;
public:
	int m_x, m_y;
	short m_lv;
	int m_hp, m_maxhp;
	TRIBE tribe;

	OBJECT(sf::Texture& t, int x, int y, int x2, int y2) {
		m_showing = false;
		m_sprite.setTexture(t);
		m_sprite.setTextureRect(sf::IntRect(x, y, x2, y2));
		set_name("NONAME");
		m_mess_end_time = chrono::system_clock::now();
	}
	OBJECT() {
		m_showing = false;
	}
	void show()
	{
		m_showing = true;
	}
	void hide()
	{
		m_showing = false;
	}

	void a_move(int x, int y) {
		m_sprite.setPosition((float)x, (float)y);
	}

	void a_draw() {
		g_window->draw(m_sprite);
	}

	void move(int x, int y) {
		m_x = x;
		m_y = y;
	}
	void draw() {
		if (false == m_showing) return;
		float rx = (m_x - g_x_origin) * 45.0f + 8;
		float ry = (m_y - g_y_origin) * 45.0f + 8;
		m_sprite.setPosition(rx, ry);
		g_window->draw(m_sprite);
		if (m_mess_end_time < chrono::system_clock::now()) {
			m_name.setPosition(rx - 10, ry - 20);
			g_window->draw(m_name);
		}
		else {
			m_chat.setPosition(rx - 10, ry - 20);
			g_window->draw(m_chat);
		}
	}
	void set_name(const char str[]) {
		m_name.setFont(g_font);
		m_name.setString(str);
		m_name.setFillColor(sf::Color(255, 255, 0));
		m_name.setStyle(sf::Text::Bold);
	}
	void set_chat(const char str[]) {
		m_chat.setFont(g_font);
		m_chat.setString(str);
		m_chat.setFillColor(sf::Color(255, 255, 255));
		m_chat.setStyle(sf::Text::Bold);
		m_mess_end_time = chrono::system_clock::now() + chrono::seconds(3);
	}
};

class Obstacle {
private:
	sf::Sprite m_sprite;
	bool m_showing;
public:
	int m_x, m_y;
	TRIBE tribe;

	Obstacle(sf::Texture& t, int x, int y, int x2, int y2) {
		m_showing = false;
		m_sprite.setTexture(t);
		m_sprite.setTextureRect(sf::IntRect(x, y, x2, y2));
	}
	Obstacle() {
		m_showing = false;
	}
	void show()
	{
		m_showing = true;
	}
	void hide()
	{
		m_showing = false;
	}

	void a_move(int x, int y) {
		m_sprite.setPosition((float)x, (float)y);
	}

	void a_draw() {
		g_window->draw(m_sprite);
	}

	void move(int x, int y) {
		m_x = x;
		m_y = y;
	}
	void draw() {
		if (false == m_showing) return;
		float rx = (m_x - g_x_origin) * 45.0f + 8;
		float ry = (m_y - g_y_origin) * 45.0f + 8;
		m_sprite.setPosition(rx, ry);
		g_window->draw(m_sprite);
	}
};

int my_exp;
int max_exp;

OBJECT avatar;
OBJECT players[MAX_USER + MAX_NPC];
Obstacle obstacles[MAX_OBSTACLE];

OBJECT white_tile;
OBJECT black_tile;

sf::Texture* board;
sf::Texture* monsters;
sf::Texture* stone;
sf::Texture* hero;

void client_initialize()
{
	board = new sf::Texture;
	monsters = new sf::Texture;
	stone = new sf::Texture;
	hero = new sf::Texture;
	if (false == g_font.loadFromFile("cour.ttf")) {
		cout << "Font Loading Error!\n";
		while (true);
	}
	/*board->loadFromFile("chessmap.bmp");
	pieces->loadFromFile("chess2.png");*/
	board->loadFromFile("chessmap2.bmp");
	monsters->loadFromFile("resource/digimon.png");
	hero->loadFromFile("resource/zelda.png");
	stone->loadFromFile("resource/stone.png");


	white_tile = OBJECT{ *board, 4, 4, TILE_WIDTH, TILE_WIDTH };
	black_tile = OBJECT{ *board, 49, 4, TILE_WIDTH, TILE_WIDTH };
	avatar = OBJECT{ *hero, 0, 3, TILE_WIDTH, TILE_WIDTH };
	
	for (int i = 0; i < MAX_USER; ++i) {
		players[i] = OBJECT{ *hero, 0, 0, 44, 44 };
	}
	for (int i = NPC_ID_START; i < NPC_ID_END - 10000; ++i) {
		players[i] = OBJECT{ *monsters, 135, 135, TILE_WIDTH, TILE_WIDTH };
	}
	for (int i = NPC_ID_END - 10000; i <= NPC_ID_END; ++i) {
		players[i] = OBJECT{ *monsters, 667, 135, TILE_WIDTH, TILE_WIDTH };
	}
	for (int i = 0; i <= MAX_OBSTACLE; ++i) {
		obstacles[i] = Obstacle{ *stone, 45, 180, TILE_WIDTH, TILE_WIDTH };
	}

}

void client_finish()
{
	delete board;
	delete monsters;
	delete stone;
	delete hero;
}

void ProcessPacket(char* ptr)
{
	static bool first_time = true;
	switch (ptr[1])
	{
	case SC_PACKET_LOGIN_OK:{
		sc_packet_login_ok* packet = reinterpret_cast<sc_packet_login_ok*>(ptr);
		g_myid = packet->id;
		avatar.set_name(packet->name);
		avatar.m_x = packet->x;
		avatar.m_y = packet->y;
		cout << packet->name << endl;
		g_x_origin = packet->x - SCREEN_WIDTH / 2;
		g_y_origin = packet->y - SCREEN_WIDTH / 2;
		avatar.move(packet->x, packet->y);
		avatar.tribe = static_cast<TRIBE>(packet->tribe);
		avatar.m_hp = packet->hp;
		avatar.m_maxhp = packet->maxhp;
		my_exp = packet->exp;
		avatar.m_lv = packet->level;
		max_exp = 100 * pow(2, (avatar.m_lv - 1));
		avatar.show();
		break;
	}
	case SC_PACKET_MOVE:{
		sc_packet_move * my_packet = reinterpret_cast<sc_packet_move *>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_x_origin = my_packet->x - SCREEN_WIDTH / 2;
			g_y_origin = my_packet->y - SCREEN_WIDTH / 2;
		}
		else if (other_id < MAX_USER) {
			players[other_id].move(my_packet->x, my_packet->y);
		}
		else {
			players[other_id].move(my_packet->x, my_packet->y);
		}
		break;
	}
	case SC_PACKET_PUT_OBJECT:{
		sc_packet_put_object* my_packet = reinterpret_cast<sc_packet_put_object*>(ptr);
		int id = my_packet->id;

		if (static_cast<TRIBE>(my_packet->object_type) != OBSTACLE) {
			if (id < MAX_USER) { // PLAYER
				players[id].set_name(my_packet->name);
				players[id].move(my_packet->x, my_packet->y);
				players[id].show();
			}
			else {  // NPC
				players[id].set_name(my_packet->name);
				players[id].move(my_packet->x, my_packet->y);
				players[id].show();
			}
			break;
		}
		else {
			obstacles[id].move(my_packet->x, my_packet->y);
			obstacles[id].show();
			break;
		}
		break;
	}
	case SC_PACKET_REMOVE_OBJECT:{
		sc_packet_remove_object* my_packet = reinterpret_cast<sc_packet_remove_object*>(ptr);
		int other_id = my_packet->id;
		if (static_cast<TRIBE>(my_packet->object_type) != OBSTACLE) {
			if (other_id == g_myid) {
				avatar.hide();
			}
			else if (other_id < MAX_USER) {
				players[other_id].hide();
			}
			else {
				players[other_id].hide();
			}
		}
		else {
			obstacles[other_id].hide();
		}
		break;
	}
	case SC_PACKET_CHAT: {
		sc_packet_chat* my_packet = reinterpret_cast<sc_packet_chat*>(ptr);
		int other_id = my_packet->id;
		if (c_vector.size() < 11) {
			c_vector.push_back(my_packet->message);
		}
		else {
			c_vector.erase(c_vector.begin());
			c_vector.push_back(my_packet->message);
		}
		break;
	}
	case SC_PACKET_LOGIN_FAIL:{
		sc_packet_login_fail* my_packet = reinterpret_cast<sc_packet_login_fail*>(ptr);
		cout << "로그인 실패" << endl;
		if (my_packet->type == 0) {
			cout << "이미 다른 주소에서 접속중인 아이디입니다" << endl;
		}
		else {
			cout << "존재하지 않는 아이디 입니다" << endl;
		}
		g_window->close();
	}
	case SC_PACKET_STATUS_CHANGE: {
		sc_packet_status_change* packet = reinterpret_cast<sc_packet_status_change*>(ptr);
		avatar.m_hp = packet->hp;
		avatar.m_maxhp = packet->maxhp;
		avatar.m_lv = packet->level;
		my_exp = packet->exp;
		max_exp = 100 * pow(2, (avatar.m_lv - 1));
		break;
	}
	case SC_PACKET_DEAD: {
		dead_screen = true;
		break;
	}
	case SC_PACKET_REVIVE: {
		dead_screen = false;
		sc_packet_revive* packet = reinterpret_cast<sc_packet_revive*>(ptr);
		avatar.m_x = packet->x;
		avatar.m_y = packet->y;
		g_x_origin = packet->x - SCREEN_WIDTH / 2;
		g_y_origin = packet->y - SCREEN_WIDTH / 2;
		avatar.move(packet->x, packet->y);
		avatar.m_hp = packet->hp;
		my_exp = packet->exp;
		avatar.show();
		break;
	}
	default:
		printf("Unknown PACKET type [%d]\n", ptr[1]);
	}
}

void process_data(char* net_buf, size_t io_byte)
{
	char* ptr = net_buf;
	static size_t in_packet_size = 0;
	static size_t saved_packet_size = 0;
	static char packet_buffer[BUF_SIZE];

	while (0 != io_byte) {
		if (0 == in_packet_size) in_packet_size = ptr[0];
		if (io_byte + saved_packet_size >= in_packet_size) {
			memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
			ProcessPacket(packet_buffer);
			ptr += in_packet_size - saved_packet_size;
			io_byte -= in_packet_size - saved_packet_size;
			in_packet_size = 0;
			saved_packet_size = 0;
		}
		else {
			memcpy(packet_buffer + saved_packet_size, ptr, io_byte);
			saved_packet_size += io_byte;
			io_byte = 0;
		}
	}
}

bool client_dead()
{
	char net_buf[BUF_SIZE];
	size_t	received;

	auto recv_result = socket.receive(net_buf, BUF_SIZE, received);
	if (recv_result == sf::Socket::Error)
	{
		wcout << L"Recv 에러!";
		while (true);
	}
	if (recv_result == sf::Socket::Disconnected)
	{
		wcout << L"서버 접속 종료.\n";
		return false;
	}
	if (recv_result != sf::Socket::NotReady)
		if (received > 0) process_data(net_buf, received);

	for (int i = 0; i < SCREEN_WIDTH; ++i){
		for (int j = 0; j < SCREEN_HEIGHT; ++j)
		{
			int tile_x = i + g_x_origin;
			int tile_y = j + g_y_origin;
			if ((tile_x < 0) || (tile_y < 0)) continue;
			if ((((tile_x / 3) + (tile_y / 3)) % 2) == 1) {
				white_tile.a_move(TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
				white_tile.a_draw();
			}
			else
			{
				black_tile.a_move(TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
				black_tile.a_draw();
			}
		}
	}
	sf::Text text;
	text.setFont(g_font);
	char buf[100];
	sprintf_s(buf, "LV : %d,  POS(%d, %d)", avatar.m_lv, avatar.m_x, avatar.m_y);
	text.setString(buf);

	// 사망화면
	sf::RectangleShape dead_box(sf::Vector2f(WINDOW_WIDTH, WINDOW_HEIGHT));
	dead_box.setFillColor(sf::Color(3, 3, 3, 250));
	dead_box.setPosition(0.0f, 0.0f);
	sf::Text dead_text;
	dead_text.setFont(g_font);
	dead_text.setFillColor(sf::Color(255, 0, 0));
	dead_text.setStyle(sf::Text::Bold);
	char d_buf[100];
	sprintf_s(d_buf, "DEAD");
	dead_text.setString(d_buf);
	dead_text.setPosition(WINDOW_WIDTH/2 - 75, WINDOW_HEIGHT/2 - 50);
	dead_text.setScale(sf::Vector2f(2.0f, 2.0f));


	//// hp바
	//float hp_bar_per = MAX_HP_BAR * ((float)avatar.m_hp / (float)avatar.m_maxhp);
	//sf::RectangleShape hp_bar(sf::Vector2f(hp_bar_per, 40));
	//sf::RectangleShape max_hp_bar(sf::Vector2f(400, 40));
	//hp_bar.setFillColor(sf::Color(255, 0, 0, 200));
	//max_hp_bar.setFillColor(sf::Color(5, 5, 5, 230));
	//hp_bar.setPosition(0.0f, 40);
	//max_hp_bar.setPosition(0.0f, 40);

	// draw()
	/*g_window->draw(max_hp_bar);
	g_window->draw(hp_bar);*/
	g_window->draw(text);
	g_window->draw(dead_box);
	g_window->draw(dead_text);

	return true;
}

bool client_main()
{
	char net_buf[BUF_SIZE];
	size_t	received;

	auto recv_result = socket.receive(net_buf, BUF_SIZE, received);
	if (recv_result == sf::Socket::Error)
	{
		wcout << L"Recv 에러!";
		while (true);
	}
	if (recv_result == sf::Socket::Disconnected)
	{
		wcout << L"서버 접속 종료.\n";
		return false;
	}
	if (recv_result != sf::Socket::NotReady)
		if (received > 0) process_data(net_buf, received);

	for (int i = 0; i < SCREEN_WIDTH; ++i)
		for (int j = 0; j < SCREEN_HEIGHT; ++j)
		{
			int tile_x = i + g_x_origin;
			int tile_y = j + g_y_origin;
			if ((tile_x < 0) || (tile_y < 0)) continue;
			if ((((tile_x / 3)  + (tile_y / 3)) % 2) == 1) {
				white_tile.a_move(TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
				white_tile.a_draw();
			}
			else
			{
				black_tile.a_move(TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
				black_tile.a_draw();
			}
		}
	avatar.draw();
	for (auto& pl : players) pl.draw();
	for (auto& ob : obstacles) ob.draw();
	sf::Text text;
	text.setFont(g_font);
	char buf[100];
	sprintf_s(buf, "LV : %d,  POS(%d, %d)", avatar.m_lv, avatar.m_x, avatar.m_y);
	text.setString(buf);
	
	// hp바
	float hp_bar_per = BAR_SIZE_WIDTH * ((float)avatar.m_hp / (float)avatar.m_maxhp);
	sf::RectangleShape hp_bar(sf::Vector2f(hp_bar_per, BAR_SIZE_HEIGHT));
	sf::RectangleShape max_hp_bar(sf::Vector2f(BAR_SIZE_WIDTH, BAR_SIZE_HEIGHT));
	hp_bar.setFillColor(sf::Color(255, 0, 0, 200));
	max_hp_bar.setFillColor(sf::Color(5, 5, 5, 230));
	hp_bar.setPosition(0.0f, BAR_SIZE_HEIGHT);
	max_hp_bar.setPosition(0.0f, BAR_SIZE_HEIGHT);
	sf::Text hp_text;
	hp_text.setFont(g_font);
	char hp_str[30];
	sprintf_s(hp_str, "HP  : %d /", avatar.m_hp);
	hp_text.setString(hp_str);
	hp_text.setPosition(0, BAR_SIZE_HEIGHT);
	sf::Text maxhp_text;
	maxhp_text.setFont(g_font);
	char maxhp_str[30];
	sprintf_s(maxhp_str, "%d", avatar.m_maxhp);
	maxhp_text.setString(maxhp_str);
	maxhp_text.setPosition(BAR_SIZE_WIDTH /2, BAR_SIZE_HEIGHT);

	// exp바
	float exp_bar_per = BAR_SIZE_WIDTH * ((float)my_exp / (float)max_exp);
	sf::RectangleShape exp_bar(sf::Vector2f(exp_bar_per, BAR_SIZE_HEIGHT));
	sf::RectangleShape max_exp_bar(sf::Vector2f(BAR_SIZE_WIDTH, BAR_SIZE_HEIGHT));
	exp_bar.setFillColor(sf::Color(0, 255, 0, 200));
	max_exp_bar.setFillColor(sf::Color(5, 5, 5, 230));
	exp_bar.setPosition(0.0f, BAR_SIZE_HEIGHT*2);
	max_exp_bar.setPosition(0.0f, BAR_SIZE_HEIGHT*2);
	sf::Text exp_text;
	exp_text.setFont(g_font);
	char exp_str[30];
	sprintf_s(exp_str, "EXP : %d /", my_exp);
	exp_text.setString(exp_str);
	exp_text.setPosition(0, BAR_SIZE_HEIGHT*2);
	sf::Text maxexp_text;
	maxexp_text.setFont(g_font);
	char maxexp_str[30];
	sprintf_s(maxexp_str, "%d", max_exp);
	maxexp_text.setString(maxexp_str);
	maxexp_text.setPosition(BAR_SIZE_WIDTH /2, BAR_SIZE_HEIGHT*2);

	// draw()
	g_window->draw(max_hp_bar);
	g_window->draw(hp_bar);
	g_window->draw(text);
	g_window->draw(hp_text);
	g_window->draw(maxhp_text);
	g_window->draw(max_exp_bar);
	g_window->draw(exp_bar);
	g_window->draw(exp_text);
	g_window->draw(maxexp_text);

	return true;
}

void send_move_packet(char dr)
{
	cs_packet_move packet;
	packet.size = sizeof(packet);
	packet.type = CS_PACKET_MOVE;
	packet.direction = dr;
	size_t sent = 0;
	socket.send(&packet, sizeof(packet), sent);
}

void send_login_packet(string &name)
{
	cs_packet_login packet;
	packet.size = sizeof(packet);
	packet.type = CS_PACKET_LOGIN;
	strcpy_s(packet.name, name.c_str());
	size_t sent = 0;
	socket.send(&packet, sizeof(packet), sent);
}

void send_attack_packet()
{
	cs_packet_attack packet;
	packet.size = sizeof(packet);
	packet.type = CS_PACKET_ATTACK;
	size_t sent = 0;
	socket.send(&packet, sizeof(packet), sent);
}

void send_skill_packet(int skill_type)
{
	cs_packet_skill packet;
	packet.size = sizeof(packet);
	packet.type = CS_PACKET_SKILL;
	packet.skill_type = skill_type;
	size_t sent = 0;
	socket.send(&packet, sizeof(packet), sent);
}

int main()
{
	wcout.imbue(locale("korean"));
	sf::Socket::Status status = socket.connect("127.0.0.1", SERVER_PORT);

	socket.setBlocking(false);

	if (status != sf::Socket::Done) {
		wcout << L"서버와 연결할 수 없습니다.\n";
		while (true);
	}

	client_initialize();		// 렌더링 객체 초기화
	string name;
	// auto tt = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
	// name += to_string(tt % 1000);
	cout << "ID를 입력하세요 : ";
	cin >> name;
	send_login_packet(name);	
	avatar.set_name(name.c_str());
	sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "2D CLIENT");
	g_window = &window;
	

	TextField tf(20);
	tf.setPosition(0, 880);

	while (window.isOpen())
	{
		sf::Event event;
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
				window.close();
			if (event.type == sf::Event::MouseButtonReleased) {
				auto pos = sf::Mouse::getPosition(window);
				tf.setFocus(false);
				if (tf.contains(sf::Vector2f(pos))) {
					tf.setFocus(true);
				}
			}
			else {
				tf.handleInput(event);
			}

			if (event.type == sf::Event::KeyPressed && tf.getFocus()==false) {
				int direction = -1;
				switch (event.key.code) {
				case sf::Keyboard::Left:
					direction = 2;
					break;
				case sf::Keyboard::Right:
					direction = 3;
					break;
				case sf::Keyboard::Up:
					direction = 0;
					break;
				case sf::Keyboard::Down:
					direction = 1;
					break;
				case sf::Keyboard::A:
					send_attack_packet();
					break;
				case sf::Keyboard::Num1:
					send_skill_packet(0);
					break;
				case sf::Keyboard::Num2:
					send_skill_packet(1);
					break;
				case sf::Keyboard::Num3:
					send_skill_packet(2);
					break;
				case sf::Keyboard::Escape:
					window.close();
					break;
				}
				if (-1 != direction) send_move_packet(direction);
			}

		}

		window.clear();
		if (false == client_main())
			window.close();

		tf.draw();
		window.display();


		while (dead_screen) {
			window.clear();
			if (false == client_dead())
				window.close();
			window.display();
		}
	}
	client_finish();

	return 0;
}
