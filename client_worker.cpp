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
#include <algorithm>
#include <iterator>
#include <sstream>
#include <iomanip>
#include <ios>

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

    ///
    /// \brief client_worker
    /// \param arg
    /// \return
    ///
    void* client_worker(void* arg) {
        log& l = log::inst();

        client_routine_arg* client_arg =
            reinterpret_cast<client_routine_arg*>(arg);

        proxy_impl* _this =
            client_arg->_proxy;

        int s_in_fd  = client_arg->_sc_in_pd;
        int s_out_fd = client_arg->_cs_out_pd;
        int w_in_fd  = client_arg->_wc_in_pd;
        int w_out_fd = client_arg->_cw_out_pd;

        int listen_sd = -1;
        int new_sd = -1;
        struct sockaddr_in proxy_addr;
        unsigned char buffer[DATA_BUFFER_SIZE] = { 0 };
        int rc = 0;
        int on = 1;
        int len = 0;
        struct pollfd fds[POLLING_REQUESTS_SIZE];
        int nfds = 0;
        int timeout = 0;
        bool compress_array = false;
        int cur_fd = -1;
        result_t rc_prx_hlpr = RES_CODE_UNKNOWN;
        std::map<int, int> db; // key: client descriptor

        ///
        /// RU: Упаковка массива с дескрипторами (удаление закрытых)
        ///
        auto compress_array_f = [&]() {
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

        ///
        /// RU: Новое подключение клиента
        ///
        auto new_connect_f = [&](){
            // RU: Какое-то действие на сокете со стороны клиентов
            l(Ilog::LEVEL_DEBUG, [&](auto file, auto line)->std::string {
                std::stringstream ss;
                ss << "C: Listening socket is readable (socket="
                   << listen_sd
                   << "). FILE:" << file
                   << ": " << line << ".";
                return ss.str();
            }(__FILE__, __LINE__));

            do {
                struct sockaddr_in client_addr;
                socklen_t client_addr_len = 0;

                ::memset(&client_addr, 0, sizeof(client_addr));

                new_sd = ::accept(listen_sd,
                                  reinterpret_cast<struct sockaddr*>(
                                      &client_addr),
                                  &client_addr_len);
                if(new_sd < 0) {
                    if(errno != EWOULDBLOCK) {
                        l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                          ->std::string {
                            std::stringstream ss;
                            ss << "C: 'accept' failed ("
                               << ::strerror(errno)
                               << ") (socket="
                               << new_sd
                               << "). FILE:" << file
                               << ": " << line << ".";
                            return ss.str();
                            }(__FILE__, __LINE__));
                        _this->end_proxy = true;
                        _this->c_last_err = RES_CODE_ERROR;
                    }

                    break;
                }
                else {
                    // RU: Есть новое соединение.
                    // Попытка сделать его неблокирующим.
                    int optval = _this->client_keep_alive ? 1 : 0;
                    socklen_t optlen = sizeof(optval);

                    rc_prx_hlpr = _this->set_nonblock(new_sd,
                        [&_this](int fd, int){
                            (void) ::close(fd);
                            _this->end_proxy = true;
                            _this->c_last_err = RES_CODE_ERROR;
                    });

                    if(RES_CODE_OK != rc_prx_hlpr) {
                        break;
                    }

                    rc = ::setsockopt(new_sd, SOL_SOCKET,
                                      SO_KEEPALIVE, &optval, optlen);
                    if(rc < 0) {
                        l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                          ->std::string {
                              std::stringstream ss;
                              ss << "C: 'setsockopt' failed ("
                                 << ::strerror(errno)
                                 << ") (socket="
                                 << new_sd
                                 << "). FILE:" << file
                                 << ": " << line << ".";
                              return ss.str();
                        }(__FILE__, __LINE__));
                    }

                    rc = ::getsockopt(new_sd, SOL_SOCKET,
                                      SO_KEEPALIVE, &optval, &optlen);
                    if(rc < 0) {
                        l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                          ->std::string {
                              std::stringstream ss;
                              ss << "C: 'setsockopt' failed ("
                                 << ::strerror(errno)
                                 << ") (socket="
                                 << new_sd
                                 << "). FILE:" << file
                                 << ": " << line << ".";
                              return ss.str();
                        }(__FILE__, __LINE__));
                    }
                    else {
                        l(Ilog::LEVEL_DEBUG, [&](auto file, auto line)
                          ->std::string {
                            std::stringstream ss;
                            ss << "C: SO_KEEPALIVE is "
                               << (optval ? "ON" : "OFF")
                               << " on socket=" << new_sd
                               << ". FILE:" << file
                               << ": " << line << ".";
                            return ss.str();
                        }(__FILE__, __LINE__));
                    }

                    // RU: Новый клиент подключен.
                    [&]()->void {
                        data d(DIRECTION_CLIENT_TO_WORKER,
                               TOD_NEW_CONNECT,
                               new_sd, -1,
                               0, nullptr,
                               &client_addr, &proxy_addr, nullptr);

                        rc = ::write(w_out_fd, &d, sizeof(d));
                        if(rc < 0) {
                            l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                              ->std::string {
                                  std::stringstream ss;
                                  ss << "C: 'write' failed ("
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

                        d.direction = DIRECTION_CLIENT_TO_SERVER;

                        rc = ::write(s_out_fd, &d, sizeof(d));
                        if(rc < 0) {
                            l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                              ->std::string {
                                  std::stringstream ss;
                                  ss << "C: 'write' failed ("
                                     << ::strerror(errno)
                                     << ") (fd="
                                     << s_out_fd
                                     << "). FILE:" << file
                                     << ": " << line << ".";
                                  return ss.str();
                            }(__FILE__, __LINE__));
                        }
                        else {
                            assert(rc == sizeof(d));
                        }
                    }();

                    l(Ilog::LEVEL_INFO, [&](auto file, auto line)
                      ->std::string {
                        std::stringstream ss;
                        ss << "C: New incoming connection ("
                           << "socket=" << new_sd << "; "
                           << inet_ntoa(client_addr.sin_addr)
                           << ":"
                           << ntohs(client_addr.sin_port)
                           << "). FILE:" << file
                           << ": " << line << ".";
                        return ss.str();
                    }(__FILE__, __LINE__));

                    db[new_sd] = -1;
                    fds[nfds].fd = new_sd;
                    fds[nfds].events = POLLIN;
                    nfds++;
                }
            }
            while(new_sd != -1);
        };

        ///
        /// RU: Данные от воркера
        ///
        auto from_worker_f = [&](){
            // RU: Получены данные от воркера.
            //     Данные с этого канала приходить не должны!

            data d;

            rc = ::read(cur_fd, &d, sizeof(d));
            if(rc < 0) {
                // TODO: проверить код ошибки!
                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)->std::string {
                    std::stringstream ss;
                    ss << "C: 'read' failed ("
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

            _this->debug_log_info(d, "C");
        };

        ///
        /// RU: Данные от сервера
        ///
        auto from_server_f = [&](){
            // RU: Получены данные от сервера.
            //     Надо их обработать и, в случае необходимости,
            //     отправить клиенту

            data d;

            rc = ::read(cur_fd, &d, sizeof(d));
            if(rc < 0) {
                // TODO: проверить код ошибки!
                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)->std::string {
                      std::stringstream ss;
                      ss << "C: 'read' failed ("
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

            _this->debug_log_info(d, "C");

            // RU: Проверить, а данные точно от сервера?
            if(DIRECTION_SERVER_TO_CLIENT == d.direction) {
                switch(d.tod) {
                case TOD_NEW_CONNECT:
                    do {
                        // RU: Сервер подтвердил установку соединения
                        auto search = db.find(d.c_sd);
                        if(search != db.end()) {
                            // RU: Соединение найдено
                            db[d.c_sd] = d.s_sd;
                        }
                        else {
                            // RU: Найти подобное соединение не удалось
                            l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                              ->std::string {
                                std::stringstream ss;
                                ss << "C: Unknown socket descriptor "
                                   << "(new connect)! (c_sd="
                                   << d.c_sd << "; s_sd" << d.s_sd
                                   << "). FILE:" << file
                                   << ": " << line << ".";
                                return ss.str();
                                }(__FILE__, __LINE__));

                            data a(DIRECTION_CLIENT_TO_WORKER,
                                   TOD_CONNECT_NOT_FOUND,
                                   d.c_sd, d.s_sd,
                                   0, nullptr,
                                   nullptr, nullptr, nullptr);

                            rc = ::write(w_out_fd, &a, sizeof(a));
                            if(rc < 0) {
                                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                                  ->std::string {
                                      std::stringstream ss;
                                      ss << "C: 'write' failed ("
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

                            d.direction = DIRECTION_CLIENT_TO_SERVER;
                            rc = ::write(s_out_fd, &a, sizeof(a));
                            if(rc < 0) {
                                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                                  ->std::string {
                                      std::stringstream ss;
                                      ss << "C: 'write' failed ("
                                         << ::strerror(errno)
                                         << ") (fd="
                                         << s_out_fd
                                         << "). FILE:" << file
                                         << ": " << line << ".";
                                      return ss.str();
                                }(__FILE__, __LINE__));
                            }
                            else {
                                assert(rc == sizeof(d));
                            }
                        }
                    }
                    while(false);
                    break;
                case TOD_DATA:
                    do {
                        auto search = db.find(d.c_sd);
                        if(search != db.end()) {
                            // RU: Соединение найдено.
                            //     Отправить данные клиенту
                            rc = send(d.c_sd, d.buffer, d.buffer_len, 0);
                            if(rc < 0) {
                                if(errno != EWOULDBLOCK) {
                                    l(Ilog::LEVEL_ERROR, [&](auto file,
                                      auto line)->std::string {
                                          std::stringstream ss;
                                          ss << "C: 'send' failed ("
                                             << ::strerror(errno)
                                             << ") (socket="
                                             << cur_fd
                                             << "). FILE:" << file
                                             << ": " << line << ".";
                                          return ss.str();
                                    }(__FILE__, __LINE__));
                                }
                            }
                        }
                        else {
                            // RU: Найти подобное соединение не удалось
                            l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                              ->std::string {
                                std::stringstream ss;
                                ss << "C: Unknown socket descriptor (data)! "
                                   << "(c_sd=" << d.c_sd << "; s_sd" << d.s_sd
                                   << "). FILE:" << file
                                   << ": " << line << ".";
                                return ss.str();
                            }(__FILE__, __LINE__));

                            data a(DIRECTION_CLIENT_TO_WORKER,
                                   TOD_CONNECT_NOT_FOUND,
                                   d.c_sd, d.s_sd,
                                   0, nullptr,
                                   nullptr, nullptr, nullptr);

                            rc = ::write(w_out_fd, &a, sizeof(a));
                            if(rc < 0) {
                                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                                  ->std::string {
                                      std::stringstream ss;
                                      ss << "C: 'write' failed ("
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

                            d.direction = DIRECTION_CLIENT_TO_SERVER;
                            rc = ::write(s_out_fd, &a, sizeof(a));
                            if(rc < 0) {
                                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                                  ->std::string {
                                      std::stringstream ss;
                                      ss << "C: 'write' failed ("
                                         << ::strerror(errno)
                                         << ") (fd="
                                         << s_out_fd
                                         << "). FILE:" << file
                                         << ": " << line << ".";
                                      return ss.str();
                                }(__FILE__, __LINE__));
                            }
                            else {
                                assert(rc == sizeof(d));
                            }
                        }
                    }
                    while(false);
                    break;
                case TOD_NOT_CONNECT:
                    // RU: Сервер так и не смог установить соединение и
                    //     сообщает об этом
                case TOD_DISCONNECT:
                    // RU: Разрыв соединения по инициативе сервера
                    l(Ilog::LEVEL_DEBUG, [&](auto file, auto line)
                      ->std::string {
                          std::stringstream ss;
                          ss << "C: It's a signal from server thread - "
                              << "'connect failed' or 'disconnect'. ("
                              << "c_sd=" << d.c_sd << "; s_sd=" << d.s_sd
                              << "). FILE:" << file
                              << ": " << line << ".";
                          return ss.str();
                    }(__FILE__, __LINE__));

                    do {
                        (void) ::close(d.c_sd);

                        db.erase(d.c_sd);

                        int count = nfds;
                        for(int i = 0; i < count; i++) {
                            if(fds[i].fd == d.c_sd) {
                               fds[i].fd = -1;
                               break;
                            }
                        }

                        compress_array = true;
                    }
                    while(false);
                    break;
                case TOD_CONNECT_NOT_FOUND:
                    // RU: Игнорируем данное сообщение. Скорее всего, сервер
                    //     получил сообщение о соединении, которое уже
                    //     закрыто (т.е. он не может обработать данные).
                    l(Ilog::LEVEL_DEBUG, [&](auto file, auto line)
                      ->std::string {
                          std::stringstream ss;
                          ss << "C: It's a signal from server thread - "
                              << "'connect not found'. ("
                              << "c_sd=" << d.c_sd << "; s_sd=" << d.s_sd
                              << "). FILE:" << file
                              << ": " << line << ".";
                          return ss.str();
                    }(__FILE__, __LINE__));
                default:
                    break;
                }
            }
            else {
                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)->std::string {
                      std::stringstream ss;
                      ss << "C: Internal error! WTF!? "
                         << "FILE:" << file
                         << ": " << line << ".";
                      return ss.str();
                }(__FILE__, __LINE__));
            }
        };

        ///
        /// RU: Данные от клиентов
        ///
        auto from_clients_f = [&](){
            struct sockaddr_in client_addr;
            socklen_t client_addr_len = 0;
            bool close_conn = false;
            int count_bytes = 0;
            int srv_cur_fd = -1;

#ifdef USE_FULL_DEBUG
            l(Ilog::LEVEL_DEBUG, [&](auto file, auto line)->std::string {
                  std::stringstream ss;
                  ss << "C: Descriptor is readable (socket=" << cur_fd << "). "
                     << "FILE:" << file
                     << ": " << line << ".";
                  return ss.str();
            }(__FILE__, __LINE__));
#endif // USE_FULL_DEBUG

            close_conn = false;

            ::memset(&client_addr, 0, sizeof(client_addr));

            rc = ::getsockname(cur_fd,
                               reinterpret_cast<struct sockaddr*>(
                                   &client_addr),
                               &client_addr_len);
            if(rc < 0) {
                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)->std::string {
                    std::stringstream ss;
                    ss << "C: 'getsockname' failed ("
                       << ::strerror(errno)
                       << ") (socket="
                       << cur_fd
                       << "). FILE:" << file
                       << ": " << line << ".";
                    return ss.str();
                    }(__FILE__, __LINE__));

                _this->c_last_err = RES_CODE_ERROR;
                _this->end_proxy = true;
                return;
            }

            rc = ::ioctl(cur_fd, FIONREAD, &count_bytes);
            if(rc < 0) {
                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)->std::string {
                      std::stringstream ss;
                      ss << "C: 'read' failed (with FIONREAD) ("
                         << ::strerror(errno)
                         << ") (socket="
                         << cur_fd
                         << "). FILE:" << file
                         << ": " << line << ".";
                      return ss.str();
                }(__FILE__, __LINE__));

                close_conn = true;
            }

