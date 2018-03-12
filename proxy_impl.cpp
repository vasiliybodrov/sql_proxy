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

/*
 * NOTE (EN): This file includes the code in pure C-style!
 * NOTE (RU): Этот файл содержит код в стиле языка Си!
 * -----------------------------------------------------------------------------
 * NOTE (EN):
 * NOTE (RU):
 *   КЛИЕНТ - обслуживает подключения пользователей;
 *   СЕРВЕР - обслуживает подключения к серверу СУБД;
 *   ВОРКЕР - обслуживает обработку данных и их логирование.
 * -----------------------------------------------------------------------------
 */

#include <array>
#include <vector>
#include <numeric>
#include <algorithm>
#include <iterator>
#include <sstream>
#include <iostream>
#include <fstream>
#include <mutex>
#include <cerrno>
#include <cstring>
#include <cassert>
#include <ios>
#include <iomanip>
#include <functional>

#include <boost/make_shared.hpp>
#include <boost/cstdint.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#ifdef USE_FULL_DEBUG
//#include <boost/stacktrace.hpp> // TODO: Доделать бэктрейс на буст
#endif // USE_FULL_DEBUG

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#include "log.hpp"
#include "proxy_result.hpp"
#include "proxy.hpp"
//#include "proxy_impl.hpp" // Instead of "proxy_impl.hpp" we use "proxy.hpp".

#ifndef __USER_DEFAULT_PROXY_PORT
    #define __USER_DEFAULT_PROXY_PORT 4880
#endif // __USER_DEFAULT_PROXY_PORT

#ifndef __USER_DEFAULT_SERVER_PORT
    //#define __USER_DEFAULT_SERVER_PORT 3306
    #define __USER_DEFAULT_SERVER_PORT 5555
#endif // __USER_DEFAULT_SERVER_PORT

#ifndef __USER_DEFAULT_SERVER_IP
    #define __USER_DEFAULT_SERVER_IP "127.0.0.1"
#endif // __USER_DEFAULT_SERVER_IP

#ifndef __USER_DEFAULT_CLIENT_POLL_TIMEOUT
    #define __USER_DEFAULT_CLIENT_POLL_TIMEOUT 1000
#endif // __USER_DEFAULT_CLIENT_POLL_TIMEOUT

#ifndef __USER_DEFAULT_SERVER_POLL_TIMEOUT
    #define __USER_DEFAULT_SERVER_POLL_TIMEOUT 1000
#endif // __USER_DEFAULT_SERVER_POLL_TIMEOUT

#ifndef __USER_DEFAULT_WORKER_POLL_TIMEOUT
    #define __USER_DEFAULT_WORKER_POLL_TIMEOUT 1000
#endif // __USER_DEFAULT_WORKER_POLL_TIMEOUT

#ifndef __USER_DEFAULT_CONNECT_TIMEOUT
    #define __USER_DEFAULT_CONNECT_TIMEOUT 3000
#endif // __USER_DEFAULT_CONNECT_TIMEOUT

#ifndef __USER_DEFAULT_CLIENT_KEEP_ALIVE
#define __USER_DEFAULT_CLIENT_KEEP_ALIVE 0
#endif // __USER_DEFAULT_CLIENT_KEEP_ALIVE

#ifndef __USER_DEFAULT_SERVER_KEEP_ALIVE
#define __USER_DEFAULT_SERVER_KEEP_ALIVE 0
#endif // __USER_DEFAULT_SERVER_KEEP_ALIVE

#ifndef __USER_DEFAULT_CLIENT_TCP_NO_DELAY
#define __USER_DEFAULT_CLIENT_TCP_NO_DELAY 0
#endif // __USER_DEFAULT_CLIENT_TCP_NO_DELAY

#ifndef __USER_DEFAULT_SERVER_TCP_NO_DELAY
#define __USER_DEFAULT_SERVER_TCP_NO_DELAY 0
#endif // __USER_DEFAULT_SERVER_TCP_NO_DELAY

namespace proxy_ns {
    namespace {
        size_t __set_max_pipe_size_helper(int fd,
                                          size_t cur_size,
                                          size_t set_size,
                                          size_t min_size,
                                          size_t step) {
            size_t new_cur_size = set_size;
            int rc = 0;

            if(set_size < min_size) {
                return cur_size;
            }

            rc = ::fcntl(fd, F_SETPIPE_SZ, new_cur_size);
            if(rc >= 0) {
                return new_cur_size;
            }
            else {
                std::cout << strerror(errno) << std::endl;
            }

            return __set_max_pipe_size_helper(fd,
                                              cur_size,
                                              new_cur_size - step,
                                              min_size,
                                              step);
        }
    }

	using namespace log_ns;

    extern void* client_worker(void* arg);
    extern void* server_worker(void* arg);
    extern void* worker_worker(void* arg);

    /*
     * NOTE (EN):
     * A to B (A -> B)
     * E.g.: SERVER_CLIENT = The SERVER works with CLIENT.
     *
     * NOTE (RU):
     * От A к B (A -> B)
     * Для примера: SERVER_CLIENT = SERVER взаимодействует с CLIENT.
     */
	int const proxy_impl::SERVER_CLIENT_IN  = 0;
	int const proxy_impl::SERVER_CLIENT_OUT = 1;
	int const proxy_impl::CLIENT_SERVER_IN  = 0;
	int const proxy_impl::CLIENT_SERVER_OUT = 1;
	int const proxy_impl::SERVER_WORKER_IN  = 0;
	int const proxy_impl::SERVER_WORKER_OUT = 1;
	int const proxy_impl::WORKER_SERVER_IN  = 0;
	int const proxy_impl::WORKER_SERVER_OUT = 1;
	int const proxy_impl::CLIENT_WORKER_IN  = 0;
	int const proxy_impl::CLIENT_WORKER_OUT = 1;
	int const proxy_impl::WORKER_CLIENT_IN  = 0;
	int const proxy_impl::WORKER_CLIENT_OUT = 1;

