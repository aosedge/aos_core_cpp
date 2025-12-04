/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <core/common/tools/logger.hpp>

#include "resourcemanager.hpp"

namespace aos::sm::resourcemanager {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

void ParseMount(const common::utils::CaseInsensitiveObjectWrapper& object, Mount& mount)
{
    auto err = mount.mDestination.Assign(object.GetValue<std::string>("destination").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse mount destination");

    err = mount.mType.Assign(object.GetValue<std::string>("type").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse mount type");

    err = mount.mSource.Assign(object.GetValue<std::string>("source").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse mount source");

    common::utils::ForEach(object, "options", [&](const Poco::Dynamic::Var& optionVar) {
        auto err = mount.mOptions.EmplaceBack(optionVar.convert<std::string>().c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse mount option");
    });
}

void ParseHost(const common::utils::CaseInsensitiveObjectWrapper& object, Host& host)
{
    auto err = host.mHostname.Assign(object.GetValue<std::string>("hostname").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse hostname");

    err = host.mIP.Assign(object.GetValue<std::string>("ip").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse host ip");
}

void ParseResourceInfo(const common::utils::CaseInsensitiveObjectWrapper& object, ResourceInfo& resource)
{
    auto err = resource.mName.Assign(object.GetValue<std::string>("name").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse resource name");

    resource.mSharedCount = object.GetValue<size_t>("sharedCount", 0);

    common::utils::ForEach(object, "groups", [&](const Poco::Dynamic::Var& var) {
        auto err = resource.mGroups.EmplaceBack(var.convert<std::string>().c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse group name");
    });

    common::utils::ForEach(object, "mounts", [&](const Poco::Dynamic::Var& var) {
        auto err = resource.mMounts.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse mount info");

        ParseMount(common::utils::CaseInsensitiveObjectWrapper(var), resource.mMounts.Back());
    });

    common::utils::ForEach(object, "envs", [&](const Poco::Dynamic::Var& var) {
        auto err = resource.mEnv.EmplaceBack(var.convert<std::string>().c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse env variable");
    });

    common::utils::ForEach(object, "hosts", [&](const Poco::Dynamic::Var& var) {
        auto err = resource.mHosts.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse host info");

        ParseHost(common::utils::CaseInsensitiveObjectWrapper(var), resource.mHosts.Back());
    });

    common::utils::ForEach(object, "hostDevices", [&](const Poco::Dynamic::Var& var) {
        auto err = resource.mHostDevices.EmplaceBack(var.convert<std::string>().c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse host device name");
    });
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error ResourceManager::Init(const Config& config)
{
    LOG_DBG() << "Initialize resource manager";

    mConfig = config;

    return ParseResourceInfos();
}

Error ResourceManager::GetResourcesInfos(Array<ResourceInfo>& resources)
{
    LOG_DBG() << "Getting resources info";

    return resources.Assign(Array<ResourceInfo>(mResources.data(), mResources.size()));
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error ResourceManager::ParseResourceInfos()
{
    std::ifstream file(mConfig.mResourceInfoFilePath.CStr());
    if (!file.is_open()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't open resource info file"));
    }

    auto parseResult = common::utils::ParseJson(file);
    if (!parseResult.mError.IsNone()) {
        return AOS_ERROR_WRAP(parseResult.mError);
    }

    auto items = parseResult.mValue.extract<Poco::JSON::Array::Ptr>();
    if (!items) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "invalid resource info format"));
    }

    mResources.reserve(items->size());

    for (const auto& item : *items) {
        mResources.emplace_back();

        try {
            ParseResourceInfo(common::utils::CaseInsensitiveObjectWrapper(item), mResources.back());
        } catch (const std::exception& e) {
            return AOS_ERROR_WRAP(common::utils::ToAosError(e));
        }
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::resourcemanager
