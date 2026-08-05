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

#include "spritemanager.h"

#include "game.h"
#include "gameconfig.h"
#include "spriteappearances.h"
#include "framework/core/asyncdispatcher.h"
#include "framework/core/filestream.h"
#include "framework/core/graphicalapplication.h"
#include "framework/core/logger.h"
#include "framework/core/resourcemanager.h"
#include "framework/graphics/image.h"

#include <cmath>
#include <cstring>

SpriteManager g_sprites;

FileMetadata::FileMetadata(const FileStreamPtr& file) {
    offset = file->getU32();
    fileSize = file->getU32();
    fileName = file->getString();
    spriteId = std::stoi(fileName);
}

RawSpriteMetadata::RawSpriteMetadata(const FileStreamPtr& file)
{
    spriteId = file->getU32();
    offset = file->getU32();
    width = file->getU16();
    height = file->getU16();
    fileSize = file->getU32();
    flags = file->getU8();
}

void SpriteManager::init() {}
void SpriteManager::terminate() { unload(); }

void SpriteManager::reload() {
    if (g_app.isEncrypted())
        return;

    if (m_sourceBaseName.empty())
        return;

    loadSpr(m_sourceBaseName);
}

void SpriteManager::load(std::string_view fileName, std::vector<std::unique_ptr<FileStream_m>>& targetFiles) {
    targetFiles.clear();
    targetFiles.resize(g_asyncDispatcher->get_thread_count());
    if (g_app.isLoadingAsyncTexture()) {
        for (auto& file : targetFiles)
            file = std::make_unique<FileStream_m>(g_resources.openFile(std::string(fileName)));
    } else (targetFiles[0] = std::make_unique<FileStream_m>(g_resources.openFile(std::string(fileName))))->file->cache(true);
}

bool SpriteManager::loadSpr(std::string file)
{
    unload();

    m_sourceBaseName = file;
    m_spritesCount = 0;
    m_signature = 0;
    m_loaded = false;
    m_spritesHd = false;

    const auto sprFile = g_resources.guessFilePath(file, "spr");
    const auto hdpFile = g_resources.guessFilePath(file, "hdp");
    const auto cwmFile = g_resources.guessFilePath(file, "cwm");

    bool loaded = false;

    if (g_resources.fileExists(sprFile)) {
        loaded = loadRegularSpr(sprFile) || loaded;
    }

    if (m_enableHdFastPack && g_resources.fileExists(hdpFile)) {
        loaded = loadHdpSpr(hdpFile) || loaded;
    }

    if ((m_enableHdFastPack || m_enableHdOverrides) && g_resources.fileExists(cwmFile)) {
        loaded = loadCwmSpr(cwmFile) || loaded;
    }

    m_loaded = loaded;
    if (loaded && m_preloadAllSpritesEnabled)
        preloadAllSprites();
    return loaded;
}

bool SpriteManager::loadRegularSpr(std::string file)
{
    try {
        const auto sprFile = g_resources.guessFilePath(file, "spr");
        load(sprFile, m_spritesFiles);

        m_signature = getRegularSpriteFile()->getU32();
        m_spritesCount = g_game.getFeature(Otc::GameSpritesU32) ? getRegularSpriteFile()->getU32() : getRegularSpriteFile()->getU16();
        m_spritesOffset = getRegularSpriteFile()->tell();

        g_lua.callGlobalField("g_sprites", "onLoadSpr", file);
        return true;
    } catch (const stdext::exception& e) {
        g_logger.error("Failed to load sprites from '{}': {}", file, e.what());
        return false;
    }
}

bool SpriteManager::loadCwmSpr(std::string file)
{
    m_cwmSpritesMetadata.clear();

    try {
        const auto cwmFile = g_resources.guessFilePath(file, "cwm");
        load(cwmFile, m_hdSpritesFiles);

        const auto& spritesFile = getHdSpriteFile();

        const uint8_t version = spritesFile->getU8();
        if (version != 0x01) {
            g_logger.error("Invalid CWM file version - {}", file);
            return false;
        }

        const uint16_t hdSpritesCount = spritesFile->getU16();

        const uint32_t entries = spritesFile->getU32();
        m_cwmSpritesMetadata.reserve(entries);
        for (uint32_t i = 0; i < entries; ++i) {
            FileMetadata spriteMetadata{ spritesFile };
            m_cwmSpritesMetadata[spriteMetadata.getSpriteId()] = std::move(spriteMetadata);
        }

        m_hdSpritesOffset = spritesFile->tell();

        if (m_spritesCount == 0) {
            m_spritesCount = hdSpritesCount;
        }

        if (hdSpritesCount == 0 && m_spritesCount == 0) {
            g_logger.error("Failed to load sprites from '{}' - no sprites", file);
            return false;
        }

        g_lua.callGlobalField("g_sprites", "onLoadCWMSpr", file);
        m_spritesHd = true;
        return true;
    } catch (stdext::exception& e) {
        g_logger.error("Failed to load sprites from '{}': {}", file, e.what());
        return false;
    }
}

