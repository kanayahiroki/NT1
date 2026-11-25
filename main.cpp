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

// ※メッセージ種別の番号はスライド資料の定義に合わせて変更してください
enum class MessageType : uint8_t {
	JoinRequest = 1,
	AssignPlayerId = 2,
	WorldState = 3,
	PlayerInput = 4,
	Disconnect = 5,
};

struct Player {
	uint32_t id;
	float x;
	float y;
};

// 指定バイト数を確実に送信する
bool SendAll(SOCKET sock, const char* data, int length) {
	int totalSent = 0;
	while (totalSent < length) {
		int sent = send(sock, data + totalSent, length - totalSent, 0);
		if (sent <= 0)
			return false;
		totalSent += sent;
	}
	return true;
}

// 指定バイト数を確実に受信する
bool RecvAll(SOCKET sock, char* buffer, int length) {
	int totalRecv = 0;
	while (totalRecv < length) {
		int received = recv(sock, buffer + totalRecv, length - totalRecv, 0);
		if (received <= 0)
			return false;
		totalRecv += received;
	}
	return true;
}

// 受信バッファにデータが来ているか確認（ブロッキングソケットのまま受信可否を判定）
bool HasReadableData(SOCKET sock) {
	fd_set readSet;
	FD_ZERO(&readSet);
	FD_SET(sock, &readSet);
	timeval timeout = {0, 0};
	int res = select(0, &readSet, nullptr, nullptr, &timeout);
	return res > 0 && FD_ISSET(sock, &readSet);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	Novice::Initialize("NT1_01_04", 1280, 720);
	char host[64] = "34.104.202.113";
	char port[8] = "8004"; // 01_04用のポート番号に変更してください
	bool connected = false;

	WSADATA wsaData;
	SOCKET sock = INVALID_SOCKET;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	char keys[256] = {0};
	char preKeys[256] = {0};

	std::vector<Player> players;
	uint32_t playerId = 0; // サーバーから受け取る自分のID（0は未割当想定）
	bool hasPlayerId = false;
	uint32_t loopCount = 0; // 自分の周りに広がる輪用のループ回数

	// 【切断】Disconnectの受信・切断検出後は接続を閉じ、一覧とIDを戻す
	auto DisconnectFromServer = [&]() {
		if (sock != INVALID_SOCKET) {
			closesocket(sock);
			sock = INVALID_SOCKET;
		}
		connected = false;
		players.clear();
		playerId = 0;
		hasPlayerId = false;
	};

	while (Novice::ProcessMessage() == 0) {
		Novice::BeginFrame();
		memcpy(preKeys, keys, 256);
		Novice::GetHitKeyStateAll(keys);

		loopCount++;

		if (connected && sock != INVALID_SOCKET) {
			// --- 1. 【送信】PlayerInputを毎ループ作り、無入力の0も送る ---
			// ※入力フォーマットは資料に合わせて変更してください（例：1バイトのビットフラグ）
			uint8_t inputFlags = 0;
			if (keys[DIK_W])
				inputFlags |= (1 << 0); // 上
			if (keys[DIK_S])
				inputFlags |= (1 << 1); // 下
			if (keys[DIK_A])
				inputFlags |= (1 << 2); // 左
			if (keys[DIK_D])
				inputFlags |= (1 << 3); // 右

			// ヘッダー4バイト + 本文1バイト（資料の形式に合わせる）
			uint8_t inputFrame[5] = {
			    0x00, 0x01, // 本文長 1
			    static_cast<uint8_t>(MessageType::PlayerInput),
			    0x00,      // 予備
			    inputFlags // 本文（無入力なら0）
			};
			if (!SendAll(sock, reinterpret_cast<const char*>(inputFrame), sizeof(inputFrame))) {
				DisconnectFromServer();
			}

			// --- 2. 【受信】届いているフレームを読み切る ---
			while (connected && HasReadableData(sock)) {
				uint8_t header[4] = {};
				if (!RecvAll(sock, reinterpret_cast<char*>(header), 4)) {
					DisconnectFromServer();
					break;
				}

				uint16_t bodyLen = (static_cast<uint16_t>(header[0]) << 8) | header[1];
				uint8_t msgType = header[2];

				std::vector<uint8_t> body(bodyLen);
				if (bodyLen > 0) {
					if (!RecvAll(sock, reinterpret_cast<char*>(body.data()), bodyLen)) {
						DisconnectFromServer();
						break;
					}
				}

				if (msgType == static_cast<uint8_t>(MessageType::Disconnect)) {
					DisconnectFromServer();
					break;
				} else if (msgType == static_cast<uint8_t>(MessageType::AssignPlayerId)) {
					// 【ID】AssignPlayerIdの本文長を確かめ、本文のIDをplayerIdへ保存する
					if (bodyLen >= 4) {
						uint32_t netId = 0;
						std::memcpy(&netId, body.data(), 4);
						playerId = ntohl(netId);
						hasPlayerId = true;
					}
				} else if (msgType == static_cast<uint8_t>(MessageType::WorldState)) {
					// 【一覧】WorldStateから全員の座標を取得（クライアント側では座標計算しない）
					if (bodyLen >= 1) {
						uint8_t count = body[0];
						size_t expectedSize = 1 + count * 12; // 資料の構造に合わせて調整

						if (bodyLen >= expectedSize) {
							players.clear();
							size_t offset = 1;
							for (uint8_t i = 0; i < count; ++i) {
								Player p{};
								uint32_t netId, netX, netY;
								std::memcpy(&netId, &body[offset], 4);
								std::memcpy(&netX, &body[offset + 4], 4);
								std::memcpy(&netY, &body[offset + 8], 4);
								offset += 12;

								p.id = ntohl(netId);
								uint32_t hostX = ntohl(netX);
								uint32_t hostY = ntohl(netY);
								std::memcpy(&p.x, &hostX, sizeof(float));
								std::memcpy(&p.y, &hostY, sizeof(float));

								players.push_back(p);
							}
						}
					}
				}
			}
		}

		// --- 3. 【描画】WorldStateの座標で全員を描画 ---
		for (const auto& p : players) {
			bool isMe = (hasPlayerId && p.id == playerId);

			// 自分のIDだけ色を変える（他人は 0x55BD79FF、自分は例えば橙色 0xF59E0BFF）
			unsigned int color = isMe ? 0xF59E0BFF : 0x55BD79FF;

			Novice::DrawEllipse(static_cast<int>(p.x), static_cast<int>(p.y), 16, 16, 0.0f, color, kFillModeSolid);

			// 自分の周りにループの回数で広がる輪を描く
			if (isMe) {
				int ringRadius = 18 + (loopCount % 30); // 18〜47pxで広がるアニメーション例
				Novice::DrawEllipse(static_cast<int>(p.x), static_cast<int>(p.y), ringRadius, ringRadius, 0.0f, color, kFillModeWireFrame);
			}
		}

		// --- ImGui UI ---
#ifdef USE_IMGUI
		ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
		ImGui::SetNextWindowSize(ImVec2(1260, 120), ImGuiCond_Once);
		ImGui::Begin("Network Status");

		ImGui::BeginDisabled(connected);
		ImGui::SetNextItemWidth(180);
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
				sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
				if (sock != INVALID_SOCKET) {
					if (connect(sock, result->ai_addr, (int)result->ai_addrlen) != SOCKET_ERROR) {
						connected = true;

						// 【参加】JoinRequestは接続後に一度送り、入力送信より先にする
						uint8_t joinFrame[4] = {0x00, 0x00, static_cast<uint8_t>(MessageType::JoinRequest), 0x00};
						if (!SendAll(sock, reinterpret_cast<const char*>(joinFrame), 4)) {
							DisconnectFromServer();
						}
					} else {
						closesocket(sock);
						sock = INVALID_SOCKET;
					}
				}
				freeaddrinfo(result);
			}
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::BeginDisabled(!connected);
		if (ImGui::Button("Disconnect")) {
			DisconnectFromServer();
		}
		ImGui::EndDisabled();

		ImGui::Text("Status: %s | My ID: %u | Players: %zu | Loop: %u", connected ? "Connected" : "Not connected", playerId, players.size(), loopCount);

		ImGui::End();
#endif
		Novice::EndFrame();
	}

	DisconnectFromServer();
	WSACleanup();
	Novice::Finalize();
	return 0;
}