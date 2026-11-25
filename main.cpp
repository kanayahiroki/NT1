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
};

struct PlayerState {
	uint16_t id, x, y;
};

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	Novice::Initialize("NT1_01_03", 1280, 720);
	char host[64] = "34.104.202.113";
	char port[8] = "8000";
	bool connected = false;

	WSADATA wsaData{};
	SOCKET clientSocket = INVALID_SOCKET;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		Novice::Finalize();
		return 1;
	}

	std::vector<PlayerState> players;
	uint32_t gameLoop = 0;

	while (Novice::ProcessMessage() == 0) {
		Novice::BeginFrame();
		gameLoop++;

		// --- 受信・一覧更新処理 ---
		if (connected) {
			char receiveFrame[1028]{};
			int receivedBytes = 0;
			uint16_t payloadSize = 0;
			MessageType type{};

			// 1. ヘッダー4バイトを受信
			while (connected && receivedBytes < 4) {
				int n = recv(clientSocket, receiveFrame + receivedBytes, 4 - receivedBytes, 0);
				if (n <= 0) {
					connected = false;
					break;
				}
				receivedBytes += n;
			}

			// 2. 本文長 payloadSize と種別 type を2バイトずつ読む
			if (connected) {
				std::memcpy(&payloadSize, receiveFrame, 2);
				std::memcpy(&type, receiveFrame + 2, 2);

				if (payloadSize > 1024) {
					connected = false;
				}
			}

			// 3. 接続中なら、receivedBytes が 4 + payloadSize になるまで recv する
			while (connected && receivedBytes < 4 + payloadSize) {
				int n = recv(clientSocket, receiveFrame + receivedBytes, (4 + payloadSize) - receivedBytes, 0);
				if (n <= 0) {
					connected = false;
					break;
				}
				receivedBytes += n;
			}

			// 4. 1フレームがそろったら、種別に応じた処理へ進む
			if (connected) {
				const char* payload = receiveFrame + 4;
				if (type == MessageType::Disconnect) {
					connected = false;
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

			// 切断していたらソケットを閉じ、一覧を空にする
			if (!connected && clientSocket != INVALID_SOCKET) {
				closesocket(clientSocket);
				clientSocket = INVALID_SOCKET;
				players.clear();
			}
		}

		// --- 描画処理：一覧の X・Y で全員を同じ色に描く ---
		for (const PlayerState& player : players) {
			uint32_t color = 0x55BD79FF;
			Novice::DrawEllipse(player.x, player.y, 16, 16, 0.0f, color, kFillModeSolid);
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

						// JoinRequest をヘッダーだけで作る
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
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::Text("Status: %s | Players: %zu | Game loop: %u", connected ? "Connected" : "Not connected", players.size(), gameLoop);

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