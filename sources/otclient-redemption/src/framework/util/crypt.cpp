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

#include "crypt.h"

#include <array>

#ifndef USE_PRECOMPILED_HEADERS
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <zlib.h>
#endif

#include <cppcodec/base64_rfc4648.hpp>
#include <openssl/evp.h>

#include "framework/core/graphicalapplication.h"
#include "framework/core/resourcemanager.h"
#include "framework/platform/platform.h"
#include "framework/stdext/math.h"

#ifndef USE_GMP
#ifndef OPENSSL_API_COMPAT
#define OPENSSL_API_COMPAT 0x10100000L
#endif
#ifndef OPENSSL_SUPPRESS_DEPRECATED
#define OPENSSL_SUPPRESS_DEPRECATED
#endif
#include <openssl/bn.h>
#include <openssl/rsa.h>
#endif

constexpr std::size_t CHECKSUM_BYTES = sizeof(uint32_t);

namespace
{
constexpr std::array<uint8_t, 32> CAM_FORENSIC_PUBLIC_KEY = {
    0x51, 0xcd, 0x8d, 0xc1, 0xa8, 0x93, 0x65, 0x7c,
    0x1b, 0x13, 0xce, 0x53, 0xab, 0xf2, 0x22, 0x92,
    0xf6, 0x8b, 0x17, 0x35, 0xd7, 0x1b, 0x2f, 0xf7,
    0x19, 0xcb, 0x88, 0x15, 0x85, 0x56, 0xbc, 0x46
};
}

Crypt g_crypt;

Crypt::Crypt()
{
#ifdef USE_GMP
    mpz_init(m_p);
    mpz_init(m_q);
    mpz_init(m_d);
    mpz_init(m_e);
    mpz_init(m_n);
#else
    m_rsa = RSA_new();
#endif
}

Crypt::~Crypt()
{
#ifdef USE_GMP
    mpz_clear(m_p);
    mpz_clear(m_q);
    mpz_clear(m_n);
    mpz_clear(m_d);
    mpz_clear(m_e);
#else
    RSA_free(m_rsa);
#endif
}

std::string Crypt::base64Encode(const std::string& decoded_string) {
    return cppcodec::base64_rfc4648::encode(decoded_string);
}

std::string Crypt::base64Decode(const std::string_view& encoded_string) {
    try {
        return cppcodec::base64_rfc4648::decode<std::string>(encoded_string);
    } catch (const std::invalid_argument&) {
        return {};
    }
}

bool Crypt::verifyCamForensicSignature(const std::string& payload, const std::string& signatureBase64)
{
    const std::string signature = base64Decode(signatureBase64);
    if (signature.size() != 64)
        return false;

    EVP_PKEY* key = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_ED25519, nullptr,
        CAM_FORENSIC_PUBLIC_KEY.data(), CAM_FORENSIC_PUBLIC_KEY.size());
    if (!key)
        return false;

    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (!context) {
        EVP_PKEY_free(key);
        return false;
    }

    const bool valid =
        EVP_DigestVerifyInit(context, nullptr, nullptr, nullptr, key) == 1 &&
        EVP_DigestVerify(
            context,
            reinterpret_cast<const unsigned char*>(signature.data()), signature.size(),
            reinterpret_cast<const unsigned char*>(payload.data()), payload.size()) == 1;

    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    return valid;
}

std::string Crypt::sha256Hex(const std::string& value)
{
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digestSize = 0;
    if (EVP_Digest(
            value.data(), value.size(),
            digest.data(), &digestSize,
            EVP_sha256(), nullptr) != 1 || digestSize != 32) {
        return {};
    }

    static constexpr char HEX[] = "0123456789abcdef";
    std::string encoded(digestSize * 2, '0');
    for (unsigned int index = 0; index < digestSize; ++index) {
        encoded[index * 2] = HEX[digest[index] >> 4];
        encoded[index * 2 + 1] = HEX[digest[index] & 0x0F];
    }
    return encoded;
}

void Crypt::xorCrypt(std::string& buffer, const std::string& key)
{
    if (key.empty())
        return;

    const size_t keySize = key.size();
    for (size_t i = 0; i < buffer.size(); ++i)
        buffer[i] ^= key[i % keySize];
}

std::string Crypt::genUUID() {
    return uuids::to_string(uuids::uuid_random_generator{ stdext::random_gen() }());
}

bool Crypt::setMachineUUID(std::string uuidstr)
{
    if (uuidstr.empty())
        return false;

    uuidstr = _decrypt(uuidstr, false);

    if (uuidstr.length() != 36)
        return false;

    m_machineUUID = uuids::uuid::from_string(uuidstr).value();

    return true;
}

std::string Crypt::getMachineUUID()
{
    if (m_machineUUID.is_nil()) {
        m_machineUUID = uuids::uuid_random_generator{ stdext::random_gen() }();
    }
    return _encrypt(uuids::to_string(m_machineUUID), false);
}

std::string Crypt::getCryptKey(const bool useMachineUUID) const
{
    static const uuids::uuid default_uuid{};
    const uuids::uuid& uuid = useMachineUUID ? m_machineUUID : default_uuid;

    const uuids::uuid u = uuids::uuid_name_generator(uuid)(
        g_app.getCompactName() + g_platform.getCPUName() + g_platform.getOSName() + g_resources.getUserDir()
    );

    const std::size_t hash = std::hash<uuids::uuid>{}(u);

    return std::string(reinterpret_cast<const char*>(&hash), sizeof(hash));
}

