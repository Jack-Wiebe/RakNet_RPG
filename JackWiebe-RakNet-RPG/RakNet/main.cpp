
#include "MessageIdentifiers.h"
#include "BitStream.h"
#include "RakPeer.h"
#include "RakPeerInterface.h"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <map>
#include <mutex>
#include <iostream>
#include <thread>

static unsigned int SERVER_PORT = 65000;
static unsigned int CLIENT_PORT = SERVER_PORT + 1;
static unsigned int MAX_CONNECTIONS = 4;
static unsigned int MAX_GAME_SIZE = 3;

enum NetworkState
{
	NS_INIT = 0,
	NS_PENDINGSTART,
	//NS_STARTING,
	NS_RUNNING, // started
	NS_CHATROOM,
	NS_GAME,
	NS_BATTLE,
	NS_LOBBY,
	NS_PENDING,
	NS_CLOSING,
};

bool isRunning = true;
bool isServer = false;



//State of the Network
NetworkState g_networkState = NS_INIT;
std::mutex g_networkState_mutex;

//Total current player count in lobby
unsigned short g_totalPlayers = 0;

//CLIENT-SERVER INTERFACE
RakNet::RakPeerInterface *g_serverInterface = nullptr;
RakNet::SystemAddress g_serverAddress;

// Holds packets
RakNet::Packet* packet;

// GetPacketIdentifier returns this
unsigned char packetIdentifier;
unsigned char GetPacketIdentifier(RakNet::Packet *p);

// Record the first client that connects to us so we can pass it to the ping function
RakNet::SystemAddress clientID = RakNet::UNASSIGNED_SYSTEM_ADDRESS;

char userInput[255];
char message[2048];

enum {

	ID_THEGAME_LOBBY_READY = ID_USER_PACKET_ENUM,
	ID_PLAYER_READY,
	ID_CHATACTION,
	ID_GAMESTART,
	ID_SELECTCLASS,
	ID_CLASS_SET,
	ID_BATTLESTART,
	ID_STATQUERY,
	ID_DISPLAYSTATS,
	ID_THEGAMEACTION,
	ID_ATTACKHEAL,
	ID_NEXTTURN,
	ID_PLAYERDIED,
	ID_GAMEOVER,
	ID_INVALIDTARGET

};

enum EPlayerClass
{
	Fighter = 0,
	Mage,
	Cleric,
};

struct SPlayer
{
	std::string m_name;
	std::string m_lastMsg;
	unsigned int m_health;
	EPlayerClass m_class;
	bool isTurn = false;
	bool isDead = false;
	NetworkState m_state;
	int m_order;

	//function to send a packet with name/health/class etc
	void SendName(RakNet::SystemAddress systemAddress, bool isBroadcast)
	{
		RakNet::BitStream writeBs;
		writeBs.Write((RakNet::MessageID)ID_PLAYER_READY);
		RakNet::RakString name(m_name.c_str());
		writeBs.Write(name);

		//returns 0 when something is wrong
		assert(g_serverInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, systemAddress, isBroadcast));
	}

	void SendDisconnect(RakNet::SystemAddress systemAddress, bool isBroadcast)
	{
		RakNet::BitStream writeBs;
		writeBs.Write((RakNet::MessageID)ID_PLAYER_READY);
		std::string msg = m_name + " has disconnected...\n\n";
		RakNet::RakString disconnect(msg.c_str());
		writeBs.Write(disconnect);

		//returns 0 when something is wrong
		assert(g_serverInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, systemAddress, isBroadcast));
	}

	void SendChat(RakNet::SystemAddress systemAddress, bool isBroadcast)
	{
		RakNet::BitStream writeBs;
		writeBs.Write((RakNet::MessageID)ID_PLAYER_READY);
		std::string msg = userInput;
		RakNet::RakString disconnect(msg.c_str());
		writeBs.Write(disconnect);

		//returns 0 when something is wrong
		assert(g_serverInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, systemAddress, isBroadcast));
	}

	void SendClass(RakNet::SystemAddress systemAddress, bool isBroadcast, std::string className)
	{
		RakNet::BitStream writeBs;
		writeBs.Write((RakNet::MessageID)ID_CLASS_SET);
		RakNet::RakString choice(className.c_str());
		writeBs.Write(choice);

		//std::cout << "CLASS" << std::endl;

		//returns 0 when something is wrong
		assert(g_serverInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, systemAddress, isBroadcast));
	}

	void SendBattleReady(RakNet::SystemAddress systemAddress, bool isBroadcast)
	{
		RakNet::BitStream writeBs;
		writeBs.Write((RakNet::MessageID)ID_CLASS_SET);
		RakNet::RakString order;
		writeBs.Write(order);

		std::cout << m_name << "the " << m_class << "is going " << std::endl;

		//returns 0 when something is wrong
		assert(g_serverInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, systemAddress, isBroadcast));
	}

	void SendStats(RakNet::SystemAddress systemAddress, bool isBroadcast)
	{

		RakNet::BitStream writeBs;
		writeBs.Write((RakNet::MessageID)ID_DISPLAYSTATS);
		std::string temp = m_name + " is at " + std::to_string(m_health) + "\n";
		RakNet::RakString ting(temp.c_str());
		writeBs.Write(ting);

		assert(g_serverInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, systemAddress, isBroadcast));

	}

	void SendAction()
	{

	}


};

