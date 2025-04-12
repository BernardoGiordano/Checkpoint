/*
 *   This file is part of PKSM-Core
 *   Copyright (C) 2016-2022 Bernardo Giordano, Admiral Fish, piepie62
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

#ifndef SMALLVECTOR_HPP
#define SMALLVECTOR_HPP

#include <algorithm>
#include <compare>
#include <cstdint>
#include <iterator>
#include <memory>
#include <span>
#include <type_traits>

template <typename T, std::size_t Size>
    requires std::same_as<T, std::remove_cvref_t<T>> && (Size > 0) && std::is_destructible_v<T>
class SmallVector {
private:
    template <typename U, std::size_t OSize>
        requires std::same_as<U, std::remove_cvref_t<U>> && (OSize > 0) && std::is_destructible_v<U>
    friend class SmallVector;

    // Once again, I really wish anonymous structures were a thing
    // This allows the data array to have its lifetime start without its elements' lifetime starting
    union SVData {
        struct SubData {
            T data[Size];
            std::size_t populated;
        } data;

        constexpr SVData() { data.populated = 0; }

        constexpr ~SVData() {}
    } alldata;

    constexpr std::size_t& populated() { return alldata.data.populated; }

    constexpr std::size_t populated() const { return alldata.data.populated; }

    static constexpr bool nothrow_movable =
        std::is_nothrow_move_constructible_v<T> && (!std::is_move_assignable_v<T> || std::is_nothrow_move_assignable_v<T>);

    static constexpr bool nothrow_copyable =
        std::is_nothrow_copy_constructible_v<T> && (!std::is_copy_assignable_v<T> || std::is_nothrow_copy_assignable_v<T>);

    static constexpr void nothrow_migrate(T* dest, T&& src) noexcept(nothrow_movable)
    {
        if constexpr (std::is_nothrow_move_assignable_v<T> || (std::is_move_assignable_v<T> && !std::is_nothrow_move_constructible_v<T>)) {
            *dest = std::forward<T&&>(src);
        }
        else {
            std::destroy_at(dest);
            std::construct_at(dest, std::forward<T&&>(src));
        }
    }

    static constexpr void nothrow_migrate(T* dest, const T& src) noexcept(nothrow_copyable)
    {
        if constexpr (std::is_nothrow_copy_assignable_v<T> || (std::is_copy_assignable_v<T> && !std::is_nothrow_copy_constructible_v<T>)) {
            *dest = std::forward<const T&>(src);
        }
        else {
            std::destroy_at(dest);
            std::construct_at(dest, std::forward<const T&>(src));
        }
    }

public:
    using value_type             = T;
    using size_type              = std::size_t;
    using difference_type        = std::ptrdiff_t;
    using reference              = value_type&;
    using const_reference        = const value_type&;
    using pointer                = value_type*;
    using const_pointer          = const value_type*;
    using iterator               = T*;
    using const_iterator         = const T*;
    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    constexpr SmallVector() = default;

    constexpr ~SmallVector() noexcept(std::is_nothrow_destructible_v<T>) { std::destroy_n(data(), size()); }

    template <std::size_t OSize = Size>
        requires(OSize <= Size) && std::is_copy_constructible_v<T>
    constexpr SmallVector(const SmallVector<T, OSize>& o) noexcept(noexcept(emplace_back(o[0])))
    {
        for (std::size_t i = 0; i < o.size(); i++) {
            emplace_back(o[i]);
        }
    }

    template <std::size_t OSize = Size>
        requires(OSize <= Size) && std::is_move_constructible_v<T>
    constexpr SmallVector(SmallVector<T, OSize>&& o) noexcept(noexcept(emplace_back(std::move(o[0]))) && std::is_nothrow_destructible_v<T>)
    {
        for (std::size_t i = 0; i < o.size(); i++) {
            emplace_back(std::move(o[i]));
        }
        std::destroy_n(o.data(), o.size());
        o.populated() = 0;
    }

    template <std::size_t OSize = Size>
        requires(OSize <= Size) && std::is_copy_constructible_v<T>
    constexpr SmallVector& operator=(const SmallVector<T, OSize>& o) noexcept(
        noexcept(nothrow_migrate(std::addressof(data()[0]), o[0])) && std::is_nothrow_destructible_v<T>)
    {
        std::size_t minsize     = std::min(size(), o.size());
        std::size_t targetsize  = o.size();
        std::size_t currentsize = size();

        for (std::size_t i = 0; i < minsize; i++) {
            nothrow_migrate(std::addressof(data()[i]), o[i]);
        }
        for (std::size_t i = minsize; i < targetsize; i++) {
            std::construct_at(std::addressof(data()[i]), o[i]);
        }
        // not destroy_n to avoid overflow
        for (std::size_t i = targetsize; i < currentsize; i++) {
            std::destroy_at(std::addressof(data()[i]));
        }

        populated() = o.populated();

        return *this;
    }

    template <std::size_t OSize = Size>
        requires(OSize <= Size) && std::is_move_constructible_v<T>
    constexpr SmallVector& operator=(SmallVector<T, OSize>&& o) noexcept(
        noexcept(nothrow_migrate(std::addressof(data()[0]), std::move(o[0]))) && std::is_nothrow_destructible_v<T>)
    {
        std::size_t minsize     = std::min(size(), o.size());
        std::size_t targetsize  = o.size();
        std::size_t currentsize = size();

        for (std::size_t i = 0; i < minsize; i++) {
            nothrow_migrate(std::addressof(data()[i]), std::move(o[i]));
            std::destroy_at(std::addressof(o[i]));
        }
        for (std::size_t i = minsize; i < targetsize; i++) {
            std::construct_at(std::addressof(data()[i]), std::move(o[i]));
            std::destroy_at(std::addressof(o[i]));
        }
        // not destroy_n to avoid overflow
        for (std::size_t i = targetsize; i < currentsize; i++) {
            std::destroy_at(std::addressof(data()[i]));
        }

        populated()   = o.populated();
        o.populated() = 0;

        return *this;
    }

    template <typename... Args>
        requires(std::convertible_to<Args, T> && ...) && (sizeof...(Args) <= Size)
    constexpr SmallVector(Args&&... args) noexcept((noexcept(emplace_back(std::forward<decltype(args)>(args))) && ...))
    {
        (emplace_back(std::forward<decltype(args)>(args)) && ...);
    }

    template <std::size_t SpanSize, typename Contained>
        requires std::convertible_to<Contained, T> && (SpanSize <= Size) && (SpanSize != std::dynamic_extent)
    constexpr explicit(!std::is_same_v<std::remove_cvref_t<Contained>, T>)
        SmallVector(const std::span<Contained, SpanSize>& in) noexcept(noexcept(emplace_back(in[0])))
    {
        for (std::size_t i = 0; i < in.size(); i++) {
            emplace_back(in[i]);
        }
    }

    // Returns true if the value fit, false if it didn't
    constexpr bool push_back(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::is_copy_constructible_v<T>
    {
        if (size() < capacity()) {
            std::construct_at(std::addressof(data()[populated()++]), std::forward<const T&>(value));
            return true;
        }

        return false;
    }

    constexpr bool push_back(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires std::is_move_constructible_v<T>
    {
        if (size() < capacity()) {
            std::construct_at(std::addressof(data()[populated()++]), std::forward<T&&>(value));
            return true;
        }

        return false;
    }

    template <typename... Args>
    constexpr bool emplace_back(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, decltype(args)...>)
        requires std::is_constructible_v<T, decltype(args)...>
    {
        if (size() < capacity()) {
            std::construct_at(std::addressof(data()[populated()++]), std::forward<decltype(args)>(args)...);
            return true;
        }

        return false;
    }

    constexpr void pop_back() noexcept(std::is_nothrow_destructible_v<T>)
    {
        if (!empty()) {
            std::destroy_at(std::addressof(data()[--populated()]));
        }
    }

    constexpr void clear() noexcept(std::is_nothrow_destructible_v<T>)
    {
        std::destroy_n(begin(), size());
        populated() = 0;
    }

    constexpr iterator erase(const_iterator pos) noexcept(
        noexcept(nothrow_migrate(std::addressof(data()[0]), std::move(data()[0]))) && std::is_nothrow_destructible_v<T>)
    {
        iterator movedest  = begin() + (pos - begin());
        iterator movestart = movedest + 1;
        iterator moveend   = end();

        while (movestart != moveend) {
            nothrow_migrate(std::addressof(*(movedest++)), std::move(*(movestart++)));
        }

        std::destroy_at(std::addressof(data()[--populated()]));

        return begin() + (pos - begin());
    }

    constexpr iterator erase(const_iterator first, const_iterator last) noexcept(
        noexcept(nothrow_migrate(std::addressof(data()[0]), std::move(data()[0]))) && std::is_nothrow_destructible_v<T>)
    {
        if (last <= first) {
            return begin() + (last - begin());
        }
        iterator movedest  = begin() + (first - begin());
        iterator movestart = begin() + (last - begin());
        iterator moveend   = end();

        while (movestart != moveend) {
            nothrow_migrate(std::addressof(*(movedest++)), std::move(*(movestart++)));
        }

        std::destroy_n(std::addressof(data()[populated() - (last - first)]), last - first);

        populated() -= (last - first);

        return begin() + (last - first);
    }

    constexpr bool insert(const_iterator pos, const T& data) noexcept(
        noexcept(nothrow_migrate(std::addressof(data()[0]), std::move(data()[0]))) && noexcept(nothrow_migrate(std::addressof(data()[0]), data()[0])))
        requires std::is_copy_constructible_v<T>
    {
        if (pos == end()) {
            return push_back(std::forward<const T&>(data));
        }
        if (size() < capacity()) {
            for (auto i = end() - 1; i >= pos; i--) {
                // Special case to increase size
                if (i + 1 == end()) {
                    emplace_back(std::move(*(end() - 1)));
                }
                else {
                    nothrow_migrate(std::addressof(*(i + 1)), std::move(*i));
                }
            }

            nothrow_migrate(begin() + (pos - begin()), data);

            return true;
        }

        return false;
    }

    constexpr bool insert(const_iterator pos, T&& data) noexcept(noexcept(nothrow_migrate(std::addressof(data()[0]), std::move(data()[0]))))
        requires std::is_move_constructible_v<T>
    {
        if (pos == end()) {
            return push_back(std::forward<T&&>(data));
        }
        if (size() < capacity()) {
            for (auto i = end() - 1; i >= pos; i--) {
                // Special case to increase size
                if (i + 1 == end()) {
                    emplace_back(std::move(*i));
                }
                else {
                    nothrow_migrate(std::addressof(*(i + 1)), std::move(*i));
                }
            }

            nothrow_migrate(begin() + (pos - begin()), std::forward<T&&>(data));

            return true;
        }

        return false;
    }

    template <typename... Args>
    constexpr bool emplace(const_iterator pos, Args&&... args) noexcept(
        noexcept(nothrow_migrate(std::addressof(data()[0]), std::move(data()[0]))) && std::is_nothrow_constructible_v<T, decltype(args)...>)
        requires std::is_constructible_v<T, decltype(args)...>
    {
        if (pos == end()) {
            return emplace_back(std::forward<decltype(args)>(args)...);
        }
        else if (size() < capacity()) {
            for (auto i = end() - 1; i >= pos; i--) {
                // Special case to increase size
                if (i + 1 == end()) {
                    emplace_back(std::move(*i));
                }
                else {
                    nothrow_migrate(std::addressof(*(i + 1)), std::move(*i));
                }
            }

            T data{std::forward<decltype(args)>(args)...};

            nothrow_migrate(begin() + (pos - begin()), std::move(data));

            return true;
        }

        return false;
    }

    constexpr T* data() noexcept { return alldata.data.data; }

    constexpr const T* data() const noexcept { return alldata.data.data; }

    constexpr const_reference operator[](std::size_t i) const noexcept { return data()[i]; }

    constexpr reference operator[](std::size_t i) noexcept { return data()[i]; }

    constexpr reference front() noexcept { return *begin(); }

    constexpr const_reference front() const noexcept { return *begin(); }

    constexpr reference back() noexcept { return *rbegin(); }

    constexpr const_reference back() const noexcept { return *rbegin(); }

    constexpr size_type size() const noexcept { return populated(); }

    constexpr size_type capacity() const noexcept { return Size; }

    constexpr size_type max_size() const noexcept { return Size; }

    constexpr bool empty() const noexcept { return size() == 0; }

    constexpr iterator begin() noexcept { return iterator{data()}; }

    constexpr iterator end() noexcept { return iterator{data()} + size(); }

    constexpr reverse_iterator rbegin() noexcept { return std::make_reverse_iterator(end()); }

    constexpr reverse_iterator rend() noexcept { return std::make_reverse_iterator(begin()); }

    constexpr const_iterator begin() const noexcept { return const_iterator{data()}; }

    constexpr const_iterator end() const noexcept { return const_iterator{data()} + size(); }

    constexpr const_reverse_iterator rbegin() const noexcept { return std::make_reverse_iterator(end()); }

    constexpr const_reverse_iterator rend() const noexcept { return std::make_reverse_iterator(begin()); }

    constexpr const_iterator cbegin() const noexcept { return const_iterator{data()}; }

    constexpr const_iterator cend() const noexcept { return const_iterator{data()} + size(); }

    constexpr const_reverse_iterator crbegin() const noexcept { return std::make_reverse_iterator(cend()); }

    constexpr const_reverse_iterator crend() const noexcept { return std::make_reverse_iterator(cbegin()); }
};

template <typename... Args>
SmallVector(Args...) -> SmallVector<std::common_type_t<Args...>, sizeof...(Args)>;
template <typename T, std::size_t Size>
SmallVector(std::span<T, Size>) -> SmallVector<T, Size>;

#endif
