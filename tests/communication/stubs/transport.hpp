/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>
#include <map>
#include <vector>

#include <unistd.h>

#include <openssl/engine.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <pthread.h>

#include "communication/communicationchannel.hpp"
#include "communication/types.hpp"
#include "communication/utils.hpp"

using namespace aos::mp::communication;

class Pipe : public TransportItf {
public:
    Pipe() = default;

    void SetFds(int readFd, int writeFd)
    {
        mReadFd  = readFd;
        mWriteFd = writeFd;
    }

    aos::Error Connect() override { return aos::ErrorEnum::eNone; }

    aos::Error Read(std::vector<uint8_t>& message) override
    {
        auto bytesRead = read(mReadFd, message.data(), message.size());
        if (bytesRead <= 0) {
            return aos::Error(aos::ErrorEnum::eRuntime, "failed to read");
        }

        message.resize(bytesRead);

        return aos::ErrorEnum::eNone;
    }

    aos::Error Write(std::vector<uint8_t> message) override
    {
        ssize_t bytesWritten = write(mWriteFd, message.data(), message.size());
        if (bytesWritten <= 0) {
            return aos::Error(aos::ErrorEnum::eRuntime, "failed to write");
        }

        return aos::ErrorEnum::eNone;
    }

    aos::Error Close() override
    {
        if (close(mReadFd) == -1) {
            return aos::Error(aos::ErrorEnum::eRuntime, "failed to close read fd");
        }

        if (close(mWriteFd) == -1) {
            return aos::Error(aos::ErrorEnum::eRuntime, "failed to close write fd");
        }

        return aos::ErrorEnum::eNone;
    }

private:
    int mReadFd {-1};
    int mWriteFd {-1};
};

class PipePair {
public:
    PipePair() = default;

    aos::Error CreatePair(Pipe& transport1, Pipe& transport2)
    {
        if (pipe(mPipeFd1) == -1 || pipe(mPipeFd2) == -1) {
            return aos::Error(aos::ErrorEnum::eRuntime, "failed to create pipe");
        }

        // transport1: write to mPipeFd1[1], read from mPipeFd2[0]
        // transport2: read from mPipeFd1[0], write to mPipeFd2[1]
        transport1.SetFds(mPipeFd2[0], mPipeFd1[1]);
        transport2.SetFds(mPipeFd1[0], mPipeFd2[1]);

        return aos::ErrorEnum::eNone;
    }

private:
    int mPipeFd1[2] {-1};
    int mPipeFd2[2] {-1};
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

    aos::Error Connect() override
    {
        auto err = createContext();
        if (err != aos::ErrorEnum::eNone)
            return err;

        err = initializeOpenSSL();
        if (err != aos::ErrorEnum::eNone)
            return err;

        err = configureContext();
        if (err != aos::ErrorEnum::eNone)
            return err;

        err = setupSSL();
        if (err != aos::ErrorEnum::eNone)
            return err;

        return performHandshake();
    }

    aos::Error Read(std::vector<uint8_t>& message) override
    {
        int bytesRead = SSL_read(mSSL, message.data(), message.size());
        if (bytesRead <= 0) {
            return aos::Error(aos::ErrorEnum::eRuntime, "SSL read failed");
        }

        return aos::ErrorEnum::eNone;
    }

    aos::Error Write(std::vector<uint8_t> message) override
    {
        int bytesWritten = SSL_write(mSSL, message.data(), message.size());
        if (bytesWritten <= 0) {
            return aos::Error(aos::ErrorEnum::eRuntime, "SSL write failed");
        }

        return aos::ErrorEnum::eNone;
    }

    aos::Error Close() override
    {
        if (mSSL) {
            SSL_shutdown(mSSL);
            SSL_free(mSSL);
            mSSL = nullptr;
        }

        if (mCtx) {
            SSL_CTX_free(mCtx);
            mCtx = nullptr;
        }

        if (mEngine) {
            ENGINE_finish(mEngine);
            ENGINE_free(mEngine);
            mEngine = nullptr;
        }

        EVP_cleanup();

        return aos::ErrorEnum::eNone;
    }

private:
    CommChannelItf& mChannel;
    std::string     mKeyID;
    std::string     mCertPEM;
    std::string     mCaCertPath;
    SSL_CTX*        mCtx       = nullptr;
    SSL*            mSSL       = nullptr;
    ENGINE*         mEngine    = nullptr;
    BIO_METHOD*     mBioMethod = nullptr;