std::map<unsigned long, SPlayer> m_playerMap;

SPlayer& GetPlayer(RakNet::RakNetGUID raknetID)
{
	unsigned long guid = RakNet::RakNetGUID::ToUint32(raknetID);
	std::map<unsigned long, SPlayer>::iterator it = m_playerMap.find(guid);
	assert(it != m_playerMap.end());
	return it->second;

}

void ResolveAction(RakNet::Packet* packet)
{
	unsigned long guid = RakNet::RakNetGUID::ToUint32(packet->guid);

	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageID;
	bs.Read(messageID);
	RakNet::RakString msg;
	bs.Read(msg);

	std::cout << msg.C_String() << std::endl;



	//SPlayer& player = GetPlayer(packet->guid);
	//if(player.isTurn)
	//	std::cout << "Type stats for game stats\nattack [TARGET] to attack\nheal [TARGET] to heal" << std::endl;
}

void GameOver(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageID;
	bs.Read(messageID);
	RakNet::RakString msg;
	bs.Read(msg);
	g_networkState = NS_CLOSING;

	std::cout << msg.C_String() << std::endl;
}

void InvalidTarget(RakNet::Packet* packet)
{

	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageID;
	bs.Read(messageID);
	RakNet::RakString msg;
	bs.Read(msg);

	std::cout << msg.C_String() << std::endl;

}

