#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cJSON.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct AudioStreamPacket {
    int sample_rate = 0;
    int frame_duration = 0;
    uint32_t timestamp = 0;
    std::vector<uint8_t> payload;
};

enum AbortReason {
    kAbortReasonNone,
    kAbortReasonWakeWordDetected
};

enum ListeningMode {
    kListeningModeAutoStop,
    kListeningModeManualStop,
    kListeningModeRealtime // 需要 AEC 支持
};

class Protocol {
public:
    virtual ~Protocol() = default;

    inline int server_sample_rate() const {
        return server_sample_rate_;
    }
    inline int server_frame_duration() const {
        return server_frame_duration_;
    }

    void OnIncomingAudio(std::function<void(std::unique_ptr<AudioStreamPacket> packet)> callback) {
        on_incoming_audio_ = callback;
    }
    void OnIncomingJson(std::function<void(const cJSON* root)> callback) {
        on_incoming_json_ = callback;
    }
    void OnAudioChannelOpened(std::function<void()> callback) {
        on_audio_channel_opened_ = callback;
    }
    void OnAudioChannelClosed(std::function<void()> callback) {
        on_audio_channel_closed_ = callback;
    }
    void OnNetworkError(std::function<void(const std::string& message)> callback) {
        on_network_error_ = callback;
    }
    void OnConnected(std::function<void()> callback) {
        on_connected_ = callback;
    }
    void OnDisconnected(std::function<void()> callback) {
        on_disconnected_ = callback;
    }

    virtual bool Start() = 0;
    virtual bool OpenAudioChannel() = 0;
    virtual void CloseAudioChannel() = 0;
    virtual bool IsAudioChannelOpened() const = 0;
    virtual bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) = 0;
    virtual void SendWakeWordDetected(const std::string& wake_word) = 0;
    virtual void SendStartListening(ListeningMode mode) = 0;
    virtual void SendStopListening() = 0;
    virtual void SendAbortSpeaking(AbortReason reason) = 0;

protected:
    std::function<void(const cJSON* root)> on_incoming_json_;
    std::function<void(std::unique_ptr<AudioStreamPacket> packet)> on_incoming_audio_;
    std::function<void()> on_audio_channel_opened_;
    std::function<void()> on_audio_channel_closed_;
    std::function<void(const std::string& message)> on_network_error_;
    std::function<void()> on_connected_;
    std::function<void()> on_disconnected_;

    int server_sample_rate_ = 24000;
    int server_frame_duration_ = 60;

    void SetError(const std::string& message) {
        if (on_network_error_ != nullptr) {
            on_network_error_(message);
        }
    }
};

#endif // PROTOCOL_H