    boost::uint16_t const proxy_impl::DEFAULT_PROXY_PORT =
            __USER_DEFAULT_PROXY_PORT;

    boost::uint16_t const proxy_impl::DEFAULT_SERVER_PORT =
            __USER_DEFAULT_SERVER_PORT;

    std::string const proxy_impl::DEFAULT_SERVER_IP =
            __USER_DEFAULT_SERVER_IP;

    boost::int32_t const proxy_impl::DEFAULT_CLIENT_POLL_TIMEOUT =
            __USER_DEFAULT_CLIENT_POLL_TIMEOUT;

    boost::int32_t const proxy_impl::DEFAULT_SERVER_POLL_TIMEOUT =
            __USER_DEFAULT_SERVER_POLL_TIMEOUT;

    boost::int32_t const proxy_impl::DEFAULT_WORKER_POLL_TIMEOUT =
            __USER_DEFAULT_WORKER_POLL_TIMEOUT;

    boost::int32_t const proxy_impl::DEFAULT_CONNECT_TIMEOUT =
            __USER_DEFAULT_CONNECT_TIMEOUT;

    bool const proxy_impl::DEFAULT_CLIENT_KEEP_ALIVE =
            __USER_DEFAULT_CLIENT_KEEP_ALIVE;

    bool const proxy_impl::DEFAULT_SERVER_KEEP_ALIVE =
            __USER_DEFAULT_SERVER_KEEP_ALIVE;

    bool const proxy_impl::DEFAULT_CLIENT_TCP_NO_DELAY =
            __USER_DEFAULT_CLIENT_TCP_NO_DELAY;

    bool const proxy_impl::DEFAULT_SERVER_TCP_NO_DELAY =
            __USER_DEFAULT_SERVER_TCP_NO_DELAY;

    data::data(void) {
        this->direction = DIRECTION_UNKNOWN;
        this->tod = TOD_UNKNOWN;

        this->c_sd = -1;
        this->s_sd = -1;

        this->buffer_len = 0;

        std::fill_n(reinterpret_cast<char*>(this->buffer),
                    sizeof(this->buffer), '\0');

        std::fill_n(reinterpret_cast<char*>(&this->client_addr),
                    sizeof(this->client_addr), '\0');
        std::fill_n(reinterpret_cast<char*>(&this->proxy_addr),
                    sizeof(this->proxy_addr), '\0');
        std::fill_n(reinterpret_cast<char*>(&this->server_addr),
                    sizeof(this->server_addr), '\0');
    }

    data::data(direction_t const& _direction,
               type_of_data_t const& _tod,
               int const& _c_sd,
               int const& _s_sd,
               unsigned int const& _buffer_len,
               unsigned char const* _buffer,
               struct sockaddr_in const& _client_addr,
               struct sockaddr_in const& _proxy_addr,
               struct sockaddr_in const& _server_addr) :
        data(_direction,
             _tod,
             _c_sd,
             _s_sd,
             _buffer_len,
             _buffer,
             &_client_addr,
             &_proxy_addr,
             &_server_addr) {}

    data::data(direction_t const& _direction,
               type_of_data_t const& _tod,
               int const& _c_sd,
               int const& _s_sd,
               unsigned int const& _buffer_len,
               unsigned char const* _buffer,
               struct sockaddr_in const* _client_addr,
               struct sockaddr_in const* _proxy_addr,
               struct sockaddr_in const* _server_addr) :
        direction(_direction),
        tod(_tod),
        c_sd(_c_sd),
        s_sd(_s_sd),
        buffer_len(_buffer_len) {

#ifdef USE_FULL_DEBUG
        if(!((_buffer == nullptr && _buffer_len == 0) ||
             (_buffer != nullptr && _buffer_len != 0))) {

            log& l = log::inst();

            l(Ilog::LEVEL_DEBUG, [&]()->std::string {
                  std::stringstream ss;

                  ss << "'data::data' failed! "
                     << "_buffer: " << ((_buffer) ? "not null" : "null") << "; "
                     << "_buffer_len: " << _buffer_len << ".";

                  return ss.str();
              }());
        }
#endif // USE_FULL_DEBUG

        assert((_buffer == nullptr && _buffer_len == 0) ||
               (_buffer != nullptr && _buffer_len != 0));

        if(nullptr == _buffer) {
            std::fill_n(reinterpret_cast<char*>(this->buffer),
                        sizeof(this->buffer), '\0');
        }
        else {
            std::copy(reinterpret_cast<char const*>(_buffer),
                      reinterpret_cast<char const*>(_buffer) +
                        _buffer_len,
                      reinterpret_cast<char*>(&this->buffer));
        }

        if(nullptr == _client_addr) {
            std::fill_n(reinterpret_cast<char*>(&this->client_addr),
                        sizeof(this->client_addr), '\0');
        }
        else {
            std::copy(reinterpret_cast<char const*>(_client_addr),
                      reinterpret_cast<char const*>(_client_addr),
                      reinterpret_cast<char*>(&this->client_addr));
        }

        if(nullptr == _proxy_addr) {
            std::fill_n(reinterpret_cast<char*>(&this->proxy_addr),
                        sizeof(this->proxy_addr), '\0');
        }
        else {
            std::copy(reinterpret_cast<char const*>(_proxy_addr),
                      reinterpret_cast<char const*>(_proxy_addr) +
                        sizeof(this->proxy_addr),
                    reinterpret_cast<char*>(&this->proxy_addr));
        }

        if(nullptr == _server_addr) {
            std::fill_n(reinterpret_cast<char*>(&this->server_addr),
                        sizeof(this->server_addr), '\0');
        }
        else {
            std::copy(reinterpret_cast<char const*>(_server_addr),
                      reinterpret_cast<char const*>(_server_addr) +
                        sizeof(this->server_addr),
                    reinterpret_cast<char*>(&this->server_addr));
        }
    }

