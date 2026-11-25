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

// ヘッダー構造（全体4バイト）
// ※授業資料の定義（2バイト本文長 + 2バイト種別、または 2バイト本文長 + 1バイト種別 + 1バイト予備）に合わせて調整してください
#pragma pack(push, 1)
struct MessageHeader {
	uint16_t bodyLength; // 本文長（ネットワークバイトオーダー）
	uint16_t type;       // 種別（例: 1 = text）
};
#pragma pack(pop)

// 種別の定数（※配布資料の定義値に合わせる）
constexpr uint16_t kMessageTypeText = 1;
// 本文長の上限（不正な巨大データ防止）
constexpr uint16_t kMaxBodyLength = 1024;

// 指定バイト数を最後まで送信する関数
bool SendAll(SOCKET sock, const char* data, int length) {
	int totalSent = 0;
	while (totalSent < length) {
		int sent = send(sock, data + totalSent, length - totalSent, 0);
		if (sent <= 0) {
			return false;
		}
		totalSent += sent;
	}
	return true;
}

// 指定バイト数を最後まで受信する関数
bool RecvAll(SOCKET sock, char* buffer, int length) {
	int totalRecv = 0;
	while (totalRecv < length) {
		int received = recv(sock, buffer + totalRecv, length - totalRecv, 0);
		if (received <= 0) {
			return false;
		}
		totalRecv += received;
	}
	return true;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	Novice::Initialize("NT1_01_02", 640, 360);
	char host[64] = "34.104.202.113";
	char port[8] = "8000"; // 01_02以降は8000番
	bool connected = false;

	WSADATA wsaData;
	SOCKET sock = INVALID_SOCKET;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	char sendText[256] = "Hello";
	char replyText[1024] = "";
	int sentBytes = 0;
	int receivedBytes = 0;

	auto Disconnect = [&]() {
		if (sock != INVALID_SOCKET) {
			closesocket(sock);
			sock = INVALID_SOCKET;
		}
		connected = false;
	};

	while (Novice::ProcessMessage() == 0) {
		Novice::BeginFrame();
#ifdef USE_IMGUI
		ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Once);
		ImGui::SetNextWindowSize(ImVec2(600, 300), ImGuiCond_Once);
		ImGui::Begin("TCP Message");

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
			Disconnect();
		}
		ImGui::EndDisabled();

		ImGui::Text("Status: %s", connected ? "Connected" : "Not connected");
		ImGui::Separator();

		ImGui::BeginDisabled(!connected);
		ImGui::InputText("Text", sendText, sizeof(sendText));

		if (ImGui::Button("Send")) {
			uint16_t bodyLen = static_cast<uint16_t>(std::strlen(sendText));

			// 1. フレーム送信：ヘッダー(4バイト)と本文をまとめて最後まで送る
			MessageHeader sendHeader{};
			sendHeader.bodyLength = htons(bodyLen);
			sendHeader.type = htons(kMessageTypeText);

			std::vector<char> sendFrame(sizeof(MessageHeader) + bodyLen);
			std::memcpy(sendFrame.data(), &sendHeader, sizeof(MessageHeader));
			if (bodyLen > 0) {
				std::memcpy(sendFrame.data() + sizeof(MessageHeader), sendText, bodyLen);
			}

			if (SendAll(sock, sendFrame.data(), static_cast<int>(sendFrame.size()))) {
				sentBytes = static_cast<int>(sendFrame.size());

				// 2. ヘッダー受信：4バイト受信し、本文長と種別を読む
				MessageHeader recvHeader{};
				if (RecvAll(sock, reinterpret_cast<char*>(&recvHeader), sizeof(MessageHeader))) {
					uint16_t recvBodyLen = ntohs(recvHeader.bodyLength);
					uint16_t recvType = ntohs(recvHeader.type);

					// 本文長の上限を確かめる
					if (recvBodyLen <= kMaxBodyLength) {
						std::vector<char> recvBody(recvBodyLen + 1, '\0');

						// 3. 本文受信：ヘッダーが示す本文長まで最後まで受信する
						if (recvBodyLen == 0 || RecvAll(sock, recvBody.data(), recvBodyLen)) {
							receivedBytes = sizeof(MessageHeader) + recvBodyLen;

							// 4. 表示：本文がそろい、種別が text なら終端を置き Reply へ表示する
							if (recvType == kMessageTypeText) {
								recvBody[recvBodyLen] = '\0';
								std::memcpy(replyText, recvBody.data(), recvBodyLen + 1);
							}
						} else {
							Disconnect();
						}
					} else {
						// 上限超過時は安全のため切断
						Disconnect();
					}
				} else {
					Disconnect();
				}
			} else {
				Disconnect();
			}
		}
		ImGui::EndDisabled();

		// ヘッダーを含む合計バイト数と Reply の表示
		ImGui::Text("Sent / Received: %d / %d bytes", sentBytes, receivedBytes);
		ImGui::Text("Reply: %s", replyText);

		ImGui::End();
#endif
		Novice::EndFrame();
	}

	Disconnect();
	WSACleanup();
	Novice::Finalize();
	return 0;
}