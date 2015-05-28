/*******************************************************************************
 * tests/core/hash_table_bench.cpp
 *
 * Part of Project c7a.
 *
 *
 * This file has no license. Only Chuck Norris can compile it.
 ******************************************************************************/

#include <c7a/core/reduce_pre_table.hpp>
#include <c7a/core/reduce_pre_table_bench.hpp>
#include <tests/c7a_tests.hpp>
#include <c7a/api/context.hpp>

#include <functional>
#include <cstdio>

#include "gtest/gtest.h"

TEST(BenchTable, ActualTable1KKInts) {
    auto emit = [](int in) {
                    in = in;
                    //std::cout << in << std::endl;
                };

    auto key_ex = [](int in) {
                      return in;
                  };

    auto red_fn = [](int in1, int in2) {
                      return in1 + in2;
                  };

    c7a::core::ReducePreTableBench<decltype(key_ex), decltype(red_fn), decltype(emit)>
    table(1, key_ex, red_fn, { emit });

    for (int i = 0; i < 1000000; i++) {
        table.Insert(i * 17);
    }

    table.Flush();
}

TEST(BenchTable, ChausTable1KKInts) {
    auto emit = [](int in) {
                    in = in;
                    //std::cout << in << std::endl;
                };

    auto key_ex = [](int in) {
                      return in;
                  };

    auto red_fn = [](int in1, int in2) {
                      return in1 + in2;
                  };

    c7a::core::ReducePreTableBench<decltype(key_ex), decltype(red_fn), decltype(emit)>
    table(1, key_ex, red_fn, { emit });

    for (int i = 0; i < 1000000; i++) {
        table.Insert(i * 17);
    }

    table.Flush();
}

TEST(BenchTable, ActualTable10Workers) {
    auto emit = [](int in) {
                    in = in;
                    //std::cout << in << std::endl;
                };

    auto key_ex = [](int in) {
                      return in;
                  };

    auto red_fn = [](int in1, int in2) {
                      return in1 + in2;
                  };

    c7a::core::ReducePreTableBench<decltype(key_ex), decltype(red_fn), decltype(emit)>
    table(10, key_ex, red_fn, { emit });

    for (int i = 0; i < 1000000; i++) {
        table.Insert(i * 17);
    }

    table.Flush();
}

TEST(BenchTable, ChausTable10Workers) {
    auto emit = [](int in) {
                    in = in;
                    //std::cout << in << std::endl;
                };

    auto key_ex = [](int in) {
                      return in;
                  };

    auto red_fn = [](int in1, int in2) {
                      return in1 + in2;
                  };

    c7a::core::ReducePreTableBench<decltype(key_ex), decltype(red_fn), decltype(emit)>
    table(10, key_ex, red_fn, { emit });

    for (int i = 0; i < 1000000; i++) {
        table.Insert(i * 17);
    }

    table.Flush();
}

// TODO(ms): add one test with a for loop inserting 10000 items. -> trigger
// resize!

/******************************************************************************/