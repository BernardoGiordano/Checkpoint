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

#ifndef THREAD_HPP
#define THREAD_HPP

#include "alignsort_tuple.hpp"
#include <3ds/types.h>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace Threads {
    inline constexpr int MAX_THREADS = 32;

    inline constexpr size_t DEFAULT_STACK = 0x4000;
    inline constexpr size_t WORKER_STACK  = 0x8000;

    bool init(u8 minWorkers, u8 maxWorkers);

    inline bool init(u8 workers)
    {
        return init(workers, workers);
    }

    // stackSize will be ignored on systems that don't provide explicit setting of it. KEEP THIS IN
    // MIND IF YOU ARE PORTING
    bool create(void (*entrypoint)(void*), void* arg = nullptr, std::optional<size_t> stackSize = std::nullopt);
    // Executes task on a worker thread with stack size of 0x8000 (if settable).
    void executeTask(void (*task)(void*), void* arg);

    namespace internal {
        template <typename EPFunc, typename... Args>
        std::pair<void (*)(void*), void*> getFuncAndArg(EPFunc&& entrypoint, Args&&... args)
            requires std::invocable<std::remove_cvref_t<EPFunc>, std::remove_cvref_t<Args>...> &&
                     std::constructible_from<std::remove_cvref_t<EPFunc>, decltype(entrypoint)> &&
                     (std::constructible_from<std::remove_cvref_t<Args>, decltype(args)> && ...)
        {
            using tuple_type = alignsort_tuple<std::remove_cvref_t<EPFunc>, std::remove_cvref_t<Args>...>;
            return {std::invoke(
                        []<std::size_t... Indices>(std::index_sequence<Indices...>) {
                            return +[](void* argsRaw) {
                                std::unique_ptr<tuple_type> args = std::unique_ptr<tuple_type>(reinterpret_cast<tuple_type*>(argsRaw));

                                std::invoke(std::get<Indices>(*args)...);
                            };
                        },
                        std::index_sequence_for<EPFunc, Args...>{}),
                static_cast<void*>(new tuple_type(std::forward<decltype(entrypoint)>(entrypoint), std::forward<decltype(args)>(args)...))};
        }

        // Convert any function refs to function pointers
        template <typename EPFunc, typename... Args>
        std::pair<void (*)(void*), void*> getFuncAndArg(EPFunc&& entrypoint, Args&&... args)
            requires std::is_function_v<std::remove_cvref_t<EPFunc>> && std::is_reference_v<EPFunc>
        {
            return getFuncAndArg(std::addressof(entrypoint), std::forward<decltype(args)>(args)...);
        }

        // Optimization for pointer-to-member-func with object arg or pointer to
        // function with single pointer arg
        template <typename EPFunc, typename Arg>
        std::pair<void (*)(void*), void*> getFuncAndArg(EPFunc&& entrypoint, Arg&& a)
            requires std::invocable<EPFunc, Arg> &&
                     (std::is_member_function_pointer_v<std::remove_cvref_t<EPFunc>> ||
                         std::is_function_v<std::remove_pointer_t<std::remove_cvref_t<EPFunc>>>) &&
                     (sizeof(std::remove_cvref_t<EPFunc>) == sizeof(void (*)(void*))) && std::is_pointer_v<std::remove_cvref_t<Arg>>
        {
            return {reinterpret_cast<void (*)(void*)>(entrypoint), reinterpret_cast<void*>(a)};
        }

        // Optimization for zero-arg pointer to func (or empty capture lambda)
        template <typename EPFunc>
        std::pair<void (*)(void*), void*> getFuncAndArg(EPFunc&& entrypoint)
            requires std::invocable<EPFunc> && std::is_convertible_v<EPFunc, std::invoke_result_t<EPFunc> (*)()> &&
                     (sizeof(std::invoke_result_t<EPFunc> (*)()) == sizeof(void*))
        {
            return {+[](void* a) { std::invoke(reinterpret_cast<std::invoke_result_t<EPFunc> (*)()>(a)); },
                reinterpret_cast<void*>(static_cast<std::invoke_result_t<EPFunc> (*)()>(entrypoint))};
        }

        // Optimization for non-empty captures lambda (or other stateful functor)
        template <typename EPFunc>
        std::pair<void (*)(void*), void*> getFuncAndArg(EPFunc&& entrypoint)
            requires std::invocable<EPFunc> &&
                     (!std::is_convertible_v<EPFunc, std::invoke_result_t<EPFunc> (*)()>) && std::copy_constructible<std::remove_cvref_t<EPFunc>>
        {
            using EPFuncSelf = std::remove_cvref_t<EPFunc>;
            return getFuncAndArg(
                +[](EPFuncSelf* func) {
                    std::unique_ptr<EPFuncSelf> delOnScopeExit(func);
                    std::invoke(*delOnScopeExit);
                },
                new EPFuncSelf(std::forward<decltype(entrypoint)>(entrypoint)));
        }

        template <typename MF>
            requires std::is_member_pointer_v<MF>
        struct member_pointer_class {
        private:
            template <typename C, typename T>
            static C declclass(T C::* m);

        public:
            using type = decltype(declclass(std::declval<MF>()));
        };

        template <typename MF>
        using member_pointer_class_t = typename member_pointer_class<MF>::type;
    } // namespace internal

    template <typename EPFunc, typename... Args>
    bool create(std::optional<size_t> stackSize, EPFunc&& entrypoint, Args&&... args)
        requires requires { internal::getFuncAndArg(std::forward<decltype(entrypoint)>(entrypoint), std::forward<decltype(args)>(args)...); }
    {
        auto func = internal::getFuncAndArg(std::forward<decltype(entrypoint)>(entrypoint), std::forward<decltype(args)>(args)...);
        return create(func.first, func.second, stackSize);
    }

    template <typename EPFunc, typename... Args>
    bool create(EPFunc&& entrypoint, Args&&... args)
        requires requires { internal::getFuncAndArg(std::forward<decltype(entrypoint)>(entrypoint), std::forward<decltype(args)>(args)...); }
    {
        auto func = internal::getFuncAndArg(std::forward<decltype(entrypoint)>(entrypoint), std::forward<decltype(args)>(args)...);
        return create(func.first, func.second, std::nullopt);
    }

    template <typename EPFunc, typename... Args>
    void executeTask(EPFunc&& entrypoint, Args&&... args)
        requires requires { internal::getFuncAndArg(std::forward<decltype(entrypoint)>(entrypoint), std::forward<decltype(args)>(args)...); }
    {
        auto func = internal::getFuncAndArg(std::forward<decltype(entrypoint)>(entrypoint), std::forward<decltype(args)>(args)...);
        executeTask(func.first, func.second);
    }

    template <auto MP>
    bool create(std::optional<size_t> stackSize, internal::member_pointer_class_t<std::remove_cvref_t<decltype(MP)>>* cv)
        requires std::is_member_function_pointer_v<std::remove_cvref_t<decltype(MP)>>
    {
        return create(stackSize, +[](decltype(cv) cv) { return std::invoke(MP, cv); }, cv);
    }

    template <auto MP>
    bool create(internal::member_pointer_class_t<std::remove_cvref_t<decltype(MP)>>* cv)
        requires std::is_member_function_pointer_v<std::remove_cvref_t<decltype(MP)>>
    {
        return create<MP>(std::nullopt, cv);
    }

    template <auto MP>
    bool executeTask(internal::member_pointer_class_t<std::remove_cvref_t<decltype(MP)>>* cv)
        requires std::is_member_function_pointer_v<std::remove_cvref_t<decltype(MP)>>
    {
        return executeTask(+[](decltype(cv) cv) { return std::invoke(MP, cv); }, static_cast<void*>(cv));
    }

    void exit(void);
}

#endif