void GameAction(RakNet::Packet* packet)
{

	unsigned long guid = RakNet::RakNetGUID::ToUint32(packet->guid);

	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageID;
	bs.Read(messageID);
	RakNet::RakString msg;
	bs.Read(msg);
	RakNet::RakString trg;
	bs.Read(trg);

	SPlayer& player = GetPlayer(packet->guid);
	bool targetValid = false;
	SPlayer& targetPlayer = SPlayer();

	if (player.isTurn && !player.isDead)
	{
		for (std::map<unsigned long, SPlayer>::iterator it = m_playerMap.begin(); it != m_playerMap.end(); ++it)
		{
			//skip over the player who just joined
			/*if (guid == it->first)
			{
			continue;
			}*/

			SPlayer& otherPlayer = it->second;
			if (!otherPlayer.isDead && otherPlayer.m_name == trg.C_String())
			{
				//otherPlayer.SendStats(packet->systemAddress, false);
				targetValid = true;
				targetPlayer = otherPlayer;
			}
		}

		if (targetValid)
		{

			for (std::map<unsigned long, SPlayer>::iterator it = m_playerMap.begin(); it != m_playerMap.end(); ++it)
			{

				SPlayer& otherPlayer = it->second;
				if (otherPlayer.m_name == trg.C_String())
				{
					if (msg == "heal")
					{
						it->second.m_health += 10;
					}
					else
					{
						it->second.m_health -= 10;
					}

					if (it->second.m_health <= 0)
					{
						it->second.isDead = true;
						RakNet::BitStream writeBS;
						writeBS.Write((RakNet::MessageID)ID_PLAYERDIED);
						std::string temp = it->second.m_name + " has died\n\n";
						RakNet::RakString msg(temp.c_str());
						writeBS.Write(msg);

						assert(g_serverInterface->Send(&writeBS, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, true));
						//m_playerMap.erase(it);
					}

				}
			}

			std::map<unsigned long, SPlayer> aliveMap;
			for (std::map<unsigned long, SPlayer>::iterator it = m_playerMap.begin(); it != m_playerMap.end(); ++it)
			{
				if (it->second.isDead == false)
				{
					aliveMap.insert(std::make_pair(it->first, it->second));
					//aliveMap.insert(it->first, it->second);
				}
			}

			if (aliveMap.size() <= 1)
			{

				for (std::map<unsigned long, SPlayer>::iterator it = m_playerMap.begin(); it != m_playerMap.end(); ++it)
				{
					it->second.m_state = NS_CLOSING;
				}
				RakNet::BitStream writeBS;
				writeBS.Write((RakNet::MessageID)ID_GAMEOVER);
				std::string temp = aliveMap.begin()->second.m_name + " has Won the game!!! \nPress any key to exit";
				RakNet::RakString msg(temp.c_str());
				writeBS.Write(msg);

				assert(g_serverInterface->Send(&writeBS, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, true));
				g_networkState = NS_CLOSING;
				return;
			}


			player.isTurn = false;
			std::string nextPlayer;
			unsigned long guid = RakNet::RakNetGUID::ToUint32(packet->guid);
			std::map<unsigned long, SPlayer>::iterator playerIt = aliveMap.find(guid);

			int nextOrder;
			playerIt++;

			if (playerIt == aliveMap.end())
			{
				nextOrder = aliveMap.begin()->second.m_order;
			}
			else
			{
				nextOrder = playerIt->second.m_order;
			}

			/*int nextOrder = player.m_order + 1;
			if (nextOrder > m_playerMap.size())
			{
			nextOrder = 1;
			}*/

			for (std::map<unsigned long, SPlayer>::iterator it = m_playerMap.begin(); it != m_playerMap.end(); ++it)
			{
				if (it->second.m_order == nextOrder)
				{

					it->second.isTurn = true;
					nextPlayer = it->second.m_name;
				}
			}


			if (msg == "heal")
			{
				std::cout << player.m_name << " heals " << trg << std::endl;

				RakNet::BitStream writeBS;
				writeBS.Write((RakNet::MessageID)ID_ATTACKHEAL);
				std::string temp = player.m_name + " healed " + targetPlayer.m_name + " for 10 hit points" + "\nit is " + nextPlayer + "'s turn...\n\n";
				RakNet::RakString msg(temp.c_str());
				writeBS.Write(msg);

				assert(g_serverInterface->Send(&writeBS, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, true));

			}
			else
			{
				std::cout << player.m_name << " attacks " << trg << std::endl;

				RakNet::BitStream writeBS;
				writeBS.Write((RakNet::MessageID)ID_ATTACKHEAL);
				std::string temp = player.m_name + " attacked " + targetPlayer.m_name + " for 10 damage" + "\nit is " + nextPlayer + "'s turn...\n\n";
				RakNet::RakString msg(temp.c_str());
				writeBS.Write(msg);

				assert(g_serverInterface->Send(&writeBS, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, true));
			}


		}
		else
		{
			RakNet::BitStream writeBS;
			writeBS.Write((RakNet::MessageID)ID_INVALIDTARGET);
			std::string temp = "Invalid Target... Try again!\n";
			RakNet::RakString msg(temp.c_str());
			writeBS.Write(msg);

			assert(g_serverInterface->Send(&writeBS, HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->systemAddress, false));
		}
	}
	else if (player.isDead)
	{
		player.isTurn = false;
		std::string nextPlayer;
		int nextOrder = player.m_order + 1;
		if (nextOrder > m_playerMap.size())
		{
			nextOrder = 1;
		}

		for (std::map<unsigned long, SPlayer>::iterator it = m_playerMap.begin(); it != m_playerMap.end(); ++it)
		{
			if (it->second.m_order == nextOrder)
			{

				it->second.isTurn = true;
				nextPlayer = it->second.m_name;
			}
		}
	}
	else
	{

		std::cout << "it is not your turn\n\n" << std::endl;
	}

	//std::cout << msg << std::endl;
}

