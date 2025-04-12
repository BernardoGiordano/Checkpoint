/*
 *   This file is part of PKSM
 *   Copyright (C) 2016-2025 Bernardo Giordano, Admiral Fish, piepie62
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
 *       * Requiring preservation of specified reasonable legal notices or
 *         author attributions in that material or in the Appropriate Legal
 *         Notices displayed by works containing it.
 *       * Prohibiting misrepresentation of the origin of that material,
 *         or requiring that modified versions of such material be marked in
 *         reasonable ways as different from the original version.
 */

#ifndef ALIGNSORT_TUPLE_HPP
#define ALIGNSORT_TUPLE_HPP

#include <array>
#include <tuple>
#include <utility>

namespace internal {
    template <typename... Args>
    struct alignsort_tuple_calc {
    private:
        static constexpr std::array<std::size_t, sizeof...(Args)> alignments = {alignof(Args)...};

        template <std::strict_weak_order<std::size_t, std::size_t> Checker>
        static constexpr void stable_sort_on(std::array<std::size_t, sizeof...(Args)>& sortMe, const Checker& checkFunc)
        {
            for (std::size_t i = 1; i < sortMe.size(); i++) {
                for (std::size_t j = i; j > 0 && checkFunc(sortMe[j - 1], sortMe[j]); j--) {
                    std::swap(sortMe[j], sortMe[j - 1]);
                }
            }
        }

    public:
        static constexpr std::array<std::size_t, sizeof...(Args)> indices = [] {
            std::array<std::size_t, sizeof...(Args)> ret{};

            for (std::size_t i = 0; i < ret.size(); i++) {
                ret[i] = i;
            }

            // Sort by alignment
            stable_sort_on(ret, [](std::size_t a, std::size_t b) { return alignments[a] < alignments[b]; });

            return ret;
        }();

        static constexpr std::array<std::size_t, sizeof...(Args)> revIndices = [] {
            std::array<std::size_t, sizeof...(Args)> ret{};

            for (std::size_t i = 0; i < ret.size(); i++) {
                ret[indices[i]] = i;
            }

            return ret;
        }();

    private:
        template <std::size_t... ArrAccess>
        static constexpr auto get_tuple(std::index_sequence<ArrAccess...>)
            -> std::tuple<std::tuple_element_t<indices[ArrAccess], std::tuple<Args...>>...>;

    public:
        using type = decltype(get_tuple(std::make_index_sequence<indices.size()>{}));
    };

    template <typename... Ts>
    using alignsort_tuple_calc_t = typename alignsort_tuple_calc<Ts...>::type;
} // namespace internal

template <typename... Ts>
struct alignsort_tuple;

namespace std {
    template <std::size_t I, typename... Args>
    struct tuple_element<I, alignsort_tuple<Args...>> : tuple_element<I, tuple<Args...>> {};

    template <typename... Args>
    struct tuple_size<alignsort_tuple<Args...>> : tuple_size<tuple<Args...>> {};
} // namespace std

template <typename... Ts>
struct alignsort_tuple : public internal::alignsort_tuple_calc_t<Ts...> {
    template <std::size_t I, typename... Args>
    constexpr friend std::tuple_element_t<I, alignsort_tuple<Args...>>& std::get(alignsort_tuple<Args...>&);
    template <std::size_t I, typename... Args>
    constexpr friend std::tuple_element_t<I, alignsort_tuple<Args...>>&& std::get(alignsort_tuple<Args...>&&);
    template <std::size_t I, typename... Args>
    constexpr friend const std::tuple_element_t<I, alignsort_tuple<Args...>>& std::get(const alignsort_tuple<Args...>&);
    template <std::size_t I, typename... Args>
    constexpr friend const std::tuple_element_t<I, alignsort_tuple<Args...>>& std::get(const alignsort_tuple<Args...>&&);

private:
    using calc = internal::alignsort_tuple_calc<Ts...>;

    using parent                       = typename calc::type;
    static constexpr std::array revIdx = calc::revIndices;

    static consteval std::make_index_sequence<calc::indices.size()> idx() { return std::make_index_sequence<calc::indices.size()>{}; }

    template <typename... Args, std::size_t... ArrAccess>
    constexpr alignsort_tuple(std::tuple<Args...>&& argsAsTuple, std::index_sequence<ArrAccess...>)
        : calc::type(std::get<calc::indices[ArrAccess]>(std::forward<decltype(argsAsTuple)>(argsAsTuple))...)
    {
    }

