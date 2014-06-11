/***
* ==++==
*
* Copyright (c) Microsoft Corporation. All rights reserved.
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* ==--==
* =+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
*
* oauth2_handler.cpp
*
* HTTP Library: Oauth 2.0 protocol handler
*
* For the latest on this and related APIs, please see http://casablanca.codeplex.com.
*
* =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
****/

#include "stdafx.h"
#include "cpprest/oauth2_handler.h"
#include "cpprest/http_helpers.h"

using namespace utility;
using namespace web;
using namespace web::http;
using namespace web::http::client;
using namespace web::http::details;

namespace web { namespace http { namespace client { namespace experimental
{


utility::string_t oauth2_config::build_authorization_uri()
{
    const utility::string_t response_type((implicit_grant()) ? oauth2_strings::token : oauth2_strings::code);
    uri_builder ub(auth_endpoint());
    ub.append_query(oauth2_strings::response_type, response_type);
    ub.append_query(oauth2_strings::client_id, client_key());
    ub.append_query(oauth2_strings::redirect_uri, redirect_uri());

    if (custom_state().empty())
    {
        m_state = m_state_generator.generate();
    }
    else
    {
        m_state = custom_state();
    }
    ub.append_query(oauth2_strings::state, state());

    if (!scope().empty())
    {
        ub.append_query(oauth2_strings::scope, scope());
    }
    return ub.to_string();
}

pplx::task<void> oauth2_config::token_from_redirected_uri(web::http::uri redirected_uri)
{
    auto query = uri::split_query((implicit_grant()) ? redirected_uri.fragment() : redirected_uri.query());
    
    auto state_param = query.find(oauth2_strings::state);
    if (state_param == query.end())
    {
        return pplx::task_from_exception<void>(oauth2_exception(U("parameter 'state' missing from redirected URI.")));
    }
    if (state() != state_param->second)
    {
        utility::ostringstream_t err;
        err << U("redirected URI parameter 'state'='") << state_param->second
            << U("' does not match state='") << state() << U("'.");
        return pplx::task_from_exception<void>(oauth2_exception(err.str().c_str()));
    }

    auto code_param = query.find(oauth2_strings::code);
    if (code_param != query.end())
    {
        return token_from_code(code_param->second);
    }

    // NOTE: The redirected URI contains access token only in the implicit grant.
    // The implicit grant never passes a refresh token.
    auto token_param = query.find(oauth2_strings::access_token);
    if (token_param == query.end())
    {
        return pplx::task_from_exception<void>(oauth2_exception(U("either 'code' or 'access_token' parameter must be in the redirected URI.")));
    }

    set_token(token_param->second);
    return pplx::create_task([](){});
}

pplx::task<void> oauth2_config::_request_token(uri_builder&& request_body_ub)
{
    http_request request;
    request.set_method(methods::POST);
    request.set_request_uri(utility::string_t());

    if (!scope().empty())
    {
        request_body_ub.append_query(oauth2_strings::scope, uri::encode_data_string(scope()), false);
    }

    if (http_basic_auth())
    {
        std::vector<unsigned char> creds_vec(conversions::to_body_data(
            uri::encode_data_string(client_key()) + U(":") + uri::encode_data_string(client_secret()))
        );
        request.headers().add(header_names::authorization, U("Basic ") + conversions::to_base64(std::move(creds_vec)));
    }
    else
    {
        request_body_ub.append_query(oauth2_strings::client_id, uri::encode_data_string(client_key()), false);
        request_body_ub.append_query(oauth2_strings::client_secret, uri::encode_data_string(client_secret()), false);
    }
    request.set_body(request_body_ub.query(), mime_types::application_x_www_form_urlencoded);

    http_client token_client(token_endpoint());

    return token_client.request(request)
    .then([this](pplx::task<http_response> req_task)
    {
        json::value resp_json;
        try
        {
            resp_json = req_task.get().extract_json().get();
        }
        catch (http_exception &e)
        {
            throw oauth2_exception(U("encountered http_exception: ") + conversions::to_string_t(std::string(e.what())));
        }
        catch (json::json_exception &e)
        {
            throw oauth2_exception(U("encountered json_exception: ") + conversions::to_string_t(std::string(e.what())));
        }
        catch (std::exception &e)
        {
            throw oauth2_exception(U("encountered exception: ") + conversions::to_string_t(std::string(e.what())));
        }
        catch (...)
        {
            throw oauth2_exception(U("encountered unknown exception"));
        }

        set_token(_parse_token_from_json(resp_json));
    });
}

oauth2_token oauth2_config::_parse_token_from_json(json::value& token_json)
{
    oauth2_token result;

    if (token_json.has_field(oauth2_strings::access_token))
    {
        result.set_access_token(token_json[oauth2_strings::access_token].as_string());
    }
    else
    {
        throw oauth2_exception(U("response json contains no 'access_token': ") + token_json.serialize());
    }

    if (token_json.has_field(oauth2_strings::token_type))
    {
        result.set_token_type(token_json[oauth2_strings::token_type].as_string());
    }
    else
    {
        // Some services don't return 'token_type' while it's required by OAuth 2.0 spec:
        // http://tools.ietf.org/html/rfc6749#section-5.1
        // As workaround we act as if 'token_type=bearer' was received.
        result.set_token_type(oauth2_strings::bearer);
    }
    if (!utility::details::str_icmp(result.token_type(), oauth2_strings::bearer))
    {
        throw oauth2_exception(U("only 'token_type=bearer' access tokens are currently supported: ") + token_json.serialize());
    }

    if (token_json.has_field(oauth2_strings::refresh_token))
    {
        result.set_refresh_token(token_json[oauth2_strings::refresh_token].as_string());
    }
    else
    {
        // Do nothing. Preserves the old refresh token.
    }

    if (token_json.has_field(oauth2_strings::expires_in))
    {
        result.set_expires_in(token_json[oauth2_strings::expires_in].as_integer());
    }
    else
    {
        result.set_expires_in(-1); // Set as unspecified.
    }

    if (token_json.has_field(oauth2_strings::scope))
    {
        result.set_scope(token_json[oauth2_strings::scope].as_string());
    }
    else
    {
        result.set_scope(scope()); // Set to current scope().
    }

    return result;
}


#define _OAUTH2_STRINGS
#define DAT(a_, b_) const oauth2_string oauth2_strings::a_(_XPLATSTR(b_));
#include "cpprest/http_constants.dat"
#undef _OAUTH2_STRINGS
#undef DAT


}}}} // namespace web::http::client::experimental