void PlayerDied(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageID;
	bs.Read(messageID);
	RakNet::RakString msg;
	bs.Read(msg);

	std::cout << msg.C_String() << std::endl;

	//assert(g_serverInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, true));

}

void DisplayStats(RakNet::Packet* packet)
{

	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageID;
	bs.Read(messageID);
	RakNet::RakString msg;
	bs.Read(msg);

	std::cout << msg << std::endl;
}

void QueryStats(RakNet::Packet* packet)
{

	unsigned long guid = RakNet::RakNetGUID::ToUint32(packet->guid);
	for (std::map<unsigned long, SPlayer>::iterator it = m_playerMap.begin(); it != m_playerMap.end(); ++it)
	{
		//skip over the player who just joined
		if (guid == it->first)
		{
			continue;
		}

		SPlayer& otherPlayer = it->second;
		otherPlayer.SendStats(packet->systemAddress, false);
	}

	/*RakNet::BitStream bs;
	bs.Write((RakNet::MessageID)ID_DISPLAYSTATS);

	assert(g_serverInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->systemAddress, false));*/
}

void OnIncomingConnection(RakNet::Packet* packet)
{
	//must be server in order to recieve connection
	assert(isServer);
	m_playerMap.insert(std::make_pair(RakNet::RakNetGUID::ToUint32(packet->guid), SPlayer()));
	GetPlayer(packet->guid).m_state = NS_LOBBY;
	g_totalPlayers++;
	unsigned int numConnections = g_serverInterface->NumberOfConnections();
	std::cout << "Total Players: " << m_playerMap.size() << "\n Total Number or Connections: " << numConnections << std::endl;
}

void OnConnectionAccepted(RakNet::Packet* packet)
{
	assert(!isServer);

	g_networkState_mutex.lock();
	g_networkState = NS_LOBBY;
	g_networkState_mutex.unlock();
	g_serverAddress = packet->systemAddress;
	g_serverInterface->SetTimeoutTime(5000, g_serverAddress);
}

void OnLostConnection(RakNet::Packet* packet)
{
	SPlayer& lostPlayer = GetPlayer(packet->guid);
	lostPlayer.SendName(RakNet::UNASSIGNED_SYSTEM_ADDRESS, true);
	unsigned long guid = RakNet::RakNetGUID::ToUint32(packet->guid);
	m_playerMap.erase(guid);
}

void OnLobbyReady(RakNet::Packet* packet) {
	unsigned long guid = RakNet::RakNetGUID::ToUint32(packet->guid);
	/*std::map<unsigned long, SPlayer>::iterator it = m_playerMap.find(guid);
	//somehow player didnt connect but is now in the lobby
	assert(it != m_playerMap.end());*/

	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageID;
	bs.Read(messageID);
	RakNet::RakString userName;
	bs.Read(userName);

	SPlayer& player = GetPlayer(packet->guid);
	player.m_state = NS_PENDING;
	player.m_name = userName;
	std::cout << userName.C_String() << " AKA " << player.m_name << " is ready!!!!" << std::endl;

	//notify all other connected players that this plyer has joined the game
	for (std::map<unsigned long, SPlayer>::iterator it = m_playerMap.begin(); it != m_playerMap.end(); ++it)
	{
		//skip over the player who just joined
		if (guid == it->first)
		{
			continue;
		}

		SPlayer& otherPlayer = it->second;
		otherPlayer.SendName(packet->systemAddress, false);
		/*RakNet::BitStream writeBs;
		writeBs.Write((RakNet::MessageID)ID_PLAYER_READY);
		RakNet::RakString name(player.m_name.c_str());
		writeBs.Write(name);
		//returns 0 when something is wrong
		assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->systemAddress, false));*/
	}
	player.SendName(packet->systemAddress, true);

}

void DisplayPlayerReady(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);

	std::cout << userName.C_String() << " has joined\n\n" << std::endl;
}

