/***
* Copyright (C) Microsoft. All rights reserved.
* Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
*
* =+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
*
* HTTP Library: Client-side APIs.
*
* For the latest on this and related APIs, please see: https://github.com/Microsoft/cpprestsdk
*
* =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
****/
#pragma once

#ifndef _CASA_CLIENT_AUTHENTICATION_H
#define _CASA_CLIENT_AUTHENTICATION_H

#include <vector>
#include <memory>

#include "cpprest/details/web_utilities.h"

namespace web { namespace http { namespace client {

/// <summary>
/// Client Authentication Info class, used to identify whether a client certificate is needed to establish a tls connection.
/// </summary>
class client_authentication_info
{
public:
    using pointer_type = void*;
    using type = std::remove_pointer<pointer_type>::type;

    client_authentication_info() = default;

    client_authentication_info(
        const std::vector<utility::string_t> certification_authorities,
        std::shared_ptr<type> native_ca_obj)
        : m_certification_authorities(certification_authorities),
        m_native_ca_handle(native_ca_obj),
        m_initialized(true)
    {}

    /// <summary>
    /// Identify whether a client certificate is needed to establish a tls
    /// connection.
    /// </summary>
    /// <returns>True if a client certificate is needed. Otherwise returns
    /// false.</returns>
    bool need_client_authentication() const
    {
        return !m_certification_authorities.empty();
    }

    /// <summary>
    /// Vector contains the dn from the supported certification authorities.
    /// </summary>
    /// <returns>Returns a std::vector. Each element contains a dn from a supported CA.</returns>
    const std::vector<utility::string_t>& certification_authorities() const
    {
        return m_certification_authorities;
    }

    /// <summary>
    /// Native Handle.
    /// </summary>
    /// <returns>Windows: Returns a shared_ptr. Need to reinterpret_cast this into a SecPkgContext_IssuerListInfoEx pointer.</returns>
    std::shared_ptr<type> get_native_handle() const
    {
        return m_native_ca_handle;
    }

    operator bool() const
    {
        return m_initialized;
    }

private:
    std::vector<utility::string_t> m_certification_authorities;
    std::shared_ptr<type> m_native_ca_handle;
    bool m_initialized = false;
};
}}}  // namespace web::http::client

#endif // _CASA_CLIENT_AUTHENTICATION_H