    ///
    /// \brief proxy_impl::proxy_impl
    ///
        proxy_impl::proxy_impl(void) :
		Iproxy_impl(),
		s_last_err(RES_CODE_UNKNOWN),
		c_last_err(RES_CODE_UNKNOWN),
		w_last_err(RES_CODE_UNKNOWN),
		end_proxy(false),
        proxy_port(self::DEFAULT_PROXY_PORT),
        server_port(self::DEFAULT_SERVER_PORT),
        server_ip(self::DEFAULT_SERVER_IP),
        client_poll_timeout(self::DEFAULT_CLIENT_POLL_TIMEOUT),
        server_poll_timeout(self::DEFAULT_SERVER_POLL_TIMEOUT),
        worker_poll_timeout(self::DEFAULT_WORKER_POLL_TIMEOUT),
        connect_timeout(self::DEFAULT_CONNECT_TIMEOUT),
        client_keep_alive(self::DEFAULT_CLIENT_KEEP_ALIVE),
        server_keep_alive(self::DEFAULT_SERVER_KEEP_ALIVE),
        client_tcp_no_delay(self::DEFAULT_CLIENT_TCP_NO_DELAY),
        server_tcp_no_delay(self::DEFAULT_SERVER_TCP_NO_DELAY),
        s_thread(0),
        c_thread(0),
        w_thread(0),
        use_pipe(0),
        pipe_reserved_percent(50),
        pipe_reserved_size(0),
        max_pipe_data_size(0),
        max_pipe_size(0),
        max_pipe_size_system(0) {

        std::function<size_t(std::string const&)> f_get_max_system_size =
                [](std::string const& procfilename) -> size_t {
            size_t res = 0;
            struct utsname name;
            int rc = 0;

            rc = ::uname(&name);
            if(rc < 0) {
            }
            else {
                /*
                 * TODO: RU: Механизм определения версии ядра Linux, с целью
                 * определения файла, из которого считывать значние не доделан.
                 * В дальнейшем, необходимо реализовать.
                std::string release = name.release;
                std::string delimiter1 = "-";
                std::string kernel_version =
                    release.substr(0, release.find(delimiter1));

                std::vector<std::string> vec_krnl_ver;
                boost::split(vec_krnl_ver, kernel_version, [](char c) {
                    return (c == '.');
                });

                std::for_each(vec_krnl_ver.begin(), vec_krnl_ver.end(),
                              [](auto s) {
                    std::cout << s << std::endl;
                });
                */

                std::ifstream procfile (procfilename);
                std::string line;
                if(procfile.is_open()) {
                    std::getline(procfile, line);
                    procfile.close();

                    try {
                        res = boost::lexical_cast<size_t>(line);
                    }
                    catch(boost::bad_lexical_cast const&) {
                        res = 0;
                    }
                    catch(...) {
                        res = 0;
                    }
                }
            }
            return res;
        };

        // TODO: для сокетов получать максимальное значение действительно,
        //       а затем пытаться выставить это значние.
        //       Сейчас берётся значение по-умолчанию и оно считается
        //       максимальным.
        this->max_pipe_size_system = (this->use_pipe) ?
            f_get_max_system_size("/proc/sys/fs/pipe-max-size") :
                std::min(
                    f_get_max_system_size("/proc/sys/net/core/rmem_default"),
                    f_get_max_system_size("/proc/sys/net/core/wmem_default"));
	}
	
