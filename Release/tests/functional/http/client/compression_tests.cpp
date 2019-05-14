/***
 * Copyright (C) Microsoft. All rights reserved.
 * Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
 *
 * =+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
 *
 * compression_tests.cpp
 *
 * Tests cases, including client/server, for the web::http::compression namespace.
 *
 * =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
 ****/

#include "stdafx.h"

#include "cpprest/asyncrt_utils.h"
#include "cpprest/details/http_helpers.h"
#include "cpprest/version.h"
#include <fstream>

using namespace web;
using namespace utility;
using namespace web::http;
using namespace web::http::client;
using namespace web::http::compression;

using namespace tests::functional::http::utilities;

namespace tests
{
namespace functional
{
namespace http
{
namespace client
{
SUITE(compression_tests)
{
    // A fake "pass-through" compressor/decompressor for testing
    class fake_provider : public compress_provider, public decompress_provider
    {
    public:
        static const utility::string_t FAKE;

        fake_provider(size_t size = static_cast<size_t>(-1)) : _size(size), _so_far(0), _done(false) {}

        virtual const utility::string_t& algorithm() const { return FAKE; }

        virtual size_t decompress(const uint8_t* input,
                                  size_t input_size,
                                  uint8_t* output,
                                  size_t output_size,
                                  operation_hint hint,
                                  size_t& input_bytes_processed,
                                  bool& done)
        {
            size_t bytes;

            if (_done)
            {
                input_bytes_processed = 0;
                done = true;
                return 0;
            }
            if (_size == static_cast<size_t>(-1) || input_size > _size - _so_far)
            {
                std::stringstream ss;
                ss << "Fake decompress - invalid data " << input_size << ", " << output_size << " with " << _so_far
                   << " / " << _size;
                throw std::runtime_error(std::move(ss.str()));
            }
            bytes = std::min(input_size, output_size);
            if (bytes)
            {
                memcpy(output, input, bytes);
            }
            _so_far += bytes;
            _done = (_so_far == _size);
            done = _done;
            input_bytes_processed = bytes;
            return input_bytes_processed;
        }

        virtual pplx::task<operation_result> decompress(
            const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size, operation_hint hint)
        {
            operation_result r;

            try
            {
                r.output_bytes_produced =
                    decompress(input, input_size, output, output_size, hint, r.input_bytes_processed, r.done);
            }
            catch (...)
            {
                pplx::task_completion_event<operation_result> ev;
                ev.set_exception(std::current_exception());
                return pplx::create_task(ev);
            }

            return pplx::task_from_result<operation_result>(r);
        }

        virtual size_t compress(const uint8_t* input,
                                size_t input_size,
                                uint8_t* output,
                                size_t output_size,
                                operation_hint hint,
                                size_t& input_bytes_processed,
                                bool& done)
        {
            size_t bytes;

            if (_done)
            {
                input_bytes_processed = 0;
                done = true;
                return 0;
            }
            if (_size == static_cast<size_t>(-1) || input_size > _size - _so_far)
            {
                std::stringstream ss;
                ss << "Fake compress - invalid data " << input_size << ", " << output_size << " with " << _so_far
                   << " / " << _size;
                throw std::runtime_error(std::move(ss.str()));
            }
            bytes = std::min(input_size, output_size);
            if (bytes)
            {
                memcpy(output, input, bytes);
            }
            _so_far += bytes;
            _done = (hint == operation_hint::is_last && _so_far == _size);
            done = _done;
            input_bytes_processed = bytes;
            return input_bytes_processed;
        }

        virtual pplx::task<operation_result> compress(
            const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size, operation_hint hint)
        {
            operation_result r;

            try
            {
                r.output_bytes_produced =
                    compress(input, input_size, output, output_size, hint, r.input_bytes_processed, r.done);
            }
            catch (...)
            {
                pplx::task_completion_event<operation_result> ev;
                ev.set_exception(std::current_exception());
                return pplx::create_task(ev);
            }

            return pplx::task_from_result<operation_result>(r);
        }

        virtual void reset()
        {
            _done = false;
            _so_far = 0;
        }

    private:
        size_t _size;
        size_t _so_far;
        bool _done;
    };

    const utility::string_t fake_provider::FAKE = _XPLATSTR("fake");

    void compress_and_decompress(std::unique_ptr<compress_provider> compressor,
                                 std::unique_ptr<decompress_provider> decompressor,
                                 const size_t buffer_size,
                                 const size_t chunk_size,
                                 bool compressible)
    {
        std::vector<uint8_t> input_buffer;
        size_t i;

        VERIFY_ARE_EQUAL(compressor->algorithm(), decompressor->algorithm());

        input_buffer.reserve(buffer_size);
        for (i = 0; i < buffer_size; ++i)
        {
            uint8_t element;
            if (compressible)
            {
                element = static_cast<uint8_t>('a' + i % 26);
            }
            else
            {
                element = static_cast<uint8_t>(std::rand());
            }

            input_buffer.push_back(element);
        }

        // compress in chunks
        std::vector<size_t> chunk_sizes;
        std::vector<uint8_t> cmp_buffer(buffer_size);
        size_t cmpsize = buffer_size;
        size_t csize = 0;
        operation_result r = {0};
        operation_hint hint = operation_hint::has_more;
        for (i = 0; i < buffer_size || csize == cmpsize || !r.done; i += r.input_bytes_processed)
        {
            if (i == buffer_size)
            {
                // the entire input buffer has been consumed by the compressor
                hint = operation_hint::is_last;
            }
            if (csize == cmpsize)
            {
                // extend the output buffer if there may be more compressed bytes to retrieve
                cmpsize += std::min(chunk_size, (size_t)200);
                cmp_buffer.resize(cmpsize);
            }
            r = compressor
                    ->compress(input_buffer.data() + i,
                               std::min(chunk_size, buffer_size - i),
                               cmp_buffer.data() + csize,
                               std::min(chunk_size, cmpsize - csize),
                               hint)
                    .get();
            VERIFY_IS_TRUE(r.input_bytes_processed == std::min(chunk_size, buffer_size - i) ||
                           r.output_bytes_produced == std::min(chunk_size, cmpsize - csize));
            VERIFY_IS_TRUE(hint == operation_hint::is_last || !r.done);
            chunk_sizes.push_back(r.output_bytes_produced);
            csize += r.output_bytes_produced;
        }
        VERIFY_ARE_EQUAL(r.done, true);

        // once more with no input or output, to assure no error and done
        r = compressor->compress(NULL, 0, NULL, 0, operation_hint::is_last).get();
        VERIFY_ARE_EQUAL(r.input_bytes_processed, 0);
        VERIFY_ARE_EQUAL(r.output_bytes_produced, 0);
        VERIFY_ARE_EQUAL(r.done, true);

        cmp_buffer.resize(csize); // actual

        // decompress in as-compressed chunks
        std::vector<uint8_t> dcmp_buffer(buffer_size);
        size_t dsize = 0;
        size_t nn = 0;
        for (std::vector<size_t>::iterator it = chunk_sizes.begin(); it != chunk_sizes.end(); ++it)
        {
            if (*it)
            {
                auto hint = operation_hint::has_more;
                if (it == chunk_sizes.begin())
                {
                    hint = operation_hint::is_last;
                }

                r = decompressor
                        ->decompress(cmp_buffer.data() + nn,
                                     *it,
                                     dcmp_buffer.data() + dsize,
                                     std::min(chunk_size, buffer_size - dsize),
                                     hint)
                        .get();
                nn += *it;
                dsize += r.output_bytes_produced;
            }
        }
        VERIFY_ARE_EQUAL(csize, nn);
        VERIFY_ARE_EQUAL(dsize, buffer_size);
        VERIFY_ARE_EQUAL(input_buffer, dcmp_buffer);
        VERIFY_IS_TRUE(r.done);

        // decompress again in fixed-size chunks
        nn = 0;
        dsize = 0;
        decompressor->reset();
        memset(dcmp_buffer.data(), 0, dcmp_buffer.size());
        do
        {
            size_t n = std::min(chunk_size, csize - nn);
            do
            {
                r = decompressor
                        ->decompress(cmp_buffer.data() + nn,
                                     n,
                                     dcmp_buffer.data() + dsize,
                                     std::min(chunk_size, buffer_size - dsize),
                                     operation_hint::has_more)
                        .get();
                dsize += r.output_bytes_produced;
                nn += r.input_bytes_processed;
                n -= r.input_bytes_processed;
            } while (n);
        } while (nn < csize || !r.done);
        VERIFY_ARE_EQUAL(csize, nn);
        VERIFY_ARE_EQUAL(dsize, buffer_size);
        VERIFY_ARE_EQUAL(input_buffer, dcmp_buffer);
        VERIFY_IS_TRUE(r.done);

        // once more with no input, to assure no error and done
        r = decompressor->decompress(NULL, 0, NULL, 0, operation_hint::has_more).get();
        VERIFY_ARE_EQUAL(r.input_bytes_processed, 0);
        VERIFY_ARE_EQUAL(r.output_bytes_produced, 0);
        VERIFY_IS_TRUE(r.done);

        // decompress all at once
        decompressor->reset();
        memset(dcmp_buffer.data(), 0, dcmp_buffer.size());
        r = decompressor
                ->decompress(cmp_buffer.data(), csize, dcmp_buffer.data(), dcmp_buffer.size(), operation_hint::is_last)
                .get();
        VERIFY_ARE_EQUAL(r.output_bytes_produced, buffer_size);
        VERIFY_ARE_EQUAL(input_buffer, dcmp_buffer);

        if (decompressor->algorithm() != fake_provider::FAKE)
        {
            // invalid decompress buffer, first and subsequent tries
            cmp_buffer[0] = ~cmp_buffer[1];
            decompressor->reset();
            for (i = 0; i < 2; i++)
            {
                nn = 0;
                try
                {
                    r = decompressor
                            ->decompress(cmp_buffer.data(),
                                         csize,
                                         dcmp_buffer.data(),
                                         dcmp_buffer.size(),
                                         operation_hint::is_last)
                            .get();
                    VERIFY_IS_FALSE(r.done && r.output_bytes_produced == buffer_size);
                }
                catch (std::runtime_error)
                {
                }
            }
        }
    }

    void compress_test(std::shared_ptr<compress_factory> cfactory, std::shared_ptr<decompress_factory> dfactory)
    {
        size_t tuples[][2] = {{3, 1024},
                              {7999, 8192},
                              {8192, 8192},
                              {16001, 8192},
                              {16384, 8192},
                              {140000, 65536},
                              {256 * 1024, 65536},
                              {256 * 1024, 256 * 1024},
                              {263456, 256 * 1024}};

        for (int i = 0; i < sizeof(tuples) / sizeof(tuples[0]); i++)
        {
            for (int j = 0; j < 2; j++)
            {
                if (!cfactory)
                {
                    auto size = tuples[i][0];
                    compress_and_decompress(utility::details::make_unique<fake_provider>(tuples[i][0]),
                                            utility::details::make_unique<fake_provider>(tuples[i][0]),
                                            tuples[i][0],
                                            tuples[i][1],
                                            !!j);
                }
                else
                {
                    compress_and_decompress(
                        cfactory->make_compressor(), dfactory->make_decompressor(), tuples[i][0], tuples[i][1], !!j);
                }
            }
        }
    }

    TEST_FIXTURE(uri_address, compress_and_decompress)
    {
        compress_test(nullptr, nullptr); // FAKE
        if (builtin::algorithm::supported(builtin::algorithm::GZIP))
        {
            compress_test(builtin::get_compress_factory(builtin::algorithm::GZIP),
                          builtin::get_decompress_factory(builtin::algorithm::GZIP));
        }
        if (builtin::algorithm::supported(builtin::algorithm::DEFLATE))
        {
            compress_test(builtin::get_compress_factory(builtin::algorithm::DEFLATE),
                          builtin::get_decompress_factory(builtin::algorithm::DEFLATE));
        }
        if (builtin::algorithm::supported(builtin::algorithm::BROTLI))
        {
            compress_test(builtin::get_compress_factory(builtin::algorithm::BROTLI),
                          builtin::get_decompress_factory(builtin::algorithm::BROTLI));
        }
    }

    TEST_FIXTURE(uri_address, compress_headers)
    {
        const utility::string_t _NONE = _XPLATSTR("none");

        std::unique_ptr<compress_provider> c;
        std::unique_ptr<decompress_provider> d;

        std::shared_ptr<compress_factory> fcf =
            make_compress_factory(fake_provider::FAKE, []() -> std::unique_ptr<compress_provider> {
                return utility::details::make_unique<fake_provider>();
            });
        std::vector<std::shared_ptr<compress_factory>> fcv;
        fcv.push_back(fcf);
        std::shared_ptr<decompress_factory> fdf =
            make_decompress_factory(fake_provider::FAKE, 800, []() -> std::unique_ptr<decompress_provider> {
                return utility::details::make_unique<fake_provider>();
            });
        std::vector<std::shared_ptr<decompress_factory>> fdv;
        fdv.push_back(fdf);

        std::shared_ptr<compress_factory> ncf =
            make_compress_factory(_NONE, []() -> std::unique_ptr<compress_provider> {
                return utility::details::make_unique<fake_provider>();
            });
        std::vector<std::shared_ptr<compress_factory>> ncv;
        ncv.push_back(ncf);
        std::shared_ptr<decompress_factory> ndf =
            make_decompress_factory(_NONE, 800, []() -> std::unique_ptr<decompress_provider> {
                return utility::details::make_unique<fake_provider>();
            });
        std::vector<std::shared_ptr<decompress_factory>> ndv;
        ndv.push_back(ndf);

        // Supported algorithms
        VERIFY_ARE_EQUAL(builtin::supported(), builtin::algorithm::supported(builtin::algorithm::GZIP));
        VERIFY_ARE_EQUAL(builtin::supported(), builtin::algorithm::supported(builtin::algorithm::DEFLATE));
        if (builtin::algorithm::supported(builtin::algorithm::BROTLI))
        {
            VERIFY_IS_TRUE(builtin::supported());
        }
        VERIFY_IS_FALSE(builtin::algorithm::supported(_XPLATSTR("")));
        VERIFY_IS_FALSE(builtin::algorithm::supported(_XPLATSTR("foo")));

        // Strings that double as both Transfer-Encoding and TE
        std::vector<utility::string_t> encodings = {_XPLATSTR("gzip"),
                                                    _XPLATSTR("gZip  "),
                                                    _XPLATSTR(" GZIP"),
                                                    _XPLATSTR(" gzip "),
                                                    _XPLATSTR("  gzip  ,   chunked  "),
                                                    _XPLATSTR(" gZip , chunked "),
                                                    _XPLATSTR("GZIP,chunked")};

        // Similar, but geared to match a non-built-in algorithm
        std::vector<utility::string_t> fake = {_XPLATSTR("fake"),
                                               _XPLATSTR("faKe  "),
                                               _XPLATSTR(" FAKE"),
                                               _XPLATSTR(" fake "),
                                               _XPLATSTR("  fake  ,   chunked  "),
                                               _XPLATSTR(" faKe , chunked "),
                                               _XPLATSTR("FAKE,chunked")};

        std::vector<utility::string_t> invalid = {_XPLATSTR(","),
                                                  _XPLATSTR(",gzip"),
                                                  _XPLATSTR("gzip,"),
                                                  _XPLATSTR(",gzip, chunked"),
                                                  _XPLATSTR(" ,gzip, chunked"),
                                                  _XPLATSTR("gzip, chunked,"),
                                                  _XPLATSTR("gzip, chunked, "),
                                                  _XPLATSTR("gzip,, chunked"),
                                                  _XPLATSTR("gzip , , chunked"),
                                                  _XPLATSTR("foo")};

        std::vector<utility::string_t> invalid_tes = {
            _XPLATSTR("deflate;q=0.5, gzip;q=2"),
            _XPLATSTR("deflate;q=1.5, gzip;q=1"),
        };

        std::vector<utility::string_t> empty = {_XPLATSTR(""), _XPLATSTR(" ")};

        // Repeat for Transfer-Encoding (which also covers part of TE) and Content-Encoding (which also covers all of
        // Accept-Encoding)
        for (int transfer = 0; transfer < 2; transfer++)
        {
            compression::details::header_types ctype =
                transfer ? compression::details::header_types::te : compression::details::header_types::accept_encoding;
            compression::details::header_types dtype = transfer ? compression::details::header_types::transfer_encoding
                                                                : compression::details::header_types::content_encoding;

            // No compression - Transfer-Encoding
            d = compression::details::get_decompressor_from_header(
                _XPLATSTR(" chunked "), compression::details::header_types::transfer_encoding);
            VERIFY_IS_FALSE((bool)d);

            utility::string_t gzip(builtin::algorithm::GZIP);
            for (auto encoding = encodings.begin(); encoding != encodings.end(); encoding++)
            {
                bool has_comma = false;

                has_comma = encoding->find(_XPLATSTR(",")) != utility::string_t::npos;

                // Built-in only
                c = compression::details::get_compressor_from_header(*encoding, ctype);
                VERIFY_ARE_EQUAL((bool)c, builtin::supported());
                if (c)
                {
                    VERIFY_ARE_EQUAL(c->algorithm(), gzip);
                }

                try
                {
                    d = compression::details::get_decompressor_from_header(*encoding, dtype);
                    VERIFY_ARE_EQUAL((bool)d, builtin::supported());
                    if (d)
                    {
                        VERIFY_ARE_EQUAL(d->algorithm(), gzip);
                    }
                }
                catch (http_exception)
                {
                    VERIFY_IS_TRUE(transfer == !has_comma);
                }
            }

            for (auto encoding = fake.begin(); encoding != fake.end(); encoding++)
            {
                bool has_comma = false;

                has_comma = encoding->find(_XPLATSTR(",")) != utility::string_t::npos;

                // Supplied compressor/decompressor
                c = compression::details::get_compressor_from_header(*encoding, ctype, fcv);
                VERIFY_IS_TRUE((bool)c);
                VERIFY_IS_TRUE(c->algorithm() == fcf->algorithm());

                try
                {
                    d = compression::details::get_decompressor_from_header(*encoding, dtype, fdv);
                    VERIFY_IS_TRUE((bool)d);
                    VERIFY_IS_TRUE(d->algorithm() == fdf->algorithm());
                }
                catch (http_exception)
                {
                    VERIFY_IS_TRUE(transfer == !has_comma);
                }

                // No matching compressor
                c = compression::details::get_compressor_from_header(*encoding, ctype, ncv);
                VERIFY_IS_FALSE((bool)c);

                try
                {
                    d = compression::details::get_decompressor_from_header(*encoding, dtype, ndv);
                    VERIFY_IS_FALSE(true);
                }
                catch (http_exception)
                {
                }
            }

            // Negative tests - invalid headers, no matching algorithm, etc.
            for (auto encoding = invalid.begin(); encoding != invalid.end(); encoding++)
            {
                try
                {
                    c = compression::details::get_compressor_from_header(*encoding, ctype);
                    VERIFY_IS_TRUE(encoding->find(_XPLATSTR(",")) == utility::string_t::npos);
                    VERIFY_IS_FALSE((bool)c);
                }
                catch (http_exception)
                {
                }

                try
                {
                    d = compression::details::get_decompressor_from_header(*encoding, dtype);
                    VERIFY_IS_TRUE(!builtin::supported() && encoding->find(_XPLATSTR(",")) == utility::string_t::npos);
                    VERIFY_IS_FALSE((bool)d);
                }
                catch (http_exception)
                {
                }
            }

            // Negative tests - empty headers
            for (auto encoding = empty.begin(); encoding != empty.end(); encoding++)
            {
                c = compression::details::get_compressor_from_header(*encoding, ctype);
                VERIFY_IS_FALSE((bool)c);

                try
                {
                    d = compression::details::get_decompressor_from_header(*encoding, dtype);
                    VERIFY_IS_FALSE(true);
                }
                catch (http_exception)
                {
                }
            }

            // Negative tests - invalid rankings
            for (auto te = invalid_tes.begin(); te != invalid_tes.end(); te++)
            {
                try
                {
                    c = compression::details::get_compressor_from_header(*te, ctype);
                    VERIFY_IS_FALSE(true);
                }
                catch (http_exception)
                {
                }
            }

            utility::string_t builtin;
            std::vector<std::shared_ptr<decompress_factory>> dv;

            // Builtins
            builtin = compression::details::build_supported_header(ctype);
            if (transfer)
            {
                VERIFY_ARE_EQUAL(!builtin.empty(), builtin::supported());
            }
            else
            {
                VERIFY_IS_FALSE(builtin.empty());
            }

            // Null decompressor - effectively forces no compression algorithms
            dv.push_back(std::shared_ptr<decompress_factory>());
            builtin = compression::details::build_supported_header(ctype, dv);
            VERIFY_ARE_EQUAL(transfer != 0, builtin.empty());
            dv.pop_back();

            if (builtin::supported())
            {
                dv.push_back(builtin::get_decompress_factory(builtin::algorithm::GZIP));
                builtin = compression::details::build_supported_header(ctype, dv); // --> "gzip;q=1.0"
                VERIFY_IS_FALSE(builtin.empty());
            }
            else
            {
                builtin = _XPLATSTR("gzip;q=1.0");
            }

            // TE- and/or Accept-Encoding-specific test cases, regenerated for each pass
            std::vector<utility::string_t> tes = {
                builtin,
                _XPLATSTR("  deflate;q=0.777  ,foo;q=0,gzip;q=0.9,     bar;q=1.0, xxx;q=1  "),
                _XPLATSTR("gzip ; q=1, deflate;q=0.5"),
                _XPLATSTR("gzip;q=1.0, deflate;q=0.5"),
                _XPLATSTR("deflate;q=0.5, gzip;q=1"),
                _XPLATSTR("gzip,deflate;q=0.7"),
                _XPLATSTR("trailers,gzip,deflate;q=0.7")};

            for (int fake = 0; fake < 2; fake++)
            {
                if (fake)
                {
                    // Switch built-in vs. supplied results the second time around
                    for (auto& te : tes)
                    {
                        te.replace(te.find(builtin::algorithm::GZIP), gzip.size(), fake_provider::FAKE);
                        if (te.find(builtin::algorithm::DEFLATE) != utility::string_t::npos)
                        {
                            te.replace(te.find(builtin::algorithm::DEFLATE),
                                       utility::string_t(builtin::algorithm::DEFLATE).size(),
                                       _NONE);
                        }
                    }
                }

                for (auto te = tes.begin(); te != tes.end(); te++)
                {
                    // Built-in only
                    c = compression::details::get_compressor_from_header(*te, ctype);
                    if (c)
                    {
                        VERIFY_IS_TRUE(builtin::supported());
                        VERIFY_IS_FALSE(fake != 0);
                        VERIFY_ARE_EQUAL(c->algorithm(), gzip);
                    }
                    else
                    {
                        VERIFY_IS_TRUE(fake != 0 || !builtin::supported());
                    }

                    // Supplied compressor - both matching and non-matching
                    c = compression::details::get_compressor_from_header(*te, ctype, fcv);
                    VERIFY_ARE_EQUAL(c != 0, fake != 0);
                    if (c)
                    {
                        VERIFY_ARE_EQUAL(c->algorithm(), fake_provider::FAKE);
                    }
                }
            }
        }
    }

    template<typename _CharType>
    class my_rawptr_buffer : public concurrency::streams::rawptr_buffer<_CharType>
    {
    public:
        my_rawptr_buffer(const _CharType* data, size_t size)
            : concurrency::streams::rawptr_buffer<_CharType>(data, size)
        {
        }

        // No acquire(), to force non-acquire compression client codepaths
        virtual bool acquire(_Out_ _CharType*& ptr, _Out_ size_t& count)
        {
            (void)ptr;
            (void)count;
            return false;
        }

        virtual void release(_Out_writes_(count) _CharType* ptr, _In_ size_t count)
        {
            (void)ptr;
            (void)count;
        }

        static concurrency::streams::basic_istream<_CharType> open_istream(const _CharType* data, size_t size)
        {
            return concurrency::streams::basic_istream<_CharType>(
                concurrency::streams::streambuf<_CharType>(std::make_shared<my_rawptr_buffer<_CharType>>(data, size)));
        }
    };

} // SUITE(request_helper_tests)
} // namespace client
} // namespace http
} // namespace functional
} // namespace tests
