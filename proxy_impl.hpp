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

#ifndef __PROXY_IMPL_HPP__
#define __PROXY_IMPL_HPP__

#ifndef __PROXY_HPP__
#error "Never use <proxy_impl.hpp> directly; include <proxy.hpp> instead."
#endif // __PROXY_HPP__

#include <mutex>
#include <atomic>
#include <functional>

#include <boost/cstdint.hpp>

#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

#include "proxy_result.hpp"

#ifndef POLLING_REQUESTS_SIZE
    #define POLLING_REQUESTS_SIZE 1000
#endif // POLLING_REQUESTS_SIZE

#ifndef DATA_BUFFER_SIZE
    #define DATA_BUFFER_SIZE 10240
#endif // DATA_BUFFER_SIZE

namespace proxy_ns {
	class Iproxy_impl;
	class proxy_impl;

    ///
    ///
    ///
    typedef enum {
        DIRECTION_UNKNOWN = 0,
        DIRECTION_CLIENT_TO_SERVER,
        DIRECTION_SERVER_TO_CLIENT,
        DIRECTION_CLIENT_TO_WORKER,
        DIRECTION_WORKER_TO_CLIENT,
        DIRECTION_SERVER_TO_WORKER,
        DIRECTION_WORKER_TO_SERVER,
        DIRECTION_END
    } direction_t;

    ///
    ///
    ///
    /// RU:
    /// Семантика пакета зависит от его типа и направления.
    /// Типы пакетов:
    /// * TOD_UNKNOWN - неизвестный тип пакета (пакет создан некорректно или
    ///                 не создан);
    /// * TOD_NEW_CONNECT - новое соединение (указание что появилось новое
    ///                     подключение);
    /// * TOD_DISCONNECT - указание что соединение разорвано;
    /// * TOD_DATA - непосредственно данные;
    /// * TOD_NOT_CONNECT - нет соединения (обычно, отправляется от сервера
    ///                     клиенту и воркеру при запросе к серверу о создании
    ///                     нового соединения, если таковое не удалось);
    /// * TOD_CONNECT_NOT_FOUND - запрашиваемое соединение не найдено (обычно
    ///                           отправляется сервером клиенту или наоборот
    ///                           при указании что закрываемое соединение
    ///                           не найдено);
    /// * TOD_OTHER - иной тип пакета (зарезервированно и оставлено для
    ///               дальнейшего расширения функциональности);
    /// * TOD_END - окончание перечисления. Не может являться типом пакета.
    ///
    typedef enum {
        TOD_UNKNOWN = 0,
        TOD_NEW_CONNECT,
        TOD_DISCONNECT,
        TOD_DATA,
        TOD_NOT_CONNECT,
        TOD_CONNECT_NOT_FOUND,
        TOD_OTHER,
        TOD_END
    } type_of_data_t;

	///
	///
    ///
	struct data {
        data(void);

        data(direction_t const& _direction,
             type_of_data_t const& _tod,
             int const& _c_sd,
             int const& _s_sd,
             unsigned int const& _buffer_len,
             unsigned char const* _buffer,
             struct sockaddr_in const& _client_addr,
             struct sockaddr_in const& _proxy_addr,
             struct sockaddr_in const& _server_addr);

        data(direction_t const& _direction,
             type_of_data_t const& _tod,
             int const& _c_sd,
             int const& _s_sd,
             unsigned int const& _buffer_len,
             unsigned char const* _buffer,
             struct sockaddr_in const* _client_addr,
             struct sockaddr_in const* _proxy_addr,
             struct sockaddr_in const* _server_addr);

        direction_t direction;
        type_of_data_t tod;
        int c_sd;
        int s_sd;
        unsigned int buffer_len;
        unsigned char buffer[DATA_BUFFER_SIZE];
        struct sockaddr_in client_addr;
        struct sockaddr_in proxy_addr;
        struct sockaddr_in server_addr;
	};

	///
	///
	///
	struct server_routine_arg {
        proxy_impl* _proxy;      // Pointer to proxy_impl class
        int _sc_out_pd;          // S: S->C - write only
        int _cs_in_pd;           // S: C->S - read only
        int _sw_out_pd;          // S: S->W - write only
        int _ws_in_pd;           // S: W->S - read only
	};
	
	///
	///
	///
	struct client_routine_arg {
        proxy_impl* _proxy;      // Pointer to proxy_impl class
        int _cs_out_pd;          // C: C->S - write only
        int _sc_in_pd;           // C: S->C - read only
        int _cw_out_pd;          // C: C->W - write only
        int _wc_in_pd;           // C: W->C - read only
	};

