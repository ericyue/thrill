/*******************************************************************************
 * tests/data/iterator_test.cpp
 *
 * Part of Project c7a.
 *
 *
 * This file has no license. Only Chuck Norris can compile it.
 ******************************************************************************/

#include <c7a/data/iterator.hpp>

#include <vector>
#include <string>

#include "gtest/gtest.h"

using namespace c7a::data;
using namespace c7a::net;

struct TestIterator : public::testing::Test {
    TestIterator() :
        threeStrings({ "foo", "bar", "blub" }),
        oneString({ "." }),
        emptyBuffer(nullptr, 0),
        threeStringsBuffer(StringsToBuffer(threeStrings)),
        oneStringBuffer(StringsToBuffer(oneString)) { }

    BinaryBuffer StringsToBuffer(std::vector<std::string> strings) const {
        BinaryBufferBuilder builder;
        for (std::string s : strings) {
            builder.PutString(s);
        }
        auto result = BinaryBuffer(builder);
        builder.Detach();
        return result;
    }

    std::vector<std::string> threeStrings;
    std::vector<std::string> oneString;
    BinaryBuffer             emptyBuffer;
    BinaryBuffer             threeStringsBuffer;
    BinaryBuffer             oneStringBuffer;
    BufferChain              chain;
};

TEST_F(TestIterator, EmptyHasNotNext) {
    Iterator<std::string> it(chain);
    ASSERT_FALSE(it.HasNext());
}

TEST_F(TestIterator, EmptyIsNotClosed) {
    Iterator<std::string> it(chain);
    ASSERT_FALSE(it.IsClosed());
}

TEST_F(TestIterator, ClosedReturnsIsClosed) {
    chain.Append(oneStringBuffer);
    chain.Close();
    Iterator<std::string> it(chain);
    ASSERT_TRUE(it.IsClosed());
}

TEST_F(TestIterator, IterateOverStringsInSingleBuffer) {
    chain.Append(oneStringBuffer);
    chain.Append(threeStringsBuffer);
    Iterator<std::string> it(chain);
    ASSERT_EQ(".", it.Next());
    ASSERT_EQ("foo", it.Next());
}

TEST_F(TestIterator, IterateOverStringsInTwoBuffers) {
    chain.Append(threeStringsBuffer);
    Iterator<std::string> it(chain);
    ASSERT_EQ("foo", it.Next());
    ASSERT_EQ("bar", it.Next());
}

TEST_F(TestIterator, HasNextReturnsFalseAtTheEnd) {
    chain.Append(threeStringsBuffer);
    Iterator<std::string> it(chain);
    (void)it.Next();
    (void)it.Next();
    (void)it.Next();
    ASSERT_FALSE(it.HasNext());
}

TEST_F(TestIterator, IsClosedReturnsFalseAtTheEnd) {
    chain.Append(threeStringsBuffer);
    Iterator<std::string> it(chain);
    (void)it.Next();
    (void)it.Next();
    (void)it.Next();
    ASSERT_FALSE(it.IsClosed());
}

TEST_F(TestIterator, HasNextReturnsTrueInTheMiddle) {
    chain.Append(threeStringsBuffer);
    Iterator<std::string> it(chain);
    (void)it.Next();
    ASSERT_TRUE(it.HasNext());
}

TEST_F(TestIterator, HasNextReturnsTrueBetweenBuffers) {
    chain.Append(threeStringsBuffer);
    chain.Append(oneStringBuffer);
    Iterator<std::string> it(chain);
    (void)it.Next();
    (void)it.Next();
    (void)it.Next();
    ASSERT_TRUE(it.HasNext());
}
/******************************************************************************/