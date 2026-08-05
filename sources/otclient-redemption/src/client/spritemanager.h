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

#include <framework/core/declarations.h>
#include <framework/graphics/declarations.h>

#include <mutex>

class FileMetadata
{
public:
    FileMetadata() = default;
    FileMetadata(const FileStreamPtr& file);

    uint32_t getSpriteId() const { return spriteId; }
    const std::string& getFileName() const { return fileName; }
    uint32_t getOffset() const { return offset; }
    uint32_t getFileSize() const { return fileSize; }
private:
    std::string fileName;
    uint32_t offset = 0;
    uint32_t fileSize = 0;
    uint32_t spriteId = 0;
};

class RawSpriteMetadata
{
public:
    RawSpriteMetadata() = default;
    RawSpriteMetadata(const FileStreamPtr& file);

    uint32_t getSpriteId() const { return spriteId; }
    uint32_t getOffset() const { return offset; }
    uint32_t getFileSize() const { return fileSize; }
    uint16_t getWidth() const { return width; }
    uint16_t getHeight() const { return height; }
    bool hasTransparentPixel() const { return flags & 0x01; }

private:
    uint32_t spriteId = 0;
    uint32_t offset = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t fileSize = 0;
    uint8_t flags = 0;
};

//@bindsingleton g_sprites
class SpriteManager
{
public:
    void init();
    void terminate();

    bool loadSpr(std::string file);
    bool loadRegularSpr(std::string file);
    bool loadCwmSpr(std::string file);
    bool loadHdpSpr(std::string file);
    void reload();
    void unload();

#ifdef FRAMEWORK_EDITOR
    void saveSpr(const std::string& fileName);
#endif

    uint32_t getSignature() { return m_signature; }
    int getSpritesCount() { return m_spritesCount; }
    void setHdOverridesEnabled(bool enabled) { m_enableHdOverrides = enabled; }
    bool isHdOverridesEnabled() const { return m_enableHdOverrides || m_enableHdFastPack; }
    void setHdFastPackEnabled(bool enabled) { m_enableHdFastPack = enabled; }
    bool isHdFastPackEnabled() const { return m_enableHdFastPack; }
    void setPreloadAllSpritesEnabled(bool enabled);
    bool isPreloadAllSpritesEnabled() const { return m_preloadAllSpritesEnabled; }
    void preloadAllSprites();
    void clearPreloadedSprites();

    ImagePtr getSpriteImage(int id) {
        bool isLoading = false;
        return  getSpriteImage(id, isLoading);
    }

    ImagePtr getSpriteImage(int id, bool& isLoading);
    bool isLoaded() { return m_loaded; }

private:
    enum class SpriteLoadState
    {
        NONE,
        LOADING,
        LOADED
    };

    struct FileStream_m
    {
        FileStreamPtr file;
        std::atomic<SpriteLoadState> m_loadingState = SpriteLoadState::NONE;

        FileStream_m(FileStreamPtr f) : file(std::move(f)) {}
    };

    void load(std::string_view fileName, std::vector<std::unique_ptr<FileStream_m>>& targetFiles);
    FileStreamPtr getRegularSpriteFile() const {
        return m_spritesFiles[0]->file;
    }
    FileStreamPtr getHdSpriteFile() const {
        return m_hdSpritesFiles[0]->file;
    }
    FileStreamPtr getHdFastSpriteFile() const {
        return m_hdFastSpritesFiles[0]->file;
    }

    ImagePtr getSpriteImageHd(int id, const FileStreamPtr& file);
    ImagePtr getSpriteImageHdp(int id, const FileStreamPtr& file);
    ImagePtr getSpriteImage(int id, const FileStreamPtr& file);

    std::string m_sourceBaseName;

    bool m_enableHdOverrides{ false };
    bool m_enableHdFastPack{ false };
    bool m_preloadAllSpritesEnabled{ false };
    bool m_spritesHd{ false };
    bool m_loaded{ false };
    uint32_t m_signature{ 0 };
    uint32_t m_spritesCount{ 0 };
    uint32_t m_spritesOffset{ 0 };
    uint32_t m_hdSpritesOffset{ 0 };
    uint32_t m_hdFastSpritesOffset{ 0 };

    std::vector<std::unique_ptr<FileStream_m>> m_spritesFiles;
    std::vector<std::unique_ptr<FileStream_m>> m_hdSpritesFiles;
    std::vector<std::unique_ptr<FileStream_m>> m_hdFastSpritesFiles;
    std::unordered_map<uint32_t, FileMetadata> m_cwmSpritesMetadata;
    std::unordered_map<uint32_t, RawSpriteMetadata> m_hdpSpritesMetadata;
    std::unordered_map<uint32_t, ImagePtr> m_preloadedSpriteImages;
    std::mutex m_preloadedSpriteImagesMutex;
};

extern SpriteManager g_sprites;
