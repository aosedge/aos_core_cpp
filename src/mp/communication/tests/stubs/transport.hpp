/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_MP_COMMUNICATION_TRANSPORT_HPP_
#define AOS_MP_COMMUNICATION_TRANSPORT_HPP_

#include <fstream>
#include <map>
#include <vector>

#include <Poco/Net/StreamSocket.h>

#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <pthread.h>

#include "common/utils/pkcs11helper.hpp"
#include "mp/communication/communicationchannel.hpp"
#include "mp/communication/types.hpp"
#include "mp/communication/utils.hpp"

using namespace aos::mp::communication;

class SocketClient {
public:
    SocketClient(const std::string& address, int port)
        : mAddress(address)
        , mPort(port)
    {
    }

    bool WaitForConnection()
    {
        std::unique_lock lock {mMutex};

        mCondVar.wait_for(lock, cWaitTimeout, [this] { return mConnected.load(); });

        return mConnected;
    }

    aos::Error Connect()
    {
        try {
            if (mClientSocket.impl()->initialized()) {
                mClientSocket.close();
            }

            mClientSocket = Poco::Net::StreamSocket();

            mClientSocket.connect(Poco::Net::SocketAddress(mAddress, mPort));
            mConnected = true;
            mCondVar.notify_all();

            return aos::ErrorEnum::eNone;

        } catch (const Poco::Exception& e) {
            return aos::Error {aos::ErrorEnum::eRuntime, e.displayText().c_str()};
        }
    }

    aos::Error Read(std::vector<uint8_t>& message)
    {
        try {
            int totalRead = 0;
            while (totalRead < static_cast<int>(message.size())) {
                int bytesRead = mClientSocket.receiveBytes(message.data() + totalRead, message.size() - totalRead);
                if (bytesRead == 0) {
                    return aos::Error {ECONNRESET};
                }
                totalRead += bytesRead;
            }
            return aos::ErrorEnum::eNone;
        } catch (const Poco::Exception& e) {
            return aos::Error {aos::ErrorEnum::eRuntime, e.displayText().c_str()};
        }
    }

    aos::Error Write(std::vector<uint8_t> message)
    {
        try {
            int totalSent = 0;
            while (totalSent < static_cast<int>(message.size())) {
                int bytesSent = mClientSocket.sendBytes(message.data() + totalSent, message.size() - totalSent);
                if (bytesSent == 0) {
                    return aos::Error {ECONNRESET};
                }
                totalSent += bytesSent;
            }
            return aos::ErrorEnum::eNone;
        } catch (const Poco::Exception& e) {
            return aos::Error {aos::ErrorEnum::eRuntime, e.displayText().c_str()};
        }
    }

    aos::Error Close()
    {
        LOG_INF() << "Closing connection to " << mAddress.c_str() << ":" << mPort;

        if (mClientSocket.impl()->initialized()) {
            try {
                mClientSocket.shutdown();
                mClientSocket.close();
            } catch (const Poco::Exception&) {
            }
        }

        mClientSocket = Poco::Net::StreamSocket();
        mConnected    = false;
        mCondVar.notify_all();

        return aos::ErrorEnum::eNone;
    }

private:
    static constexpr auto cWaitTimeout = std::chrono::seconds(3);

    Poco::Net::StreamSocket mClientSocket;
    std::string             mAddress;
    int                     mPort;
    std::atomic<bool>       mConnected {false};
    std::mutex              mMutex;
    std::condition_variable mCondVar;
};

class SecureClientChannel : public CommChannelItf {
public:
    SecureClientChannel(
        CommChannelItf& channel, const std::string& keyID, const std::string& certPEM, const std::string& caCertPath)
        : mChannel(channel)
        , mKeyID(keyID)
        , mCertPEM(certPEM)
        , mCaCertPath(caCertPath)
    {
        SSL_load_error_strings();
        OpenSSL_add_ssl_algorithms();
    }

