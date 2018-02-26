/* *****************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 Vasiliy V. Bodrov aka Bodro, Ryazan, Russia
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
 * OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ************************************************************************** */

#pragma once

#ifndef __PROXY_HPP__
#define __PROXY_HPP__

#include <exception>
#include <stdexcept>
#include <memory>

#include "proxy_result.hpp"
#include "proxy_impl.hpp"

namespace proxy_ns {
	class Iproxy {
	public:
		virtual result_t run(void) = 0;

        virtual void set_proxy_port(boost::uint16_t value) = 0;
        virtual void set_server_port(boost::uint16_t value) = 0;
        virtual void set_server_ip(std::string const& value) = 0;
        virtual void set_server_ip(char const* value) = 0;
        virtual void set_timeout(boost::int32_t value) = 0;
        virtual void set_connect_timeout(boost::int32_t value) = 0;
        virtual void set_client_keep_alive(bool value) = 0;
        virtual void set_server_keep_alive(bool value) = 0;

        virtual boost::uint16_t get_proxy_port(void) const = 0;
        virtual boost::uint16_t get_server_port(void) const = 0;
        virtual std::string const& get_server_ip(void) const = 0;
        virtual boost::int32_t get_timeout(void) const = 0;
        virtual boost::int32_t get_connect_timeout(void) const = 0;
        virtual bool get_client_keep_alive(void) const = 0;
        virtual bool get_server_keep_alive(void) const = 0;
			
		virtual ~Iproxy(void) {}
	};

	class proxy : public Iproxy {
	public:
		proxy(void) : p(std::make_unique<proxy_impl>()) {
		}
	
        virtual result_t run(void) {
			return p.get()->run();
		}
		
        virtual void set_proxy_port(boost::uint16_t value) {
            p.get()->set_proxy_port(value);
        }

        virtual void set_server_port(boost::uint16_t value) {
            p.get()->set_server_port(value);
        }

        virtual void set_server_ip(std::string const& value) {
            p.get()->set_server_ip(value);
        }

        virtual void set_server_ip(char const* value) {
            p.get()->set_server_ip(value);
        }

        virtual void set_timeout(boost::int32_t value) {
            p.get()->set_timeout(value);
        }

        virtual void set_connect_timeout(boost::int32_t value) {
            p.get()->set_connect_timeout(value);
        }

        virtual void set_client_keep_alive(bool value) {
            p.get()->set_client_keep_alive(value);
        }

        virtual void set_server_keep_alive(bool value) {
            p.get()->set_server_keep_alive(value);
        }

        virtual boost::uint16_t get_proxy_port(void) const {
            return p.get()->get_proxy_port();
        }

        virtual boost::uint16_t get_server_port(void) const {
            return p.get()->get_server_port();
        }

        virtual std::string const& get_server_ip(void) const {
            return p.get()->get_server_ip();
        }

        virtual boost::int32_t get_timeout(void) const {
            return p.get()->get_timeout();
        }

        virtual boost::int32_t get_connect_timeout(void) const {
            return p.get()->get_connect_timeout();
        }

        virtual bool get_client_keep_alive(void) const {
            return p.get()->get_client_keep_alive();
        }

        virtual bool get_server_keep_alive(void) const {
            return p.get()->get_server_keep_alive();
        }

		virtual ~proxy(void) {
		}
	private:
		std::unique_ptr<Iproxy_impl> p;
	};

    class IEproxy : public std::exception {
    protected:
        IEproxy(void) noexcept {}
    public:
        virtual ~IEproxy() noexcept {}
        virtual char const* what(void) const noexcept {
            static std::string const msg("IEproxy");
            return msg.c_str();
        }
    };

    class Eproxy_running : public IEproxy {
    public:
        Eproxy_running(void) noexcept {}
        virtual ~Eproxy_running() noexcept {}
        virtual char const* what(void) const noexcept {
            static std::string const msg("Proxy already running");
            return msg.c_str();
        }
    };
} // namespace proxy_ns

#endif // __PROXY_HPP__

/* *****************************************************************************
 * End of file
 * ************************************************************************** */