    aos::Error initializeOpenSSL()
    {
        mEngine = ENGINE_by_id("pkcs11");
        if (!mEngine) {
            return aos::Error(aos::ErrorEnum::eRuntime, "Failed to load PKCS#11 engine");
        }

        if (!ENGINE_init(mEngine)) {
            return aos::Error(aos::ErrorEnum::eRuntime, "Failed to initialize PKCS#11 engine");
        }

        return aos::ErrorEnum::eNone;
    }

    aos::Error createContext()
    {
        const SSL_METHOD* method = TLS_client_method();
        mCtx                     = SSL_CTX_new(method);
        if (!mCtx) {
            return aos::Error(aos::ErrorEnum::eRuntime, "Unable to create SSL context");
        }

        return aos::ErrorEnum::eNone;
    }

    aos::Error configureContext()
    {
        SSL_CTX_set_verify(mCtx, SSL_VERIFY_PEER, NULL);

        EVP_PKEY* pkey = ENGINE_load_private_key(mEngine, mKeyID.c_str(), NULL, NULL);
        if (!pkey) {
            unsigned long err = ERR_get_error();
            char          err_buf[256];
            ERR_error_string_n(err, err_buf, sizeof(err_buf));

            return aos::Error(aos::ErrorEnum::eRuntime, "Failed to load private key");
        }

        if (SSL_CTX_use_PrivateKey(mCtx, pkey) <= 0) {
            return aos::Error(aos::ErrorEnum::eRuntime, "Failed to set private key");
        }

        // Split the certificate chain into individual certificates
        BIO* bio = BIO_new_mem_buf(mCertPEM.c_str(), -1);
        if (!bio) {
            return aos::Error(aos::ErrorEnum::eRuntime, "failed to create BIO");
        }
        std::unique_ptr<BIO, decltype(&BIO_free)> bioPtr(bio, BIO_free);

        X509* cert = PEM_read_bio_X509(bio, nullptr, 0, nullptr);
        if (!cert) {
            return aos::Error(aos::ErrorEnum::eRuntime, "Failed to load certificate");
        }
        std::unique_ptr<X509, decltype(&X509_free)> certPtr(cert, X509_free);

        if (SSL_CTX_use_certificate(mCtx, cert) <= 0) {
            return aos::Error(aos::ErrorEnum::eRuntime, "Failed to set certificate");
        }

        // Load the intermediate certificates
        STACK_OF(X509)* chain  = sk_X509_new_null();
        X509* intermediateCert = nullptr;
        while ((intermediateCert = PEM_read_bio_X509(bio, nullptr, 0, nullptr)) != nullptr) {
            sk_X509_push(chain, intermediateCert);
        }

        if (SSL_CTX_set1_chain(mCtx, chain) <= 0) {
            sk_X509_pop_free(chain, X509_free);
            return aos::Error(aos::ErrorEnum::eRuntime, "Failed to set certificate chain");
        }

        sk_X509_pop_free(chain, X509_free);

        if (SSL_CTX_load_verify_locations(mCtx, mCaCertPath.c_str(), NULL) <= 0) {
            return aos::Error(aos::ErrorEnum::eRuntime, "Failed to load CA certificate");
        }

        return aos::ErrorEnum::eNone;
    }

