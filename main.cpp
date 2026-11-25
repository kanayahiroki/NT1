#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Novice.h>
#include <cstdint>
#include <cstring>
#include <imgui.h>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

enum class MessageType : uint16_t {
	Text = 1,
	AssignPlayerId = 2,
	JoinRequest = 3,
	WorldState = 4,
	Disconnect = 5,
	PlayerInput = 6,
};

struct PlayerState {
	uint16_t id, x, y;
};

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	Novice::Initialize("NT1_01_04", 1280, 720);
	char host[64] = "34.104.202.113";
	char port[8] = "8000";
	bool connected = false;

	WSADATA wsaData{};
	SOCKET clientSocket = INVALID_SOCKET;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		Novice::Finalize();
		return 1;
	}

	char keys[256]{};
	std::vector<PlayerState> players;
	uint16_t playerId = 0;
	uint32_t loopCount = 0;

	while (Novice::ProcessMessage() == 0) {
		Novice::BeginFrame();
		loopCount++;

		Novice::GetHitKeyStateAll(keys);

		if (connected) {
			// --- 1. 入力を2バイトにまとめて PlayerInput（6バイト）を送信（p12〜p14） ---
			uint16_t inputBits = 0;
			if (keys[DIK_W]) {
				inputBits |= 0x0001;
			}
			if (keys[DIK_S]) {
				inputBits |= 0x0002;
			}
			if (keys[DIK_A]) {
				inputBits |= 0x0004;
			}
			if (keys[DIK_D]) {
				inputBits |= 0x0008;
			}

			uint16_t sendPayloadSize = 2;
			MessageType sendType = MessageType::PlayerInput;
			char sendFrame[6]{};
			std::memcpy(sendFrame, &sendPayloadSize, 2);
			std::memcpy(sendFrame + 2, &sendType, 2);
			std::memcpy(sendFrame + 4, &inputBits, 2);

			int frameSize = 6;
			int sentBytes = 0;
			while (connected && sentBytes < frameSize) {
				int n = send(clientSocket, sendFrame + sentBytes, frameSize - sentBytes, 0);
				if (n <= 0) {
					connected = false;
					break;
				}
				sentBytes += n;
			}

			// --- 2. 1フレームを受信（ヘッダー4バイト + 本文） ---
			char receiveFrame[1028]{};
			int receivedBytes = 0;
			uint16_t payloadSize = 0;
			MessageType type{};

			while (connected && receivedBytes < 4) {
				int n = recv(clientSocket, receiveFrame + receivedBytes, 4 - receivedBytes, 0);
				if (n <= 0) {
					connected = false;
					break;
				}
				receivedBytes += n;
			}

			if (connected) {
				std::memcpy(&payloadSize, receiveFrame, 2);
				std::memcpy(&type, receiveFrame + 2, 2);

				if (payloadSize > 1024) {
					connected = false;
				}
			}

			while (connected && receivedBytes < 4 + payloadSize) {
				int n = recv(clientSocket, receiveFrame + receivedBytes, (4 + payloadSize) - receivedBytes, 0);
				if (n <= 0) {
					connected = false;
					break;
				}
				receivedBytes += n;
			}

			// --- 3. 種別ごとの処理（AssignPlayerId / WorldState / Disconnect） ---
			if (connected) {
				const char* payload = receiveFrame + 4;
				if (type == MessageType::Disconnect) {
					connected = false;
				} else if (type == MessageType::AssignPlayerId && payloadSize == 2) {
					std::memcpy(&playerId, payload, 2);
				} else if (type == MessageType::WorldState && payloadSize >= 2) {
					uint16_t count = 0;
					std::memcpy(&count, payload, 2);

					if (count <= 128 && payloadSize == 2 + count * 6) {
						players.resize(count);
						for (int i = 0; i < count; ++i) {
							std::memcpy(&players[i].id, payload + 2 + i * 6, 2);
							std::memcpy(&players[i].x, payload + 4 + i * 6, 2);
							std::memcpy(&players[i].y, payload + 6 + i * 6, 2);
						}
					}
				}
			}

			// --- 4. 切断検出後は接続を閉じ、一覧とIDを戻す ---
			if (!connected && clientSocket != INVALID_SOCKET) {
				closesocket(clientSocket);
				clientSocket = INVALID_SOCKET;
				players.clear();
				playerId = 0;
			}
		}

		// --- 5. 描画処理（p6・p7） ---
		for (const PlayerState& player : players) {
			uint32_t color = (player.id == playerId) ? 0xFFAB40FF : 0x55BD79FF;
			Novice::DrawEllipse(player.x, player.y, 16, 16, 0.0f, color, kFillModeSolid);

			if (player.id == playerId) {
				int ring = 20 + static_cast<int>(loopCount % 30); // 20〜49pxをくり返す
				Novice::DrawEllipse(player.x, player.y, ring, ring, 0.0f, 0xFFAB40FF, kFillModeWireFrame);
			}
		}

		// --- ImGui UI ---
#ifdef USE_IMGUI
		ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
		ImGui::SetNextWindowSize(ImVec2(1260, 100), ImGuiCond_Once);
		ImGui::Begin("TCP Client");

		ImGui::BeginDisabled(connected);
		ImGui::SetNextItemWidth(160);
		ImGui::InputText("Host", host, sizeof(host));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80);
		ImGui::InputText("Port", port, sizeof(port));
		ImGui::SameLine();
		if (ImGui::Button("Connect")) {
			struct addrinfo hints = {};
			struct addrinfo* result = nullptr;
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;

			if (getaddrinfo(host, port, &hints, &result) == 0) {
				clientSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
				if (clientSocket != INVALID_SOCKET) {
					if (connect(clientSocket, result->ai_addr, (int)result->ai_addrlen) != SOCKET_ERROR) {
						connected = true;
						playerId = 0;

						// JoinRequestは接続後に一度送り、入力送信より先にする
						uint16_t payloadSize = 0;
						MessageType type = MessageType::JoinRequest;
						char sendFrame[4]{};
						std::memcpy(sendFrame, &payloadSize, 2);
						std::memcpy(sendFrame + 2, &type, 2);

						int sentBytes = 0;
						while (connected && sentBytes < 4) {
							int n = send(clientSocket, sendFrame + sentBytes, 4 - sentBytes, 0);
							if (n <= 0) {
								connected = false;
								break;
							}
							sentBytes += n;
						}

						if (!connected) {
							closesocket(clientSocket);
							clientSocket = INVALID_SOCKET;
						}
					} else {
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
					}
				}
				freeaddrinfo(result);
			}
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::BeginDisabled(!connected);
		if (ImGui::Button("Disconnect")) {
			connected = false;
			if (clientSocket != INVALID_SOCKET) {
				closesocket(clientSocket);
				clientSocket = INVALID_SOCKET;
			}
			players.clear();
			playerId = 0;
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::Text("Status: %s", connected ? "Connected" : "Not connected");

		ImGui::Text("%s: done Game loop: %u Player ID: %u / Players: %zu", port, loopCount, playerId, players.size());

		ImGui::End();
#endif
		Novice::EndFrame();
	}

	if (clientSocket != INVALID_SOCKET) {
		closesocket(clientSocket);
		clientSocket = INVALID_SOCKET;
	}
	WSACleanup();
	Novice::Finalize();
	return 0;
}