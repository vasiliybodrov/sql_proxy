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
 *   СЕРВЕР - обслуживает подключения к серверу СУБД (или иному серверу);
 *   ВОРКЕР - обслуживает обработку данных и их логирование.
 * -----------------------------------------------------------------------------
 */

#include <map>
#include <list>
#include <algorithm>
#include <numeric>
#include <iterator>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ios>
#include <chrono>

#include <ctime>
#include <cerrno>
#include <cstring>

#include <boost/make_shared.hpp>
#include <boost/cstdint.hpp>
#include <boost/core/ignore_unused.hpp>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#include "log.hpp"
#include "proxy_result.hpp"
#include "proxy.hpp"
//#include "proxy_impl.hpp" // Instead of "proxy_impl.hpp" we use "proxy.hpp".

namespace proxy_ns {
    using namespace log_ns;

    class go_to_finish {};

    // -------------------------------------------------------------------------
    //
    // -------------------------------------------------------------------------
    ///
    /// \brief server_worker
    /// \param arg
    /// \return
    ///
    void* server_worker(void* arg) {
        log& l = log::inst();

        server_routine_arg* server_arg =
            reinterpret_cast<server_routine_arg*>(arg);

        proxy_impl* _this =
            server_arg->_proxy;

        int c_in_fd  = server_arg->_cs_in_pd;
        int c_out_fd = server_arg->_sc_out_pd;
        int w_in_fd  = server_arg->_ws_in_pd;
        int w_out_fd = server_arg->_sw_out_pd;

        int rc = 0;
        struct pollfd fds[POLLING_REQUESTS_SIZE];
        int nfds = 0;
        int timeout = 0;
        bool compress_array = false;
        int len = 0;
        unsigned char buffer[DATA_BUFFER_SIZE] = { 0 };
        int cur_fd = -1;
        short int cur_events = 0;
        result_t rc_prx_hlpr = RES_CODE_UNKNOWN;
        std::map<int, int> db; // key: server descriptor
        std::map<int, std::chrono::system_clock::time_point>
                db_con_wait; // key: server descriptor; value: time


        // ---------------------------------------------------------------------
        //
        // ---------------------------------------------------------------------
        ///
        ///
        ///
        auto compress_array_f = [&](){
            compress_array = false;
            for(int i = 0; i < nfds; i++) {
                if(fds[i].fd == -1) {
                    for(int j = i; j < nfds; j++) {
                        fds[j].fd = fds[j + 1].fd;
                    }
                    nfds--;
                }
            }
        };

        // ---------------------------------------------------------------------
        //
        // ---------------------------------------------------------------------
        ///
        ///
        ///
        auto erase_old_wait_connect_f = [&](){
            if(db_con_wait.empty()) {
                return;
            }

            // RU: Поиск просроченных дескрипторов, у которых
            //     истёк интервал ожидания подключения
            auto cur_time = std::chrono::system_clock::now();
            std::map<int,
                    std::chrono::system_clock::time_point>::iterator it =
                    db_con_wait.begin();

            for(it = db_con_wait.begin(); it != db_con_wait.end();) {
                auto start_time = it->second;

                auto dur = std::chrono::duration_cast<
                        std::chrono::milliseconds>(
                            cur_time - start_time).count();

                if(dur > _this->connect_timeout) {
                    // RU: Удаляем просроченные дескрипторы и оповещаем
                    //     клиента и воркера
                    data x(DIRECTION_SERVER_TO_CLIENT,
                           TOD_NOT_CONNECT,
                           db[it->first], -1,
                            0, nullptr,
                            nullptr, nullptr, nullptr);

                    rc = ::write(c_out_fd, &x, sizeof(x));
                    if(rc < 0) {
                        l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                          ->std::string {
                              std::stringstream ss;
                              ss << "S: 'write' failed ("
                                 << ::strerror(errno)
                                 << ") (fd="
                                 << c_out_fd
                                 << "). FILE:" << file
                                 << ": " << line << ".";
                              return ss.str();
                        }(__FILE__, __LINE__));
                    }
                    else {
                        assert(rc == sizeof(x));
                    }

                    x.direction = DIRECTION_SERVER_TO_WORKER;

                    rc = ::write(w_out_fd, &x, sizeof(x));
                    if(rc < 0) {
                        l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                          ->std::string {
                              std::stringstream ss;
                              ss << "S: 'write' failed ("
                                 << ::strerror(errno)
                                 << ") (fd="
                                 << w_out_fd
                                 << "). FILE:" << file
                                 << ": " << line << ".";
                              return ss.str();
                        }(__FILE__, __LINE__));
                    }
                    else {
                        assert(rc == sizeof(x));
                    }

                    (void) ::close(it->first);

                    db.erase(it->first);

                    int count = nfds;
                    for(int i = 0; i < count; i++) {
                        if(fds[i].fd == it->first) {
                            fds[i].fd = -1;
                            break;
                        }
                    }

                    compress_array = true;

                    it = db_con_wait.erase(it);
                }
                else {
                    it++;
                }
            }