bool SpriteManager::loadHdpSpr(std::string file)
{
    m_hdpSpritesMetadata.clear();

    try {
        const auto hdpFile = g_resources.guessFilePath(file, "hdp");
        load(hdpFile, m_hdFastSpritesFiles);

        const auto& spritesFile = getHdFastSpriteFile();

        char magic[4];
        spritesFile->read(magic, sizeof(magic));
        if (std::memcmp(magic, "HDP1", sizeof(magic)) != 0) {
            g_logger.error("Invalid HDP file magic - {}", file);
            return false;
        }

        const uint16_t hdSpritesCount = spritesFile->getU16();
        const uint32_t entries = spritesFile->getU32();
        m_hdpSpritesMetadata.reserve(entries);
        for (uint32_t i = 0; i < entries; ++i) {
            RawSpriteMetadata spriteMetadata{ spritesFile };
            if (spriteMetadata.getWidth() == 0 || spriteMetadata.getHeight() == 0 || spriteMetadata.getFileSize() != static_cast<uint32_t>(spriteMetadata.getWidth()) * spriteMetadata.getHeight() * 4) {
                g_logger.error("Invalid HDP sprite metadata for sprite {} - {}", spriteMetadata.getSpriteId(), file);
                m_hdpSpritesMetadata.clear();
                return false;
            }
            m_hdpSpritesMetadata[spriteMetadata.getSpriteId()] = spriteMetadata;
        }

        m_hdFastSpritesOffset = spritesFile->tell();

        if (m_spritesCount == 0) {
            m_spritesCount = hdSpritesCount;
        }

        if (hdSpritesCount == 0 && m_spritesCount == 0) {
            g_logger.error("Failed to load sprites from '{}' - no sprites", file);
            return false;
        }

        g_lua.callGlobalField("g_sprites", "onLoadHDPSpr", file);
        m_spritesHd = true;
        return true;
    } catch (stdext::exception& e) {
        g_logger.error("Failed to load sprites from '{}': {}", file, e.what());
        return false;
    }
}

#ifdef FRAMEWORK_EDITOR
void SpriteManager::saveSpr(const std::string& fileName)
{
    if (!m_loaded)
        throw Exception("failed to save, spr is not loaded");

    static constexpr uint32_t SPRITE_SIZE = 32;
    static constexpr uint32_t SPRITE_DATA_SIZE = SPRITE_SIZE * SPRITE_SIZE * 4;

    try {
        const auto& fin = g_resources.createFile(fileName);
        if (!fin)
            throw Exception("failed to open file '{}' for write", fileName);

        fin->cache();

        fin->addU32(m_signature);
        if (g_game.getFeature(Otc::GameSpritesU32))
            fin->addU32(m_spritesCount);
        else
            fin->addU16(m_spritesCount);

        const uint32_t offset = fin->tell();
        uint32_t spriteAddress = offset + 4 * m_spritesCount;
        for (uint_fast32_t i = 1; i <= m_spritesCount; ++i)
            fin->addU32(0);

        for (uint_fast32_t i = 1; i <= m_spritesCount; ++i) {
            getRegularSpriteFile()->seek((i - 1) * 4 + m_spritesOffset);
            const uint32_t fromAdress = getRegularSpriteFile()->getU32();
            if (fromAdress != 0) {
                fin->seek(offset + (i - 1) * 4);
                fin->addU32(spriteAddress);
                fin->seek(spriteAddress);

                getRegularSpriteFile()->seek(fromAdress);
                fin->addU8(getRegularSpriteFile()->getU8());
                fin->addU8(getRegularSpriteFile()->getU8());
                fin->addU8(getRegularSpriteFile()->getU8());

                const uint16_t dataSize = getRegularSpriteFile()->getU16();
                fin->addU16(dataSize);
                char spriteData[SPRITE_DATA_SIZE];
                getRegularSpriteFile()->read(spriteData, dataSize);
                fin->write(spriteData, dataSize);

                spriteAddress = fin->tell();
            }
            //TODO: Check for overwritten sprites.
        }

        fin->flush();
        fin->close();
    } catch (const std::exception& e) {
        g_logger.error("Failed to save '{}': {}", fileName, e.what());
    }
}
#endif

void SpriteManager::unload()
{
    clearPreloadedSprites();
    m_spritesCount = 0;
    m_signature = 0;
    m_spritesOffset = 0;
    m_hdSpritesOffset = 0;
    m_hdFastSpritesOffset = 0;
    m_loaded = false;
    m_spritesHd = false;
    m_spritesFiles.clear();
    m_hdSpritesFiles.clear();
    m_hdFastSpritesFiles.clear();
    m_cwmSpritesMetadata.clear();
    m_hdpSpritesMetadata.clear();
}

