#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "common/ceph_context.h"
#include <boost/asio/spawn.hpp>
#include "rgw/rgw_auth_keystone.h" 

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::DoAll;
using ::testing::SaveArg;

// Mock class for secret_cache
class MockSecretCache {
public:
    MOCK_METHOD(boost::optional<boost::tuple<rgw::keystone::TokenEnvelope, std::string>>, find, (const std::string&, optional_yield), (const));
    MOCK_METHOD(void, add, (const std::string&, const rgw::keystone::TokenEnvelope&, const std::string&), ());
};

// Mock class for EC2Engine to mock Keystone requests
class MockEC2Engine : public EC2Engine {
public:
    MOCK_METHOD((std::tuple<boost::optional<rgw::keystone::TokenEnvelope>, int>),
                get_from_keystone, 
                (const DoutPrefixProvider*, const std::string_view&, const std::string&, const std::string_view&, optional_yield), 
                (const override));

    MOCK_METHOD((std::tuple<boost::optional<std::string>, int>),
                get_secret_from_keystone,
                (const DoutPrefixProvider*, const std::string&, const std::string_view&, optional_yield),
                (const override));
};

TEST(EC2EngineTest, TestDuplicateAuthRequestsWaitForToken) {
    boost::asio::io_context io_context;
    boost::asio::spawn(io_context, [&](boost::asio::yield_context yield) {
        // Shared variables to simulate waiting
        boost::optional<boost::tuple<rgw::keystone::TokenEnvelope, std::string>> cache_result;
        std::mutex mtx;
        std::condition_variable cv;
        bool request_completed = false;

        // Create mock objects
        auto mock_secret_cache = std::make_shared<MockSecretCache>();
        auto mock_ec2_engine = std::make_shared<MockEC2Engine>();

        // Set up mock behavior for secret_cache.find()
        EXPECT_CALL(*mock_secret_cache, find(_, _))
            .Times(2)  // Expect two calls (one for each request)
            .WillOnce(Invoke([&](const std::string&, optional_yield) -> boost::optional<boost::tuple<rgw::keystone::TokenEnvelope, std::string>> {
                std::unique_lock<std::mutex> lock(mtx);
                cv.wait(lock, [&] { return request_completed; });  // Simulate waiting
                return cache_result;  // Return the token once it's available
            }))
            .WillOnce(Invoke([&](const std::string&, optional_yield) {
                return cache_result;  // Second call should return immediately
            }));

        // Mock Keystone call (should only happen once)
        EXPECT_CALL(*mock_ec2_engine, get_from_keystone(_, _, _, _, _))
            .Times(1)
            .WillOnce(Return(std::make_tuple(boost::optional<rgw::keystone::TokenEnvelope>(), 0)));

        // First coroutine (auth request)
        boost::asio::spawn(io_context, [&](boost::asio::yield_context y) {
            auto result = mock_ec2_engine->get_access_token(nullptr, "test_key", "test_string", "test_sig", {}, false, y);
            std::unique_lock<std::mutex> lock(mtx);
            cache_result = boost::make_tuple(rgw::keystone::TokenEnvelope(), "test_secret");
            request_completed = true;
            cv.notify_all();  // Release waiting coroutines
        });

        // Second coroutine (duplicate request)
        boost::asio::spawn(io_context, [&](boost::asio::yield_context y) {
            auto result = mock_ec2_engine->get_access_token(nullptr, "test_key", "test_string", "test_sig", {}, false, y);
            EXPECT_TRUE(result);  // Should receive the same token
        });

        io_context.run();
    });
}
