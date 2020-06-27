/*   This paricular file is licensed under the following terms: */

/*
 *   This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held liable
 *   for any damages arising from the use of this software.
 *
 *   Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it
 *   and redistribute it freely, subject to the following restrictions:
 *
 *    The origin of this software must not be misrepresented; you must not claim that you wrote the original software.
 *    If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
 *
 *    Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
 *    This notice may not be removed or altered from any source distribution.
 */

/* This file was modified from https://github.com/LumaTeam/Luma3DS to only have svcControlService and be compatible with C++ */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <3ds/types.h>

/// Operations for svcControlService
enum ServiceOp
{
    SERVICEOP_STEAL_CLIENT_SESSION = 0, ///< Steal a client session given a service or global port name
    SERVICEOP_GET_NAME,                 ///< Get the name of a service or global port given a client or session handle
};

/**
 * @brief Performs actions related to services or global handles.
 * @param op The operation to perform, see @ref ServiceOp.
 *
 * Examples:
 *     svcControlService(SERVICEOP_GET_NAME, (char [12])outName, (Handle)clientOrSessionHandle);
 *     svcControlService(SERVICEOP_STEAL_CLIENT_SESSION, (Handle *)&outHandle, (const char *)name);
 */
Result svcControlService(ServiceOp op, ...);

#ifdef __cplusplus
}
#endif