void SpriteManager::setPreloadAllSpritesEnabled(const bool enabled)
{
    m_preloadAllSpritesEnabled = enabled;
    if (!enabled) {
        clearPreloadedSprites();
        return;
    }

    if (m_loaded)
        preloadAllSprites();
}

void SpriteManager::clearPreloadedSprites()
{
    std::scoped_lock lock(m_preloadedSpriteImagesMutex);
    m_preloadedSpriteImages.clear();
}

void SpriteManager::preloadAllSprites()
{
    if (!m_loaded || m_spritesCount == 0)
        return;

    {
        std::scoped_lock lock(m_preloadedSpriteImagesMutex);
        if (!m_preloadedSpriteImages.empty())
            return;
    }

    std::unordered_map<uint32_t, ImagePtr> preloaded;
    preloaded.reserve(m_spritesCount);

    for (uint32_t spriteId = 1; spriteId <= m_spritesCount; ++spriteId) {
        bool isLoading = false;
        auto image = getSpriteImage(static_cast<int>(spriteId), isLoading);
        if (image) {
            preloaded.emplace(spriteId, std::move(image));
        }
    }

    {
        std::scoped_lock lock(m_preloadedSpriteImagesMutex);
        m_preloadedSpriteImages = std::move(preloaded);
    }
}

ImagePtr SpriteManager::getSpriteImage(const int id, bool& isLoading)
{
    if (g_game.getProtocolVersion() >= 1281 && !g_game.getFeature(Otc::GameLoadSprInsteadProtobuf)) {
        return g_spriteAppearances.getSpriteImage(id, isLoading);
    }

    {
        std::scoped_lock lock(m_preloadedSpriteImagesMutex);
        const auto it = m_preloadedSpriteImages.find(static_cast<uint32_t>(id));
        if (it != m_preloadedSpriteImages.end())
            return it->second;
    }

    const auto threadId = g_app.isLoadingAsyncTexture() ? stdext::getThreadId() : 0;

    if (m_spritesHd && !m_hdFastSpritesFiles.empty() && m_hdpSpritesMetadata.find(id) != m_hdpSpritesMetadata.end()) {
        const auto& hdSf = m_hdFastSpritesFiles[threadId % m_hdFastSpritesFiles.size()];
        if (hdSf->m_loadingState.exchange(SpriteLoadState::LOADING, std::memory_order_acq_rel) == SpriteLoadState::LOADING) {
            isLoading = true;
            return nullptr;
        }

        auto image = getSpriteImageHdp(id, hdSf->file);
        hdSf->m_loadingState.store(SpriteLoadState::LOADED, std::memory_order_release);

        if (image)
            return image;
    }

    if (m_spritesHd && !m_hdSpritesFiles.empty() && m_cwmSpritesMetadata.find(id) != m_cwmSpritesMetadata.end()) {
        const auto& hdSf = m_hdSpritesFiles[threadId % m_hdSpritesFiles.size()];
        if (hdSf->m_loadingState.exchange(SpriteLoadState::LOADING, std::memory_order_acq_rel) == SpriteLoadState::LOADING) {
            isLoading = true;
            return nullptr;
        }

        auto image = getSpriteImageHd(id, hdSf->file);
        hdSf->m_loadingState.store(SpriteLoadState::LOADED, std::memory_order_release);

        if (image)
            return image;
    }

    if (!m_spritesFiles.empty()) {
        const auto& sf = m_spritesFiles[threadId % m_spritesFiles.size()];
        if (sf->m_loadingState.exchange(SpriteLoadState::LOADING, std::memory_order_acq_rel) == SpriteLoadState::LOADING) {
            isLoading = true;
            return nullptr;
        }

        auto image = getSpriteImage(id, sf->file);

        sf->m_loadingState.store(SpriteLoadState::LOADED, std::memory_order_release);

        return image;
    }

    return nullptr;
}

ImagePtr SpriteManager::getSpriteImageHdp(const int id, const FileStreamPtr& file)
{
    const auto it = m_hdpSpritesMetadata.find(id);
    if (it == m_hdpSpritesMetadata.end())
        return nullptr;

    const auto& metadata = it->second;
    if (metadata.getFileSize() != static_cast<uint32_t>(metadata.getWidth()) * metadata.getHeight() * 4)
        return nullptr;

    std::vector<uint8_t> buffer(metadata.getFileSize());

    file->seek(m_hdFastSpritesOffset + metadata.getOffset());
    file->read(buffer.data(), metadata.getFileSize());

    auto image = std::make_shared<Image>(Size(metadata.getWidth(), metadata.getHeight()), 4, buffer.data());
    image->setTransparentPixel(metadata.hasTransparentPixel());
    return image;
}