    ~SecureClientChannel()
    {
        Close();
        EVP_cleanup();
        if (mBioMethod) {
            BIO_meth_free(mBioMethod);
            mBioMethod = nullptr;
        }
    }

    aos::Error Connect() override
    {
        if (mConnected) {
            return aos::ErrorEnum::eNone;
        }

        mCtx = nullptr;
        mSSL = nullptr;

        int retryCount = 0;

        while (retryCount < cMaxRetryCount) {
            if (auto err = AttemptConnect(); err.IsNone()) {
                mConnected = true;

                return aos::ErrorEnum::eNone;
            }

            retryCount++;

            std::this_thread::sleep_for(cConnectionTimeout);
        }

        return aos::Error(aos::ErrorEnum::eRuntime, "failed to connect");
    }

    aos::Error Read(std::vector<uint8_t>& message) override
    {
        if (!mConnected || !mSSL) {
            return aos::Error(aos::ErrorEnum::eRuntime, "Not connected");
        }

        int bytesRead = SSL_read(mSSL, message.data(), message.size());
        if (bytesRead <= 0) {
            return aos::Error(aos::ErrorEnum::eRuntime, "SSL read failed");
        }

        return aos::ErrorEnum::eNone;
    }

    aos::Error Write(std::vector<uint8_t> message) override
    {
        if (!mConnected || !mSSL) {
            return aos::Error(aos::ErrorEnum::eRuntime, "Not connected");
        }

        int bytesWritten = SSL_write(mSSL, message.data(), message.size());
        if (bytesWritten <= 0) {
            return aos::Error(aos::ErrorEnum::eRuntime, "SSL write failed");
        }

        return aos::ErrorEnum::eNone;
    }

    aos::Error Close() override
    {
        if (mConnected && mSSL) {
            SSL_shutdown(mSSL);
        }

        if (mSSL) {
            SSL_free(mSSL);
            mSSL = nullptr;
        }

        if (mCtx) {
            SSL_CTX_free(mCtx);
            mCtx = nullptr;
        }

        mConnected = false;

        if (auto err = mChannel.Close(); !err.IsNone()) {
            return err;
        }

        return aos::ErrorEnum::eNone;
    }

    bool IsConnected() const override { return mConnected; }

private:
    static constexpr auto cConnectionTimeout = std::chrono::seconds(3);
    static constexpr int  cMaxRetryCount     = 3;

    CommChannelItf&   mChannel;
    std::string       mKeyID;
    std::string       mCertPEM;
    std::string       mCaCertPath;
    SSL_CTX*          mCtx       = nullptr;
    SSL*              mSSL       = nullptr;
    BIO_METHOD*       mBioMethod = nullptr;
    std::atomic<bool> mConnected {false};

    aos::Error AttemptConnect()
    {
        if (mConnected) {
            Close();
        }

        if (auto err = mChannel.Connect(); !err.IsNone()) {
            return err;
        }

        auto err = CreateContext();
        if (err != aos::ErrorEnum::eNone)
            return err;

        err = ConfigureContext();
        if (err != aos::ErrorEnum::eNone)
            return err;

        err = SetupSSL();
        if (err != aos::ErrorEnum::eNone)
            return err;

        return PerformHandshake();
    }

    aos::Error CreateContext()
    {
        const SSL_METHOD* method = TLS_client_method();
        mCtx                     = SSL_CTX_new(method);
        if (!mCtx) {
            return aos::Error(aos::ErrorEnum::eRuntime, "unable to create SSL context");
        }

        return aos::ErrorEnum::eNone;
    }

