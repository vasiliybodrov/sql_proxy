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

#include "log.hpp"
#include "proxy_result.hpp"

#ifndef POLLING_REQUESTS_SIZE
    #define POLLING_REQUESTS_SIZE 1000
#endif // POLLING_REQUESTS_SIZE

#ifndef DATA_BUFFER_SIZE
    #define DATA_BUFFER_SIZE 1024
#endif // DATA_BUFFER_SIZE

namespace proxy_ns {
    using namespace log_ns;

	class Iproxy_impl;
	class proxy_impl;
    class common_logic_log;

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
        virtual void set_client_tcp_no_delay(bool value) = 0;
        virtual void set_server_tcp_no_delay(bool value) = 0;

        virtual boost::uint16_t get_proxy_port(void) const = 0;
        virtual boost::uint16_t get_server_port(void) const = 0;
        virtual std::string const& get_server_ip(void) const = 0;
        virtual boost::int32_t get_timeout(void) const = 0;
        virtual boost::int32_t get_connect_timeout(void) const = 0;
        virtual bool get_client_keep_alive(void) const = 0;
        virtual bool get_server_keep_alive(void) const = 0;
        virtual bool get_client_tcp_no_delay(void) const = 0;
        virtual bool get_server_tcp_no_delay(void) const = 0;

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

        friend class server_logic;
        friend class client_logic;
        friend class worker_logic;
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
        virtual void set_client_tcp_no_delay(bool value);
        virtual void set_server_tcp_no_delay(bool value);

        virtual boost::uint16_t get_proxy_port(void) const;
        virtual boost::uint16_t get_server_port(void) const;
        virtual std::string const& get_server_ip(void) const;
        virtual boost::int32_t get_timeout(void) const;
        virtual boost::int32_t get_connect_timeout(void) const;
        virtual bool get_client_keep_alive(void) const;
        virtual bool get_server_keep_alive(void) const;
        virtual bool get_client_tcp_no_delay(void) const;
        virtual bool get_server_tcp_no_delay(void) const;

		virtual ~proxy_impl(void);

        static inline void fok_placeholder(int) {}
        static inline void ferr_placeholder(int, int) {}
	protected:
		virtual void server_run(void);
		virtual void client_run(void);
		virtual void worker_run(void);
	private:
        int pipe_create(int* d);
        size_t set_max_pipe_size(int pipe_fd);
        size_t set_max_pipe_size_force(int pipe_fd, size_t new_size);

        size_t get_data_size_in_pipe(int pipe_fd) const;

        bool can_write_to_pipe_universal(int pipe_w_fd,
                                         size_t max_size,
                                         size_t size) const;

        bool can_write_to_pipe(int pipe_w_fd, size_t size) const;

        bool can_write_to_pipe_data(int pipe_w_fd, size_t size) const;

        result_t set_nonblock(int sd,
                              std::function<void (int)> fok =
                [](int) -> void {},
                              std::function<void (int, int)> ferr =
                [](int, int) -> void {}) const;

        result_t set_reuseaddr(int sd, bool val,
                              std::function<void (int)> fok =
                [](int) -> void {},
                              std::function<void (int, int)> ferr =
                [](int, int) -> void {}) const;

        result_t set_keep_alive(int sd, bool val,
                                std::function<void (int)> fok =
                [](int) -> void {},
                                std::function<void (int, int)> ferr =
                [](int, int) -> void {}) const;

        result_t set_tcp_no_delay(int sd, bool val,
                                  std::function<void (int)> fok =
                [](int) -> void {},
                                  std::function<void (int, int)> ferr =
                [](int, int) -> void {}) const;

        result_t get_keep_alive(int sd, int& val,
                                std::function<void (int)> fok =
                [](int) -> void {},
                                std::function<void (int, int)> ferr =
                [](int, int) -> void {}) const;