            if(compress_array) {
                compress_array_f();
            }
        };

        // ---------------------------------------------------------------------
        //
        // ---------------------------------------------------------------------
        ///
        ///
        ///
        auto from_worker_f = [&](){
            data d;

            rc = ::read(cur_fd, &d, sizeof(d));
            if(rc < 0) {
                // TODO: проверить код ошибки!
                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)->std::string {
                      std::stringstream ss;
                      ss << "S: 'read' failed ("
                         << ::strerror(errno)
                         << ") (socket="
                         << cur_fd
                         << "). FILE:" << file
                         << ": " << line << ".";
                      return ss.str();
                }(__FILE__, __LINE__));
            }
            else {
                assert(rc == sizeof(d));
            }

            _this->debug_log_info(d, "S");
        };

        // ---------------------------------------------------------------------
        //
        // ---------------------------------------------------------------------
        ///
        ///
        ///
        auto from_client_f = [&](){
            // RU: Получены данные от клиента.
            //     Надо проверить их и принять меры

            data d;

            rc = ::read(cur_fd, &d, sizeof(d));
            if(rc < 0) {
                // TODO: проверить код ошибки!
                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)->std::string {
                      std::stringstream ss;
                      ss << "S: 'read' failed ("
                         << ::strerror(errno)
                         << ") (fd="
                         << cur_fd
                         << "). FILE:" << file
                         << ": " << line << ".";
                      return ss.str();
                }(__FILE__, __LINE__));
            }
            else {
                assert(rc == sizeof(d));
            }

            _this->debug_log_info(d, "S");

            // RU: Проверить, а данные точно от клиента?
            if(DIRECTION_CLIENT_TO_SERVER == d.direction) {
                switch(d.tod) {
                case TOD_NEW_CONNECT:
                    do {
                        struct sockaddr_in server_addr;
                        int optval = _this->server_keep_alive ? 1 : 0;
                        socklen_t optlen = sizeof(optval);

                        int new_server_sd = ::socket(AF_INET, SOCK_STREAM, 0);

                        rc_prx_hlpr = _this->set_nonblock(new_server_sd,
                            [&_this](int fd, int){
                                (void) ::close(fd);
                                _this->s_last_err = RES_CODE_ERROR;
                        });

                        if(RES_CODE_OK != rc_prx_hlpr) {
                            throw go_to_finish();
                        }

                        rc = ::setsockopt(new_server_sd, SOL_SOCKET,
                                          SO_KEEPALIVE, &optval, optlen);
                        if(rc < 0) {
                            l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                              ->std::string {
                                  std::stringstream ss;
                                  ss << "S: 'setsockopt' failed ("
                                     << ::strerror(errno)
                                     << ") (socket="
                                     << new_server_sd
                                     << "). FILE:" << file
                                     << ": " << line << ".";
                                  return ss.str();
                            }(__FILE__, __LINE__));
                        }

                        rc = ::getsockopt(new_server_sd, SOL_SOCKET,
                                          SO_KEEPALIVE, &optval, &optlen);
                        if(rc < 0) {
                            l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                              ->std::string {
                                  std::stringstream ss;
                                  ss << "S: 'setsockopt' failed ("
                                     << ::strerror(errno)
                                     << ") (socket="
                                     << new_server_sd
                                     << "). FILE:" << file
                                     << ": " << line << ".";
                                  return ss.str();
                            }(__FILE__, __LINE__));
                        }
                        else {
                            l(Ilog::LEVEL_DEBUG, [&](auto file, auto line)
                              ->std::string {
                                std::stringstream ss;
                                ss << "S: SO_KEEPALIVE is "
                                   << (optval ? "ON" : "OFF")
                                   << " on socket=" << new_server_sd
                                   << ". FILE:" << file
                                   << ": " << line << ".";
                                return ss.str();
                            }(__FILE__, __LINE__));
                        }

                        server_addr.sin_family =
                                AF_INET;

                        server_addr.sin_port =
                                htons(_this->server_port);

                        server_addr.sin_addr.s_addr =
                                inet_addr(_this->server_ip.c_str());

                        rc = ::connect(new_server_sd,
                                       reinterpret_cast<struct sockaddr*>(
                                           &server_addr),
                                       sizeof(server_addr));
                        if(rc < 0) {
                            if(EINPROGRESS == errno) {
                                // RU: Для установки соединения требуется время
                                l(Ilog::LEVEL_DEBUG, [&](auto file, auto line)
                                  ->std::string {
                                      std::stringstream ss;
                                      ss << "S: The connection takes time... "
                                         << "(socket=" << new_server_sd << "). "
                                         << "FILE:" << file
                                         << ": " << line << ".";
                                      return ss.str();
                                }(__FILE__, __LINE__));

                                db_con_wait[new_server_sd] =
                                        std::chrono::system_clock::now();

                                db[new_server_sd] = d.c_sd;
                                fds[nfds].fd = new_server_sd;
                                fds[nfds].events = POLLIN | POLLOUT;
                                nfds++;
                            }
                            else {
                                // RU: Соединение не удалось! Это не факт что
                                //     ошибка. Вероятно, сервер недоступен.
                                l(Ilog::LEVEL_INFO, [&](auto file, auto line)
                                  ->std::string {
                                      std::stringstream ss;
                                      ss << "S: 'connect' failed ("
                                         << strerror(errno) << ") "
                                         << "(socket=" << new_server_sd << "). "
                                         << "FILE:" << file
                                         << ": " << line << ".";
                                      return ss.str();
                                }(__FILE__, __LINE__));

                                (void) ::close(new_server_sd);

                                data x(DIRECTION_SERVER_TO_CLIENT,
                                       TOD_NOT_CONNECT,
                                       d.c_sd, -1,
                                       0, nullptr,
                                       nullptr, nullptr, &server_addr);

                                rc = ::write(c_out_fd, &x, sizeof(x));
                                if(rc < 0) {
                                    l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                                      ->std::string {
                                          std::stringstream ss;
                                          ss << "S: 'write' failed ("
                                             << ::strerror(errno)
                                             << ") (fd="
                                             << c_out_fd
                                             << "). FILE:" << file
                                             << ": " << line << ".";
                                          return ss.str();
                                    }(__FILE__, __LINE__));
                                }
                                else {
                                    assert(rc == sizeof(d));
                                }

                                x.direction = DIRECTION_SERVER_TO_WORKER;

                                rc = ::write(w_out_fd, &x, sizeof(x));
                                if(rc < 0) {
                                    l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                                      ->std::string {
                                          std::stringstream ss;
                                          ss << "S: 'write' failed ("
                                             << ::strerror(errno)
                                             << ") (fd="
                                             << w_out_fd
                                             << "). FILE:" << file
                                             << ": " << line << ".";
                                          return ss.str();
                                    }(__FILE__, __LINE__));
                                }
                                else {
                                    assert(rc == sizeof(d));
                                }

                                // RU: Покидаем цикл - соединение не удалось!
                                break;
                            }
                        }
                        else {
                            // RU: Соединение удалось сразу
                            l(Ilog::LEVEL_INFO, [&](auto file, auto line)
                              ->std::string {
                                  std::stringstream ss;
                                  ss << "S: The connection was immediately ("
                                     << "socket=" << new_server_sd << "). "
                                     << "FILE:" << file
                                     << ": " << line << ".";
                                  return ss.str();
                            }(__FILE__, __LINE__));

                            data x(DIRECTION_SERVER_TO_CLIENT,
                                   TOD_NEW_CONNECT,
                                   d.c_sd, new_server_sd,
                                   0, nullptr,
                                   nullptr, nullptr, &server_addr);

                            rc = ::write(c_out_fd, &x, sizeof(x));
                            if(rc < 0) {
                                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                                  ->std::string {
                                      std::stringstream ss;
                                      ss << "S: 'write' failed ("
                                         << ::strerror(errno)
                                         << ") (fd="
                                         << c_out_fd
                                         << "). FILE:" << file
                                         << ": " << line << ".";
                                      return ss.str();
                                }(__FILE__, __LINE__));
                            }
                            else {
                                assert(rc == sizeof(d));
                            }

                            x.direction = DIRECTION_SERVER_TO_WORKER;

                            rc = ::write(w_out_fd, &x, sizeof(x));
                            if(rc < 0) {
                                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                                  ->std::string {
                                      std::stringstream ss;
                                      ss << "S: 'write' failed ("
                                         << ::strerror(errno)
                                         << ") (fd="
                                         << w_out_fd
                                         << "). FILE:" << file
                                         << ": " << line << ".";
                                      return ss.str();
                                }(__FILE__, __LINE__));
                            }
                            else {
                                assert(rc == sizeof(d));
                            }

                            db[new_server_sd] = d.c_sd;
                            fds[nfds].fd = new_server_sd;
                            fds[nfds].events = POLLIN;
                            nfds++;
                        }
                    }
                    while(false);
                    break;
                case TOD_DATA:
                    do {
                        if(d.s_sd < 0) {
                            l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                              ->std::string {
                                  std::stringstream ss;
                                  ss << "S: Internal error. WTF!? (s_sd="
                                     << d.s_sd << "). "
                                     << "FILE:" << file
                                     << ": " << line << ".";
                                  return ss.str();
                            }(__FILE__, __LINE__));
                        }
                        else {
                            rc = send(d.s_sd, d.buffer, d.buffer_len, 0);
                            if(rc < 0) {
                                if(errno != EWOULDBLOCK) {
                                    l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                                      ->std::string {
                                          std::stringstream ss;
                                          ss << "S: 'send' failed ("
                                          << strerror(errno) << ") (s_sd="
                                          << d.s_sd << "). "
                                          << "FILE:" << file
                                          << ": " << line << ".";
                                          return ss.str();
                                    }(__FILE__, __LINE__));
                                }
                            }
                        }
                    }
                    while(false);
                    break;
                case TOD_CONNECT_NOT_FOUND:
                case TOD_DISCONNECT:
                    do {
                        l(Ilog::LEVEL_DEBUG, [&](auto file, auto line)
                          ->std::string {
                              std::stringstream ss;
                              ss << "S: It's a signal from client thread - "
                                  << "'connect not found' or 'disconnect'. ("
                                  << "c_sd=" << d.c_sd << "; s_sd=" << d.s_sd
                                  << "). FILE:" << file
                                  << ": " << line << ".";
                              return ss.str();
                        }(__FILE__, __LINE__));

                        (void) ::close(d.s_sd);

                        db.erase(d.s_sd);

                        int count = nfds;
                        for(int i = 0; i < count; i++) {
                            if(fds[i].fd == d.s_sd) {
                               fds[i].fd = -1;
                               break;
                            }
                        }

                        compress_array = true;
                    }
                    while(false);
                    break;
                default:
                    // RU: По-идее, в данную секцию попадать не должны. Однако,
                    //     если мы тут оказались - это вовсе не означает что
                    //     произошла ошибка. Но обращать внимание на это стоит.
                    l(Ilog::LEVEL_DEBUG, [&](auto file, auto line)
                      ->std::string {
                          std::stringstream ss;
                          ss << "S: Unsupperted data from client. (TOD="
                             << d.tod << "). "
                             << "FILE:" << file
                             << ": " << line << ".";
                          return ss.str();
                    }(__FILE__, __LINE__));
                    break;
                }
            }
            else {
                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)->std::string {
                      std::stringstream ss;
                      ss << "S: Internal error! WTF!? "
                         << "FILE:" << file
                         << ": " << line << ".";
                      return ss.str();
                }(__FILE__, __LINE__));
            }
        };

        // ---------------------------------------------------------------------
        //
        // ---------------------------------------------------------------------
        ///
        ///
        ///
        auto from_server_f = [&](){
            bool close_conn = false;
            bool do_work = true;

#ifdef USE_FULL_DEBUG
            l(Ilog::LEVEL_DEBUG, [&](auto file, auto line)->std::string {
                  std::stringstream ss;
                  ss << "S: Descriptor is readable (socket=" << cur_fd << "). "
                     << "FILE:" << file
                     << ": " << line << ".";
                  return ss.str();
            }(__FILE__, __LINE__));
#endif // USE_FULL_DEBUG

            auto search = db_con_wait.find(cur_fd);
            if(search != db_con_wait.end()) {
                // RU: Текущий сокет ожидает подключения
                socklen_t err_len = 0;
                int error = 0;

                err_len = sizeof(error);
                rc = ::getsockopt(cur_fd, SOL_SOCKET, SO_ERROR,
                                  &error, &err_len);
                if(rc < 0 || error) {
                    // RU: Произошла ошибка - соединение не удалось
                    l(Ilog::LEVEL_INFO, [&](auto file, auto line)->std::string {
                          std::stringstream ss;
                          ss << "S: The server does not respond. "
                             << "Connection failed ("
                             << ((rc < 0) ? ::strerror(errno) : "error != 0")
                             << "). (socket=" << cur_fd << "). "
                             << "FILE:" << file
                             << ": " << line << ".";
                          return ss.str();
                    }(__FILE__, __LINE__));

                    data x(DIRECTION_SERVER_TO_CLIENT,
                           TOD_NOT_CONNECT,
                           db[cur_fd], -1,
                           0, nullptr,
                           nullptr, nullptr, nullptr);

                    rc = ::write(c_out_fd, &x, sizeof(x));
                    if(rc < 0) {
                        l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                          ->std::string {
                              std::stringstream ss;
                              ss << "S: 'write' failed ("
                                 << ::strerror(errno)
                                 << ") (fd="
                                 << c_out_fd
                                 << "). FILE:" << file
                                 << ": " << line << ".";
                              return ss.str();
                        }(__FILE__, __LINE__));
                    }
                    else {
                        assert(rc == sizeof(x));
                    }

                    x.direction = DIRECTION_SERVER_TO_WORKER;

                    rc = ::write(w_out_fd, &x, sizeof(x));
                    if(rc < 0) {
                        l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                          ->std::string {
                              std::stringstream ss;
                              ss << "S: 'write' failed ("
                                 << ::strerror(errno)
                                 << ") (fd="
                                 << w_out_fd
                                 << "). FILE:" << file
                                 << ": " << line << ".";
                              return ss.str();
                        }(__FILE__, __LINE__));
                    }
                    else {
                        assert(rc == sizeof(x));
                    }

                    (void) ::close(cur_fd);

                    db.erase(cur_fd);
                    cur_fd = -1;

                    compress_array = true;

                    do_work = false;
                }
                else {
                    // RU: Сокет, ожидающий подключения, подключился
                    l(Ilog::LEVEL_INFO, [&](auto file, auto line)->std::string {
                          std::stringstream ss;
                          ss << "S: Connection was successful (socket="
                             << cur_fd << "). "
                             << "FILE:" << file
                             << ": " << line << ".";
                          return ss.str();
                    }(__FILE__, __LINE__));

                    data x(DIRECTION_SERVER_TO_CLIENT,
                           TOD_NEW_CONNECT,
                           db[cur_fd], cur_fd,
                           0, nullptr,
                           nullptr, nullptr, nullptr);

                    rc = ::write(c_out_fd, &x, sizeof(x));
                    if(rc < 0) {
                        l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                          ->std::string {
                              std::stringstream ss;
                              ss << "S: 'write' failed ("
                                 << ::strerror(errno)
                                 << ") (fd="
                                 << c_out_fd
                                 << "). FILE:" << file
                                 << ": " << line << ".";
                              return ss.str();
                        }(__FILE__, __LINE__));
                    }
                    else {
                        assert(rc == sizeof(x));
                    }

                    x.direction = DIRECTION_SERVER_TO_WORKER;

                    rc = ::write(w_out_fd, &x, sizeof(x));
                    if(rc < 0) {
                        l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                          ->std::string {
                              std::stringstream ss;
                              ss << "S: 'write' failed ("
                                 << ::strerror(errno)
                                 << ") (fd="
                                 << w_out_fd
                                 << "). FILE:" << file
                                 << ": " << line << ".";
                              return ss.str();
                        }(__FILE__, __LINE__));
                    }
                    else {
                        assert(rc == sizeof(x));
                    }

                    cur_events = POLLIN;

                    do_work = true;
                }

                // RU: В любом случае, данный дескриптор более не
                //     находится среди ожидающих окончания соединения
                db_con_wait.erase(cur_fd);
            }

            if(do_work) {
                do {
                    rc = ::recv(cur_fd, buffer, sizeof(buffer), 0);
                    if(rc < 0) {
                        if(errno != EWOULDBLOCK) {
                            l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                              ->std::string {
                                  std::stringstream ss;
                                  ss << "S: 'recv' failed ("
                                     << ::strerror(errno)
                                     << ") (socket="
                                     << cur_fd
                                     << "). FILE:" << file
                                     << ": " << line << ".";
                                  return ss.str();
                            }(__FILE__, __LINE__));

                            close_conn = true;
                        }

                        // Выход из цикла по причине отсутствия
                        // данных в сокете (все данные прочитаны или
                        // произошла ошибка)
                        break;
                    }
                    else if(0 == rc) {
                        // RU: Соединение закрыто?
                        l(Ilog::LEVEL_INFO, [&](auto file, auto line)->std::string {
                              std::stringstream ss;
                              ss << "S: Connection closed. (socket=" << cur_fd
                                 << "). FILE:" << file
                                 << ": " << line << ".";
                              return ss.str();
                        }(__FILE__, __LINE__));

                        close_conn = true;

                        // Выход из цикла по причине закрытия соединения
                        break;
                    }

                    len = rc;

                    // RU: Отправка данных воркеру и серверу
                    data d(DIRECTION_SERVER_TO_WORKER,
                           TOD_DATA,
                           db[cur_fd], cur_fd,
                           len, buffer,
                           nullptr, nullptr, nullptr);

                    rc = ::write(w_out_fd, &d, sizeof(d));
                    if(rc < 0) {
                        l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                          ->std::string {
                              std::stringstream ss;
                              ss << "S: 'write' failed ("
                                 << ::strerror(errno)
                                 << ") (fd="
                                 << w_out_fd
                                 << "). FILE:" << file
                                 << ": " << line << ".";
                              return ss.str();
                        }(__FILE__, __LINE__));
                    }
                    else {
                        assert(rc == sizeof(d));
                    }

                    d.direction = DIRECTION_SERVER_TO_CLIENT;
                    rc = ::write(c_out_fd, &d, sizeof(d));
                    if(rc < 0) {
                        l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                          ->std::string {
                              std::stringstream ss;
                              ss << "S: 'write' failed ("
                                 << ::strerror(errno)
                                 << ") (fd="
                                 << c_out_fd
                                 << "). FILE:" << file
                                 << ": " << line << ".";
                              return ss.str();
                        }(__FILE__, __LINE__));
                    }
                    else {
                        assert(rc == sizeof(d));
                    }
                }
                while(true);
            }

            if(close_conn) {
                do {
                    data d(DIRECTION_SERVER_TO_WORKER,
                           TOD_DISCONNECT,
                           db[cur_fd], cur_fd,
                            0, nullptr,
                            nullptr, nullptr, nullptr);

                    rc = ::write(w_out_fd, &d, sizeof(d));
                    if(rc < 0) {
                        l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                          ->std::string {
                              std::stringstream ss;
                              ss << "S: 'write' failed ("
                                 << ::strerror(errno)
                                 << ") (fd="
                                 << w_out_fd
                                 << "). FILE:" << file
                                 << ": " << line << ".";
                              return ss.str();
                        }(__FILE__, __LINE__));
                    }
                    else {
                        assert(rc == sizeof(d));
                    }

                    d.direction = DIRECTION_SERVER_TO_CLIENT;
                    rc = ::write(c_out_fd, &d, sizeof(d));
                    if(rc < 0) {
                        l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                          ->std::string {
                              std::stringstream ss;
                              ss << "S: 'write' failed ("
                                 << ::strerror(errno)
                                 << ") (fd="
                                 << c_out_fd
                                 << "). FILE:" << file
                                 << ": " << line << ".";
                              return ss.str();
                        }(__FILE__, __LINE__));
                    }
                    else {
                        assert(rc == sizeof(d));
                    }
                }
                while(false);

                (void) ::close(cur_fd);
                db.erase(cur_fd);
                cur_fd = -1;
                compress_array = true;
            }
        };

        // ---------------------------------------------------------------------
        //
        // ---------------------------------------------------------------------
        ///
        ///
        ///
        auto prepare_f = [&](){
            _this->s_last_err = RES_CODE_OK;

            nfds = 0;

            fds[nfds].fd = c_in_fd;
            fds[nfds].events = POLLIN;
            nfds++;

            fds[nfds].fd = w_in_fd;
            fds[nfds].events = POLLIN;
            nfds++;

            timeout = _this->server_poll_timeout;
        };

        ///
        ///
        ///
        auto run_f = [&](){
            do {
        #ifdef USE_FULL_DEBUG
            #ifdef USE_FULL_DEBUG_POLL_INTERVAL
                l(Ilog::LEVEL_DEBUG, "Waiting on poll (server)...");
            #endif // USE_FULL_DEBUG_POLL_INTERVAL
        #endif // USE_FULL_DEBUG

                erase_old_wait_connect_f();

                rc = ::poll(fds, nfds, timeout);
                if(rc < 0) {
                    l(Ilog::LEVEL_ERROR, [&](auto file, auto line)->std::string {
                        std::stringstream ss;
                        ss << "S: 'poll' failed ("
                           << ::strerror(errno)
                           << "). "
                           << "FILE:" << file
                           << ": " << line << ".";
                        return ss.str();
                    }(__FILE__, __LINE__));
                    _this->c_last_err = RES_CODE_ERROR;
                    break;
                }
                else if(0 == rc) {
                    // RU: Интервал времени истёк, либо, кто-то прервал
                    continue;
                }

                int current_size = nfds;

                for(int i = 0; i < current_size; i++) {
                    if(0 == fds[i].revents) {
                        continue;
                    }
                    else if(POLLIN != fds[i].revents &&
                            POLLOUT != fds[i].revents) {
                        l(Ilog::LEVEL_DEBUG, [&](auto file, auto line)
                          ->std::string {
                              std::stringstream ss;
                              ss << "S: Revents aren't pure POLLIN or POLLOUT ("
                                 << "descriptor=" << fds[i].fd << "; "
                                 << "events = "
                                 << "0x" << std::hex << fds[i].events
                                 << " [" << std::dec << fds[i].events << "]; "
                                 << "revents = "
                                 << "0x" << std::hex << fds[i].revents
                                 << " [" << std::dec << fds[i].revents << "]"
                                 << "). FILE:" << file
                                 << ": " << line << ".";
                              return ss.str();
                        }(__FILE__, __LINE__));

                        if(fds[i].revents & POLLHUP) {
                            l(Ilog::LEVEL_DEBUG, [&](auto file, auto line)
                              ->std::string {
                                  std::stringstream ss;
                                  ss << "S: Revent includes POLLHUP ("
                                     << "descriptor=" << fds[i].fd
                                     << "). FILE:" << file
                                     << ": " << line << ".";
                                  return ss.str();
                            }(__FILE__, __LINE__));

                            db_con_wait.erase(fds[i].fd);

                            data d(DIRECTION_SERVER_TO_WORKER,
                                   TOD_DISCONNECT,
                                   db[fds[i].fd], fds[i].fd,
                                    0, nullptr,
                                    nullptr, nullptr, nullptr);

                            rc = ::write(w_out_fd, &d, sizeof(d));
                            if(rc < 0) {
                                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                                  ->std::string {
                                      std::stringstream ss;
                                      ss << "S: 'write' failed ("
                                         << ::strerror(errno)
                                         << ") (fd="
                                         << w_out_fd
                                         << "). FILE:" << file
                                         << ": " << line << ".";
                                      return ss.str();
                                }(__FILE__, __LINE__));
                            }
                            else {
                                assert(rc == sizeof(d));
                            }

                            d.direction = DIRECTION_SERVER_TO_CLIENT;
                            rc = ::write(c_out_fd, &d, sizeof(d));
                            if(rc < 0) {
                                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                                  ->std::string {
                                      std::stringstream ss;
                                      ss << "S: 'write' failed ("
                                         << ::strerror(errno)
                                         << ") (fd="
                                         << c_out_fd
                                         << "). FILE:" << file
                                         << ": " << line << ".";
                                      return ss.str();
                                }(__FILE__, __LINE__));
                            }
                            else {
                                assert(rc == sizeof(d));
                            }

                            (void) ::close(fds[i].fd);
                            db.erase(fds[i].fd);
                            fds[i].fd = -1;
                            compress_array = true;
                        }
                        else {
                            _this->end_proxy = true;
                            _this->c_last_err = RES_CODE_ERROR;

                            break;
                        }
                    }

                    cur_fd = fds[i].fd;
                    cur_events = fds[i].events;

                    if(cur_fd == -1) {
                        compress_array = true;
                        continue;
                    }

                    if(fds[i].fd == c_in_fd) {
                        from_client_f();
                    }
                    else if(fds[i].fd == w_in_fd) {
                        from_worker_f();
                    }
                    else {
                        from_server_f();
                    }

                    fds[i].fd = cur_fd;
                    fds[i].events = cur_events;
                }

                if(compress_array) {
                    compress_array_f();
                }
            }
            while(!_this->end_proxy);
        };

        // ---------------------------------------------------------------------
        //
        // ---------------------------------------------------------------------
        ///
        ///
        ///
        auto done_f = [&]() noexcept {
            try {
                for(int i = 0; i < nfds; i++) {
                    if(fds[i].fd >= 0) {
                        (void) ::close(fds[i].fd);
                    }
                }

                _this->end_proxy = true;
            }
            catch(...) {
                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)->std::string {
                      std::stringstream ss;
                      ss << "S: Unknown exception! WTF!? "
                         << "). FILE:" << file
                         << ": " << line << ".";
                      return ss.str();
                }(__FILE__, __LINE__));
            }
        };

        // *******************************************
        // ******************* GO! *******************
        // *******************************************

        try {
            prepare_f();
            run_f();
        }
        catch(go_to_finish const&) {}
        catch(...) {}

        done_f();

        return &(_this->c_last_err);

        // *******************************************
        // ***************** FINISH! *****************
        // *******************************************
    }
} // namespace proxy_ns

/* *****************************************************************************
 * End of file
 * ************************************************************************** */