    ///
    /// \brief proxy_impl::run
    /// \return
    ///
	result_t proxy_impl::run(void) {
        std::lock_guard<std::mutex> lock(this->run_mutex);

        log_ns::log& l = log_ns::log::inst();
		
        size_t res_pipe_size = 0;
		int rc = 0;

        rc = this->pipe_create(this->pipe_sc_pd);
		if(rc < 0) {
			l(Ilog::LEVEL_ERROR, "'pipe' error");
		}

        rc = this->pipe_create(this->pipe_cs_pd);
        if(rc < 0) {
            l(Ilog::LEVEL_ERROR, "'pipe' error");
        }

        rc = this->pipe_create(this->pipe_sw_pd);
		if(rc < 0) {
			l(Ilog::LEVEL_ERROR, "'pipe' error");
		}

        rc = this->pipe_create(this->pipe_ws_pd);
        if(rc < 0) {
            l(Ilog::LEVEL_ERROR, "'pipe' error");
        }

        rc = this->pipe_create(this->pipe_cw_pd);
		if(rc < 0) {
			l(Ilog::LEVEL_ERROR, "'pipe' error");
		}

        rc = this->pipe_create(this->pipe_wc_pd);
        if(rc < 0) {
            l(Ilog::LEVEL_ERROR, "'pipe' error");
        }
		
        if(this->use_pipe) {
            res_pipe_size = this->set_max_pipe_size(this->pipe_sc_pd[1]);
            l(Ilog::LEVEL_DEBUG,
              std::string("Pipe buffer size (pipe_sc_pd): ") +
              std::to_string(res_pipe_size));
        }
        else {
            res_pipe_size = this->set_max_pipe_size(this->pipe_sc_pd[0]);
            l(Ilog::LEVEL_DEBUG,
              std::string("Pipe buffer size (pipe_sc_pd[0]): ") +
              std::to_string(res_pipe_size));

            res_pipe_size = this->set_max_pipe_size(this->pipe_sc_pd[1]);
            l(Ilog::LEVEL_DEBUG,
              std::string("Pipe buffer size (pipe_sc_pd[1]): ") +
              std::to_string(res_pipe_size));
        }

        if(this->use_pipe) {
            res_pipe_size = this->set_max_pipe_size(this->pipe_cs_pd[1]);
            l(Ilog::LEVEL_DEBUG,
              std::string("Pipe buffer size (pipe_cs_pd): ") +
              std::to_string(res_pipe_size));
        }
        else {
            res_pipe_size = this->set_max_pipe_size(this->pipe_cs_pd[0]);
            l(Ilog::LEVEL_DEBUG,
              std::string("Pipe buffer size (pipe_cs_pd[0]): ") +
              std::to_string(res_pipe_size));

            res_pipe_size = this->set_max_pipe_size(this->pipe_cs_pd[1]);
            l(Ilog::LEVEL_DEBUG,
              std::string("Pipe buffer size (pipe_cs_pd[1]): ") +
              std::to_string(res_pipe_size));
        }

        if(this->use_pipe) {
            res_pipe_size = this->set_max_pipe_size(this->pipe_sw_pd[1]);
            l(Ilog::LEVEL_DEBUG,
              std::string("Pipe buffer size (pipe_sw_pd): ") +
              std::to_string(res_pipe_size));
        }
        else {
            res_pipe_size = this->set_max_pipe_size(this->pipe_sw_pd[0]);
            l(Ilog::LEVEL_DEBUG,
              std::string("Pipe buffer size (pipe_sw_pd[0]): ") +
              std::to_string(res_pipe_size));

            res_pipe_size = this->set_max_pipe_size(this->pipe_sw_pd[1]);
            l(Ilog::LEVEL_DEBUG,
              std::string("Pipe buffer size (pipe_sw_pd[1]): ") +
              std::to_string(res_pipe_size));
        }

        if(this->use_pipe) {
            res_pipe_size = this->set_max_pipe_size(this->pipe_ws_pd[1]);
            l(Ilog::LEVEL_DEBUG,
              std::string("Pipe buffer size (pipe_ws_pd): ") +
              std::to_string(res_pipe_size));
        }
        else {
            res_pipe_size = this->set_max_pipe_size(this->pipe_ws_pd[0]);
            l(Ilog::LEVEL_DEBUG,
              std::string("Pipe buffer size (pipe_ws_pd[0]): ") +
              std::to_string(res_pipe_size));

            res_pipe_size = this->set_max_pipe_size(this->pipe_ws_pd[1]);
            l(Ilog::LEVEL_DEBUG,
              std::string("Pipe buffer size (pipe_ws_pd[1]): ") +
              std::to_string(res_pipe_size));
        }

        if(this->use_pipe) {
            res_pipe_size = this->set_max_pipe_size(this->pipe_cw_pd[1]);
            l(Ilog::LEVEL_DEBUG,
              std::string("Pipe buffer size (pipe_cw_pd): ") +
              std::to_string(res_pipe_size));
        }
        else {
            res_pipe_size = this->set_max_pipe_size(this->pipe_cw_pd[0]);
            l(Ilog::LEVEL_DEBUG,
              std::string("Pipe buffer size (pipe_cw_pd[0]): ") +
              std::to_string(res_pipe_size));

            res_pipe_size = this->set_max_pipe_size(this->pipe_cw_pd[1]);
            l(Ilog::LEVEL_DEBUG,
              std::string("Pipe buffer size (pipe_cw_pd[1]): ") +
              std::to_string(res_pipe_size));
        }

        if(this->use_pipe) {
            res_pipe_size = this->set_max_pipe_size(this->pipe_wc_pd[1]);
            l(Ilog::LEVEL_DEBUG,
              std::string("Pipe buffer size (pipe_wc_pd): ") +
              std::to_string(res_pipe_size));
        }
        else {
            res_pipe_size = this->set_max_pipe_size(this->pipe_wc_pd[0]);
            l(Ilog::LEVEL_DEBUG,
              std::string("Pipe buffer size (pipe_wc_pd[0]): ") +
              std::to_string(res_pipe_size));

            res_pipe_size = this->set_max_pipe_size(this->pipe_wc_pd[1]);
            l(Ilog::LEVEL_DEBUG,
              std::string("Pipe buffer size (pipe_wc_pd[1]): ") +
              std::to_string(res_pipe_size));
        }

        l(Ilog::LEVEL_DEBUG,
          std::string("Pipe buffer size (max system [bytes]): ") +
          std::to_string(this->max_pipe_size_system));

        l(Ilog::LEVEL_DEBUG,
          std::string("Pipe buffer size (reserved [percent]): ") +
          std::to_string(this->pipe_reserved_percent));

        l(Ilog::LEVEL_DEBUG,
          std::string("Pipe buffer size (reserved [bytes]): ") +
          std::to_string(this->pipe_reserved_size));

        l(Ilog::LEVEL_DEBUG,
          std::string("Pipe buffer size (max current [bytes]): ") +
          std::to_string(this->max_pipe_size));

        l(Ilog::LEVEL_DEBUG,
          std::string("Pipe buffer size (max for data [bytes]): ") +
          std::to_string(this->max_pipe_data_size));

		this->server_run();
		this->client_run();
		this->worker_run();

		rc = ::pthread_join(this->s_thread, nullptr);
		if(!rc) {
			l(Ilog::LEVEL_ERROR, "'pthread_join' failed (server thread)");
		}
        else {
            l(Ilog::LEVEL_DEBUG, "'pthread_join' ok (server thread)");
        }

        rc = ::pthread_join(this->c_thread, nullptr);
        if(!rc) {
            l(Ilog::LEVEL_ERROR, "'pthread_join' failed (client thread)");
        }
        else {
            l(Ilog::LEVEL_DEBUG, "'pthread_join' ok (client thread)");
        }

        rc = ::pthread_join(this->w_thread, nullptr);
        if(!rc) {
            l(Ilog::LEVEL_ERROR, "'pthread_join' failed (worker thread)");
        }
        else {
            l(Ilog::LEVEL_DEBUG, "'pthread_join' ok (worker thread)");
        }

        (void) ::close(this->pipe_sc_pd[0]);
        (void) ::close(this->pipe_sc_pd[1]);
        (void) ::close(this->pipe_cs_pd[0]);
        (void) ::close(this->pipe_cs_pd[1]);
        (void) ::close(this->pipe_sw_pd[0]);
        (void) ::close(this->pipe_sw_pd[1]);
        (void) ::close(this->pipe_ws_pd[0]);
        (void) ::close(this->pipe_ws_pd[1]);
        (void) ::close(this->pipe_cw_pd[0]);
        (void) ::close(this->pipe_cw_pd[1]);
        (void) ::close(this->pipe_wc_pd[0]);
        (void) ::close(this->pipe_wc_pd[1]);
		
        return (((RES_CODE_OK == s_last_err) &&
                 (RES_CODE_OK == c_last_err) &&
                 (RES_CODE_OK == w_last_err)) ?
                    RES_CODE_OK : RES_CODE_ERROR);
	}
		