std::string Crypt::_encrypt(const std::string& decrypted_string, const bool useMachineUUID)
{
    const uint32_t sum = stdext::computeChecksum(
        { reinterpret_cast<const uint8_t*>(decrypted_string.data()),
          decrypted_string.size() });

    std::string tmp(CHECKSUM_BYTES, '\0');
    tmp.append(decrypted_string);

    stdext::writeULE32(reinterpret_cast<uint8_t*>(tmp.data()), sum);

    const auto key = getCryptKey(useMachineUUID);
    xorCrypt(tmp, key);
    return base64Encode(tmp);
}

std::string Crypt::_decrypt(const std::string& encrypted_string, const bool useMachineUUID)
{
    std::string decoded = base64Decode(encrypted_string);
    if (decoded.size() < CHECKSUM_BYTES)
        return {};

    const auto key = getCryptKey(useMachineUUID);
    xorCrypt(decoded, key);

    const uint32_t readsum =
        stdext::readULE32(reinterpret_cast<const uint8_t*>(decoded.data()));
    decoded.erase(0, CHECKSUM_BYTES);

    const uint32_t sum = stdext::computeChecksum(
        { reinterpret_cast<const uint8_t*>(decoded.data()), decoded.size() });

    return (readsum == sum) ? std::move(decoded) : std::string();
}

void Crypt::rsaSetPublicKey(const std::string& n, const std::string& e)
{
#ifdef USE_GMP
    mpz_set_str(m_n, n.data(), 10);
    mpz_set_str(m_e, e.data(), 10);
#else
    BIGNUM* bn = nullptr, * be = nullptr;
    BN_dec2bn(&bn, n.data());
    BN_dec2bn(&be, e.data());
    RSA_set0_key(m_rsa, bn, be, nullptr);
#endif
}

void Crypt::rsaSetPrivateKey(const std::string& p, const std::string& q, const std::string& d)
{
#ifdef USE_GMP
    mpz_set_str(m_p, p.data(), 10);
    mpz_set_str(m_q, q.data(), 10);
    mpz_set_str(m_d, d.data(), 10);

    // n = p * q
    mpz_mul(m_n, m_p, m_q);
#else
#if OPENSSL_VERSION_NUMBER < 0x10100005L
    BN_dec2bn(&m_rsa->p, p.data());
    BN_dec2bn(&m_rsa->q, q.data());
    BN_dec2bn(&m_rsa->d, d.data());
    // clear rsa cache
    if (m_rsa->_method_mod_p) {
        BN_MONT_CTX_free(m_rsa->_method_mod_p);
        m_rsa->_method_mod_p = nullptr;
    }
    if (m_rsa->_method_mod_q) {
        BN_MONT_CTX_free(m_rsa->_method_mod_q);
        m_rsa->_method_mod_q = nullptr;
    }
#else
    BIGNUM* bp = nullptr, * bq = nullptr, * bd = nullptr;
    BN_dec2bn(&bp, p.data());
    BN_dec2bn(&bq, q.data());
    BN_dec2bn(&bd, d.data());
    RSA_set0_key(m_rsa, nullptr, nullptr, bd);
    RSA_set0_factors(m_rsa, bp, bq);
#endif
#endif
}

bool Crypt::rsaEncrypt(uint8_t* msg, int size)
{
    if (size != rsaGetSize())
        return false;

#ifdef USE_GMP
    mpz_t c, m;
    mpz_init(c);
    mpz_init(m);
    mpz_import(m, size, 1, 1, 0, 0, msg);

    // c = m^e mod n
    mpz_powm(c, m, m_e, m_n);

    size_t count = (mpz_sizeinbase(m, 2) + 7) / 8;
    memset((char*)msg, 0, size - count);
    mpz_export((char*)msg + (size - count), nullptr, 1, 1, 0, 0, c);

    mpz_clear(c);
    mpz_clear(m);

    return true;
#else
    return RSA_public_encrypt(size, msg, msg, m_rsa, RSA_NO_PADDING) != -1;
#endif
}

bool Crypt::rsaDecrypt(uint8_t* msg, int size)
{
    if (size != rsaGetSize())
        return false;

#ifdef USE_GMP
    mpz_t c, m;
    mpz_init(c);
    mpz_init(m);
    mpz_import(c, size, 1, 1, 0, 0, msg);

    // m = c^d mod n
    mpz_powm(m, c, m_d, m_n);

    size_t count = (mpz_sizeinbase(m, 2) + 7) / 8;
    memset((char*)msg, 0, size - count);
    mpz_export((char*)msg + (size - count), nullptr, 1, 1, 0, 0, m);

    mpz_clear(c);
    mpz_clear(m);

    return true;
#else
    return RSA_private_decrypt(size, msg, msg, m_rsa, RSA_NO_PADDING) != -1;
#endif
}

int Crypt::rsaGetSize()
{
#ifdef USE_GMP
    size_t count = (mpz_sizeinbase(m_n, 2) + 7) / 8;
    return ((int)count / 128) * 128;
#else
    return RSA_size(m_rsa);
#endif
}

std::string Crypt::crc32(const std::string& decoded_string, const bool upperCase)
{
    uint32_t crc = ::crc32(0, nullptr, 0);
    crc = ::crc32(crc, reinterpret_cast<const Bytef*>(decoded_string.data()), decoded_string.size());
    std::string result = stdext::dec_to_hex(crc);
    if (upperCase)
        std::ranges::transform(result, result.begin(), toupper);
    else
        std::ranges::transform(result, result.begin(), tolower);
    return result;
}
