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

#include "packet_recorder.h"

#include "inputmessage.h"
#include "framework/core/clock.h"
#include "framework/core/resourcemanager.h"

namespace
{
bool isSafeCamRecordFileName(const std::string_view file)
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
}

PacketRecorder::PacketRecorder(const std::string_view& file, const uint16_t clientVersion, const uint16_t protocolVersion,
                               const uint8_t awareLeft, const uint8_t awareTop, const uint8_t awareRight, const uint8_t awareBottom)
{
    if (!isSafeCamRecordFileName(file)) {
        g_logger.error("Refusing unsafe CAM file name '{}'.", file);
        return;
    }

    g_resources.makeDir("records");
    const auto path = std::filesystem::path(g_resources.getWriteDir()) / "records" / std::string(file);
    m_stream.open(path, std::ios::out | std::ios::trunc);
    if (!m_stream.is_open()) {
        g_logger.error("Unable to create CAM file '{}'.", path.string());
        return;
    }

    m_stream << "OTCAM 1 " << clientVersion << ' ' << protocolVersion << ' '
             << static_cast<uint16_t>(awareLeft) << ' ' << static_cast<uint16_t>(awareTop) << ' '
             << static_cast<uint16_t>(awareRight) << ' ' << static_cast<uint16_t>(awareBottom) << '\n';
}

PacketRecorder::~PacketRecorder()
{
}

void PacketRecorder::addInputPacket(const InputMessagePtr& packet)
{
    if (!m_stream.is_open() || !packet)
        return;

    if (!m_started) {
        m_start = g_clock.millis();
        m_started = true;
    }

    m_stream << "< " << (g_clock.millis() - m_start) << " ";
    for (const auto& buffer : packet->getBodyBuffer()) {
        m_stream << std::setfill('0') << std::setw(2) << std::hex << (uint16_t)(uint8_t)buffer;
    }
    m_stream << std::dec << "\n";
}