#ifdef USE_FULL_DEBUG
            l(Ilog::LEVEL_DEBUG, [&](auto file, auto line)->std::string {
                std::stringstream ss;
                ss << "C: Bytes available for reading (socket="
                   << cur_fd
                   << "): "
                   << count_bytes
                   << "; rc = "
                   << rc
                   << ". FILE:" << file
                   << ": " << line << ".";
                return ss.str();
            }(__FILE__, __LINE__));
#endif // USE_FULL_DEBUG

            // RU: Если мы здесь оказались, значит на сокете точно
            //     произошло какое-то событие. Если данных в сокете нет,
            //     то это событие - закрытие соединения клиентом.
            //     Если данные есть, то необходимо убедиться, готов ли
            //     сервер принять их?
            if(!count_bytes) {
                l(Ilog::LEVEL_INFO, [&](auto file, auto line)->std::string {
                    std::stringstream ss;
                    ss << "C: Connection closed (socket=" << cur_fd
                       << "). FILE:" << file
                       << ": " << line << ".";
                    return ss.str();
                }(__FILE__, __LINE__));

                close_conn = true;
            }
            else {
                srv_cur_fd = db[cur_fd];

                if(srv_cur_fd < 0) {
                    // RU: Сервер не готов принять данные
#ifdef USE_FULL_DEBUG
                    l(Ilog::LEVEL_DEBUG, "C: Server is not ready. Waiting...");
#endif // USE_FULL_DEBUG
                }
                else {
                    do {
                        rc = ::recv(cur_fd, buffer, sizeof(buffer), 0);
                        if(rc < 0) {
                            if(errno != EWOULDBLOCK) {
                                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                                  ->std::string {
                                      std::stringstream ss;
                                      ss << "C: 'recv' failed ("
                                         << ::strerror(errno)
                                         << ") (socket="
                                         << cur_fd
                                         << "). FILE:" << file
                                         << ": " << line << ".";
                                      return ss.str();
                                }(__FILE__, __LINE__));

                                close_conn = true;
                            }

                            // RU: Выход из цикла по причине отсутствия
                            // данных в сокете (все данные прочитаны или
                            // произошла ошибка)
                            break;
                        }
                        else if(0 == rc) {
                            l(Ilog::LEVEL_INFO, [&](auto file, auto line)
                              ->std::string {
                                std::stringstream ss;
                                ss << "C: Connection closed (socket=" << cur_fd
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
                        data d(DIRECTION_CLIENT_TO_WORKER,
                               TOD_DATA,
                               cur_fd, srv_cur_fd,
                               len, buffer,
                               &client_addr, &proxy_addr, nullptr);

                        rc = ::write(w_out_fd, &d, sizeof(d));
                        if(rc < 0) {
                            l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                              ->std::string {
                                  std::stringstream ss;
                                  ss << "C: 'write' failed ("
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

                        d.direction = DIRECTION_CLIENT_TO_SERVER;
                        rc = ::write(s_out_fd, &d, sizeof(d));
                        if(rc < 0) {
                            l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                              ->std::string {
                                  std::stringstream ss;
                                  ss << "C: 'write' failed ("
                                     << ::strerror(errno)
                                     << ") (fd="
                                     << s_out_fd
                                     << "). FILE:" << file
                                     << ": " << line << ".";
                                  return ss.str();
                            }(__FILE__, __LINE__));
                        }
                        else {
                            assert(rc == sizeof(d));
                        }

                        break; // DEBUG. Чтобы работыло в несколько потоков
                    }
                    while(true);
                }
            }

            if(close_conn) {
                data d(DIRECTION_CLIENT_TO_WORKER,
                       TOD_DISCONNECT,
                       cur_fd, db[cur_fd],
                       0, nullptr,
                       &client_addr, &proxy_addr, nullptr);

                rc = ::write(w_out_fd, &d, sizeof(d));
                if(rc < 0) {
                    l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                      ->std::string {
                          std::stringstream ss;
                          ss << "C: 'write' failed ("
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

                d.direction = DIRECTION_CLIENT_TO_SERVER;
                rc = ::write(s_out_fd, &d, sizeof(d));
                if(rc < 0) {
                    l(Ilog::LEVEL_ERROR, [&](auto file, auto line)
                      ->std::string {
                          std::stringstream ss;
                          ss << "C: 'write' failed ("
                             << ::strerror(errno)
                             << ") (fd="
                             << s_out_fd
                             << "). FILE:" << file
                             << ": " << line << ".";
                          return ss.str();
                    }(__FILE__, __LINE__));
                }
                else {
                    assert(rc == sizeof(d));
                }

                (void) ::close(cur_fd);
                db.erase(cur_fd);
                cur_fd = -1;
                compress_array = true;
            }
        };

        ///
        /// RU: Подготовка работы клиентского потока
        ///
        auto prepare_f = [&](){
            _this->c_last_err = RES_CODE_OK;

            listen_sd = ::socket(AF_INET, SOCK_STREAM, 0);
            if(listen_sd < 0) {
                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)->std::string {
                      std::stringstream ss;
                      ss << "C: 'socket' failed ("
                         << ::strerror(errno)
                         << ") (socket="
                         << listen_sd
                         << "). FILE:" << file
                         << ": " << line << ".";
                      return ss.str();
                }(__FILE__, __LINE__));

                _this->c_last_err = RES_CODE_ERROR;
                throw go_to_finish();
            }

            on = 1;
            rc = ::setsockopt(listen_sd, SOL_SOCKET, SO_REUSEADDR,
                              reinterpret_cast<char*>(&on), sizeof(on));
            if(rc < 0) {
                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)->std::string {
                      std::stringstream ss;
                      ss << "C: 'setsockopt' failed ("
                         << ::strerror(errno)
                         << ") (socket="
                         << listen_sd
                         << "). FILE:" << file
                         << ": " << line << ".";
                      return ss.str();
                }(__FILE__, __LINE__));

                (void) ::close(listen_sd);
                _this->c_last_err = RES_CODE_ERROR;
                throw go_to_finish();
            }

            rc_prx_hlpr = _this->set_nonblock(listen_sd, [&_this](int fd, int){
                (void) ::close(fd);
                _this->c_last_err = RES_CODE_ERROR;
            });

            if(RES_CODE_OK != rc_prx_hlpr) {
                throw go_to_finish();
            }

            (void) ::memset(&proxy_addr, 0, sizeof(proxy_addr));

            proxy_addr.sin_family = AF_INET;
            proxy_addr.sin_port = htons(_this->proxy_port);
            proxy_addr.sin_addr.s_addr = htonl(INADDR_ANY);

            rc = ::bind(listen_sd,
                        reinterpret_cast<struct sockaddr*>(&proxy_addr),
                        sizeof(proxy_addr));
            if(rc < 0) {
                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)->std::string {
                      std::stringstream ss;
                      ss << "C: 'bind' failed ("
                         << ::strerror(errno)
                         << ") (socket="
                         << listen_sd
                         << "). FILE:" << file
                         << ": " << line << ".";
                      return ss.str();
                }(__FILE__, __LINE__));

                _this->c_last_err = RES_CODE_ERROR;
                throw go_to_finish();
            }

            rc = ::listen(listen_sd, 1);
            if(rc < 0) {
                l(Ilog::LEVEL_ERROR, [&](auto file, auto line)->std::string {
                      std::stringstream ss;
                      ss << "C: 'listen' failed ("
                         << ::strerror(errno)
                         << ") (socket="
                         << listen_sd
                         << "). FILE:" << file
                         << ": " << line << ".";
                      return ss.str();
                }(__FILE__, __LINE__));

                (void) ::close(listen_sd);
                _this->c_last_err = RES_CODE_ERROR;
                throw go_to_finish();
            }

            (void) ::memset(fds, 0, sizeof(fds));

            nfds = 0;

            fds[nfds].fd = s_in_fd;
            fds[nfds].events = POLLIN;
            nfds++;

            fds[nfds].fd = w_in_fd;
            fds[nfds].events = POLLIN;
            nfds++;

            fds[nfds].fd = listen_sd;
            fds[nfds].events = POLLIN;
            nfds++;

            timeout = _this->client_poll_timeout;

            cur_fd = -1;
        };

        ///
        /// RU: Основная функция потока
        ///
        auto run_f = [&](){
            do {
#ifdef USE_FULL_DEBUG
    #ifdef USE_FULL_DEBUG_POLL_INTERVAL
                l(Ilog::LEVEL_DEBUG, [&](auto file, auto line)->std::string {
                    std::stringstream ss;
                    ss << "C: Waiting on poll (client)... "
                       << "FILE:" << file
                       << ": " << line << ".";
                    return ss.str();
                }(__FILE__, __LINE__));
    #endif // USE_FULL_DEBUG_POLL_INTERVAL
#endif // USE_FULL_DEBUG

                rc = ::poll(fds, nfds, timeout);
                if(rc < 0) {
                    l(Ilog::LEVEL_ERROR, [&](auto file, auto line)->std::string {
                          std::stringstream ss;
                          ss << "C: 'poll' failed ("
                             << ::strerror(errno)
                             << "). FILE:" << file
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
                    else if(POLLIN != fds[i].revents) {
                        // RU: Пришли данные, по событию, которое мы не запрашивали.
                        l(Ilog::LEVEL_ERROR, [&](auto file, auto line)->std::string {
                            std::stringstream ss;
                            ss << "C: Revents aren't pure POLLIN ("
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

                        _this->end_proxy = true;
                        _this->c_last_err = RES_CODE_ERROR;
                        break;
                    }

                    cur_fd = fds[i].fd;

                    if(cur_fd == -1) {
                        compress_array = true;
                        continue;
                    }

                    if(cur_fd == listen_sd) {
                        new_connect_f();
                    }
                    else if(cur_fd == s_in_fd) {
                        from_server_f();
                    }
                    else if(cur_fd == w_in_fd) {
                        from_worker_f();
                    }
                    else {
                        from_clients_f();
                    }

                    fds[i].fd = cur_fd;
                }

                if(compress_array) {
                    compress_array_f();
                }
            }
            while(!_this->end_proxy);
        };

        ///
        /// Завершение работы потока
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
                      ss << "C: Unknown exception! WTF!? "
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
