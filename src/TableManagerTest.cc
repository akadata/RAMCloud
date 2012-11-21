/* Copyright (c) 2012 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "TestUtil.h"
#include "CoordinatorClient.h"
#include "CoordinatorService.h"
#include "MasterService.h"
#include "MembershipService.h"
#include "MockCluster.h"
#include "Recovery.h"
#include "ServerList.h"

namespace RAMCloud {

class TableManagerTest : public ::testing::Test {
  public:
    Context context;
    TableManager tableManager;
    std::mutex mutex;

    TableManagerTest()
        : context()
        , tableManager(&context)
        , mutex()
    {
        Logger::get().setLogLevels(RAMCloud::SILENT_LOG_LEVEL);
    }

    void fillMap(uint32_t entries) {
        for (uint32_t i = 0; i < entries; ++i) {
            const uint32_t b = i * 10;
            tableManager.addTablet({b + 1, b + 2, b + 3, {b + 4, b + 5},
                           Tablet::RECOVERING, {b + 6l, b + 7}});
        }
    }

    typedef std::unique_lock<std::mutex> Lock;
    DISALLOW_COPY_AND_ASSIGN(TableManagerTest);
};

TEST_F(TableManagerTest, addTablet) {
    tableManager.addTablet({1, 2, 3, {4, 5}, Tablet::RECOVERING, {6, 7}});
    EXPECT_EQ(1lu, tableManager.size());
    Tablet tablet = tableManager.getTablet(1, 2, 3);
    EXPECT_EQ(1lu, tablet.tableId);
    EXPECT_EQ(2lu, tablet.startKeyHash);
    EXPECT_EQ(3lu, tablet.endKeyHash);
    EXPECT_EQ(ServerId(4, 5), tablet.serverId);
    EXPECT_EQ(Tablet::RECOVERING, tablet.status);
    EXPECT_EQ(Log::Position(6, 7), tablet.ctime);
}

TEST_F(TableManagerTest, getTablet) {
    fillMap(3);
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t b = i * 10;
        Tablet tablet = tableManager.getTablet(b + 1, b + 2, b + 3);
        EXPECT_EQ(b + 1, tablet.tableId);
        EXPECT_EQ(b + 2, tablet.startKeyHash);
        EXPECT_EQ(b + 3, tablet.endKeyHash);
        EXPECT_EQ(ServerId(b + 4, b + 5), tablet.serverId);
        EXPECT_EQ(Tablet::RECOVERING, tablet.status);
        EXPECT_EQ(Log::Position(b + 6, b + 7), tablet.ctime);
    }
    EXPECT_THROW(tableManager.getTablet(0, 0, 0), TableManager::NoSuchTablet);
}

TEST_F(TableManagerTest, getTabletsForTable) {
    tableManager.addTablet({0, 1, 6, {0, 1}, Tablet::NORMAL, {0, 5}});
    tableManager.addTablet({1, 2, 7, {1, 1}, Tablet::NORMAL, {1, 6}});
    tableManager.addTablet({0, 3, 8, {2, 1}, Tablet::NORMAL, {2, 7}});
    tableManager.addTablet({1, 4, 9, {3, 1}, Tablet::NORMAL, {3, 8}});
    tableManager.addTablet({2, 5, 10, {4, 1}, Tablet::NORMAL, {4, 9}});
    auto tablets = tableManager.getTabletsForTable(0);
    EXPECT_EQ(2lu, tablets.size());
    EXPECT_EQ(ServerId(0, 1), tablets[0].serverId);
    EXPECT_EQ(ServerId(2, 1), tablets[1].serverId);

    tablets = tableManager.getTabletsForTable(1);
    EXPECT_EQ(2lu, tablets.size());
    EXPECT_EQ(ServerId(1, 1), tablets[0].serverId);
    EXPECT_EQ(ServerId(3, 1), tablets[1].serverId);

    tablets = tableManager.getTabletsForTable(2);
    EXPECT_EQ(1lu, tablets.size());
    EXPECT_EQ(ServerId(4, 1), tablets[0].serverId);

    tablets = tableManager.getTabletsForTable(3);
    EXPECT_EQ(0lu, tablets.size());
}

TEST_F(TableManagerTest, modifyTablet) {
    tableManager.addTablet({0, 1, 6, {0, 1}, Tablet::NORMAL, {0, 5}});
    tableManager.modifyTablet(0, 1, 6, {1, 2}, Tablet::RECOVERING, {3, 9});
    Tablet tablet = tableManager.getTablet(0, 1, 6);
    EXPECT_EQ(ServerId(1, 2), tablet.serverId);
    EXPECT_EQ(Tablet::RECOVERING, tablet.status);
    EXPECT_EQ(Log::Position(3, 9), tablet.ctime);
    EXPECT_THROW(
        tableManager.modifyTablet(0, 0, 0, {0, 0}, Tablet::NORMAL, {0, 0}),
        TableManager::NoSuchTablet);
}

TEST_F(TableManagerTest, removeTabletsForTable) {
    tableManager.addTablet({0, 1, 6, {0, 1}, Tablet::NORMAL, {0, 5}});
    tableManager.addTablet({1, 2, 7, {1, 1}, Tablet::NORMAL, {1, 6}});
    tableManager.addTablet({0, 3, 8, {2, 1}, Tablet::NORMAL, {2, 7}});

    EXPECT_EQ(0lu, tableManager.removeTabletsForTable(2).size());
    EXPECT_EQ(3lu, tableManager.size());

    auto tablets = tableManager.removeTabletsForTable(1);
    EXPECT_EQ(2lu, tableManager.size());
    foreach (const auto& tablet, tablets) {
        EXPECT_THROW(tableManager.getTablet(tablet.tableId,
                                   tablet.startKeyHash,
                                   tablet.endKeyHash),
                     TableManager::NoSuchTablet);
    }

    tablets = tableManager.removeTabletsForTable(0);
    EXPECT_EQ(0lu, tableManager.size());
    foreach (const auto& tablet, tablets) {
        EXPECT_THROW(tableManager.getTablet(tablet.tableId,
                                   tablet.startKeyHash,
                                   tablet.endKeyHash),
                     TableManager::NoSuchTablet);
    }
}

TEST_F(TableManagerTest, serialize) {
    Lock lock(mutex);
    CoordinatorServerList serverList(&context);
    ServerId id1 = serverList.generateUniqueId(lock);
    serverList.add(lock, id1, "mock:host=one", {WireFormat::MASTER_SERVICE}, 1);
    ServerId id2 = serverList.generateUniqueId(lock);
    serverList.add(lock, id2, "mock:host=two", {WireFormat::MASTER_SERVICE}, 2);
    tableManager.addTablet({0, 1, 6, id1, Tablet::NORMAL, {0, 5}});
    tableManager.addTablet({1, 2, 7, id2, Tablet::NORMAL, {1, 6}});
    ProtoBuf::Tablets tablets;
    tableManager.serialize(serverList, tablets);
    EXPECT_EQ("tablet { table_id: 0 start_key_hash: 1 end_key_hash: 6 "
              "state: NORMAL server_id: 1 service_locator: \"mock:host=one\" "
              "ctime_log_head_id: 0 ctime_log_head_offset: 5 } "
              "tablet { table_id: 1 start_key_hash: 2 end_key_hash: 7 "
              "state: NORMAL server_id: 2 service_locator: \"mock:host=two\" "
              "ctime_log_head_id: 1 ctime_log_head_offset: 6 }",
              tablets.ShortDebugString());
}

TEST_F(TableManagerTest, setStatusForServer) {
    tableManager.addTablet({0, 1, 6, {0, 1}, Tablet::NORMAL, {0, 5}});
    tableManager.addTablet({1, 2, 7, {1, 1}, Tablet::NORMAL, {1, 6}});
    tableManager.addTablet({0, 3, 8, {0, 1}, Tablet::NORMAL, {2, 7}});

    EXPECT_EQ(0lu,
        tableManager.setStatusForServer({2, 1}, Tablet::RECOVERING).size());

    auto tablets = tableManager.setStatusForServer({0, 1}, Tablet::RECOVERING);
    EXPECT_EQ(2lu, tablets.size());
    foreach (const auto& tablet, tablets) {
        Tablet inMap = tableManager.getTablet(tablet.tableId,
                                     tablet.startKeyHash,
                                     tablet.endKeyHash);
        EXPECT_EQ(ServerId(0, 1), tablet.serverId);
        EXPECT_EQ(ServerId(0, 1), inMap.serverId);
        EXPECT_EQ(Tablet::RECOVERING, tablet.status);
        EXPECT_EQ(Tablet::RECOVERING, inMap.status);
    }

    tablets = tableManager.setStatusForServer({1, 1}, Tablet::RECOVERING);
    ASSERT_EQ(1lu, tablets.size());
    auto tablet = tablets[0];
    Tablet inMap = tableManager.getTablet(tablet.tableId,
                                 tablet.startKeyHash,
                                 tablet.endKeyHash);
    EXPECT_EQ(ServerId(1, 1), tablet.serverId);
    EXPECT_EQ(ServerId(1, 1), inMap.serverId);
    EXPECT_EQ(Tablet::RECOVERING, tablet.status);
    EXPECT_EQ(Tablet::RECOVERING, inMap.status);
}

TEST_F(TableManagerTest, splitTablet) {
    tableManager.addTablet({0, 0, ~0lu, {1, 0}, Tablet::NORMAL, {2, 3}});
    tableManager.splitTablet(0, 0, ~0lu, ~0lu / 2);
    EXPECT_EQ("Tablet { tableId: 0 startKeyHash: 0 "
              "endKeyHash: 9223372036854775806 "
              "serverId: 1.0 status: NORMAL "
              "ctime: 2, 3 } "
              "Tablet { tableId: 0 "
              "startKeyHash: 9223372036854775807 "
              "endKeyHash: 18446744073709551615 "
              "serverId: 1.0 status: NORMAL "
              "ctime: 2, 3 }",
              tableManager.debugString());

    tableManager.splitTablet(0, 0, 9223372036854775806, 4611686018427387903);
    EXPECT_EQ("Tablet { tableId: 0 startKeyHash: 0 "
              "endKeyHash: 4611686018427387902 "
              "serverId: 1.0 status: NORMAL "
              "ctime: 2, 3 } "
              "Tablet { tableId: 0 "
              "startKeyHash: 9223372036854775807 "
              "endKeyHash: 18446744073709551615 "
              "serverId: 1.0 status: NORMAL "
              "ctime: 2, 3 } "
              "Tablet { tableId: 0 "
              "startKeyHash: 4611686018427387903 "
              "endKeyHash: 9223372036854775806 "
              "serverId: 1.0 status: NORMAL "
              "ctime: 2, 3 }",
              tableManager.debugString());

    EXPECT_THROW(tableManager.splitTablet(0, 0, 16, 8),
                 TableManager::NoSuchTablet);

    EXPECT_THROW(tableManager.splitTablet(0, 0, 0, ~0ul / 2),
                 TableManager::BadSplit);

    EXPECT_THROW(tableManager.splitTablet(1, 0, ~0ul, ~0ul / 2),
                 TableManager::NoSuchTablet);
}

}  // namespace RAMCloud