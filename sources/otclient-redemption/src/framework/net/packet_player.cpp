/*
* Copyright (c) 2010-2026 OTClient <https://github.com/edubart/otclient>
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*/

#include "packet_player.h"

#include "inputmessage.h"
#include "framework/core/clock.h"
#include "framework/core/eventdispatcher.h"
#include "framework/core/resourcemanager.h"

namespace
{
bool isSafeCamPlaybackFileName(const std::string_view file)
{
    if (file.empty())
        return false;

    const std::filesystem::path path{ std::string(file) };
    if (path.has_parent_path() || path.filename().string() != std::string(file) || path.stem().empty())
        return false;

    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](const unsigned char c) { return std::tolower(c); });
    return extension == ".cam";
}

uint8_t hexNibble(const char value)
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    return value - 'A' + 10;
}
}

PacketPlayer::~PacketPlayer()
{
    if (m_event)
        m_event->cancel();
}

PacketPlayer::PacketPlayer(const std::string_view& file)
{
    if (!isSafeCamPlaybackFileName(file))
        return;

    const auto path = std::filesystem::path(g_resources.getWriteDir()) / "records" / std::string(file);
    std::ifstream f(path);
    if (!f.is_open())
        return;

    std::string line;
    if (!std::getline(f, line))
        return;

    uint16_t formatVersion = 0;
    uint16_t awareLeft = 0;
    uint16_t awareTop = 0;
    uint16_t awareRight = 0;
    uint16_t awareBottom = 0;
    std::istringstream header(line);
    std::string magic;
    header >> magic >> formatVersion >> m_clientVersion >> m_protocolVersion
           >> awareLeft >> awareTop >> awareRight >> awareBottom;
    if (!header || magic != "OTCAM" || formatVersion != 1 ||
        m_clientVersion < 740 || m_protocolVersion < 740 ||
        awareLeft == 0 || awareTop == 0 || awareRight == 0 || awareBottom == 0 ||
        awareLeft > 64 || awareTop > 64 || awareRight > 64 || awareBottom > 64) {
        return;
    }

    m_awareLeft = static_cast<uint8_t>(awareLeft);
    m_awareTop = static_cast<uint8_t>(awareTop);
    m_awareRight = static_cast<uint8_t>(awareRight);
    m_awareBottom = static_cast<uint8_t>(awareBottom);

    ticks_t previousTime = 0;
    while (std::getline(f, line)) {
        std::istringstream packetLine(line);
        char type = 0;
        ticks_t time = 0;
        std::string packetHex;
        packetLine >> type >> time >> packetHex;

        if (!packetLine || type != '<' || packetHex.empty() || packetHex.size() % 2 != 0 ||
            packetHex.size() > InputMessage::BUFFER_MAXSIZE * 2 || time < previousTime ||
            !std::ranges::all_of(packetHex, [](const unsigned char c) { return std::isxdigit(c); })) {
            continue;
        }

        auto packet = std::make_shared<std::vector<uint8_t>>();
        packet->reserve(packetHex.size() / 2);
        for (size_t i = 0; i < packetHex.size(); i += 2)
            packet->push_back(static_cast<uint8_t>((hexNibble(packetHex[i]) << 4) | hexNibble(packetHex[i + 1])));

        m_input.emplace_back(time, std::move(packet));
        previousTime = time;
    }

    m_valid = !m_input.empty();
    if (m_valid)
        m_duration = m_input.back().first;
}

void PacketPlayer::start(std::function<void(std::shared_ptr<std::vector<uint8_t>>)> recvCallback,
                         std::function<void(std::error_code)> disconnectCallback)
{
    m_recvCallback = recvCallback;
    m_disconnectCallback = disconnectCallback;
    m_lastRealTick = g_clock.millis();
    m_started = true;
    scheduleProcess();
}

void PacketPlayer::stop()
{
    if (m_event)
        m_event->cancel();
    m_event = nullptr;
}

