#pragma once

#include "BluetoothManager.hpp"
#include "MDRProtocol.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace omarchy::sony::protocol {

struct FakeHeadsetProfile {
    std::vector<uint8_t> table1;
    std::vector<uint8_t> table2;
    bool table2Supported{true};
    bool answerSupportFunction{true};
    int dropFirstDataPackets{0};
    bool replyBeforeAck{false};
    uint8_t multipointSlot{0xD1};
    std::string firmware{"2.3.1"};

    static FakeHeadsetProfile xm5();
    static FakeHeadsetProfile minimal();
};

struct FakePairedDevice {
    std::string address;
    std::string name;
    uint8_t connectionStatus{0};
};

class FakeHeadset {
public:
    explicit FakeHeadset(FakeHeadsetProfile profile = FakeHeadsetProfile::xm5());

    void reconnect();
    Bytes receive(std::span<const uint8_t> hostBytes);

    Bytes pushBattery(int level, bool charging);
    Bytes pushPayload(Table table, const Bytes& payload);

    std::vector<std::pair<Table, Bytes>> received;
    std::vector<uint8_t> receivedSeqs;
    std::vector<uint8_t> hostAckSeqs;
    int droppedPackets{0};

    FakeHeadsetProfile profile;
    int battery{80};
    uint8_t charging{0x00};
    uint8_t ncTotalEffect{0x01};
    uint8_t ncMode{0x00};
    uint8_t ncAmbientMode{0x00};
    uint8_t ncLevel{14};
    uint8_t eqPreset{0xA0};
    std::array<uint8_t, 6> eqCustom{14, 12, 11, 9, 13, 10};
    bool dsee{true};
    uint8_t connectionQuality{0x00};
    uint8_t codec{0x10};
    uint8_t volume{12};
    bool speakToChat{true};
    uint8_t s2cSensitivity{0x00};
    uint8_t s2cTimeout{0x01};
    bool voiceGuidance{true};
    int8_t voiceGuidanceVolume{0};
    uint8_t autoPowerOff{0x10};
    uint8_t ncAmbButton{0x01};
    std::vector<uint8_t> assignable{0x00};
    bool earDetection{true};
    uint8_t worn{0x00};
    bool multipoint{false};
    uint8_t playbackSlot{1};
    std::vector<FakePairedDevice> devices{{"F0:1D:BC:AA:00:01", "laptop", 1}};
    std::vector<Bytes> alertsAnswered;

private:
    void handle(Table table, const Bytes& p, Bytes& out, bool& sendAckFirst);
    void emit(Table table, const Bytes& payload, Bytes& out);
    Bytes eqValue() const;
    Bytes deviceList(uint8_t type) const;
    Bytes ncAsm(uint8_t cmd, uint8_t type) const;

    StreamFramer framer_;
    uint8_t deviceSeq_{0};
    int dropped_{0};
    bool pendingMultipoint_{false};
    bool pendingMultipointValue_{false};
};

class FakeHeadsetTransport : public IBluetoothTransport {
public:
    explicit FakeHeadsetTransport(std::shared_ptr<FakeHeadset> headset);
    ~FakeHeadsetTransport() override;

    int connect(const std::string& macAddress, uint8_t channel) override;
    void disconnect() override;
    [[nodiscard]] bool isConnected() const override { return fd_ >= 0; }
    [[nodiscard]] int getFd() const override { return fd_; }
    ssize_t send(const uint8_t* data, size_t length) override;
    ssize_t recv(uint8_t* buffer, size_t maxLength) override;
    int checkConnectResult() override { return fd_ >= 0 ? 0 : ECONNREFUSED; }
    [[nodiscard]] std::string getLastError() const override { return lastError_; }

    void inject(const Bytes& bytes);

private:
    std::shared_ptr<FakeHeadset> headset_;
    int fd_{-1};
    int peerFd_{-1};
    std::string lastError_;
};

} // namespace omarchy::sony::protocol