    aos::Error ConfigureContext()
    {
        SSL_CTX_set_verify(mCtx, SSL_VERIFY_PEER, nullptr);

        auto [pkey, err] = LoadPrivateKey(mKeyID.c_str());
        if (!err.IsNone()) {
            return err;
        }

        if (SSL_CTX_use_PrivateKey(mCtx, pkey) <= 0) {
            EVP_PKEY_free(pkey);
            return aos::Error(aos::ErrorEnum::eRuntime, "failed to set private key");
        }
        EVP_PKEY_free(pkey);

        BIO* bio = BIO_new_mem_buf(mCertPEM.c_str(), -1);
        if (!bio) {
            return aos::Error(aos::ErrorEnum::eRuntime, "failed to create BIO");
        }
        std::unique_ptr<BIO, decltype(&BIO_free)> bioPtr(bio, BIO_free);

        X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
        if (!cert) {
            return aos::Error(aos::ErrorEnum::eRuntime, "failed to load certificate");
        }
        std::unique_ptr<X509, decltype(&X509_free)> certPtr(cert, X509_free);

        if (SSL_CTX_use_certificate(mCtx, cert) <= 0) {
            return aos::Error(aos::ErrorEnum::eRuntime, "failed to set certificate");
        }

        STACK_OF(X509)* chain  = sk_X509_new_null();
        X509* intermediateCert = nullptr;
        while ((intermediateCert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr)) != nullptr) {
            sk_X509_push(chain, intermediateCert);
        }

        if (sk_X509_num(chain) > 0 && SSL_CTX_set1_chain(mCtx, chain) <= 0) {
            sk_X509_pop_free(chain, X509_free);
            return aos::Error(aos::ErrorEnum::eRuntime, "failed to set certificate chain");
        }

        sk_X509_pop_free(chain, X509_free);

        if (SSL_CTX_load_verify_locations(mCtx, mCaCertPath.c_str(), nullptr) <= 0) {
            return aos::Error(aos::ErrorEnum::eRuntime, "failed to load CA certificate");
        }

        return aos::ErrorEnum::eNone;
    }

    aos::RetWithError<EVP_PKEY*> LoadPrivateKey(const std::string& keyURL)
    {
        auto [pkcs11URL, createErr] = aos::common::utils::CreatePKCS11URL(keyURL.c_str());
        if (!createErr.IsNone()) {
            return {nullptr, createErr};
        }

        auto [pem, encodeErr] = aos::common::utils::PEMEncodePKCS11URL(pkcs11URL);
        if (!encodeErr.IsNone()) {
            return {nullptr, encodeErr};
        }

        auto bio = aos::DeferRelease(BIO_new_mem_buf(pem.c_str(), pem.length()), BIO_free);
        if (!bio) {
            return {nullptr, AOS_ERROR_WRAP(aos::ErrorEnum::eRuntime)};
        }

        EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio.Get(), NULL, NULL, NULL);
        if (!pkey) {
            return {nullptr, AOS_ERROR_WRAP(aos::ErrorEnum::eRuntime)};
        }

        return {pkey, aos::ErrorEnum::eNone};
    }

    aos::Error SetupSSL()
    {
        mSSL = SSL_new(mCtx);
        if (!mSSL) {
            return aos::Error(aos::ErrorEnum::eRuntime, "failed to create SSL object");
        }

        mBioMethod = CreateCustomBioMethod();
        if (!mBioMethod) {
            return aos::Error(aos::ErrorEnum::eRuntime, "failed to create custom BIO method");
        }

        BIO* rbio = BIO_new(mBioMethod);
        BIO* wbio = BIO_new(mBioMethod);

        if (!rbio || !wbio) {
            BIO_free(rbio);
            BIO_free(wbio);

            return aos::Error(aos::ErrorEnum::eRuntime, "failed to create BIO objects");
        }

        BIO_set_data(rbio, this);
        BIO_set_data(wbio, this);

        SSL_set_bio(mSSL, rbio, wbio);

        return aos::ErrorEnum::eNone;
    }

    aos::Error PerformHandshake()
    {
        int result = SSL_connect(mSSL);
        if (result <= 0) {
            return aos::Error(aos::ErrorEnum::eRuntime, "SSL handshake failed");
        }

        mConnected = true;

        return aos::ErrorEnum::eNone;
    }

    static int CustomBioWrite(BIO* bio, const char* data, int len)
    {
        SecureClientChannel* pipe = static_cast<SecureClientChannel*>(BIO_get_data(bio));
        std::vector<uint8_t> buffer(data, data + len);
        aos::Error           err = pipe->mChannel.Write(buffer);

        return err.IsNone() ? len : -1;
    }

    static int CustomBioRead(BIO* bio, char* data, int len)
    {
        SecureClientChannel* pipe = static_cast<SecureClientChannel*>(BIO_get_data(bio));
        std::vector<uint8_t> buffer(len);
        auto                 err = pipe->mChannel.Read(buffer);
        if (!err.IsNone())
            return -1;

        std::memcpy(data, buffer.data(), buffer.size());

        return buffer.size();
    }

    static long CustomBioCtrl([[maybe_unused]] BIO* bio, int cmd, [[maybe_unused]] long num, [[maybe_unused]] void* ptr)
    {
        switch (cmd) {
        case BIO_CTRL_FLUSH:
            return 1;
        default:
            return 0;
        }
    }

    BIO_METHOD* CreateCustomBioMethod()
    {
        BIO_METHOD* method = BIO_meth_new(BIO_TYPE_SOURCE_SINK, "SecureClientChannel BIO");
        if (!method)
            return nullptr;

        BIO_meth_set_write(method, CustomBioWrite);
        BIO_meth_set_read(method, CustomBioRead);
        BIO_meth_set_ctrl(method, CustomBioCtrl);

        return method;
    }
};