ImagePtr SpriteManager::getSpriteImageHd(const int id, const FileStreamPtr& file)
{
    const auto it = m_cwmSpritesMetadata.find(id);
    if (it == m_cwmSpritesMetadata.end())
        return nullptr;

    const auto& metadata = it->second;

    std::string buffer(metadata.getFileSize(), 0);

    file->seek(m_hdSpritesOffset + metadata.getOffset());
    file->read(buffer.data(), metadata.getFileSize());

    return Image::loadPNG(buffer.data(), buffer.size());
}

uint16_t readU16FromBuffer(const uint8_t* data, size_t& offset) {
    uint16_t val = data[offset] | (data[offset + 1] << 8);
    offset += 2;
    return val;
}

ImagePtr SpriteManager::getSpriteImage(const int id, const FileStreamPtr& file)
{
    if (id <= 0 || !file)
        return nullptr;

    if (m_spritesCount > 0 && static_cast<uint32_t>(id) > m_spritesCount)
        return nullptr;

    try {
        file->seek(((id - 1) * 4) + m_spritesOffset);
        const uint32_t spriteAddress = file->getU32();
        if (spriteAddress == 0)
            return nullptr;

        file->seek(spriteAddress);
        file->skip(3); // Skip RGB color key

        const uint16_t pixelDataSize = file->getU16();
        const int spriteSize = g_gameConfig.getSpriteSize();
        const int totalPixels = spriteSize * spriteSize;
        const int maxWriteSize = totalPixels * 4;

        const bool useAlpha = g_game.getFeature(Otc::GameSpritesAlphaChannel);
        const uint8_t channels = useAlpha ? 4 : 3;

        static thread_local std::vector<uint8_t> spriteBuffer;
        spriteBuffer.resize(pixelDataSize);
        file->read(spriteBuffer.data(), pixelDataSize);

        size_t offset = 0;
        auto image = std::make_shared<Image>(Size(spriteSize));
        uint8_t* pixels = image->getPixelData();
        int writePos = 0;
        bool hasAlpha = false;
        int transparentCount = 0;

        static constexpr int MAX_PIXEL_BLOCK = 4096;
        static thread_local uint8_t tempBuffer[MAX_PIXEL_BLOCK * 4];

        while (offset + 4 <= pixelDataSize && writePos < maxWriteSize) {
            const uint16_t transparentPixels = readU16FromBuffer(spriteBuffer.data(), offset);
            const uint16_t coloredPixels = readU16FromBuffer(spriteBuffer.data(), offset);

            transparentCount += transparentPixels;

            const int transparentBytes = transparentPixels * 4;
            if (writePos + transparentBytes > maxWriteSize)
                break;

            std::memset(pixels + writePos, 0, transparentBytes);
            writePos += transparentBytes;

            const int actualColoredPixels = (coloredPixels > MAX_PIXEL_BLOCK) ? MAX_PIXEL_BLOCK : coloredPixels;
            const int bytesToRead = actualColoredPixels * channels;

            if (offset + bytesToRead > pixelDataSize)
                break;

            std::memcpy(tempBuffer, spriteBuffer.data() + offset, bytesToRead);
            offset += bytesToRead;

            if (useAlpha) {
                for (int i = 0, src = 0; i < actualColoredPixels && writePos + 4 <= maxWriteSize; ++i, src += 4) {
                    pixels[writePos + 0] = tempBuffer[src + 0];
                    pixels[writePos + 1] = tempBuffer[src + 1];
                    pixels[writePos + 2] = tempBuffer[src + 2];
                    const uint8_t alpha = tempBuffer[src + 3];
                    pixels[writePos + 3] = alpha;

                    if (alpha != 0xFF) hasAlpha = true;
                    else if (transparentCount <= 4 && alpha == 0x00) ++transparentCount;

                    writePos += 4;
                }
            } else {
                for (int i = 0, src = 0; i < actualColoredPixels && writePos + 4 <= maxWriteSize; ++i, src += 3) {
                    pixels[writePos + 0] = tempBuffer[src + 0];
                    pixels[writePos + 1] = tempBuffer[src + 1];
                    pixels[writePos + 2] = tempBuffer[src + 2];
                    pixels[writePos + 3] = 0xFF;
                    writePos += 4;
                }
            }
        }

        if (writePos < maxWriteSize) {
            std::memset(pixels + writePos, 0, maxWriteSize - writePos);
            transparentCount += maxWriteSize - writePos;
        }

        if (hasAlpha || transparentCount > 4)
            image->setTransparentPixel(true);

        return image;
    } catch (const stdext::exception& e) {
        g_logger.error("Failed to get sprite id {}: {}", id, e.what());
        return nullptr;
    }
}
