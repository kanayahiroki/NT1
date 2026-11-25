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

// ※種別(Type)の番号は授業資料の定義に合わせて変更してください
enum class MessageType : uint8_t {
	JoinRequest = 1,
	WorldState = 2,
	Disconnect = 3,
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

// 受信バッファにデータが来ているか確認（ブロッキング回避用）
bool HasReadableData(SOCKET sock) {
	fd_set readSet;
	FD_ZERO(&readSet);
	FD_SET(sock, &readSet);
	timeval timeout = {0, 0}; // 待ち時間0で即座に確認
	int res = select(0, &readSet, nullptr, nullptr, &timeout);
	return res > 0 && FD_ISSET(sock, &readSet);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	// 画面サイズは課題指定の 1280x720
	Novice::Initialize("NT1_01_03", 1280, 720);
	char host[64] = "34.104.202.113";
	char port[8] = "8003"; // 01_03用のポート番号に変更してください
	bool connected = false;

	WSADATA wsaData;
	SOCKET sock = INVALID_SOCKET;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	std::vector<Player> players;

	// 切断時の共通処理
	auto DisconnectFromServer = [&]() {
		if (sock != INVALID_SOCKET) {
			closesocket(sock);
			sock = INVALID_SOCKET;
		}
		connected = false;
		players.clear(); // 一覧を空にする
	};

	while (Novice::ProcessMessage() == 0) {
		Novice::BeginFrame();

		// --- 受信・一覧更新処理（接続中のみ毎ループ実行） ---
		if (connected && sock != INVALID_SOCKET) {
			// データが届いている場合のみ1フレーム読み切る
			while (HasReadableData(sock)) {
				// 1. ヘッダー4バイトを受信
				// 例: [0-1]本文長(2byte), [2]種別(1byte), [3]予備(1byte) ※資料の仕様に合わせること
				uint8_t header[4] = {};
				if (!RecvAll(sock, reinterpret_cast<char*>(header), 4)) {
					DisconnectFromServer();
					break;
				}

				// ヘッダーから本文長と種別を取り出す（ビッグエンディアン想定）
				uint16_t bodyLen = (static_cast<uint16_t>(header[0]) << 8) | header[1];
				uint8_t msgType = header[2];

				// 2. 本文を受信
				std::vector<uint8_t> body(bodyLen);
				if (bodyLen > 0) {
					if (!RecvAll(sock, reinterpret_cast<char*>(body.data()), bodyLen)) {
						DisconnectFromServer();
						break;
					}
				}

				// 3. 種別ごとの振り分け
				if (msgType == static_cast<uint8_t>(MessageType::Disconnect)) {
					// Disconnect なら接続を閉じる
					DisconnectFromServer();
					break;
				} else if (msgType == static_cast<uint8_t>(MessageType::WorldState)) {
					// WorldState の人数・本文長を確かめ、players を更新する
					// ※以下は「先頭1バイト(または2バイト)が人数、以降1人あたりID(4B)+X(4B)+Y(4B)」の場合の例
					if (bodyLen >= 1) {
						uint8_t count = body[0];
						size_t expectedSize = 1 + count * 12; // 資料のデータ構造に合わせて調整

						if (bodyLen >= expectedSize) {
							players.clear();
							size_t offset = 1;
							for (uint8_t i = 0; i < count; ++i) {
								Player p{};
								// ネットワークバイトオーダーから変換して読み出し
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
				// WorldState 以外は読み飛ばす（すでに本文までRecvAll済みなので何もしない）
			}
		}

		// --- 描画処理 ---
		// 全員を半径16、色 0x55BD79FF の円で描く
		for (const auto& p : players) {
			Novice::DrawEllipse(static_cast<int>(p.x), static_cast<int>(p.y), 16, 16, 0.0f, 0x55BD79FF, kFillModeSolid);
		}

		// --- ImGui UI（画面上部に配置） ---
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

						// 【参加】接続成功後に JoinRequest を一度送る（本文長0、全体4バイト）
						uint8_t joinFrame[4] = {
						    0x00, 0x00,                                     // 本文長 0
						    static_cast<uint8_t>(MessageType::JoinRequest), // 種別
						    0x00                                            // 予備
						};
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

		// 状態・Players数・Game loopの表示
		ImGui::Text("Status: %s | Players: %zu | Game loop: Running", connected ? "Connected" : "Not connected", players.size());

		ImGui::End();
#endif
		Novice::EndFrame();
	}

	DisconnectFromServer();
	WSACleanup();
	Novice::Finalize();
	return 0;
}