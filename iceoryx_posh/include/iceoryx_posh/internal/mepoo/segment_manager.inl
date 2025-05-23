// Copyright (c) 2019 by Robert Bosch GmbH. All rights reserved.
// Copyright (c) 2021 by Apex.AI Inc. All rights reserved.
// Copyright (c) 2023 by ekxide IO GmbH. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0
#ifndef IOX_POSH_MEPOO_SEGMENT_MANAGER_INL
#define IOX_POSH_MEPOO_SEGMENT_MANAGER_INL

#include "iceoryx_posh/iceoryx_posh_types.hpp"
#include "iceoryx_posh/internal/mepoo/segment_manager.hpp"
#include "iceoryx_posh/internal/posh_error_reporting.hpp"
#include "iox/assertions.hpp"

namespace iox
{
namespace mepoo
{
template <typename SegmentType>
inline SegmentManager<SegmentType>::SegmentManager(const SegmentConfig& segmentConfig,
                                                   const DomainId domainId,
                                                   BumpAllocator* managementAllocator) noexcept
    : m_managementAllocator(managementAllocator)
{
    if (segmentConfig.m_sharedMemorySegments.capacity() > m_segmentContainer.capacity())
    {
        IOX_LOG(Fatal,
                "Trying to add " << segmentConfig.m_sharedMemorySegments.capacity()
                                 << " segments while the 'SegmentManager' can manage only "
                                 << m_segmentContainer.capacity());
        IOX_PANIC("Too many segments");
    }
    for (const auto& segmentEntry : segmentConfig.m_sharedMemorySegments)
    {
        createSegment(segmentEntry, domainId);
    }
}

template <typename SegmentType>
inline void SegmentManager<SegmentType>::createSegment(const SegmentConfig::SegmentEntry& segmentEntry,
                                                       const DomainId domainId) noexcept
{
    auto readerGroup = PosixGroup(segmentEntry.m_readerGroup);
    auto writerGroup = PosixGroup(segmentEntry.m_writerGroup);
    m_segmentContainer.emplace_back(segmentEntry.m_mempoolConfig,
                                    domainId,
                                    *m_managementAllocator,
                                    readerGroup,
                                    writerGroup,
                                    segmentEntry.m_memoryInfo);
}

template <typename SegmentType>
inline typename SegmentManager<SegmentType>::SegmentMappingContainer
SegmentManager<SegmentType>::getSegmentMappings(const PosixUser& user) noexcept
{
    // get all the groups the user is in
    auto groupContainer = user.getGroups();

    SegmentManager::SegmentMappingContainer mappingContainer;
    bool foundInWriterGroup = false;

    // with the groups we can get all the segments (read or write) for the user
    for (const auto& groupID : groupContainer)
    {
        for (const auto& segment : m_segmentContainer)
        {
            if (segment.getWriterGroup() == groupID)
            {
                // a user is allowed to be only in one writer group, as we currently only support one memory manager per
                // process
                if (!foundInWriterGroup)
                {
                    mappingContainer.emplace_back(
                        segment.getWriterGroup().getName(), segment.getSegmentSize(), true, segment.getSegmentId());
                    foundInWriterGroup = true;
                }
                else
                {
                    IOX_REPORT_FATAL(PoshError::MEPOO__USER_WITH_MORE_THAN_ONE_WRITE_SEGMENT);
                    return SegmentManager::SegmentMappingContainer();
                }
            }
        }
    }

    for (const auto& groupID : groupContainer)
    {
        for (const auto& segment : m_segmentContainer)
        {
            // only add segments which are not yet added as writer
            if (segment.getReaderGroup() == groupID
                && std::find_if(mappingContainer.begin(), mappingContainer.end(), [&](const SegmentMapping& mapping) {
                       return mapping.m_segmentId == segment.getSegmentId();
                   }) == mappingContainer.end())
            {
                mappingContainer.emplace_back(
                    segment.getWriterGroup().getName(), segment.getSegmentSize(), false, segment.getSegmentId());
            }
        }
    }

    return mappingContainer;
}

template <typename SegmentType>
inline typename SegmentManager<SegmentType>::SegmentUserInformation
SegmentManager<SegmentType>::getSegmentInformationWithWriteAccessForUser(const PosixUser& user) noexcept
{
    auto groupContainer = user.getGroups();

    SegmentUserInformation segmentInfo{nullopt_t(), 0u};

    // with the groups we can search for the writable segment of this user
    for (const auto& groupID : groupContainer)
    {
        for (auto& segment : m_segmentContainer)
        {
            if (segment.getWriterGroup() == groupID)
            {
                segmentInfo.m_memoryManager = segment.getMemoryManager();
                segmentInfo.m_segmentID = segment.getSegmentId();
                return segmentInfo;
            }
        }
    }

    return segmentInfo;
}

template <typename SegmentType>
uint64_t SegmentManager<SegmentType>::requiredManagementMemorySize(const SegmentConfig& config) noexcept
{
    uint64_t memorySize{0u};
    for (auto segment : config.m_sharedMemorySegments)
    {
        memorySize += MemoryManager::requiredManagementMemorySize(segment.m_mempoolConfig);
    }
    return memorySize;
}

template <typename SegmentType>
uint64_t SegmentManager<SegmentType>::requiredChunkMemorySize(const SegmentConfig& config) noexcept
{
    uint64_t memorySize{0u};
    for (auto segment : config.m_sharedMemorySegments)
    {
        memorySize += MemoryManager::requiredChunkMemorySize(segment.m_mempoolConfig);
    }
    return memorySize;
}

template <typename SegmentType>
uint64_t SegmentManager<SegmentType>::requiredFullMemorySize(const SegmentConfig& config) noexcept
{
    return requiredManagementMemorySize(config) + requiredChunkMemorySize(config);
}

} // namespace mepoo
} // namespace iox

#endif // IOX_POSH_MEPOO_SEGMENT_MANAGER_INL