    aos::Error setupSSL()
    {
        mSSL = SSL_new(mCtx);
        if (!mSSL) {
            return aos::Error(aos::ErrorEnum::eRuntime, "Failed to create SSL object");
        }

        mBioMethod = createCustomBioMethod();
        if (!mBioMethod) {
            return aos::Error(aos::ErrorEnum::eRuntime, "Failed to create custom BIO method");
        }

        BIO* rbio = BIO_new(mBioMethod);
        BIO* wbio = BIO_new(mBioMethod);

        if (!rbio || !wbio) {
            return aos::Error(aos::ErrorEnum::eRuntime, "Failed to create BIO objects");
        }

        BIO_set_data(rbio, this);
        BIO_set_data(wbio, this);

        SSL_set_bio(mSSL, rbio, wbio);

        return aos::ErrorEnum::eNone;
    }

    aos::Error performHandshake()
    {
        int result = SSL_connect(mSSL);
        if (result <= 0) {
            unsigned long errCode = ERR_get_error();
            char          errBuffer[256];
            ERR_error_string_n(errCode, errBuffer, sizeof(errBuffer));

            return aos::Error(aos::ErrorEnum::eRuntime, "SSL handshake failed");
        }

        return aos::ErrorEnum::eNone;
    }

    static int customBioWrite(BIO* bio, const char* data, int len)
    {
        SecureClientChannel* pipe = static_cast<SecureClientChannel*>(BIO_get_data(bio));
        std::vector<uint8_t> buffer(data, data + len);
        aos::Error           err = pipe->mChannel.Write(buffer);

        return err.IsNone() ? len : -1;
    }

    static int customBioRead(BIO* bio, char* data, int len)
    {
        SecureClientChannel* pipe = static_cast<SecureClientChannel*>(BIO_get_data(bio));
        std::vector<uint8_t> buffer(len);
        auto                 err = pipe->mChannel.Read(buffer);
        if (!err.IsNone())
            return -1;

        std::memcpy(data, buffer.data(), buffer.size());

        return buffer.size();
    }

    static long customBioCtrl([[maybe_unused]] BIO* bio, int cmd, [[maybe_unused]] long num, [[maybe_unused]] void* ptr)
    {
        switch (cmd) {
        case BIO_CTRL_FLUSH:
            return 1;
        default:
            return 0;
        }
    }

    BIO_METHOD* createCustomBioMethod()
    {
        BIO_METHOD* method = BIO_meth_new(BIO_TYPE_SOURCE_SINK, "SecureClientChannel BIO");
        if (!method)
            return nullptr;

        BIO_meth_set_write(method, customBioWrite);
        BIO_meth_set_read(method, customBioRead);
        BIO_meth_set_ctrl(method, customBioCtrl);

        return method;
    }
};

class CommManager : public CommChannelItf {
public:
    CommManager(Pipe& transport)
        : mTransport(transport)
    {
        mThread = std::thread(&CommManager::ReadHandler, this);
    }

    ~CommManager()
    {
        mShutdown = true;
        mThread.join();
    }

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

    aos::Error Close() override { return aos::ErrorEnum::eNone; }
    aos::Error Connect() override { return aos::ErrorEnum::eNone; }
    aos::Error Read([[maybe_unused]] std::vector<uint8_t>& message) override { return aos::ErrorEnum::eNone; }

private:
    static void CalculateChecksum(const std::vector<uint8_t>& data, uint8_t* checksum)
    {
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        SHA256_Update(&sha256, data.data(), data.size());
        SHA256_Final(checksum, &sha256);
    }

    void ReadHandler()
    {
        while (!mShutdown) {
            std::vector<uint8_t> headerBuffer(sizeof(AosProtocolHeader));
            auto                 err = mTransport.Read(headerBuffer);
            if (!err.IsNone()) {
                return;
            }

            AosProtocolHeader header;
            std::memcpy(&header, headerBuffer.data(), sizeof(AosProtocolHeader));

            // Read body
            std::vector<uint8_t> message(header.mDataSize);
            err = mTransport.Read(message);
            if (!err.IsNone()) {
                return;
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
                return;
            }
        }
    }

    Pipe&                                                mTransport;
    std::shared_ptr<CommunicationChannel>                mCommChannel;
    std::thread                                          mThread;
    std::atomic<bool>                                    mShutdown {false};
    std::map<int, std::shared_ptr<CommunicationChannel>> mChannels;
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
