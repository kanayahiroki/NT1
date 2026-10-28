#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Novice.h>
#include <cstring>
#include <imgui.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

// Windowsアプリのmain関数。ImGuiを使うため、Debugで実行する。
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	Novice::Initialize("NT1_01_01", 640, 360);
	char host[64] = "34.104.202.113";
	char port[8] = "8001"; // 01_00・01_01のEcho用ポート
	bool connected = false;

	WSADATA wsaData;
	SOCKET sock = INVALID_SOCKET;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		// 初期化失敗時の処理
	}

	// 送受信用の変数
	char sendText[256] = "Hello";
	char echoText[256] = "";
	int totalSentBytes = 0;
	int totalRecvBytes = 0;

	while (Novice::ProcessMessage() == 0) {
		Novice::BeginFrame();
#ifdef USE_IMGUI
		ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Once);
		ImGui::SetNextWindowSize(ImVec2(600, 300), ImGuiCond_Once);
		ImGui::Begin("TCP Connection");
		ImGui::BeginDisabled(connected);
		ImGui::InputText("Host", host, sizeof(host));
		ImGui::InputText("Port", port, sizeof(port));
		if (ImGui::Button("Connect")) {
			struct addrinfo hints = {};
			struct addrinfo* result = nullptr;

			hints.ai_family = AF_INET;       // IPv4
			hints.ai_socktype = SOCK_STREAM; // TCP
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

		// 送受信UI（接続中のみ操作可能）
		ImGui::BeginDisabled(!connected);
		ImGui::InputText("Text", sendText, sizeof(sendText));

		if (ImGui::Button("Send")) {
			int sendLength = (int)strlen(sendText);
			int totalSent = 0;

			// --- 送信処理：全データを送信するまでループ ---
			while (totalSent < sendLength) {
				int sent = send(sock, sendText + totalSent, sendLength - totalSent, 0);
				if (sent == SOCKET_ERROR || sent == 0) {
					// エラー判定または切断
					break;
				}
				totalSent += sent;
			}
			totalSentBytes += totalSent;

			// --- 受信処理：送信した長さ分をしっかりと受け取る ---
			int totalRecv = 0;
			std::memset(echoText, 0, sizeof(echoText));

			while (totalRecv < sendLength) {
				int received = recv(sock, echoText + totalRecv, sendLength - totalRecv, 0);
				if (received == 0) {
					// 正常切断
					connected = false;
					closesocket(sock);
					sock = INVALID_SOCKET;
					break;
				} else if (received == SOCKET_ERROR) {
					// エラー発生による失敗判定
					break;
				}
				totalRecv += received;
			}
			totalRecvBytes += totalRecv;
		}
		ImGui::EndDisabled();

		// 結果の表示
		ImGui::Text("Echo: %s", echoText);
		ImGui::Text("Sent Bytes: %d", totalSentBytes);
		ImGui::Text("Received Bytes: %d", totalRecvBytes);

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