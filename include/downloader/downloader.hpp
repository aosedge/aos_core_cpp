/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DOWNLOADER_HPP_
#define DOWNLOADER_HPP_

#include <aos/common/downloader/downloader.hpp>

namespace aos::common::downloader {

class Downloader : public aos::downloader::DownloaderItf {
public:
    Error Download(const String& url, const String& path, aos::downloader::DownloadContent contentType) override
    {
        (void)url;
        (void)path;
        (void)contentType;

        return ErrorEnum::eNone;
    }
};

} // namespace aos::common::downloader

#endif
