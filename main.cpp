#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Novice.h>
#include <cstdint>
#include <cstring>
#include <imgui.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

// 指定したバイト数を完全に送信する関数
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

// 指定したバイト数を完全に受信する関数
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

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	Novice::Initialize("NT1_01_02", 640, 360);
	char host[64] = "34.104.202.113";
	char port[8] = "8002"; // 01_02用のポート（必要に応じて変更）
	bool connected = false;

	WSADATA wsaData;
	SOCKET sock = INVALID_SOCKET;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		// 初期化失敗処理
	}

	char sendText[256] = "Hello";
	char replyText[512] = "";
	int lastSentBytes = 0;
	int lastRecvBytes = 0;

	while (Novice::ProcessMessage() == 0) {
		Novice::BeginFrame();
#ifdef USE_IMGUI
		ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Once);
		ImGui::SetNextWindowSize(ImVec2(600, 320), ImGuiCond_Once);
		ImGui::Begin("TCP Connection");
		ImGui::BeginDisabled(connected);
		ImGui::InputText("Host", host, sizeof(host));
		ImGui::InputText("Port", port, sizeof(port));
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
			if (sock != INVALID_SOCKET) {
				closesocket(sock);
				sock = INVALID_SOCKET;
			}
			connected = false;
		}
		ImGui::EndDisabled();
		ImGui::Text("Status: %s", connected ? "Connected" : "Not connected");

		ImGui::Separator();

		ImGui::BeginDisabled(!connected);
		ImGui::InputText("Text", sendText, sizeof(sendText));

		if (ImGui::Button("Send")) {
			uint32_t bodyLen = static_cast<uint32_t>(strlen(sendText));

			// 1. フレーム送信: 4バイトのヘッダー(ネットワークバイトオーダー) + 本文
			uint32_t netLen = htonl(bodyLen); // ビッグエンディアンに変換

			// 送信バッファの作成（ヘッダー4バイト + 本文）
			char sendBuf[300];
			std::memcpy(sendBuf, &netLen, 4);
			std::memcpy(sendBuf + 4, sendText, bodyLen);

			int totalSendSize = 4 + bodyLen;
			if (SendAll(sock, sendBuf, totalSendSize)) {
				lastSentBytes = totalSendSize;

				// 2. ヘッダー受信: 4バイト受信して本文長を取得・種別確認
				uint32_t recvNetLen = 0;
				if (RecvAll(sock, reinterpret_cast<char*>(&recvNetLen), 4)) {
					uint32_t recvBodyLen = ntohl(recvNetLen); // ホストバイトオーダーに戻す

					// 3. 本文受信: ヘッダーが示す本文長まで正確に受信
					if (recvBodyLen < sizeof(replyText)) {
						std::memset(replyText, 0, sizeof(replyText));
						if (RecvAll(sock, replyText, recvBodyLen)) {
							lastRecvBytes = 4 + recvBodyLen;
						} else {
							connected = false; // 受信失敗
						}
					}
				} else {
					connected = false; // 受信失敗
				}
			} else {
				connected = false; // 送信失敗
			}
		}
		ImGui::EndDisabled();

		// 4. 表示
		ImGui::Text("Reply: %s", replyText);
		ImGui::Text("Sent / Received: %d / %d bytes", lastSentBytes, lastRecvBytes);

		ImGui::End();
#endif
		Novice::EndFrame();
	}

	if (sock != INVALID_SOCKET) {
		closesocket(sock);
		sock = INVALID_SOCKET;
	}
	WSACleanup();

	Novice::Finalize();
	return 0;
}