        result_t get_tcp_no_delay(int sd, int& val,
                                  std::function<void (int)> fok =
                [](int) -> void {},
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

        static bool const DEFAULT_CLIENT_TCP_NO_DELAY;
        static bool const DEFAULT_SERVER_TCP_NO_DELAY;
		
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

        bool client_tcp_no_delay;
        bool server_tcp_no_delay;

		pthread_t s_thread;
		pthread_t c_thread;
		pthread_t w_thread;
		
		server_routine_arg s_arg;
		client_routine_arg c_arg;
		worker_routine_arg w_arg;

        bool use_pipe;

		int pipe_sc_pd[2];
        int pipe_cs_pd[2];
		int pipe_sw_pd[2];
        int pipe_ws_pd[2];
		int pipe_cw_pd[2];
        int pipe_wc_pd[2];

        size_t const pipe_reserved_percent;
        size_t pipe_reserved_size;
        size_t max_pipe_data_size;
        size_t max_pipe_size;
        size_t max_pipe_size_system;

        mutable std::mutex run_mutex;
	};

    ///
    /// \brief The common_logic_log class
    ///
    class common_logic_log {
    public:
        ///
        /// \brief common_logic_log
        /// \param sl
        ///
        explicit common_logic_log(std::string const& prefix = "") :
            _prefix(prefix), _l(log::inst()) {
        }

        ///
        /// \brief ~common_logic_log
        ///
        virtual ~common_logic_log(void) noexcept {
        }

        ///
        /// \brief error_unknown_exception
        /// \param file
        /// \param line
        ///
        void error_unknown_exception(auto file, auto line) {
            this->_l(Ilog::LEVEL_ERROR, [&](auto _file, auto _line)->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": Unknown exception! WTF!? "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line));
        }

        ///
        /// \brief error_inernal_error
        /// \param file
        /// \param line
        ///
        void error_inernal_error(auto file, auto line) {
            this->_l(Ilog::LEVEL_ERROR, [&](auto _file, auto _line)->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": Internal error. WTF!? "
                   << " FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line));
        }

        ///
        /// \brief error_write_failed
        /// \param file
        /// \param line
        /// \param err
        /// \param fd
        ///
        void error_write_failed(auto file, auto line, int err, int fd) {
            this->_l(Ilog::LEVEL_ERROR, [&](auto _file, auto _line,
                                            int _err, int _fd) ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": 'write' failed ("
                   << ::strerror(_err) << ") (fd=" << _fd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, err, fd));
        }