    // TODO: Выбрасывать исключение, если пытаемся что-то изменить во время работы!
    void proxy_impl::set_proxy_port(boost::uint16_t value) {
        if(this->run_mutex.try_lock()) {
            this->proxy_port = value;
            this->run_mutex.unlock();
        }
        else {
            throw Eproxy_running();
        }
    }

    void proxy_impl::set_server_port(boost::uint16_t value) {
        if(this->run_mutex.try_lock()) {
            this->server_port = value;
            this->run_mutex.unlock();
        }
        else {
            throw Eproxy_running();
        }
    }

    void proxy_impl::set_server_ip(std::string const& value) {
        if(this->run_mutex.try_lock()) {
            this->server_ip = value;
            this->run_mutex.unlock();
        }
        else {
            throw Eproxy_running();
        }
    }

    void proxy_impl::set_server_ip(char const* value) {
        if(this->run_mutex.try_lock()) {
            this->server_ip = std::string(value);
            this->run_mutex.unlock();
        }
        else {
            throw Eproxy_running();
        }
    }

    void proxy_impl::set_timeout(boost::int32_t value) {
        if(this->run_mutex.try_lock()) {
            this->client_poll_timeout = value;
            this->server_poll_timeout = value;
            this->worker_poll_timeout = value;

            this->run_mutex.unlock();
        }
        else {
            throw Eproxy_running();
        }
    }

    void proxy_impl::set_connect_timeout(boost::int32_t value) {
        if(this->run_mutex.try_lock()) {
            this->connect_timeout = value;
            this->run_mutex.unlock();
        }
        else {
            throw Eproxy_running();
        }
    }

    void proxy_impl::set_client_keep_alive(bool value) {
        if(this->run_mutex.try_lock()) {
            this->client_keep_alive = value;
            this->run_mutex.unlock();
        }
        else {
            throw Eproxy_running();
        }
    }

    void proxy_impl::set_server_keep_alive(bool value) {
        if(this->run_mutex.try_lock()) {
            this->server_keep_alive = value;
            this->run_mutex.unlock();
        }
        else {
            throw Eproxy_running();
        }
    }

    void proxy_impl::set_client_tcp_no_delay(bool value) {
        if(this->run_mutex.try_lock()) {
            this->client_tcp_no_delay = value;
            this->run_mutex.unlock();
        }
        else {
            throw Eproxy_running();
        }
    }

    void proxy_impl::set_server_tcp_no_delay(bool value) {
        if(this->run_mutex.try_lock()) {
            this->server_tcp_no_delay = value;
            this->run_mutex.unlock();
        }
        else {
            throw Eproxy_running();
        }
    }

    boost::uint16_t proxy_impl::get_proxy_port(void) const {
        if(this->run_mutex.try_lock()) {
            this->run_mutex.unlock();
            return this->proxy_port;
        }
        else {
            throw Eproxy_running();
        }
    }