class CommManager : public CommChannelItf {
public:
    CommManager(SocketClient& transport)
        : mTransport(transport)
    {
        mThread = std::thread(&CommManager::Run, this);
    }

    ~CommManager() { Close(); }

    std::shared_ptr<CommChannelItf> CreateCommChannel(int port)
    {
        auto it = mChannels.find(port);
        if (it != mChannels.end()) {
            return it->second;
        }

        auto commChannel = std::make_shared<CommunicationChannel>(port, this);

        mChannels[port] = commChannel;

        return commChannel;
    }

    aos::Error Write(std::vector<uint8_t> message) override
    {
        if (auto err = mTransport.Write(message); !err.IsNone()) {
            return err;
        }

        return aos::ErrorEnum::eNone;
    }

    aos::Error Close() override
    {
        {
            std::lock_guard lock {mMutex};

            if (mShutdown) {
                return aos::ErrorEnum::eNone;
            }

            mShutdown = true;

            if (auto err = mTransport.Close(); !err.IsNone()) {
                return err;
            }
        }

        mCondVar.notify_all();

        mThread.join();

        return aos::ErrorEnum::eNone;
    }

    aos::Error Connect() override
    {
        std::lock_guard lock {mMutex};

        if (mShutdown) {
            return aos::ErrorEnum::eRuntime;
        }

        if (mConnected) {
            return aos::ErrorEnum::eNone;
        }

        mConnected = false;

        if (auto err = mTransport.Connect(); !err.IsNone()) {
            return err;
        }

        mConnected = true;

        return aos::ErrorEnum::eNone;
    }
    aos::Error Read([[maybe_unused]] std::vector<uint8_t>& message) override { return aos::ErrorEnum::eNone; }
    bool       IsConnected() const override { return mConnected; }

private:
    static constexpr auto mWaitTimeout = std::chrono::seconds(1);

    static void CalculateChecksum(const std::vector<uint8_t>& data, uint8_t* checksum)
    {
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        SHA256_Update(&sha256, data.data(), data.size());
        SHA256_Final(checksum, &sha256);
    }