    template <typename Alloc, typename... Args, std::size_t... ArrAccess>
    constexpr alignsort_tuple(const Alloc& a, std::tuple<Args...>&& argsAsTuple, std::index_sequence<ArrAccess...>)
        : calc::type(std::allocator_arg_t{}, a, std::get<calc::indices[ArrAccess]>(std::forward<decltype(argsAsTuple)>(argsAsTuple))...)
    {
    }

    // Turn std::tuple<>reftype into std::tuple<reftype>

    template <typename... Args>
    static constexpr auto create_ref_tuple(std::tuple<Args...>& t) noexcept
    {
        return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) { return std::tuple<Args&...>(std::get<Indices>(t)...); }(
                   std::index_sequence_for<Args...>{});
    }

    template <typename... Args>
    static constexpr auto create_ref_tuple(const std::tuple<Args...>& t) noexcept
    {
        return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) { return std::tuple<const Args&...>(std::get<Indices>(t)...); }(
                   std::index_sequence_for<Args...>{});
    }

    template <typename... Args>
    static constexpr auto create_ref_tuple(std::tuple<Args...>&& t) noexcept
    {
        return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) { return std::tuple<Args&&...>(std::get<Indices>(t)...); }(
                   std::index_sequence_for<Args...>{});
    }

    template <typename... Args>
    static constexpr auto create_ref_tuple(const std::tuple<Args...>&& t) noexcept
    {
        return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) { return std::tuple<const Args&&...>(std::get<Indices>(t)...); }(
                   std::index_sequence_for<Args...>{});
    }