    boost::uint16_t proxy_impl::get_server_port(void) const {
        if(this->run_mutex.try_lock()) {
            this->run_mutex.unlock();
            return this->server_port;
        }
        else {
            throw Eproxy_running();
        }
    }

    std::string const& proxy_impl::get_server_ip(void) const {
        if(this->run_mutex.try_lock()) {
            this->run_mutex.unlock();
            return this->server_ip;
        }
        else {
            throw Eproxy_running();
        }
    }

    boost::int32_t proxy_impl::get_timeout(void) const {
        if(this->run_mutex.try_lock()) {
            this->run_mutex.unlock();
            // RU: Возвращается среднее арифметическое между 3 таймаутами.
            return ([this]()->boost::int32_t {
                std::array<boost::int32_t, 3> v{
                    this->client_poll_timeout,
                    this->server_poll_timeout,
                    this->worker_poll_timeout
                };

                return (std::accumulate(v.begin(), v.end(), 0) / v.size());
            }());
        }
        else {
            throw Eproxy_running();
        }
    }

    boost::int32_t proxy_impl::get_connect_timeout(void) const {
        if(this->run_mutex.try_lock()) {
            this->run_mutex.unlock();
            return this->connect_timeout;
        }
        else {
            throw Eproxy_running();
        }
    }

    bool proxy_impl::get_client_keep_alive(void) const {
        if(this->run_mutex.try_lock()) {
            this->run_mutex.unlock();
            return this->client_keep_alive;
        }
        else {
            throw Eproxy_running();
        }
    }

    bool proxy_impl::get_server_keep_alive(void) const {
        if(this->run_mutex.try_lock()) {
            this->run_mutex.unlock();
            return this->server_keep_alive;
        }
        else {
            throw Eproxy_running();
        }
    }

    bool proxy_impl::get_client_tcp_no_delay(void) const {
        if(this->run_mutex.try_lock()) {
            this->run_mutex.unlock();
            return this->client_tcp_no_delay;
        }
        else {
            throw Eproxy_running();
        }
    }

    bool proxy_impl::get_server_tcp_no_delay(void) const {
        if(this->run_mutex.try_lock()) {
            this->run_mutex.unlock();
            return this->server_tcp_no_delay;
        }
        else {
            throw Eproxy_running();
        }
    }

    ///
    /// \brief proxy_impl::~proxy_impl
    ///
	proxy_impl::~proxy_impl(void) {
	}

    ///
    /// \brief proxy_impl::server_run
    ///
	void proxy_impl::server_run(void) {
		this->s_arg._proxy = this;
        this->s_arg._sc_out_pd = this->pipe_sc_pd[self::SERVER_CLIENT_OUT];
        this->s_arg._cs_in_pd  = this->pipe_cs_pd[self::SERVER_CLIENT_IN];
        this->s_arg._sw_out_pd = this->pipe_sw_pd[self::SERVER_WORKER_OUT];
        this->s_arg._ws_in_pd  = this->pipe_ws_pd[self::SERVER_WORKER_IN];
			
        int rc = ::pthread_create(reinterpret_cast<pthread_t*>(
                                      &(this->s_thread)),
								  nullptr,
								  server_worker,
								  reinterpret_cast<void*>(&(this->s_arg)));
		
		if(!rc) {
			this->s_last_err = RES_CODE_ERROR;
		}
	}
	
    ///
    /// \brief proxy_impl::client_run
    ///
	void proxy_impl::client_run(void) {
        this->c_arg._proxy = this;
        this->c_arg._cs_out_pd = this->pipe_cs_pd[self::CLIENT_SERVER_OUT];
        this->c_arg._sc_in_pd  = this->pipe_sc_pd[self::CLIENT_SERVER_IN];
        this->c_arg._cw_out_pd = this->pipe_cw_pd[self::CLIENT_WORKER_OUT];
        this->c_arg._wc_in_pd  = this->pipe_wc_pd[self::CLIENT_WORKER_IN];

        int rc = ::pthread_create(reinterpret_cast<pthread_t*>(
                                      &(this->c_thread)),
                                  nullptr,
                                  client_worker,
                                  reinterpret_cast<void*>(&(this->c_arg)));

        if(!rc) {
            this->c_last_err = RES_CODE_ERROR;
        }
	}
	
    ///
    /// \brief proxy_impl::worker_run
    ///
	void proxy_impl::worker_run(void) {
        this->w_arg._proxy = this;
        this->w_arg._ws_out_pd = this->pipe_ws_pd[self::WORKER_SERVER_OUT];
        this->w_arg._sw_in_pd  = this->pipe_sw_pd[self::WORKER_SERVER_IN];
        this->w_arg._wc_out_pd = this->pipe_wc_pd[self::WORKER_CLIENT_OUT];
        this->w_arg._cw_in_pd  = this->pipe_cw_pd[self::WORKER_CLIENT_IN];

        int rc = ::pthread_create(reinterpret_cast<pthread_t*>(
                                      &(this->w_thread)),
                                  nullptr,
                                  worker_worker,
                                  reinterpret_cast<void*>(&(this->w_arg)));

        if(!rc) {
            this->w_last_err = RES_CODE_ERROR;
        }
	}

    int proxy_impl::pipe_create(int* d) {
        if(this->use_pipe) {
            return ::pipe(d);
        }
        else {
            return ::socketpair(AF_UNIX, SOCK_STREAM, 0, d);
        }
    }

