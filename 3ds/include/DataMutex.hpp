/*
 *   This file is part of PKSM
 *   Copyright (C) 2016-2022 FlagBrew
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

#ifndef DATAMUTEX_HPP
#define DATAMUTEX_HPP

#include <unistd.h>
#include <utility>

template <typename T>
class DataMutex {
    friend struct DataLock;

private:
    T rawData;
    _LOCK_T mutex;

public:
    class DataLock {
        friend class DataMutex;
        T& rawData;
        _LOCK_T& mutex;

    private:
        DataLock(T& t, _LOCK_T& m) noexcept : rawData(t), mutex(m) { __lock_acquire(mutex); }

    public:
        ~DataLock() noexcept { __lock_release(mutex); }

        DataLock(const DataLock&)            = delete;
        DataLock& operator=(const DataLock&) = delete;

        T* operator->() noexcept { return &rawData; }

        const T* operator->() const noexcept { return &rawData; }

        T& operator*() noexcept { return rawData; }

        const T& operator*() const noexcept { return rawData; }

        T& get() noexcept { return rawData; }

        const T& get() const noexcept { return rawData; }
    };

    template <typename... Args>
    DataMutex(Args&&... args) noexcept : rawData(std::forward<decltype(args)>(args)...), mutex()
    {
        __lock_init(mutex);
    }

    DataMutex(const DataMutex&)            = delete;
    DataMutex& operator=(const DataMutex&) = delete;

    ~DataMutex() noexcept { __lock_close(mutex); }

    DataLock lock() noexcept { return DataLock(rawData, mutex); }
};

template <typename T>
class RecursiveDataMutex {
    friend struct DataLock;

private:
    T rawData;
    _LOCK_RECURSIVE_T mutex;

public:
    class DataLock {
        friend class RecursiveDataMutex;
        T& rawData;
        _LOCK_RECURSIVE_T& mutex;

    private:
        DataLock(T& t, _LOCK_RECURSIVE_T& m) noexcept : rawData(t), mutex(m) { __lock_acquire_recursive(mutex); }

        DataLock(const DataLock&)            = delete;
        DataLock& operator=(const DataLock&) = delete;

        ~DataLock() noexcept { __lock_release_recursive(mutex); }

        T* operator->() noexcept { return &rawData; }

        const T* operator->() const noexcept { return &rawData; }

        T& operator*() noexcept { return rawData; }

        const T& operator*() const noexcept { return rawData; }

        T& get() noexcept { return rawData; }

        const T& get() const noexcept { return rawData; }
    };

    template <typename... Args>
    RecursiveDataMutex(Args&&... args) noexcept : rawData(std::forward<decltype(args)>(args)...), mutex()
    {
        __lock_init_recursive(mutex);
    }

    RecursiveDataMutex(const RecursiveDataMutex&)            = delete;
    RecursiveDataMutex& operator=(const RecursiveDataMutex&) = delete;

    ~RecursiveDataMutex() noexcept { __lock_close_recursive(mutex); }

    DataLock lock() noexcept { return DataLock(rawData, mutex); }
};

#endif