public:
    template <typename... Args>
    constexpr alignsort_tuple(Args&&... args) : alignsort_tuple(std::tuple<decltype(args)...>(std::forward<decltype(args)>(args)...), idx())
    {
    }

    constexpr alignsort_tuple() = default;

    constexpr alignsort_tuple(const alignsort_tuple&)            = default;
    constexpr alignsort_tuple(alignsort_tuple&&)                 = default;
    constexpr alignsort_tuple& operator=(const alignsort_tuple&) = default;
    constexpr alignsort_tuple& operator=(alignsort_tuple&&)      = default;

    using parent::swap;
    using parent::operator=;

    constexpr void swap(alignsort_tuple& other) noexcept(noexcept(parent::swap(other))) { parent::swap(other); }

    // Conversion between alignsort_tuples of different arg order

    template <typename... Args>
        requires std::is_same_v<internal::alignsort_tuple_calc_t<Args...>, typename calc::type>
    constexpr alignsort_tuple(alignsort_tuple<Args...>& other) : calc::type(static_cast<typename calc::type&>(other))
    {
    }

    template <typename... Args>
        requires std::is_same_v<internal::alignsort_tuple_calc_t<Args...>, typename calc::type>
    constexpr alignsort_tuple(const alignsort_tuple<Args...>& other) : calc::type(static_cast<const typename calc::type&>(other))
    {
    }

    template <typename... Args>
        requires std::is_same_v<internal::alignsort_tuple_calc_t<Args...>, typename calc::type>
    constexpr alignsort_tuple(alignsort_tuple<Args...>&& other) : calc::type(static_cast<typename calc::type&&>(other))
    {
    }

    template <typename... Args>
        requires std::is_same_v<internal::alignsort_tuple_calc_t<Args...>, typename calc::type>
    constexpr alignsort_tuple(const alignsort_tuple<Args...>&& other) : calc::type(static_cast<const typename calc::type&&>(other))
    {
    }

    // Conversion of std::tuple

    template <typename... Args>
    constexpr alignsort_tuple(std::tuple<Args...>& other) : alignsort_tuple(create_ref_tuple(other), idx())
    {
    }

    template <typename... Args>
    constexpr alignsort_tuple(const std::tuple<Args...>& other) : alignsort_tuple(create_ref_tuple(other), idx())
    {
    }

    template <typename... Args>
    constexpr alignsort_tuple(std::tuple<Args...>&& other) : alignsort_tuple(create_ref_tuple(other), idx())
    {
    }

    template <typename... Args>
    constexpr alignsort_tuple(const std::tuple<Args...>&& other) : alignsort_tuple(create_ref_tuple(other), idx())
    {
    }

    // Conversion of std::pair

    template <typename Arg0, typename Arg1>
    constexpr alignsort_tuple(std::pair<Arg0, Arg1>& p) : alignsort_tuple(std::tuple<Arg0&, Arg1&>(std::get<0>(p), std::get<1>(p)), idx())
    {
    }

    template <typename Arg0, typename Arg1>
    alignsort_tuple(const std::pair<Arg0, Arg1>& p) : alignsort_tuple(std::tuple<const Arg0&, const Arg1&>(std::get<0>(p), std::get<1>(p)), idx())
    {
    }

    template <typename Arg0, typename Arg1>
    alignsort_tuple(std::pair<Arg0, Arg1>&& p)
        : alignsort_tuple(std::tuple<Arg0&&, Arg1&&>(std::forward<Arg0&&>(std::get<0>(p)), std::forward<Arg1&&>(std::get<1>(p))), idx())
    {
    }

    template <typename Arg0, typename Arg1>
    constexpr alignsort_tuple(const std::pair<Arg0, Arg1>&& p)
        : alignsort_tuple(std::tuple<const Arg0&, const Arg1&>(std::get<0>(p), std::get<1>(p)), idx())
    {
    }

    // Allocator versions of above

    // Empty constructor (allocator)
    template <typename Alloc>
    constexpr alignsort_tuple(std::allocator_arg_t, const Alloc& a) : calc::type(std::allocator_arg_t{}, a)
    {
    }

    // Main constructor (allocator)
    template <typename Alloc, typename... Args>
    constexpr alignsort_tuple(std::allocator_arg_t, const Alloc& a, Args&&... args)
        : alignsort_tuple(a, std::tuple<decltype(args)...>(std::forward<decltype(args)>(args)...), idx())
    {
    }

    // Conversion from other alignsort_tuples of different arg order (allocator)

    template <typename Alloc, typename... Args>
        requires std::is_same_v<internal::alignsort_tuple_calc_t<Args...>, typename calc::type>
    constexpr alignsort_tuple(std::allocator_arg_t, const Alloc& a, alignsort_tuple<Args...>& other)
        : calc::type(std::allocator_arg_t{}, a, static_cast<const typename calc::type&>(other))
    {
    }

    template <typename Alloc, typename... Args>
        requires std::is_same_v<internal::alignsort_tuple_calc_t<Args...>, typename calc::type>
    constexpr alignsort_tuple(std::allocator_arg_t, const Alloc& a, const alignsort_tuple<Args...>& other)
        : calc::type(std::allocator_arg_t{}, a, static_cast<const typename calc::type&>(other))
    {
    }

    template <typename Alloc, typename... Args>
        requires std::is_same_v<internal::alignsort_tuple_calc_t<Args...>, typename calc::type>
    constexpr alignsort_tuple(std::allocator_arg_t, const Alloc& a, alignsort_tuple<Args...>&& other)
        : calc::type(std::allocator_arg_t{}, a, static_cast<typename calc::type&&>(other))
    {
    }

    template <typename Alloc, typename... Args>
        requires std::is_same_v<internal::alignsort_tuple_calc_t<Args...>, typename calc::type>
    constexpr alignsort_tuple(std::allocator_arg_t, const Alloc& a, const alignsort_tuple<Args...>&& other)
        : calc::type(std::allocator_arg_t{}, a, static_cast<const typename calc::type&&>(other))
    {
    }

    // Conversion from std::tuple (allocator)
    template <typename Alloc, typename... Args>
    constexpr alignsort_tuple(std::allocator_arg_t, const Alloc& a, std::tuple<Args...>& other) : alignsort_tuple(a, create_ref_tuple(other), idx())
    {
    }

    template <typename Alloc, typename... Args>
    constexpr alignsort_tuple(std::allocator_arg_t, const Alloc& a, const std::tuple<Args...>& other)
        : alignsort_tuple(a, create_ref_tuple(other), idx())
    {
    }

    template <typename Alloc, typename... Args>
    constexpr alignsort_tuple(std::allocator_arg_t, const Alloc& a, std::tuple<Args...>&& other) : alignsort_tuple(a, create_ref_tuple(other), idx())
    {
    }

    template <typename Alloc, typename... Args>
    constexpr alignsort_tuple(std::allocator_arg_t, const Alloc& a, const std::tuple<Args...>&& other)
        : alignsort_tuple(a, create_ref_tuple(other), idx())
    {
    }

    // Conversion from std::pair (allocator)

    template <typename Alloc, typename Arg0, typename Arg1>
    constexpr alignsort_tuple(std::allocator_arg_t, const Alloc& a, std::pair<Arg0, Arg1>& p)
        : alignsort_tuple(a, std::tuple<Arg0&, Arg1&>(std::get<0>(p), std::get<1>(p)), idx())
    {
    }

    template <typename Alloc, typename Arg0, typename Arg1>
    constexpr alignsort_tuple(std::allocator_arg_t, const Alloc& a, const std::pair<Arg0, Arg1>& p)
        : alignsort_tuple(a, std::tuple<const Arg0&, const Arg1&>(std::get<0>(p), std::get<1>(p)), idx())
    {
    }

    template <typename Alloc, typename Arg0, typename Arg1>
    constexpr alignsort_tuple(std::allocator_arg_t, const Alloc& a, std::pair<Arg0, Arg1>&& p)
        : alignsort_tuple(a, std::tuple<Arg0&&, Arg1&&>(std::get<0>(p), std::get<1>(p)), idx())
    {
    }

    template <typename Alloc, typename Arg0, typename Arg1>
    constexpr alignsort_tuple(std::allocator_arg_t, const Alloc& a, const std::pair<Arg0, Arg1>&& p)
        : alignsort_tuple(a, std::tuple<const Arg0&, const Arg1&>(std::get<0>(p), std::get<1>(p)), idx())
    {
    }

    // Copy/move constructors (allocator)

    template <typename Alloc>
    constexpr alignsort_tuple(std::allocator_arg_t, const Alloc& a, alignsort_tuple& other)
        : calc::type(std::allocator_arg_t{}, a, static_cast<calc::type&>(other))
    {
    }

    template <typename Alloc>
    constexpr alignsort_tuple(std::allocator_arg_t, const Alloc& a, const alignsort_tuple& other)
        : calc::type(std::allocator_arg_t{}, a, static_cast<const calc::type&>(other))
    {
    }

    template <typename Alloc>
    constexpr alignsort_tuple(std::allocator_arg_t, const Alloc& a, alignsort_tuple&& other)
        : calc::type(std::allocator_arg_t{}, a, static_cast<calc::type&&>(other))
    {
    }

    template <typename Alloc>
    constexpr alignsort_tuple(std::allocator_arg_t, const Alloc& a, const alignsort_tuple&& other)
        : calc::type(std::allocator_arg_t{}, a, static_cast<const calc::type&&>(other))
    {
    }
};