    size_t proxy_impl::set_max_pipe_size(int pipe_fd) {
        if(this->use_pipe) {
            ssize_t default_max_pipe_size = 0;

            default_max_pipe_size = ::fcntl(pipe_fd, F_GETPIPE_SZ);
            if(default_max_pipe_size < 0) {
                throw Eproxy_syscall_failed("'fcntl' (F_GETPIPE_SZ)");
            }

            this->max_pipe_size = default_max_pipe_size;
            this->max_pipe_size = __set_max_pipe_size_helper(pipe_fd,
                                             default_max_pipe_size,
                                             this->max_pipe_size_system,
                                             default_max_pipe_size,
                                             1024);
        }
        else {
            // RU: В настоящее время для сокета ничего не меняем.
            //     Оставляем значение, которое было получено как
            //     "значение по-умолчанию".
            //     this->max_pipe_size_system - содержит не максимальное,
            //     а минимальное из текущих значение (для чтения и записи).
            this->max_pipe_size = this->max_pipe_size_system;
        }

        this->pipe_reserved_size = (this->max_pipe_size / 100) *
                this->pipe_reserved_percent;

        this->max_pipe_data_size = this->max_pipe_size -
                this->pipe_reserved_size;

        return this->max_pipe_size;
    }

    size_t proxy_impl::set_max_pipe_size_force(int pipe_fd, size_t new_size) {
        if(this->use_pipe) {
            int rc = ::fcntl(pipe_fd, F_SETPIPE_SZ, &new_size);
            if(rc < 0) {
                throw Eproxy_syscall_failed("'fcntl' (F_SETPIPE_SZ)");
            }

            this->max_pipe_size = new_size;
        }
        else {
            throw Eproxy_not_supported();
        }

        this->pipe_reserved_size = (this->max_pipe_size / 100) *
                this->pipe_reserved_percent;

        this->max_pipe_data_size = this->max_pipe_size -
                this->pipe_reserved_size;

        return this->max_pipe_size;
    }

    size_t proxy_impl::get_data_size_in_pipe(int pipe_fd) const {
        size_t count_bytes = 0;
        int rc = 0;

        rc = ::ioctl(pipe_fd, FIONREAD, &count_bytes);
        if(rc < 0) {
            count_bytes = 0;
            throw Eproxy_syscall_failed("'ioctl' (FIONREAD)");
        }

        return count_bytes;
    }

    bool proxy_impl::can_write_to_pipe_universal(int pipe_w_fd,
                                                 size_t max_size,
                                                 size_t size) const {
        size_t count_bytes = 0;
        bool ret = false;
        int rc = 0;

        rc = ::ioctl(pipe_w_fd, FIONREAD, &count_bytes);
        if(rc < 0) {
            ret = false;
        }
        else {
            ret = (size <= (max_size - count_bytes));
        }

        return ret;
    }

    bool proxy_impl::can_write_to_pipe(int pipe_w_fd, size_t size) const {
        return this->can_write_to_pipe_universal(pipe_w_fd,
                                                 this->max_pipe_size,
                                                 size);
    }

    bool proxy_impl::can_write_to_pipe_data(int pipe_w_fd, size_t size) const {
        return this->can_write_to_pipe_universal(pipe_w_fd,
                                                 this->max_pipe_data_size,
                                                 size);
    }

    ///
    /// \brief proxy_impl::set_nonblock
    /// \param fd
    /// \param ferr
    /// \return
    ///
    result_t proxy_impl::set_nonblock(int sd,
                                      std::function<void (int)> fok,
                                      std::function<void (int, int)> ferr)
    const {
        log_ns::log& l = log_ns::log::inst();

        result_t ret = RES_CODE_UNKNOWN;

        int flags = 0;
        int rc = 0;
        int on = 1;

        ret = RES_CODE_OK;

        on = 1;
        rc = ::ioctl(sd, FIONBIO, reinterpret_cast<char*>(&on));
        if(rc < 0) {
            l(Ilog::LEVEL_ERROR,"'ioctl' error");
            ret = RES_CODE_ERROR;
            ferr(rc, errno);
        }
        else {
            flags = ::fcntl(sd, F_GETFL, 0);
            rc = ::fcntl(sd, F_SETFL, flags | O_NONBLOCK);
            if(rc < 0) {
                l(Ilog::LEVEL_ERROR,"'fcntl' error");
                ret = RES_CODE_ERROR;
                ferr(rc, errno);
            }
            else {
                fok(rc);
            }
        }

        return ret;
    }

    ///
    /// \brief proxy_impl::set_reuseaddr
    /// \param sd
    /// \param fok
    /// \param ferr
    /// \return
    ///
    result_t proxy_impl::set_reuseaddr(int sd, bool val,
                                       std::function<void (int)> fok,
                                       std::function<void (int, int)> ferr)
    const{
        result_t ret = RES_CODE_UNKNOWN;

        int rc = 0;
        int optval_reuseaddr = val ? 1 : 0;
        socklen_t optlen_reuseaddr = sizeof(optval_reuseaddr);

        rc = ::setsockopt(sd,
                          SOL_SOCKET,
                          SO_REUSEADDR,
                          &optval_reuseaddr,
                          optlen_reuseaddr);
        if(rc < 0) {
            ret = RES_CODE_ERROR;
            ferr(rc, errno);
        }
        else {
            ret = RES_CODE_OK;
            fok(rc);
        }

        return ret;
    }

