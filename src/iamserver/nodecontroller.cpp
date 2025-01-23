/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <thread>

#include <pbconvert/common.hpp>
#include <utils/exception.hpp>

#include "logger/logmodule.hpp"
#include "nodecontroller.hpp"
#include "utils/convert.hpp"

/***********************************************************************************************************************
 * NodeStreamHandler
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

NodeStreamHandler::Ptr NodeStreamHandler::Create(const std::vector<aos::NodeStatus>& allowedStatuses,
    NodeServerReaderWriter* stream, grpc::ServerContext* context, aos::iam::nodemanager::NodeManagerItf* nodeManager,
    StreamRegistryItf* streamRegistry)
{
    return NodeStreamHandler::Ptr(new NodeStreamHandler(allowedStatuses, stream, context, nodeManager, streamRegistry));
}

NodeStreamHandler::~NodeStreamHandler()
{
    Close();
}

void NodeStreamHandler::Close()
{
    if (mIsClosed.exchange(true)) {
        return;
    }

    LOG_DBG() << "Close node stream handler";

    std::lock_guard lock {mMutex};

    mContext->TryCancel();

    mPendingMessages.clear();
}

aos::Error NodeStreamHandler::HandleStream()
{
    LOG_DBG() << "Process stream handler";

    aos::Error                    err = aos::ErrorEnum::eNone;
    iamproto::IAMOutgoingMessages outgoing;

    while (mStream->Read(&outgoing)) {
        LOG_DBG() << "Receive message: type=" << outgoing.IAMOutgoingMessage_case();

        const auto messageCase = outgoing.IAMOutgoingMessage_case();
        if (messageCase == iamproto::IAMOutgoingMessages::IAMOutgoingMessageCase::IAMOUTGOINGMESSAGE_NOT_SET) {
            continue;
        }

        if (outgoing.has_node_info()) {
            if (err = HandleNodeInfo(outgoing.node_info()); !err.IsNone()) {
                err = AOS_ERROR_WRAP(err);

                break;
            }

            continue;
        }

        std::lock_guard lock {mMutex};

        try {
            if (auto it = mPendingMessages.find(messageCase); it != mPendingMessages.end()) {
                it->second.set_value(std::move(outgoing));
            }
        } catch (const std::exception& e) {
            err = AOS_ERROR_WRAP(aos::common::utils::ToAosError(e));

            break;
        }
    }

    LOG_DBG() << "Stop stream handler: err=" << err;

    return err;
}

grpc::Status NodeStreamHandler::GetCertTypes(const iamproto::GetCertTypesRequest* request,
    iamproto::CertTypes* response, const std::chrono::seconds responseTimeout)
{
    iamproto::IAMIncomingMessages incoming;
    iamproto::IAMOutgoingMessages outgoing;

    outgoing.mutable_cert_types_response();
    incoming.mutable_get_cert_types_request()->CopyFrom(*request);

    if (auto err = SendMessage(incoming, outgoing, responseTimeout); !err.IsNone()) {
        return utils::ConvertAosErrorToGrpcStatus(err);
    }

    if (!outgoing.has_cert_types_response()) {
        return grpc::Status::CANCELLED;
    }

    response->CopyFrom(outgoing.cert_types_response());

    return grpc::Status::OK;
}

grpc::Status NodeStreamHandler::StartProvisioning(const iamproto::StartProvisioningRequest* request,
    iamproto::StartProvisioningResponse* response, const std::chrono::seconds responseTimeout)
{
    iamproto::IAMIncomingMessages incoming;
    iamproto::IAMOutgoingMessages outgoing;

    outgoing.mutable_start_provisioning_response();
    incoming.mutable_start_provisioning_request()->CopyFrom(*request);

    if (auto err = SendMessage(incoming, outgoing, responseTimeout); !err.IsNone()) {
        return utils::ConvertAosErrorToGrpcStatus(err);
    }

    if (!outgoing.has_start_provisioning_response()) {
        return grpc::Status::CANCELLED;
    }

    response->CopyFrom(outgoing.start_provisioning_response());

    return grpc::Status::OK;
}

grpc::Status NodeStreamHandler::FinishProvisioning(const iamproto::FinishProvisioningRequest* request,
    iamproto::FinishProvisioningResponse* response, const std::chrono::seconds responseTimeout)
{
    iamproto::IAMIncomingMessages incoming;
    iamproto::IAMOutgoingMessages outgoing;

    outgoing.mutable_finish_provisioning_response();
    incoming.mutable_finish_provisioning_request()->CopyFrom(*request);

    if (auto err = SendMessage(incoming, outgoing, responseTimeout); !err.IsNone()) {
        return utils::ConvertAosErrorToGrpcStatus(err);
    }

    if (!outgoing.has_finish_provisioning_response()) {
        return grpc::Status::CANCELLED;
    }

    response->CopyFrom(outgoing.finish_provisioning_response());

    return grpc::Status::OK;
}

grpc::Status NodeStreamHandler::Deprovision(const iamproto::DeprovisionRequest* request,
    iamproto::DeprovisionResponse* response, const std::chrono::seconds responseTimeout)
{
    iamproto::IAMIncomingMessages incoming;
    iamproto::IAMOutgoingMessages outgoing;

    outgoing.mutable_deprovision_response();
    incoming.mutable_deprovision_request()->CopyFrom(*request);

    if (auto err = SendMessage(incoming, outgoing, responseTimeout); !err.IsNone()) {
        return utils::ConvertAosErrorToGrpcStatus(err);
    }

    if (!outgoing.has_deprovision_response()) {
        return grpc::Status::CANCELLED;
    }

    response->CopyFrom(outgoing.deprovision_response());

    return grpc::Status::OK;
}

grpc::Status NodeStreamHandler::PauseNode(const iamproto::PauseNodeRequest* request,
    iamproto::PauseNodeResponse* response, const std::chrono::seconds responseTimeout)
{
    iamproto::IAMIncomingMessages incoming;
    iamproto::IAMOutgoingMessages outgoing;

    outgoing.mutable_pause_node_response();
    incoming.mutable_pause_node_request()->CopyFrom(*request);

    if (auto err = SendMessage(incoming, outgoing, responseTimeout); !err.IsNone()) {
        return utils::ConvertAosErrorToGrpcStatus(err);
    }

    if (!outgoing.has_pause_node_response()) {
        return grpc::Status::CANCELLED;
    }

    response->CopyFrom(outgoing.pause_node_response());

    return grpc::Status::OK;
}

grpc::Status NodeStreamHandler::ResumeNode(const iamproto::ResumeNodeRequest* request,
    iamproto::ResumeNodeResponse* response, const std::chrono::seconds responseTimeout)
{
    iamproto::IAMIncomingMessages incoming;
    iamproto::IAMOutgoingMessages outgoing;

    outgoing.mutable_resume_node_response();
    incoming.mutable_resume_node_request()->CopyFrom(*request);

    if (auto err = SendMessage(incoming, outgoing, responseTimeout); !err.IsNone()) {
        return utils::ConvertAosErrorToGrpcStatus(err);
    }

    if (!outgoing.has_resume_node_response()) {
        return grpc::Status::CANCELLED;
    }

    response->CopyFrom(outgoing.resume_node_response());

    return grpc::Status::OK;
}

grpc::Status NodeStreamHandler::CreateKey(const iamproto::CreateKeyRequest* request,
    iamproto::CreateKeyResponse* response, const std::chrono::seconds responseTimeout)
{
    iamproto::IAMIncomingMessages incoming;
    iamproto::IAMOutgoingMessages outgoing;

    outgoing.mutable_create_key_response();
    incoming.mutable_create_key_request()->CopyFrom(*request);

    if (auto err = SendMessage(incoming, outgoing, responseTimeout); !err.IsNone()) {
        return utils::ConvertAosErrorToGrpcStatus(err);
    }

    if (!outgoing.has_create_key_response()) {
        return grpc::Status::CANCELLED;
    }

    response->CopyFrom(outgoing.create_key_response());

    return grpc::Status::OK;
}

grpc::Status NodeStreamHandler::ApplyCert(const iamproto::ApplyCertRequest* request,
    iamproto::ApplyCertResponse* response, const std::chrono::seconds responseTimeout)
{
    iamproto::IAMIncomingMessages incoming;
    iamproto::IAMOutgoingMessages outgoing;

    outgoing.mutable_apply_cert_response();
    incoming.mutable_apply_cert_request()->CopyFrom(*request);

    if (auto err = SendMessage(incoming, outgoing, responseTimeout); !err.IsNone()) {
        return utils::ConvertAosErrorToGrpcStatus(err);
    }

    if (!outgoing.has_apply_cert_response()) {
        return grpc::Status::CANCELLED;
    }

    response->CopyFrom(outgoing.apply_cert_response());

    return grpc::Status::OK;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

NodeStreamHandler::NodeStreamHandler(const std::vector<aos::NodeStatus>& allowedStatuses,
    NodeServerReaderWriter* stream, grpc::ServerContext* context, aos::iam::nodemanager::NodeManagerItf* nodeManager,
    StreamRegistryItf* streamRegistry)
    : mAllowedStatuses(allowedStatuses)
    , mStream(stream)
    , mContext(context)
    , mNodeManager(nodeManager)
    , mStreamRegistry(streamRegistry)
{
}

aos::Error NodeStreamHandler::SendMessage(const iamproto::IAMIncomingMessages& request,
    iamproto::IAMOutgoingMessages& response, const std::chrono::seconds responseTimeout)
{
    if (mIsClosed) {
        return AOS_ERROR_WRAP(aos::Error(aos::ErrorEnum::eFailed, "stream is closed"));
    }

    if (!mStream->Write(request)) {
        return AOS_ERROR_WRAP(aos::Error(aos::ErrorEnum::eFailed, "failed to send message"));
    }

    try {
        std::promise<iamproto::IAMOutgoingMessages> promise;
        auto                                        responseFuture = promise.get_future();

        {
            std::lock_guard lock {mMutex};

            mPendingMessages[response.IAMOutgoingMessage_case()] = std::move(promise);
        }

        if (responseFuture.wait_for(responseTimeout) != std::future_status::ready) {
            return AOS_ERROR_WRAP(aos::Error(aos::ErrorEnum::eTimeout, "response timeout"));
        }

        response = responseFuture.get();
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(aos::common::utils::ToAosError(e, aos::ErrorEnum::eRuntime));
    }

    return aos::ErrorEnum::eNone;
}

aos::Error NodeStreamHandler::HandleNodeInfo(const iamproto::NodeInfo& info)
{
    LOG_DBG() << "Received node info: nodeID=" << info.node_id().c_str() << ", status=" << info.status().c_str();

    auto nodeInfo = std::make_unique<aos::NodeInfo>();

    if (auto err = aos::common::pbconvert::ConvertToAos(info, *nodeInfo); !err.IsNone()) {
        return err;
    }

    if (std::find(mAllowedStatuses.cbegin(), mAllowedStatuses.cend(), nodeInfo->mStatus) == mAllowedStatuses.cend()) {
        LOG_WRN() << "Node status is not in allowed list: nodeID=" << nodeInfo->mNodeID
                  << ", status=" << nodeInfo->mStatus;

        mStreamRegistry->UnlinkNodeIDFromHandler(shared_from_this());

        return aos::ErrorEnum::eNone;
    }

    if (auto err = mNodeManager->SetNodeInfo(*nodeInfo); !err.IsNone()) {
        return err;
    }

    mStreamRegistry->LinkNodeIDToHandler(info.node_id(), shared_from_this());

    return aos::ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * NodeController
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

NodeController::NodeController()
{
    Start();
}

void NodeController::Start()
{
    std::lock_guard lock {mMutex};

    mIsClosed = false;
}

void NodeController::Close()
{
    std::lock_guard lock {mMutex};

    mIsClosed = true;

    // Call Close method explicitly to avoid hanging on shutdown.
    // HandleRegisterNodeStream method references handler so destructor is not called here.
    for (auto& it : mHandlers) {
        if (it.first != nullptr) {
            it.first->Close();
        }
    }

    mHandlers.clear();
}

grpc::Status NodeController::HandleRegisterNodeStream(const std::vector<aos::NodeStatus>& allowedStatuses,
    NodeServerReaderWriter* stream, grpc::ServerContext* context, aos::iam::nodemanager::NodeManagerItf* nodeManager)
{
    {
        std::lock_guard lock {mMutex};

        if (mIsClosed) {
            LOG_DBG() << "Node controller closed, cancel node registration";

            return grpc::Status::CANCELLED;
        }
    }

    auto handler = NodeStreamHandler::Create(
        allowedStatuses, stream, context, nodeManager, static_cast<NodeStreamHandler::StreamRegistryItf*>(this));

    Store(handler);

    auto ret = handler->HandleStream();

    handler->Close();

    Remove(handler);

    return utils::ConvertAosErrorToGrpcStatus(ret);
}

NodeStreamHandler::Ptr NodeController::GetNodeStreamHandler(const std::string& nodeID)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get node controller stream handler: nodeID=" << nodeID.c_str();

    if (auto it = std::find_if(
            mHandlers.begin(), mHandlers.end(), [nodeID](const auto& pair) { return pair.second == nodeID; });
        it != mHandlers.end()) {
        return it->first;
    }

    LOG_ERR() << "Stream handler not found: nodeID=" << nodeID.c_str();

    return {nullptr};
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void NodeController::LinkNodeIDToHandler(const std::string& nodeID, NodeStreamHandler::Ptr handler)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Link node id with stream handler: nodeID=" << nodeID.c_str();

    for (auto& it : mHandlers) {
        if (it.second == nodeID) {
            it.second.clear();
        }
    }

    mHandlers[std::move(handler)] = nodeID;
}

void NodeController::UnlinkNodeIDFromHandler(NodeStreamHandler::Ptr handler)
{
    std::lock_guard lock {mMutex};

    if (auto it = std::find_if(
            mHandlers.begin(), mHandlers.end(), [&handler](const auto& pair) { return pair.first == handler; });
        it != mHandlers.end()) {

        LOG_DBG() << "Unlink nodeID from steam handler: nodeID=" << it->second.c_str();

        it->second.clear();
    }
}

void NodeController::Store(NodeStreamHandler::Ptr handler)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Store node stream handler";

    mHandlers[std::move(handler)] = {};
}

void NodeController::Remove(NodeStreamHandler::Ptr handler)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Remove node stream handler";

    mHandlers.erase(handler);
}