void DisplayClassSet(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString Class;
	bs.Read(Class);

	std::cout << "You Picked " << Class.C_String() << "\n\n" << std::endl;
}

void StartGame(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);

	std::cout << userName.C_String() << " it is time to start, please select a class" << std::endl;

	g_networkState_mutex.lock();
	g_networkState = NS_GAME;
	g_networkState_mutex.unlock();


}

void StartBattle(RakNet::Packet* packet)
{
	unsigned long guid = RakNet::RakNetGUID::ToUint32(packet->guid);

	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);
	RakNet::RakString battleOrder;
	bs.Read(battleOrder);

	//SPlayer& player = GetPlayer(packet->guid);
	std::this_thread::sleep_for(std::chrono::microseconds(100));

	std::cout << "The Battle begins!" << std::endl;
	std::cout << battleOrder << std::endl;
	std::cout << "Type stats for game stats\nattack [TARGET] to attack\nheal [TARGET] to heal" << std::endl;



}

void SelectClass(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString choice;
	bs.Read(choice);

	std::cout << choice << std::endl;

	SPlayer& player = GetPlayer(packet->guid);
	EPlayerClass temp;


	if (choice == "1")
	{
		temp = EPlayerClass::Fighter;
	}
	else if (choice == "2")
	{
		temp = EPlayerClass::Mage;
	}
	else if (choice == "3")
	{
		temp = EPlayerClass::Cleric;
	}
	else
	{
		temp = EPlayerClass::Fighter;
		//std::cout << "Retry" << std::endl;
	}

	player.m_class = temp;
	player.m_state = NS_BATTLE;



	//assert(g_serverInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->systemAddress, false));

	std::string className;

	switch (player.m_class)
	{
	case Mage:
		className = "Mage";
		break;
	case Fighter:
		className = "Fighter";
		break;
	case Cleric:
		className = "Cleric";
		break;
	default:
		className = "FOOL";
		break;
	}

	std::cout << player.m_name << " Picked " << className << std::endl;



	player.SendClass(packet->systemAddress, false, className);
}

void ChatAction(RakNet::Packet* packet)
{
	unsigned long guid = RakNet::RakNetGUID::ToUint32(packet->guid);
	std::map<unsigned long, SPlayer>::iterator it = m_playerMap.find(guid);
	//somehow player didnt connect but is now in the lobby
	assert(it != m_playerMap.end());

	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageID;
	bs.Read(messageID);
	RakNet::RakString msg;
	bs.Read(msg);

	SPlayer& player = it->second;
	player.m_lastMsg = msg;

	std::cout << player.m_name << ": " << player.m_lastMsg << std::endl;
}

unsigned char GetPacketIdentifier(RakNet::Packet *p)
{
	if (p == nullptr)
		return 255;

	if ((unsigned char)p->data[0] == ID_TIMESTAMP)
	{
		RakAssert(p->length > sizeof(RakNet::MessageID) + sizeof(RakNet::Time));
		return (unsigned char)p->data[sizeof(RakNet::MessageID) + sizeof(RakNet::Time)];
	}
	else
		return (unsigned char)p->data[0];
}