	///
	///
	///
	struct worker_routine_arg {
        proxy_impl* _proxy;       // Pointer to proxy_impl class
        int _ws_out_pd;           // W: W->S - write only
        int _sw_in_pd;            // W: S->W - read only
        int _wc_out_pd;           // W: W->C - write only
        int _cw_in_pd;            // W: C->W - read only
	};

    ///
    ///
    /// RU: Нужен для хранения связки ADDR:PORT/DESCRIPTOR
    struct connect_db_value {
        int sd;
    };

	///
	///
	///
	class Iproxy_impl {
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

		virtual ~Iproxy_impl(void) {}
	};

	///
	///
	///
	class proxy_impl : public Iproxy_impl {
		typedef proxy_impl self;
		
		friend void* server_worker(void*);
		friend void* client_worker(void*);
		friend void* worker_worker(void*);
	public:
		proxy_impl(void);
	
		virtual result_t run(void);

        virtual void set_proxy_port(boost::uint16_t value);
        virtual void set_server_port(boost::uint16_t value);
        virtual void set_server_ip(std::string const& value);
        virtual void set_server_ip(char const* value);
        virtual void set_timeout(boost::int32_t value);
        virtual void set_connect_timeout(boost::int32_t value);
        virtual void set_client_keep_alive(bool value);
        virtual void set_server_keep_alive(bool value);

        virtual boost::uint16_t get_proxy_port(void) const;
        virtual boost::uint16_t get_server_port(void) const;
        virtual std::string const& get_server_ip(void) const;
        virtual boost::int32_t get_timeout(void) const;
        virtual boost::int32_t get_connect_timeout(void) const;
        virtual bool get_client_keep_alive(void) const;
        virtual bool get_server_keep_alive(void) const;

		virtual ~proxy_impl(void);
	protected:
		virtual void server_run(void);
		virtual void client_run(void);
		virtual void worker_run(void);
	private:
        result_t set_nonblock(int fd,
                              std::function<void (int, int)> ferr =
                [](int, int) -> void {}) const;

        void debug_log_info(const data& d, const std::string& who =
                std::string("?")) const;

		static int const SERVER_CLIENT_IN;
		static int const SERVER_CLIENT_OUT;
		static int const CLIENT_SERVER_IN;
		static int const CLIENT_SERVER_OUT;
		static int const SERVER_WORKER_IN;
		static int const SERVER_WORKER_OUT;
		static int const WORKER_SERVER_IN;
		static int const WORKER_SERVER_OUT;
		static int const CLIENT_WORKER_IN;
		static int const CLIENT_WORKER_OUT;
		static int const WORKER_CLIENT_IN;
		static int const WORKER_CLIENT_OUT;

        static boost::uint16_t const DEFAULT_PROXY_PORT;
        static boost::uint16_t const DEFAULT_SERVER_PORT;

        static std::string const DEFAULT_SERVER_IP;

        static boost::int32_t const DEFAULT_CLIENT_POLL_TIMEOUT;
        static boost::int32_t const DEFAULT_SERVER_POLL_TIMEOUT;
        static boost::int32_t const DEFAULT_WORKER_POLL_TIMEOUT;

        static boost::int32_t const DEFAULT_CONNECT_TIMEOUT;

        static bool const DEFAULT_CLIENT_KEEP_ALIVE;
        static bool const DEFAULT_SERVER_KEEP_ALIVE;
		
		result_t s_last_err;
		result_t c_last_err;
		result_t w_last_err;

        std::atomic<bool> end_proxy;

        boost::uint16_t proxy_port;
        boost::uint16_t server_port;

        std::string server_ip;

        boost::int32_t client_poll_timeout;
        boost::int32_t server_poll_timeout;
        boost::int32_t worker_poll_timeout;

        boost::int32_t connect_timeout;

        bool client_keep_alive;
        bool server_keep_alive;

		pthread_t s_thread;
		pthread_t c_thread;
		pthread_t w_thread;
		
		server_routine_arg s_arg;
		client_routine_arg c_arg;
		worker_routine_arg w_arg;

		int pipe_sc_pd[2];
        int pipe_cs_pd[2];
		int pipe_sw_pd[2];
        int pipe_ws_pd[2];
		int pipe_cw_pd[2];
        int pipe_wc_pd[2];

        mutable std::mutex run_mutex;
	};
} // namespace proxy_ns

#endif // __PROXY_IMPL_HPP__

/* *****************************************************************************
 * End of file
 * ************************************************************************** */