void PacketPlayer::syncPosition()
{
    const ticks_t now = g_clock.millis();
    if (m_started && !m_paused && !m_seeking) {
        const ticks_t elapsed = std::max<ticks_t>(0, now - m_lastRealTick);
        const auto advanced = static_cast<ticks_t>(std::llround(static_cast<double>(elapsed) * m_speed));
        m_position = std::min(m_duration, m_position + advanced);
    }
    m_lastRealTick = now;
}

void PacketPlayer::scheduleProcess(const ticks_t delay)
{
    if (m_event)
        m_event->cancel();
    m_event = g_dispatcher.scheduleEvent(std::bind(&PacketPlayer::process, this), std::max<ticks_t>(1, delay));
}

ticks_t PacketPlayer::getPosition()
{
    syncPosition();
    return m_position;
}

void PacketPlayer::setSpeed(const double speed)
{
    syncPosition();
    m_speed = std::clamp(speed, 0.25, 16.0);
    if (m_started && !m_paused)
        scheduleProcess();
}

void PacketPlayer::setPaused(const bool paused)
{
    if (m_paused == paused)
        return;

    syncPosition();
    m_paused = paused;
    if (m_paused) {
        if (m_event)
            m_event->cancel();
        m_event = nullptr;
    } else {
        m_lastRealTick = g_clock.millis();
        scheduleProcess();
    }
}

void PacketPlayer::seekForward(const ticks_t position)
{
    const ticks_t currentPosition = getPosition();
    const ticks_t target = std::clamp(position, currentPosition, m_duration);
    m_pauseAfterSeek = m_paused;
    m_paused = false;
    m_seeking = true;
    m_seekTarget = target;
    scheduleProcess();
}

void PacketPlayer::setInitialPosition(const ticks_t position, const bool pauseAfterSeek)
{
    m_position = 0;
    m_packetIndex = 0;
    m_seekTarget = std::clamp<ticks_t>(position, 0, m_duration);
    m_pauseAfterSeek = pauseAfterSeek;
    m_paused = false;
    m_seeking = true;
}

void PacketPlayer::onOutputPacket(const OutputMessagePtr& packet)
{
    if (packet && !packet->getBuffer().empty() && packet->getBuffer()[0] == 0x14) { // logout
        m_disconnectCallback(asio::error::eof);
        stop();
    }
}

void PacketPlayer::process()
{
    m_event = nullptr;
    constexpr size_t MAX_PACKETS_PER_CYCLE = 250;

    if (m_seeking) {
        size_t processed = 0;
        while (m_packetIndex < m_input.size() && m_input[m_packetIndex].first <= m_seekTarget &&
               processed < MAX_PACKETS_PER_CYCLE) {
            const auto& packet = m_input[m_packetIndex];
            m_position = packet.first;
            m_recvCallback(packet.second);
            ++m_packetIndex;
            ++processed;
        }

        if (m_packetIndex < m_input.size() && m_input[m_packetIndex].first <= m_seekTarget) {
            scheduleProcess();
            return;
        }

        m_position = m_seekTarget;
        m_lastRealTick = g_clock.millis();
        m_seeking = false;
        m_paused = m_pauseAfterSeek;
        if (m_paused)
            return;
    }

    if (m_paused)
        return;

    syncPosition();
    size_t processed = 0;
    while (m_packetIndex < m_input.size() && m_input[m_packetIndex].first <= m_position &&
           processed < MAX_PACKETS_PER_CYCLE) {
        m_recvCallback(m_input[m_packetIndex].second);
        ++m_packetIndex;
        ++processed;
    }

    if (m_packetIndex >= m_input.size()) {
        m_position = m_duration;
        m_disconnectCallback(asio::error::eof);
        stop();
        return;
    }

    if (m_input[m_packetIndex].first <= m_position) {
        scheduleProcess();
        return;
    }

    const auto recordDelay = m_input[m_packetIndex].first - m_position;
    const auto realDelay = static_cast<ticks_t>(std::ceil(static_cast<double>(recordDelay) / m_speed));
    scheduleProcess(realDelay);
}
