#ifndef ESP_XIAOZHI_PROTOCOL_H
#define ESP_XIAOZHI_PROTOCOL_H

#include "protocol.h"

#include <esp_event.h>
#include <esp_xiaozhi_chat.h>

class EspXiaozhiProtocol : public Protocol {
public:
    EspXiaozhiProtocol();
    ~EspXiaozhiProtocol() override;

    bool Start() override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel() override;
    bool IsAudioChannelOpened() const override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;
    void SendWakeWordDetected(const std::string& wake_word) override;
    void SendStartListening(ListeningMode mode) override;
    void SendStopListening() override;
    void SendAbortSpeaking(AbortReason reason) override;

private:
    esp_xiaozhi_chat_handle_t chat_ = 0;
    esp_xiaozhi_chat_audio_t audio_ = {
        .format = "opus",
        .sample_rate = 16000,
        .channels = 1,
        .frame_duration = 60,
    };
    bool audio_channel_opened_ = false;
    bool event_registered_ = false;

    void HandleAudio(const uint8_t* data, int len);
    void HandleChatEvent(esp_xiaozhi_chat_event_t event, void* event_data);
    void HandleSystemEvent(int32_t event_id, void* event_data);
    void EmitJson(cJSON* root);
    void EmitTtsState(const char* state, const char* text = nullptr);
    void EmitChatText(const char* role, const char* text);

    static void AudioCallback(const uint8_t* data, int len, void* ctx);
    static void ChatEventCallback(esp_xiaozhi_chat_event_t event, void* event_data, void* ctx);
    static void SystemEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
};

#endif // ESP_XIAOZHI_PROTOCOL_H