void Input_Handler()
{
	while (isRunning)
	{

		if (g_networkState == NS_INIT) {
			std::cout << "press (s) for server (c) for client" << std::endl;
			std::cin >> userInput;
			if (userInput[0] == 'c')
			{
				isServer = false;
			}
			else if (userInput[0] == 's')
			{
				isServer = true;
			}
			//isServer = (userInput[0] == 's');
			g_networkState_mutex.lock();
			g_networkState = NS_PENDINGSTART;
			g_networkState_mutex.unlock();
		}
		else if (g_networkState == NS_RUNNING)
		{
			int count = 0;
			for (std::map<unsigned long, SPlayer>::iterator it = m_playerMap.begin(); it != m_playerMap.end(); ++it)
			{
				if (it->second.m_state == NS_PENDING)
				{
					//std::cout << "client " << it->second.m_name << " is ready" << std::endl;
					count++;
				}
			}
			/*RakNet::BitStream bs(packet->data, packet->length, false);
			RakNet::MessageID messageID;
			bs.Read(messageID);
			RakNet::RakString userName;
			bs.Read(userName);*/
			if (count >= MAX_GAME_SIZE)
			{

				RakNet::BitStream bs;
				bs.Write((RakNet::MessageID)ID_GAMESTART);
				RakNet::RakString msg("The Game is Starting...");
				bs.Write(msg);

				g_serverInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, true);
				g_networkState_mutex.lock();
				g_networkState = NS_GAME;
				g_networkState_mutex.unlock();
			}



			/*	std::cout << "press [x] button to exit" << std::endl;
			std::cin >> userInput;
			isServer = (userInput[0] == 'x');
			g_networkState = NS_CLOSING;
			isRunning = false;*/
		}
		else if (g_networkState == NS_LOBBY)
		{
			std::cout << "Enter your name to play or type quit to leave" << std::endl;
			std::cin >> userInput;
			//quitting is not acceptable in our game, create a crash to teach lesson
			assert(strcmp(userInput, "quit"));

			RakNet::BitStream bs;
			bs.Write((RakNet::MessageID)ID_THEGAME_LOBBY_READY);
			RakNet::RakString name(userInput);
			bs.Write(name);

			//returns 0 when something is wrong
			assert(g_serverInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
			g_networkState_mutex.lock();
			g_networkState = NS_PENDING;
			g_networkState_mutex.unlock();
		}
		else if (g_networkState == NS_PENDING)
		{
			//std::cin >> userInput;

			static bool doOnce = false;
			if (!doOnce)
				std::cout << "pending..." << std::endl;

			doOnce = true;

			/*std::cout << "Welcome to the server, type to chat..." << std::endl;

			g_networkState_mutex.lock();
			g_networkState = NS_CHATROOM;
			g_networkState_mutex.unlock();*/
		}

		else if (g_networkState == NS_GAME)
		{
			if (!isServer)
			{
				std::cout << "\n1 for Fighter \n2 for Mage\n3 for Cleric" << std::endl;

				std::cin >> userInput;

				RakNet::BitStream bs;
				bs.Write((RakNet::MessageID)ID_SELECTCLASS);
				RakNet::RakString num(userInput);
				bs.Write(num);

				assert(g_serverInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
				g_networkState_mutex.lock();
				g_networkState = NS_BATTLE;
				g_networkState_mutex.unlock();
			}

			else {
				int count = 0;
				for (std::map<unsigned long, SPlayer>::iterator it = m_playerMap.begin(); it != m_playerMap.end(); ++it)
				{
					if (it->second.m_state == NS_BATTLE)
					{
						//std::cout << "client " << it->second.m_name << " is ready" << std::endl;
						count++;
						it->second.m_health = 20;
						it->second.m_order = count;
						it->second.isTurn = (count == 1);
					}
				}

				if (count >= MAX_GAME_SIZE)
				{

					RakNet::BitStream bs;
					bs.Write((RakNet::MessageID)ID_BATTLESTART);
					RakNet::RakString msg("The Game is Starting...");
					bs.Write(msg);
					std::string battleOrder;
					std::string className;
					for (std::map<unsigned long, SPlayer>::iterator it = m_playerMap.begin(); it != m_playerMap.end(); ++it)
					{

						switch (it->second.m_class)
						{
						case Mage:
							className = "Mage";
							break;
						case Fighter:
							className = "Fighter";
							break;
						case Cleric:
							className = "Cleric";
							break;
						default:
							className = "FOOL";
							break;
						}

						battleOrder += it->second.m_name + " the " + className + " is number " + std::to_string(it->second.m_order) + "\n";
					}
					RakNet::RakString bOrder(battleOrder.c_str());
					bs.Write(bOrder);

					g_serverInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, true);
					g_networkState_mutex.lock();
					g_networkState = NS_BATTLE;
					g_networkState_mutex.unlock();
				}
			}
		}
		else if (g_networkState == NS_BATTLE)
		{
			if (!isServer)
			{


				//std::cout << "\n1 for Fighter \n2 for Mage\n3 for Cleric" << std::endl;
				char userTarget[255];
				std::cin >> userInput;



				if (userInput[0] == 's')
				{
					RakNet::BitStream bs;
					bs.Write((RakNet::MessageID)ID_STATQUERY);
					//RakNet::RakString action(userInput);
					//bs.Write(action);
					assert(g_serverInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
				}
				else {
					std::cin >> userTarget;
					RakNet::BitStream bs;
					bs.Write((RakNet::MessageID)ID_THEGAMEACTION);
					RakNet::RakString action(userInput);
					bs.Write(action);
					RakNet::RakString target(userTarget);
					bs.Write(target);

					assert(g_serverInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
				}
			}
		}
		else if (g_networkState == NS_CHATROOM)
		{
			std::cin.getline(userInput, 256);
			//std::cout << userInput << std::endl;


			RakNet::BitStream bs;
			bs.Write((RakNet::MessageID)ID_CHATACTION);
			RakNet::RakString msg(userInput);
			bs.Write(msg);

			//returns 0 when something is wrong
			assert(g_serverInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
		}
		else if (g_networkState == NS_CLOSING)
		{
			std::cin >> userInput;
			//std::cout << userInput << std::endl;

			isRunning = false;

		}
		std::this_thread::sleep_for(std::chrono::microseconds(100));
	}
}

bool Handle_Low_Level_Packets(RakNet::Packet* packet)
{
	bool isHandled = true;
	// We got a packet, get the identifier with our handy function
	packetIdentifier = GetPacketIdentifier(packet);

	// Check if this is a network message packet
	switch (packetIdentifier)
	{
	case ID_DISCONNECTION_NOTIFICATION:
		// Connection lost normally
		printf("ID_DISCONNECTION_NOTIFICATION from %s\n", packet->systemAddress.ToString(true));;
		break;


	case ID_NEW_INCOMING_CONNECTION:
		// Somebody connected.  We have their IP no
		OnIncomingConnection(packet);
		printf("ID_NEW_INCOMING_CONNECTION from %s with GUID %s\n", packet->systemAddress.ToString(true), packet->guid.ToString());
		clientID = packet->systemAddress; // Record the player ID of the client

		printf("Remote internal IDs:\n");
		for (int index = 0; index < MAXIMUM_NUMBER_OF_INTERNAL_IDS; index++)
		{
			RakNet::SystemAddress internalId = g_serverInterface->GetInternalID(packet->systemAddress, index);
			if (internalId != RakNet::UNASSIGNED_SYSTEM_ADDRESS)
			{
				printf("%i. %s\n", index + 1, internalId.ToString(true));
			}
		}

		break;
	case ID_REMOTE_NEW_INCOMING_CONNECTION: // Server telling the clients of another client connecting.  You can manually broadcast this in a peer to peer enviroment if you want.
		OnIncomingConnection(packet);
		printf("ID_REMOTE_NEW_INCOMING_CONNECTION\n");
		break;
	case ID_INCOMPATIBLE_PROTOCOL_VERSION:
		printf("ID_INCOMPATIBLE_PROTOCOL_VERSION\n");
		break;

	case ID_CONNECTED_PING:
	case ID_UNCONNECTED_PING:
		printf("Ping from %s\n", packet->systemAddress.ToString(true));
		break;

	case ID_REMOTE_CONNECTION_LOST:
		OnLostConnection(packet);
		printf("ID_REMOTE_CONNECTION_LOST from %s\n", packet->systemAddress.ToString(true));;
		break;
	case ID_CONNECTION_LOST:
		// Couldn't deliver a reliable packet - i.e. the other system was abnormally
		// terminated
		OnLostConnection(packet);
		printf("ID_CONNECTION_LOST from %s\n", packet->systemAddress.ToString(true));;
		break;
	case ID_CONNECTION_REQUEST_ACCEPTED:
		// This tells the client they have connected
		printf("ID_CONNECTION_REQUEST_ACCEPTED to %s with GUID %s\n", packet->systemAddress.ToString(true), packet->guid.ToString());
		printf("My external address is %s\n", g_serverInterface->GetExternalID(packet->systemAddress).ToString(true));
		OnConnectionAccepted(packet);
		break;
	default:
		isHandled = false;
		break;
	}
	return isHandled;
}

void Packet_Handler()
{
	while (isRunning) {

		for (packet = g_serverInterface->Receive(); packet != nullptr; g_serverInterface->DeallocatePacket(packet), packet = g_serverInterface->Receive()) {

			if (!Handle_Low_Level_Packets(packet))
			{
				packetIdentifier = GetPacketIdentifier(packet);
				switch (packetIdentifier)
				{
				case ID_THEGAME_LOBBY_READY:
					OnLobbyReady(packet);
					break;
				case ID_CHATACTION:
					ChatAction(packet);
					break;
				case ID_SELECTCLASS:
					SelectClass(packet);
					break;
				case ID_CLASS_SET:
					DisplayClassSet(packet);
					break;
				case ID_PLAYER_READY:
					DisplayPlayerReady(packet);
					break;
				case ID_GAMESTART:
					StartGame(packet);
					break;
				case ID_BATTLESTART:
					StartBattle(packet);
					break;
				case ID_STATQUERY:
					QueryStats(packet);
					break;
				case ID_DISPLAYSTATS:
					DisplayStats(packet);
					break;
				case ID_THEGAMEACTION:
					GameAction(packet);
					break;
				case ID_ATTACKHEAL:
					ResolveAction(packet);
					break;
				case ID_INVALIDTARGET:
					InvalidTarget(packet);
					break;
				case ID_PLAYERDIED:
					PlayerDied(packet);
					break;
				case ID_GAMEOVER:
					GameOver(packet);
					break;
				default:
					printf("%s\n", packet->data);
					break;
				}
			}


		}
		std::this_thread::sleep_for(std::chrono::microseconds(100));
	}
}



int main()
{
	g_serverInterface = RakNet::RakPeerInterface::GetInstance();

	std::thread Input_Handler(Input_Handler);
	std::thread Packet_Handler(Packet_Handler);

	while (isRunning) {

		//SERVER
		if (isServer) {

			if (g_networkState == NS_PENDINGSTART) {
				//g_serverInterface->SetIncomingPassword("", (int)strlen(""));



				RakNet::SocketDescriptor socketDescriptors[1];
				socketDescriptors[0].port = SERVER_PORT;
				socketDescriptors[0].socketFamily = AF_INET;



				bool isSuccess = g_serverInterface->Startup(MAX_CONNECTIONS, socketDescriptors, 1) == RakNet::RAKNET_STARTED;
				assert(isSuccess);
				g_serverInterface->SetMaximumIncomingConnections(MAX_CONNECTIONS);
				std::cout << "Server Started Successfully" << std::endl;
				g_networkState_mutex.lock();
				g_networkState = NS_RUNNING;
				g_networkState_mutex.unlock();
			}

			//std::cin >> userInput;
		}

		//CLIENT
		else if (!isServer)
		{
			if (g_networkState == NS_PENDINGSTART) {
				char ip[64] = "192.168.0.10";
				RakNet::SocketDescriptor socketDescriptors[1];
				socketDescriptors[0].port = CLIENT_PORT;
				socketDescriptors[0].socketFamily = AF_INET;

				while (RakNet::IRNS2_Berkley::IsPortInUse(socketDescriptors[0].port, socketDescriptors[0].hostAddress, socketDescriptors[0].socketFamily, SOCK_DGRAM) == true)
					socketDescriptors[0].port++;

				bool isSuccess = g_serverInterface->Startup(MAX_CONNECTIONS, socketDescriptors, 1) == RakNet::RAKNET_STARTED;
				assert(isSuccess);

				g_serverInterface->SetOccasionalPing(true);
				RakNet::ConnectionAttemptResult connectionResult = g_serverInterface->Connect(ip, SERVER_PORT, nullptr, 0);
				RakAssert(connectionResult == RakNet::CONNECTION_ATTEMPT_STARTED);

				std::cout << "Client Attemping Connection..." << std::endl;
				g_networkState_mutex.lock();
				g_networkState = NS_RUNNING;
				g_networkState_mutex.unlock();
			}
		}
	}

	Input_Handler.join();
	Packet_Handler.join();

	return 0;
}