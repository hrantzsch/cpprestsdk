/***
 * Copyright (C) Microsoft. All rights reserved.
 * Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
 *
 * =+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
 *
 * response_stream_tests.cpp
 *
 * Tests cases for covering receiving various responses as a stream with http_client.
 *
 * =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
 ****/

#include "stdafx.h"

#if defined(__cplusplus_winrt)
using namespace Windows::Storage;
#endif

using namespace web;
using namespace utility;
using namespace concurrency;
using namespace web::http;
using namespace web::http::client;

using namespace tests::functional::http::utilities;

namespace tests
{
namespace functional
{
namespace http
{
namespace client
{
template<typename _CharType>
pplx::task<streams::streambuf<_CharType>> OPENSTR_R(const utility::string_t& name)
{
#if !defined(__cplusplus_winrt)
    return streams::file_buffer<_CharType>::open(name, std::ios_base::in);
#else
    auto file =
        pplx::create_task(KnownFolders::DocumentsLibrary->GetFileAsync(ref new Platform::String(name.c_str()))).get();

    return streams::file_buffer<_CharType>::open(file, std::ios_base::in);
#endif
}

template<typename _CharType>
pplx::task<Concurrency::streams::basic_ostream<_CharType>> OPENSTR_W(const utility::string_t& name,
                                                                     std::ios_base::openmode mode = std::ios_base::out)
{
#if !defined(__cplusplus_winrt)
    return Concurrency::streams::file_stream<_CharType>::open_ostream(name, mode);
#else
    auto file = pplx::create_task(KnownFolders::DocumentsLibrary->CreateFileAsync(
                                      ref new Platform::String(name.c_str()), CreationCollisionOption::ReplaceExisting))
                    .get();

    return Concurrency::streams::file_stream<_CharType>::open_ostream(file, mode);
#endif
}
SUITE(response_stream_tests)
{
    TEST_FIXTURE(uri_address, set_response_stream_producer_consumer_buffer)
    {
        test_http_server::scoped_server scoped(m_uri);
        test_http_server* p_server = scoped.server();
        http_client client(m_uri);

        p_server->next_request().then([&](test_request* p_request) {
            std::map<utility::string_t, utility::string_t> headers;
            headers[U("Content-Type")] = U("text/plain");
            p_request->reply(200, U(""), headers, "This is just a bit of a string");
        });

        streams::producer_consumer_buffer<uint8_t> rwbuf;
        auto ostr = streams::ostream(rwbuf);

        http_request msg(methods::GET);
        msg.set_response_stream(ostr);
        http_response rsp = client.request(msg).get();

        rsp.content_ready().get();
        VERIFY_ARE_EQUAL(rwbuf.in_avail(), 30u);

        VERIFY_THROWS(rsp.extract_string().get(), http_exception);

        char chars[128];
        memset(chars, 0, sizeof(chars));

        rwbuf.getn((unsigned char*)chars, rwbuf.in_avail()).get();
        VERIFY_ARE_EQUAL(0, strcmp("This is just a bit of a string", chars));
    }

    TEST_FIXTURE(uri_address, set_response_stream_container_buffer)
    {
        test_http_server::scoped_server scoped(m_uri);
        test_http_server* p_server = scoped.server();
        http_client client(m_uri);

        p_server->next_request().then([&](test_request* p_request) {
            std::map<utility::string_t, utility::string_t> headers;
            headers[U("Content-Type")] = U("text/plain");
            p_request->reply(200, U(""), headers, "This is just a bit of a string");
        });

        {
            streams::container_buffer<std::vector<uint8_t>> buf;

            http_request msg(methods::GET);
            msg.set_response_stream(buf.create_ostream());
            http_response rsp = client.request(msg).get();

            rsp.content_ready().get();
            VERIFY_ARE_EQUAL(buf.collection().size(), 30);

            char bufStr[31];
            memset(bufStr, 0, sizeof(bufStr));
            memcpy(&bufStr[0], &(buf.collection())[0], 30);
            VERIFY_ARE_EQUAL(bufStr, "This is just a bit of a string");

            VERIFY_THROWS(rsp.extract_string().get(), http_exception);
        }
    }

    TEST_FIXTURE(uri_address, response_stream_file_stream)
    {
        std::string message = "A world without string is chaos.";

        test_http_server::scoped_server scoped(m_uri);
        test_http_server* p_server = scoped.server();
        http_client client(m_uri);

        p_server->next_request().then([&](test_request* p_request) {
            std::map<utility::string_t, utility::string_t> headers;
            headers[U("Content-Type")] = U("text/plain");
            p_request->reply(200, U(""), headers, message);
        });

        {
            auto fstream = OPENSTR_W<uint8_t>(U("response_stream.txt")).get();

            // Write the response into the file
            http_request msg(methods::GET);
            msg.set_response_stream(fstream);
            http_response rsp = client.request(msg).get();

            rsp.content_ready().get();
            VERIFY_IS_TRUE(fstream.streambuf().is_open());
            fstream.close().get();

            char chars[128];
            memset(chars, 0, sizeof(chars));

            streams::rawptr_buffer<uint8_t> buffer(reinterpret_cast<uint8_t*>(chars), sizeof(chars));

            streams::basic_istream<uint8_t> fistream = OPENSTR_R<uint8_t>(U("response_stream.txt")).get();
            VERIFY_ARE_EQUAL(message.length(), fistream.read_line(buffer).get());
            VERIFY_ARE_EQUAL(message, std::string(chars));
            fistream.close().get();
        }
    }

    TEST_FIXTURE(uri_address, response_stream_file_stream_close_early)
    {
        auto fstream = OPENSTR_W<uint8_t>(U("response_stream_file_stream_close_early.txt")).get();

        http_client client(m_uri);

        http_request msg(methods::GET);
        msg.set_response_stream(fstream);
        fstream.close(std::make_exception_ptr(std::exception())).wait();

        http_response resp;

        VERIFY_THROWS((resp = client.request(msg).get(), resp.content_ready().get()), std::exception);
    }

    TEST_FIXTURE(uri_address, response_stream_large_file_stream)
    {
        // Send a 100 KB data in the response body, the server will send this in multiple chunks
        // This data will get sent with content-length
        const size_t workload_size = 100 * 1024;
        utility::string_t fname(U("response_stream_large_file_stream.txt"));
        std::string responseData;
        responseData.resize(workload_size, 'a');

        test_http_server::scoped_server scoped(m_uri);
        test_http_server* p_server = scoped.server();

        http_client client(m_uri);

        p_server->next_request().then([&](test_request* p_request) {
            std::map<utility::string_t, utility::string_t> headers;
            headers[U("Content-Type")] = U("text/plain");

            p_request->reply(200, U(""), headers, responseData);
        });

        {
            auto fstream = OPENSTR_W<uint8_t>(fname).get();

            http_request msg(methods::GET);
            msg.set_response_stream(fstream);
            http_response rsp = client.request(msg).get();

            rsp.content_ready().get();
            VERIFY_IS_TRUE(fstream.streambuf().is_open());
            fstream.close().get();

            std::string rsp_string;
            rsp_string.resize(workload_size, 0);
            streams::rawptr_buffer<char> buffer(&rsp_string[0], rsp_string.size());
            streams::basic_istream<char> fistream = OPENSTR_R<char>(fname).get();

            VERIFY_ARE_EQUAL(fistream.read_to_end(buffer).get(), workload_size);
            VERIFY_ARE_EQUAL(rsp_string, responseData);
            fistream.close().get();
        }
    }

} // SUITE(responses)

} // namespace client
} // namespace http
} // namespace functional
} // namespace tests
