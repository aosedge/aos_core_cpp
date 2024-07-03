/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <thread>

#include "logger/logmodule.hpp"
#include "nodecontroller.hpp"
#include "utils/convert.hpp"

/***********************************************************************************************************************
 * NodeStreamHandler
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

NodeStreamHandler::NodeStreamHandler(const std::vector<aos::NodeStatus>& allowedStatuses,
    NodeServerReaderWriter* stream, grpc::ServerContext* context, aos::iam::nodemanager::NodeManagerItf* nodeManager)
    : mAllowedStatuses(allowedStatuses)
    , mStream(stream)
    , mContext(context)
    , mNodeManager(nodeManager)
{
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

    std::lock_guard lock {mMutex};

    mContext->TryCancel();

    mPendingMessages.clear();
}

std::string NodeStreamHandler::GetNodeID() const
{
    std::lock_guard lock {mNodeIDMutex};

    return mNodeID;
}

aos::Error NodeStreamHandler::HandleStream()
{
    LOG_DBG() << "Process stream handler";

    aos::Error                    err = aos::ErrorEnum::eNone;
    iamproto::IAMOutgoingMessages outgoing;

    while (mStream->Read(&outgoing)) {
        LOG_DBG() << "Receive message: type=" << outgoing.GetTypeName().c_str();

        const auto messageCase = outgoing.IAMOutgoingMessage_case();
        if (messageCase == iamproto::IAMOutgoingMessages::IAMOutgoingMessageCase::IAMOUTGOINGMESSAGE_NOT_SET) {
            continue;
        }

        if (outgoing.has_node_info()) {
            if (err = HandleNodeInfo(outgoing.node_info()); !err.IsNone()) {
                err = AOS_ERROR_WRAP(err);

                break;
            }
        }

        std::lock_guard lock {mMutex};

        try {
            if (auto it = mPendingMessages.find(messageCase); it != mPendingMessages.end()) {
                it->second.set_value(std::move(outgoing));
            }
        } catch (const std::exception& e) {
            err = AOS_ERROR_WRAP(aos::Error(aos::ErrorEnum::eFailed, e.what()));

            break;
        }
    }

    LOG_DBG() << "Stop stream handler";

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

aos::Error NodeStreamHandler::SendMessage(const iamproto::IAMIncomingMessages& request,
    iamproto::IAMOutgoingMessages& response, const std::chrono::seconds responseTimeout)
{
    if (mIsClosed) {
        return AOS_ERROR_WRAP(aos::Error(aos::ErrorEnum::eFailed, "Stream is closed"));
    }

    if (!mStream->Write(request)) {
        return AOS_ERROR_WRAP(aos::Error(aos::ErrorEnum::eFailed, "Failed to send message"));
    }

    try {
        std::promise<iamproto::IAMOutgoingMessages> promise;
        auto                                        responseFuture = promise.get_future();

        {
            std::lock_guard lock {mMutex};

            mPendingMessages[response.IAMOutgoingMessage_case()] = std::move(promise);
        }

        if (responseFuture.wait_for(responseTimeout) != std::future_status::ready) {
            return AOS_ERROR_WRAP(aos::Error(aos::ErrorEnum::eTimeout, "Response timeout"));
        }

        response = responseFuture.get();
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(aos::Error(aos::ErrorEnum::eRuntime, e.what()));
    }

    return aos::ErrorEnum::eNone;
}

aos::Error NodeStreamHandler::HandleNodeInfo(const iamproto::NodeInfo& info)
{
    aos::NodeInfo nodeInfo;

    if (auto err = utils::ConvertToAos(info, nodeInfo); !err.IsNone()) {
        return err;
    }

    if (std::find(mAllowedStatuses.cbegin(), mAllowedStatuses.cend(), nodeInfo.mStatus) == mAllowedStatuses.cend()) {
        return {aos::ErrorEnum::eInvalidArgument, "Node status is not allowed"};
    }

    if (auto err = mNodeManager->SetNodeInfo(nodeInfo); !err.IsNone()) {
        return err;
    }

    SetNodeID(info.node_id());

    return aos::ErrorEnum::eNone;
}

void NodeStreamHandler::SetNodeID(const std::string& nodeID)
{
    std::lock_guard lock {mNodeIDMutex};

    mNodeID = nodeID;
}

/***********************************************************************************************************************
 * NodeController
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

void NodeController::Close()
{
    std::lock_guard lock {mMutex};

    for (auto& handler : mHandlers) {
        handler.reset();
    }

    mHandlers.clear();
}

grpc::Status NodeController::HandleRegisterNodeStream(const std::vector<aos::NodeStatus>& allowedStatuses,
    NodeServerReaderWriter* stream, grpc::ServerContext* context, aos::iam::nodemanager::NodeManagerItf* nodeManager)
{
    auto handler = std::make_shared<NodeStreamHandler>(allowedStatuses, stream, context, nodeManager);
    StoreHandler(handler);

    auto ret = handler->HandleStream();

    handler->Close();

    RemoveHandler(handler);

    return utils::ConvertAosErrorToGrpcStatus(ret);
}

NodeStreamHandlerPtr NodeController::GetNodeStreamHandler(const std::string& nodeID)
{
    std::lock_guard lock {mMutex};

    auto it = std::find_if(
        mHandlers.begin(), mHandlers.end(), [&nodeID](const auto& handler) { return handler->GetNodeID() == nodeID; });
    if (it != mHandlers.end()) {
        return *it;
    }

    return {nullptr};
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void NodeController::StoreHandler(NodeStreamHandlerPtr handler)
{
    std::lock_guard lock {mMutex};

    mHandlers.push_back(std::move(handler));
}

void NodeController::RemoveHandler(NodeStreamHandlerPtr handler)
{
    std::lock_guard lock {mMutex};

    mHandlers.erase(std::remove(mHandlers.begin(), mHandlers.end(), handler), mHandlers.end());
}