    ///
    /// \brief proxy_impl::set_keep_alive
    /// \param sd
    /// \param val
    /// \param fok
    /// \param ferr
    /// \return
    ///
    result_t proxy_impl::set_keep_alive(int sd, bool val,
                                        std::function<void (int)> fok,
                                        std::function<void (int, int)> ferr)
    const {
        result_t ret = RES_CODE_UNKNOWN;

        int rc = 0;
        int optval_keep_alive = val ? 1 : 0;
        socklen_t optlen_keep_alive = sizeof(optval_keep_alive);

        rc = ::setsockopt(sd,
                          SOL_SOCKET,
                          SO_KEEPALIVE,
                          &optval_keep_alive,
                          optlen_keep_alive);
        if(rc < 0) {
            ret = RES_CODE_ERROR;
            ferr(rc, errno);
        }
        else {
            ret = RES_CODE_OK;
            fok(rc);
        }

        return ret;
    }

    result_t proxy_impl::set_tcp_no_delay(int sd, bool val,
                                          std::function<void (int)> fok,
                                          std::function<void (int, int)> ferr)
    const {
        result_t ret = RES_CODE_UNKNOWN;

        int rc = 0;
        int optval_tcp_no_delay = val ? 1 : 0;
        socklen_t optlen_tcp_no_delay = sizeof(optval_tcp_no_delay);

        rc = ::setsockopt(sd,
                          IPPROTO_TCP,
                          TCP_NODELAY,
                          &optval_tcp_no_delay,
                          optlen_tcp_no_delay);

        if(rc < 0) {
            ret = RES_CODE_ERROR;
            ferr(rc, errno);
        }
        else {
            ret = RES_CODE_OK;
            fok(rc);
        }

        return ret;
    }

    result_t proxy_impl::get_keep_alive(int sd, int& val,
                            std::function<void (int)> fok,
                            std::function<void (int, int)> ferr) const {
        result_t ret = RES_CODE_UNKNOWN;

        int rc = 0;
        socklen_t optlen_keep_alive = sizeof(val);

        rc = ::getsockopt(sd,
                          SOL_SOCKET,
                          SO_KEEPALIVE,
                          &val,
                          &optlen_keep_alive);
        if(rc < 0) {
            ret = RES_CODE_ERROR;
            ferr(rc, errno);
        }
        else {
            ret = RES_CODE_OK;
            fok(rc);
        }

        return ret;
    }

    result_t proxy_impl::get_tcp_no_delay(int sd, int& val,
                              std::function<void (int)> fok,
                              std::function<void (int, int)> ferr) const {
        result_t ret = RES_CODE_UNKNOWN;

        int rc = 0;
        socklen_t optlen_tcp_no_delay = sizeof(val);

        rc = ::getsockopt(sd,
                          IPPROTO_TCP,
                          TCP_NODELAY,
                          &val,
                          &optlen_tcp_no_delay);

        if(rc < 0) {
            ret = RES_CODE_ERROR;
            ferr(rc, errno);
        }
        else {
            ret = RES_CODE_OK;
            fok(rc);
        }

        return ret;
    }

    ///
    /// \brief proxy_impl::debug_log_info
    /// \param d
    /// \param who
    ///
    void proxy_impl::debug_log_info(data const& d, std::string const& who) const
    {
#ifdef USE_FULL_DEBUG
        // RU: Вывод отладочного сообщения в лог.
        //     (функция крайне медленная!)
        log& l = log::inst();

        [&d, &l, &who](std::string direction_txt)->void {
            std::stringstream ss;
            std::stringstream client;
            std::stringstream proxy;
            std::stringstream server;

            auto f = [](std::stringstream& s,
                        struct sockaddr_in const& addr)->void {
                s << inet_ntoa(addr.sin_addr)
                  << ":"
                  << ntohs(addr.sin_port);
            };

            f(client, d.client_addr);
            f(proxy, d.proxy_addr);
            f(server, d.server_addr);

            ss << who
               << ":"
               << direction_txt
               << "; special: "
               << (TOD_DATA != d.tod ? "t" : "f")
               << "; type: "
               << static_cast<int>(d.tod)
               << "; cd: "
               << d.c_sd
               << "; sd: "
               << d.s_sd
               << "; buflen: "
               << d.buffer_len
               << "; "
               << "client: "
               << client.str()
               << "; "
               << "proxy: "
               << proxy.str()
               << "; "
               << "server: "
               << server.str()
               << ". "
               << "[";

            for(int i = 0; i < 5; i++) {
                ss << std::setfill('0')
                   << std::setw(2)
                   << std::uppercase
                   << std::hex
                   << static_cast<int>(d.buffer[i]) << " ";
            }

            ss << "...]";

            l(Ilog::LEVEL_DEBUG, ss.str());
        }([&d](){
            switch(d.direction) {
            case DIRECTION_CLIENT_TO_SERVER:
                return std::string("C->S");
            case DIRECTION_SERVER_TO_CLIENT:
                return std::string("S->C");
            case DIRECTION_CLIENT_TO_WORKER:
                return std::string("C->W");
            case DIRECTION_WORKER_TO_CLIENT:
                return std::string("W->C");
            case DIRECTION_SERVER_TO_WORKER:
                return std::string("S->W");
            case DIRECTION_WORKER_TO_SERVER:
                return std::string("W->S");
            default:
                return std::string("?->?");
            }
        }());
#else // USE_FULL_DEBUG
        boost::ignore_unused(d, who);
#endif // USE_FULL_DEBUG
    }

} // namespace proxy_ns

/* *****************************************************************************
 * End of file
 * ************************************************************************** */