    void Run()
    {
        while (!mShutdown) {
            if (auto err = Connect(); !err.IsNone()) {
                std::unique_lock lock {mMutex};

                LOG_WRN() << "Failed connect to transport: error=" << err;

                mCondVar.wait_for(lock, mWaitTimeout, [this] { return mShutdown.load(); });

                continue;
            }

            if (auto err = ReadHandler(); !err.IsNone()) {
                LOG_ERR() << "Failed to read in transport: error=" << err;
            }

            mConnected = false;

            for (const auto& channel : mChannels) {
                channel.second->Close();
            }

            if (auto err = mTransport.Close(); !err.IsNone()) {
                LOG_ERR() << "Failed to close transport: error=" << err;
            }
        }
    }

    aos::Error ReadHandler()
    {
        while (!mShutdown) {
            std::vector<uint8_t> headerBuffer(sizeof(AosProtocolHeader));
            auto                 err = mTransport.Read(headerBuffer);
            if (!err.IsNone()) {
                return err;
            }

            AosProtocolHeader header;
            std::memcpy(&header, headerBuffer.data(), sizeof(AosProtocolHeader));

            // Read body
            std::vector<uint8_t> message(header.mDataSize);
            err = mTransport.Read(message);
            if (!err.IsNone()) {
                return err;
            }

            std::array<uint8_t, SHA256_DIGEST_LENGTH> checksum;
            CalculateChecksum(message, checksum.data());

            if (std::memcmp(checksum.data(), header.mCheckSum, SHA256_DIGEST_LENGTH) != 0) {
                continue;
            }

            if (mChannels.find(header.mPort) == mChannels.end()) {
                continue;
            }

            if (err = mChannels[header.mPort]->Receive(message); !err.IsNone()) {
                return err;
            }
        }

        return aos::ErrorEnum::eNone;
    }

    SocketClient&                                        mTransport;
    std::shared_ptr<CommunicationChannel>                mCommChannel;
    std::thread                                          mThread;
    std::atomic<bool>                                    mShutdown {false};
    std::atomic<bool>                                    mConnected {false};
    std::map<int, std::shared_ptr<CommunicationChannel>> mChannels;
    std::mutex                                           mMutex;
    std::condition_variable                              mCondVar;
};

class Handler : public HandlerItf {
public:
    void OnConnected() override { }

    void OnDisconnected() override
    {
        std::lock_guard lock {mMutex};

        mShutdown = true;
        mCondVar.notify_all();
    }

    aos::Error SendMessages(std::vector<uint8_t> messages) override
    {
        std::lock_guard lock {mMutex};

        if (mShutdown) {
            return aos::ErrorEnum::eRuntime;
        }

        mOutgoingMessages = std::move(messages);
        mCondVar.notify_all();

        return aos::ErrorEnum::eNone;
    }

    aos::RetWithError<std::vector<uint8_t>> GetOutgoingMessages()
    {
        std::unique_lock lock {mMutex};

        mCondVar.wait(lock, [this] { return !mOutgoingMessages.empty() || mShutdown; });

        if (mShutdown) {
            return {{}, aos::ErrorEnum::eRuntime};
        }

        return {std::move(mOutgoingMessages), aos::ErrorEnum::eNone};
    }

    aos::Error SetIncomingMessages(std::vector<uint8_t> messages)
    {
        std::lock_guard lock {mMutex};

        if (mShutdown) {
            return aos::ErrorEnum::eRuntime;
        }

        mIncomingMessages = std::move(messages);
        mCondVar.notify_all();

        return aos::ErrorEnum::eNone;
    }

    aos::RetWithError<std::vector<uint8_t>> ReceiveMessages() override
    {
        std::unique_lock lock {mMutex};

        mCondVar.wait(lock, [this] { return !mIncomingMessages.empty() || mShutdown; });

        if (mShutdown) {
            return {{}, aos::ErrorEnum::eRuntime};
        }

        return {std::move(mIncomingMessages), aos::ErrorEnum::eNone};
    }

private:
    std::mutex              mMutex;
    std::condition_variable mCondVar;

    std::vector<uint8_t> mOutgoingMessages;
    std::vector<uint8_t> mIncomingMessages;

    bool mShutdown {};
};

#endif