        ///
        /// \brief error_read_failed
        /// \param file
        /// \param line
        /// \param err
        /// \param fd
        ///
        void error_read_failed(auto file, auto line, int err, int fd) {
            this->_l(Ilog::LEVEL_ERROR, [&](auto _file, auto _line,
                                            int _err, int _fd) ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": 'read' failed ("
                   << ::strerror(_err) << ") (fd=" << _fd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, err, fd));
        }

        ///
        /// \brief error_setsockopt_failed
        /// \param file
        /// \param line
        /// \param err
        /// \param fd
        ///
        void error_setsockopt_failed(auto file, auto line, int err, int sd) {
            this->_l(Ilog::LEVEL_ERROR, [&](auto _file, auto _line,
                                            int _err, int _sd) ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": 'setsockopt' failed ("
                   << ::strerror(_err) << ") (sd=" << _sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, err, sd));
        }

        ///
        /// \brief error_getsockopt_failed
        /// \param file
        /// \param line
        /// \param err
        /// \param fd
        ///
        void error_getsockopt_failed(auto file, auto line, int err, int sd) {
            this->_l(Ilog::LEVEL_ERROR, [&](auto _file, auto _line,
                                            int _err, int _sd) ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": 'getsockopt' failed ("
                   << ::strerror(_err) << ") (sd=" << _sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, err, sd));
        }

        ///
        /// \brief error_socket_failed
        /// \param file
        /// \param line
        /// \param err
        /// \param fd
        ///
        void error_socket_failed(auto file, auto line, int err, int sd) {
            this->_l(Ilog::LEVEL_ERROR, [&](auto _file, auto _line,
                                            int _err, int _sd) ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": 'socket' failed ("
                   << ::strerror(_err) << ") (sd=" << _sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, err, sd));
        }

        ///
        /// \brief error_connect_failed
        /// \param file
        /// \param line
        /// \param err
        /// \param fd
        ///
        void error_connect_failed(auto file, auto line, int err, int sd) {
            this->_l(Ilog::LEVEL_ERROR, [&](auto _file, auto _line,
                                            int _err, int _sd) ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": 'connect' failed ("
                   << ::strerror(_err) << ") (sd=" << _sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, err, sd));
        }

        ///
        /// \brief error_send_failed
        /// \param file
        /// \param line
        /// \param err
        /// \param fd
        ///
        void error_send_failed(auto file, auto line, int err, int sd) {
            this->_l(Ilog::LEVEL_ERROR, [&](auto _file, auto _line,
                                            int _err, int _sd) ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": 'send' failed ("
                   << ::strerror(_err) << ") (sd=" << _sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, err, sd));
        }

        ///
        /// \brief error_recv_failed
        /// \param file
        /// \param line
        /// \param err
        /// \param fd
        ///
        void error_recv_failed(auto file, auto line, int err, int sd) {
            this->_l(Ilog::LEVEL_ERROR, [&](auto _file, auto _line,
                                            int _err, int _sd) ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": 'recv' failed ("
                   << ::strerror(_err) << ") (sd=" << _sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, err, sd));
        }

        ///
        /// \brief error_poll_failed
        /// \param file
        /// \param line
        /// \param err
        ///
        void error_poll_failed(auto file, auto line, int err) {
            this->_l(Ilog::LEVEL_ERROR, [&](auto _file, auto _line,
                                            int _err) ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": 'poll' failed ("
                   << ::strerror(_err) << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, err));
        }

        ///
        /// \brief error_getsockname_failed
        /// \param file
        /// \param line
        /// \param err
        /// \param sd
        ///
        void error_getsockname_failed(auto file, auto line, int err, int sd) {
            this->_l(Ilog::LEVEL_ERROR, [&](auto _file, auto _line,
                                            int _err, int _sd) ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": 'getsockname' failed ("
                   << ::strerror(_err) << ") (sd=" << _sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, err, sd));
        }

        ///
        /// \brief error_ioctl_failed
        /// \param file
        /// \param line
        /// \param err
        /// \param sd
        ///
        void error_ioctl_fionread_failed(auto file, auto line,
                                         int err, int sd) {
            this->_l(Ilog::LEVEL_ERROR, [&](auto _file, auto _line,
                                            int _err, int _sd) ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": 'ioctl' with FIONREAD failed ("
                   << ::strerror(_err) << ") (sd=" << _sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, err, sd));
        }

        ///
        /// \brief error_ioctl_or_fcntl_failed
        /// \param file
        /// \param line
        /// \param err
        /// \param sd
        ///
        void error_ioctl_or_fcntl_failed(auto file, auto line,
                                         int err, int sd) {
            this->_l(Ilog::LEVEL_ERROR, [&](auto _file, auto _line,
                                            int _err, int _sd) ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": 'ioctl' or 'fcntl' failed ("
                   << ::strerror(_err) << ") (sd=" << _sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, err, sd));
        }

        ///
        /// \brief error_bind_failed
        /// \param file
        /// \param line
        /// \param err
        /// \param sd
        ///
        void error_bind_failed(auto file, auto line,
                               int err, int sd) {
            this->_l(Ilog::LEVEL_ERROR, [&](auto _file, auto _line,
                                            int _err, int _sd) ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": 'bind' failed ("
                   << ::strerror(_err) << ") (sd=" << _sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, err, sd));
        }

        ///
        /// \brief error_listen_failed
        /// \param file
        /// \param line
        /// \param err
        /// \param sd
        ///
        void error_listen_failed(auto file, auto line,
                                 int err, int sd) {
            this->_l(Ilog::LEVEL_ERROR, [&](auto _file, auto _line,
                                            int _err, int _sd) ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": 'listen' failed ("
                   << ::strerror(_err) << ") (sd=" << _sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, err, sd));
        }

        ///
        /// \brief error_accept_failed
        /// \param file
        /// \param line
        /// \param err
        /// \param sd
        ///
        void error_accept_failed(auto file, auto line,
                                 int err, int sd) {
            this->_l(Ilog::LEVEL_ERROR, [&](auto _file, auto _line,
                                            int _err, int _sd) ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": 'accept' failed ("
                   << ::strerror(_err) << ") (sd=" << _sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, err, sd));
        }

        ///
        /// \brief debug_keep_alive_onoff
        /// \param file
        /// \param line
        /// \param val
        ///
        void debug_keep_alive_onoff(auto file, auto line, bool val, int sd) {
            this->_l(Ilog::LEVEL_DEBUG, [&](auto _file, auto _line,
                                            bool _val, int _sd)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": SO_KEEPALIVE is "
                   << (_val ? "ON" : "OFF")
                   << " on socket=" << _sd << ". "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, val, sd));
        }

        ///
        /// \brief debug_tcp_no_delay_onoff
        /// \param file
        /// \param line
        /// \param val
        ///
        void debug_tcp_no_delay_onoff(auto file, auto line, bool val, int sd) {
            this->_l(Ilog::LEVEL_DEBUG, [&](auto _file, auto _line,
                                            bool _val, int _sd)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": TCP_NODELAY is "
                   << (_val ? "ON" : "OFF")
                   << " on socket=" << _sd << ". "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, val, sd));
        }

        ///
        /// \brief debug_connect_take_time
        /// \param file
        /// \param line
        /// \param sd
        ///
        void debug_connect_take_time(auto file, auto line, int sd) {
            this->_l(Ilog::LEVEL_DEBUG, [&](auto _file, auto _line, int _sd)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": The connection takes time... "
                   << "(socket=" << _sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, sd));
        }

        ///
        /// \brief info_connect_immediately
        /// \param file
        /// \param line
        /// \param sd
        ///
        void info_connect_immediately(auto file, auto line, int sd) {
            this->_l(Ilog::LEVEL_INFO, [&](auto _file, auto _line, int _sd)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": The connection was immediately "
                   << "(socket=" << _sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, sd));
        }

        ///
        /// \brief info_connect_successful
        /// \param file
        /// \param line
        /// \param sd
        ///
        void info_connect_successful(auto file, auto line, int sd) {
            this->_l(Ilog::LEVEL_INFO, [&](auto _file, auto _line, int _sd)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": Connection was successful "
                   << "(socket=" << _sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, sd));
        }

        ///
        /// \brief info_connect_close
        /// \param file
        /// \param line
        /// \param sd
        ///
        void info_connect_close(auto file, auto line, int sd,
                                boost::uint32_t count_sent,
                                boost::uint32_t count_recv,
                                boost::uint32_t count_buffered,
                                boost::uint32_t count_lost) {
            this->_l(Ilog::LEVEL_INFO, [&](auto _file, auto _line, int _sd,
                     boost::uint32_t _count_sent,
                     boost::uint32_t _count_recv,
                     boost::uint32_t _count_buffered,
                     boost::uint32_t _count_lost)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": Connection closed "
                   << "(socket=" << _sd << "). "
                   << "Stat: "
                   << "Sent=" << _count_sent << "; "
                   << "Recv=" << _count_recv << "; "
                   << "Buf=" << _count_buffered << "; "
                   << "Lost=" << _count_lost << ". "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, sd,
              count_sent, count_recv, count_buffered, count_lost));
        }

        ///
        /// \brief debug_unsupported_data_client
        /// \param file
        /// \param line
        /// \param tod
        ///
        void debug_unsupported_data_client(auto file, auto line,
                                           type_of_data_t tod) {
            this->_l(Ilog::LEVEL_INFO, [&](auto _file, auto _line, auto _tod)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": Unsupperted data from client. (TOD="
                   << _tod << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, tod));
        }

        ///
        /// \brief debug_unsupported_data_server
        /// \param file
        /// \param line
        /// \param tod
        ///
        void debug_unsupported_data_server(auto file, auto line,
                                           type_of_data_t tod) {
            this->_l(Ilog::LEVEL_INFO, [&](auto _file, auto _line, auto _tod)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": Unsupperted data from server. (TOD="
                   << _tod << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, tod));
        }

        ///
        /// \brief debug_signal_client_connect_not_found
        /// \param file
        /// \param line
        /// \param c_sd
        /// \param s_sd
        ///
        void debug_signal_client_connect_not_found(auto file, auto line,
                                                   int c_sd, int s_sd) {
            this->_l(Ilog::LEVEL_DEBUG, [&](auto _file, auto _line,
                                            auto _c_sd, auto _s_sd)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": Signal from client 'connect not "
                   << "found' (c_sd=" << _c_sd << "; s_sd=" << _s_sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, c_sd, s_sd));
        }

        ///
        /// \brief debug_signal_client_disconnect
        /// \param file
        /// \param line
        /// \param c_sd
        /// \param s_sd
        ///
        void debug_signal_client_disconnect(auto file, auto line,
                                            int c_sd, int s_sd) {
            this->_l(Ilog::LEVEL_DEBUG, [&](auto _file, auto _line,
                                            auto _c_sd, auto _s_sd)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": Signal from client 'disconnect'"
                   << " (c_sd=" << _c_sd << "; s_sd=" << _s_sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, c_sd, s_sd));
        }

        ///
        /// \brief debug_signal_server_connect_not_found
        /// \param file
        /// \param line
        /// \param c_sd
        /// \param s_sd
        ///
        void debug_signal_server_connect_not_found(auto file, auto line,
                                                   int c_sd, int s_sd) {
            this->_l(Ilog::LEVEL_DEBUG, [&](auto _file, auto _line,
                                            auto _c_sd, auto _s_sd)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": Signal from server 'connect not "
                   << "found' (c_sd=" << _c_sd << "; s_sd=" << _s_sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, c_sd, s_sd));
        }

        ///
        /// \brief debug_signal_server_disconnect
        /// \param file
        /// \param line
        /// \param c_sd
        /// \param s_sd
        ///
        void debug_signal_server_disconnect(auto file, auto line,
                                            int c_sd, int s_sd) {
            this->_l(Ilog::LEVEL_DEBUG, [&](auto _file, auto _line,
                                            auto _c_sd, auto _s_sd)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": Signal from server 'disconnect'"
                   << " (c_sd=" << _c_sd << "; s_sd=" << _s_sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, c_sd, s_sd));
        }

        ///
        /// \brief debug_signal_server_not_connect
        /// \param file
        /// \param line
        /// \param c_sd
        /// \param s_sd
        ///
        void debug_signal_server_not_connect(auto file, auto line,
                                             int c_sd, int s_sd) {
            this->_l(Ilog::LEVEL_DEBUG, [&](auto _file, auto _line,
                                            auto _c_sd, auto _s_sd)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": Signal from server 'not connect'"
                   << " (c_sd=" << _c_sd << "; s_sd=" << _s_sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, c_sd, s_sd));
        }

        ///
        /// \brief debug_revent_includes_pollhup
        /// \param file
        /// \param line
        /// \param descriptor
        ///
        void debug_revent_includes_pollhup(auto file, auto line,
                                           int descriptor) {
            this->_l(Ilog::LEVEL_DEBUG, [&](auto _file,
                                            auto _line,
                                            auto _descriptor)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": Revent includes POLLHUP ("
                   << "descriptor=" << _descriptor << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, descriptor));
        }

        ///
        /// \brief debug_revent_includes_pollerr
        /// \param file
        /// \param line
        /// \param descriptor
        ///
        void debug_revent_includes_pollerr(auto file, auto line,
                                           int descriptor) {
            this->_l(Ilog::LEVEL_DEBUG, [&](auto _file,
                                            auto _line,
                                            auto _descriptor)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": Revent includes POLLERR ("
                   << "descriptor=" << _descriptor << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, descriptor));
        }

        ///
        /// \brief debug_revent_includes_pollnval
        /// \param file
        /// \param line
        /// \param descriptor
        ///
        void debug_revent_includes_pollnval(auto file, auto line,
                                           int descriptor) {
            this->_l(Ilog::LEVEL_DEBUG, [&](auto _file,
                                            auto _line,
                                            auto _descriptor)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": Revent includes POLLNVAL ("
                   << "descriptor=" << _descriptor << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, descriptor));
        }

        ///
        /// \brief info_server_not_respond
        /// \param file
        /// \param line
        /// \param err
        /// \param error
        ///
        void info_server_not_respond(auto file, auto line, int err,
                                     int error, int sd) {
            this->_l(Ilog::LEVEL_DEBUG, [&](auto _file, auto _line,
                                     auto _err, auto _error, auto _sd)
              ->std::string {
                boost::ignore_unused(_error);
                std::stringstream ss;
                ss << this->_prefix << ": The server does not respond. "
                   << "Connection failed ("
                   << ((_err < 0) ? ::strerror(_err) : "error != 0") << "). "
                   << "(socket=" << _sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, err, error, sd));
        }

        ///
        /// \brief debug_listen_socket_readable
        /// \param file
        /// \param line
        /// \param sd
        ///
        void debug_listen_socket_readable(auto file, auto line, int sd) {
            this->_l(Ilog::LEVEL_DEBUG, [&](auto _file,
                                            auto _line,
                                            auto _sd)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": Listening socket is readable ("
                   << "socket=" << _sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, sd));
        }

        ///
        /// \brief info_new_incoming_connection
        /// \param file
        /// \param line
        /// \param sd
        /// \param addr
        /// \param p
        ///
        void info_new_incoming_connection(auto file, auto line, int sd,
                                          char const* addr, boost::uint16_t p) {
            this->_l(Ilog::LEVEL_INFO, [&](auto _file,
                                           auto _line,
                                           auto _sd,
                                           auto _addr,
                                           auto _p)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ":  New incoming connection("
                   << "socket=" << _sd << "; "
                   << _addr << ":" << _p << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, sd, addr, p));
        }

        ///
        /// \brief error_unknown_socket_descriptor
        /// \param file
        /// \param line
        /// \param sd
        ///
        void error_unknown_socket_descriptor(auto file, auto line, int sd) {
            this->_l(Ilog::LEVEL_ERROR, [&](auto _file,
                                            auto _line,
                                            auto _sd)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": Unknown socket descriptor ("
                   << "socket=" << _sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, sd));
        }

        ///
        /// \brief info_connection_closed
        /// \param file
        /// \param line
        /// \param sd
        ///
        void info_connection_closed(auto file, auto line, int sd) {
            this->_l(Ilog::LEVEL_INFO, [&](auto _file,
                                           auto _line,
                                           auto _sd)
              ->std::string {
                std::stringstream ss;
                ss << this->_prefix << ": Connection closed ("
                   << "socket=" << _sd << "). "
                   << "FILE:" << _file << ":" << _line << ".";
                return ss.str();
            }(file, line, sd));
        }
    private:
        std::string const _prefix;
        log_ns::log& _l;
    };
} // namespace proxy_ns

#endif // __PROXY_IMPL_HPP__

/* *****************************************************************************
 * End of file
 * ************************************************************************** */