namespace std {
    template <std::size_t I, typename... Args>
    constexpr tuple_element_t<I, alignsort_tuple<Args...>>& get(alignsort_tuple<Args...>& self)
    {
        using AT = alignsort_tuple<Args...>;
        return std::get<AT::revIdx[I]>(static_cast<typename AT::parent&>(self));
    }

    template <std::size_t I, typename... Args>
    constexpr tuple_element_t<I, alignsort_tuple<Args...>>&& get(alignsort_tuple<Args...>&& self)
    {
        using AT = alignsort_tuple<Args...>;
        return std::get<AT::revIdx[I]>(static_cast<typename AT::parent&&>(self));
    }

    template <std::size_t I, typename... Args>
    constexpr const tuple_element_t<I, alignsort_tuple<Args...>>& get(const alignsort_tuple<Args...>& self)
    {
        using AT = alignsort_tuple<Args...>;
        return std::get<AT::revIdx[I]>(static_cast<const typename AT::parent&>(self));
    }

    template <std::size_t I, typename... Args>
    constexpr const tuple_element_t<I, alignsort_tuple<Args...>>&& get(const alignsort_tuple<Args...>&& self)
    {
        using AT = alignsort_tuple<Args...>;
        return std::get<AT::revIdx[I]>(static_cast<const typename AT::parent&&>(self));
    }
} // namespace std

template <typename... Args>
alignsort_tuple(Args...) -> alignsort_tuple<Args...>;

template <typename... Args>
alignsort_tuple(alignsort_tuple<Args...>) -> alignsort_tuple<Args...>;

template <typename T1, typename T2>
alignsort_tuple(std::pair<T1, T2>) -> alignsort_tuple<T1, T2>;

template <typename... Args>
alignsort_tuple(std::tuple<Args...>) -> alignsort_tuple<Args...>;

template <typename Alloc, typename... Args>
alignsort_tuple(std::allocator_arg_t, Alloc, Args...) -> alignsort_tuple<Args...>;

template <typename Alloc, typename T1, typename T2>
alignsort_tuple(std::allocator_arg_t, Alloc, std::pair<T1, T2>) -> alignsort_tuple<T1, T2>;

template <typename Alloc, typename... Types>
alignsort_tuple(std::allocator_arg_t, Alloc, std::tuple<Types...>) -> alignsort_tuple<Types...>;

template <typename Alloc, typename... Args>
alignsort_tuple(std::allocator_arg_t, Alloc, alignsort_tuple<Args...>) -> alignsort_tuple<Args...>;

#endif
