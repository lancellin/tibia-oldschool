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

#pragma once

#include <framework/net/outputmessage.h>

#include "framework/core/declarations.h"

class PacketPlayer : public LuaObject
{
public:
    PacketPlayer(const std::string_view& file);
    virtual ~PacketPlayer();

    void start(std::function<void(std::shared_ptr<std::vector<uint8_t>>)> recvCallback, std::function<void(std::error_code)> disconnectCallback);
    void stop();

    void onOutputPacket(const OutputMessagePtr& packet);

    void setSpeed(double speed);
    double getSpeed() const { return m_speed; }
    void setPaused(bool paused);
    bool isPaused() const { return m_paused; }
    bool isSeeking() const { return m_seeking; }
    ticks_t getPosition();
    ticks_t getDuration() const { return m_duration; }
    void seekForward(ticks_t position);
    void setInitialPosition(ticks_t position, bool pauseAfterSeek);

    bool isValid() const { return m_valid; }
    uint16_t getClientVersion() const { return m_clientVersion; }
    uint16_t getProtocolVersion() const { return m_protocolVersion; }
    uint8_t getAwareLeft() const { return m_awareLeft; }
    uint8_t getAwareTop() const { return m_awareTop; }
    uint8_t getAwareRight() const { return m_awareRight; }
    uint8_t getAwareBottom() const { return m_awareBottom; }

private:
    void process();
    void syncPosition();
    void scheduleProcess(ticks_t delay = 1);

    ScheduledEventPtr m_event;
    std::vector<std::pair<ticks_t, std::shared_ptr<std::vector<uint8_t>>>> m_input;
    std::function<void(std::shared_ptr<std::vector<uint8_t>>)> m_recvCallback;
    std::function<void(std::error_code)> m_disconnectCallback;
    size_t m_packetIndex{ 0 };
    ticks_t m_position{ 0 };
    ticks_t m_duration{ 0 };
    ticks_t m_lastRealTick{ 0 };
    ticks_t m_seekTarget{ 0 };
    double m_speed{ 1.0 };
    uint16_t m_clientVersion{ 0 };
    uint16_t m_protocolVersion{ 0 };
    uint8_t m_awareLeft{ 0 };
    uint8_t m_awareTop{ 0 };
    uint8_t m_awareRight{ 0 };
    uint8_t m_awareBottom{ 0 };
    bool m_started{ false };
    bool m_paused{ false };
    bool m_seeking{ false };
    bool m_pauseAfterSeek{ false };
    bool m_valid{ false };
};
