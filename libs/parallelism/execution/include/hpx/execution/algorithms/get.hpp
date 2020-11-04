//  Copyright (c) 2020 ETH Zurich
//
//  SPDX-License-Identifier: BSL-1.0
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <hpx/config.hpp>
#include <hpx/datastructures/optional.hpp>
#include <hpx/execution_base/operation_state.hpp>
#include <hpx/execution_base/sender.hpp>
#include <hpx/synchronization/condition_variable.hpp>
#include <hpx/synchronization/mutex.hpp>
#include <hpx/type_support/pack.hpp>

#include <boost/variant.hpp>

#include <exception>
#include <tuple>

namespace hpx { namespace execution { namespace experimental {
    template <typename... Variants>
    struct get_single_result
    {
        static_assert(sizeof...(Variants) == 0,
            "get expects the predecessor sender to have a single variant in "
            "sender_traits<>::value_types");
    };

    template <typename T>
    struct get_single_result<hpx::util::pack<hpx::util::pack<T>>>
    {
        using type = T;
    };

    namespace detail {
        template <typename T>
        struct get_receiver
        {
            struct state
            {
                hpx::lcos::local::condition_variable cv;
                hpx::lcos::local::mutex m;
                bool done = false;
                bool has_exception = false;
                std::exception_ptr ep;
                hpx::util::optional<T> result;
            };

            state& st;

            void signal_done() noexcept
            {
                std::unique_lock<hpx::lcos::local::mutex> l(st.m);
                st.done = true;
                st.cv.notify_one();
            }

            void set_error(std::exception_ptr ep) noexcept
            {
                st.ep = ep;
                st.has_exception = true;
                signal_done();
            }

            void set_done() noexcept
            {
                signal_done();
            };

            template <typename T_>
            void set_value(T_&& t) noexcept
            {
                st.result = std::forward<T_>(t);
                signal_done();
            }
        };
    }    // namespace detail

    template <typename S>
    auto get(S&& s)
    {
        using value_types =
            typename hpx::execution::experimental::traits::sender_traits<
                S>::template value_types<hpx::util::pack, hpx::util::pack>;
        using result_type = typename get_single_result<value_types>::type;
        using state_type = typename detail::get_receiver<result_type>::state;

        state_type st{};
        hpx::execution::experimental::start(
            hpx::execution::experimental::connect(
                std::forward<S>(s), detail::get_receiver<result_type>{st}));

        {
            std::unique_lock<hpx::lcos::local::mutex> l(st.m);
            if (!st.done)
            {
                st.cv.wait(l);
            }
        }

        if (st.has_exception)
        {
            std::rethrow_exception(st.ep);
        }
        else
        {
            return std::move(st.result.value());
        }
    }
}}}    // namespace hpx::execution::experimental
