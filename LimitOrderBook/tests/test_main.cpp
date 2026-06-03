#include "matching_engine.hpp"
#include "order_book.hpp"
#include "types.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <optional>
#include <thread>

class OrderBookTest : public testing::Test {
protected:
  lob::OrderBook book;

  lob::Order makeOrder(lob::OrderId id, lob::Side side, lob::Price price,
                       lob::Quantity qty) {
    return lob::Order{price, 0, qty, id, side};
  }
};

class MatchingEngineTest : public testing::Test {
    protected:
    lob::MatchingEngine matchingEngine;
};

TEST_F(OrderBookTest, CancelNonExistentOrderReturnsFalse) { book.cancel(999); }

TEST_F(OrderBookTest, GetBestBidReturnCorrectBid) {
  auto order = makeOrder(1, lob::Side::Buy, 100000, 100);
  book.add(order);
  auto bestBid = book.getBestBid();
  EXPECT_EQ(bestBid, 100000);
}

TEST_F(OrderBookTest, CancelRestingBid) {
  auto order = makeOrder(1, lob::Side::Buy, 100000, 100);
  book.add(order);
  book.cancel(1);
  EXPECT_EQ(book.getBestBid(), std::nullopt);
}

TEST_F(OrderBookTest, FullMatch) {
  auto order1 = makeOrder(1, lob::Side::Buy, 100000, 100);
  auto order2 = makeOrder(2, lob::Side::Sell, 100000, 100);
  book.add(order1);
  book.add(order2);
  EXPECT_EQ(book.getBestAsk(), std::nullopt);
  EXPECT_EQ(book.getBestBid(), std::nullopt);
}

TEST_F(OrderBookTest, PartialMatch) {
  auto order1 = makeOrder(1, lob::Side::Buy, 100000, 100);
  book.add(order1);
  auto order2 = makeOrder(2, lob::Side::Sell, 100000, 60);
  book.add(order2);
  EXPECT_EQ(book.getBestAsk(), std::nullopt);
  EXPECT_EQ(book.getBestBid(), 100000);
}

TEST_F(OrderBookTest, MultiLevelSellSide) {
  auto order1 = makeOrder(1, lob::Side::Buy, 100000, 100);
  auto order2 = makeOrder(2, lob::Side::Buy, 50000, 60);
  book.add(order1);
  book.add(order2);
  auto order3 = makeOrder(3, lob::Side::Sell, 50000, 200);
  book.add(order3);
  EXPECT_EQ(book.getBestBid(), std::nullopt);
  EXPECT_EQ(book.getBestAsk(), 50000);
}

TEST_F(OrderBookTest, MultiLevelBuySide) {
  auto order1 = makeOrder(1, lob::Side::Sell, 100000, 100);
  auto order2 = makeOrder(2, lob::Side::Sell, 50000, 60);
  book.add(order1);
  book.add(order2);
  auto order3 = makeOrder(3, lob::Side::Buy, 50000, 200);
  book.add(order3);
  EXPECT_EQ(book.getBestBid(), 50000);
  EXPECT_EQ(book.getBestAsk(), 100000);
}

// Regression for the match() it++ bug: when an incoming order has leftover
// quantity AND the current price level still has resting orders, the matcher
// must keep consuming from that level instead of advancing past it.
TEST_F(OrderBookTest, CrossConsumesMultipleOrdersAtSameLevel) {
  book.add(makeOrder(1, lob::Side::Buy, 100, 50));
  book.add(makeOrder(2, lob::Side::Buy, 100, 50));
  book.add(makeOrder(3, lob::Side::Sell, 100, 100));
  EXPECT_EQ(book.getBestBid(), std::nullopt);
  EXPECT_EQ(book.getBestAsk(), std::nullopt);
}

// ── Multithreaded tests ───────────────────────────────────────────────────────

// Poll `pred` until it returns true or `timeout` elapses. Returns true on
// success. Lets tests succeed the instant the worker reaches the expected
// state, instead of relying on a fixed sleep that may be too short under load.
template <typename Pred>
static bool waitFor(Pred pred,
                    std::chrono::milliseconds timeout = std::chrono::seconds(1))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::yield();
    }
    return false;
}

// RAII wrapper: run the engine on a background thread for the test's lifetime.
// Destructor stops + joins, so the worker is always cleaned up even on failure.
struct EngineRunner {
    explicit EngineRunner(lob::MatchingEngine& e) : engine(e) {
        worker = std::thread([&e]{ e.run(); });
    }
    ~EngineRunner() {
        engine.stop();
        if (worker.joinable()) worker.join();
    }
    lob::MatchingEngine& engine;
    std::thread          worker;
};

TEST_F(MatchingEngineTest, MatchTest) {
    EngineRunner runner(matchingEngine);

    // Sequence the submissions so each step waits for a *visible change* the
    // worker can only have produced by actually processing the previous order.
    // An "empty book" predicate alone would be vacuously true at startup —
    // both atomics are 0 before the worker has done anything.
    matchingEngine.submit(100000, 100, lob::Side::Buy);
    ASSERT_TRUE(waitFor([&]{
        return matchingEngine.getBestBid() == 100000;
    })) << "resting buy never reached the worker";

    matchingEngine.submit(100000, 100, lob::Side::Sell);
    EXPECT_TRUE(waitFor([&]{
        return !matchingEngine.getBestBid() && !matchingEngine.getBestAsk();
    })) << "cross-match never cleared the book";
}

// Exercises the command-queue cancel path introduced for item 7: a cancel
// issued from the producer thread must travel through the SPSC queue, be
// applied by the worker, and become visible via the atomic top-of-book.
TEST_F(MatchingEngineTest, CancelThroughCommandQueue) {
    EngineRunner runner(matchingEngine);

    // Ids are assigned sequentially from 0 by a fresh MatchingEngine, so the
    // first submitted order is id 0. Coupled to the id generator — revisit if
    // submit() ever returns the assigned id.
    constexpr lob::OrderId restingId = 0;

    matchingEngine.submit(100000, 100, lob::Side::Buy);

    ASSERT_TRUE(waitFor([&]{
        return matchingEngine.getBestBid() == 100000;
    })) << "resting bid was never published by the worker";

    matchingEngine.cancel(restingId);

    EXPECT_TRUE(waitFor([&]{
        return !matchingEngine.getBestBid();
    })) << "cancel was not applied via the command queue";
    EXPECT_EQ(matchingEngine.getBestAsk(), std::nullopt);
}
