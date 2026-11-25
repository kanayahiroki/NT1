#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Novice.h>
#include <cstdint>
#include <cstring>
#include <imgui.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

// 01_02 資料 p6 の定義通り
enum class MessageType : uint16_t {
	Text = 1,
};

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	Novice::Initialize("NT1_01_02", 640, 360);
	char host[64] = "34.104.202.113";
	char port[8] = "8000"; // 01_02以降は8000番
	bool connected = false;

	WSADATA wsaData{};
	SOCKET clientSocket = INVALID_SOCKET;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		Novice::Finalize();
		return 1;
	}

	char sendText[256] = "Hello";
	char replyText[1025] = "";
	int lastSentBytes = 0;
	int lastRecvBytes = 0;

	auto Disconnect = [&]() {
		if (clientSocket != INVALID_SOCKET) {
			closesocket(clientSocket);
			clientSocket = INVALID_SOCKET;
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
				clientSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
				if (clientSocket != INVALID_SOCKET) {
					if (connect(clientSocket, result->ai_addr, (int)result->ai_addrlen) != SOCKET_ERROR) {
						connected = true;
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
			Disconnect();
		}
		ImGui::EndDisabled();

		ImGui::Text("Status: %s", connected ? "Connected" : "Not connected");
		ImGui::Separator();

		ImGui::BeginDisabled(!connected);
		ImGui::InputText("Text", sendText, sizeof(sendText));

		if (ImGui::Button("Send")) {
			// 1. 本文長・種別・本文を1つの配列へまとめる（p13）
			uint16_t length = static_cast<uint16_t>(std::strlen(sendText));
			if (length <= 1024) {
				MessageType sendType = MessageType::Text;
				char sendFrame[1028]{};
				std::memcpy(sendFrame, &length, 2);
				std::memcpy(sendFrame + 2, &sendType, 2);
				if (length > 0) {
					std::memcpy(sendFrame + 4, sendText, length);
				}

				int totalSendSize = 4 + length;
				int sentBytes = 0;

				// 2. 作ったフレーム全体を send で最後まで送る（p14）
				while (connected && sentBytes < totalSendSize) {
					int n = send(clientSocket, sendFrame + sentBytes, totalSendSize - sentBytes, 0);
					if (n <= 0) {
						connected = false;
						break;
					}
					sentBytes += n;
				}

				if (connected) {
					lastSentBytes = sentBytes;

					// 3. 返信を受信する（ヘッダー4 + 本文上限1024 + 終端1 = 1029バイト）（p20）
					char receiveFrame[1029]{};
					int receivedBytes = 0;

					// まずヘッダー4バイトを受信
					while (connected && receivedBytes < 4) {
						int n = recv(clientSocket, receiveFrame + receivedBytes, 4 - receivedBytes, 0);
						if (n <= 0) {
							connected = false;
							break;
						}
						receivedBytes += n;
					}

					uint16_t payloadSize = 0;
					MessageType recvType{};
					if (connected) {
						std::memcpy(&payloadSize, receiveFrame, 2);
						std::memcpy(&recvType, receiveFrame + 2, 2);

						// 本文長の上限（1024バイト）チェック
						if (payloadSize > 1024) {
							connected = false;
						}
					}

					// ヘッダーが示す本文長まで受信
					while (connected && receivedBytes < 4 + payloadSize) {
						int n = recv(clientSocket, receiveFrame + receivedBytes, (4 + payloadSize) - receivedBytes, 0);
						if (n <= 0) {
							connected = false;
							break;
						}
						receivedBytes += n;
					}

					// 4. 全体がそろい、種別が Text であることを確かめてから表示する（p20）
					if (connected) {
						lastRecvBytes = receivedBytes;
						if (recvType == MessageType::Text) {
							receiveFrame[receivedBytes] = '\0'; // 受信済みの直後に表示用の終端を置く
							std::memcpy(replyText, receiveFrame + 4, payloadSize + 1);
						}
					}
				}

				if (!connected) {
					Disconnect();
				}
			}
		}
		ImGui::EndDisabled();

		ImGui::Text("Sent / Received: %d / %d bytes", lastSentBytes, lastRecvBytes);
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