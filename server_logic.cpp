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

#include <map>
#include <array>
#include <vector>
#include <list>
#include <deque>
#include <list>
#include <algorithm>
#include <numeric>
#include <iterator>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ios>
#include <new>
#include <chrono>

#include <ctime>
#include <cerrno>
#include <cstring>

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
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
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#include "log.hpp"
#include "proxy_result.hpp"
#include "proxy.hpp"
//#include "proxy_impl.hpp" // Instead of "proxy_impl.hpp" we use "proxy.hpp".

#include "server_logic.hpp"

namespace proxy_ns {
    /* ***************************************************************** */
    /* ********************** CLASS: server_logic ********************** */
    /* **************************** PUBLIC ***************************** */
    /* ***************************************************************** */

    ///
    /// \brief server_logic::server_logic
    /// \param _s_arg
    /// \param _pi
    ///
    server_logic::server_logic(server_routine_arg* _s_arg, proxy_impl* _pi) :
        s_arg(_s_arg),
        pi(_pi),
        l(new proxy_ns::common_logic_log("S")),
        c_in_fd(_s_arg->_cs_in_pd),
        c_out_fd(_s_arg->_sc_out_pd),
        w_in_fd(_s_arg->_ws_in_pd),
        w_out_fd(_s_arg->_sw_out_pd),
        nfds(0),
        timeout(0),
        flag_compress_array(false),
        cur_fd(-1),
        cur_events(0),
        cur_revents(0),
        c_read_enable(false),
        w_read_enable(false),
        c_write_enable(false),
        w_write_enable(false) {

        std::fill_n(reinterpret_cast<char*>(this->fds),
                    sizeof(this->fds), '\0');
    }

    ///
    /// \brief server_logic::~server_logic
    ///
    server_logic::~server_logic(void) noexcept {
        this->done();
    }

    ///
    /// \brief server_logic::prepare
    ///
    void server_logic::prepare(void) {
        this->pi->s_last_err = RES_CODE_OK;

        this->nfds = 0;

        this->fds[nfds].fd = this->c_in_fd;
        this->fds[nfds].events = POLLIN;
        this->nfds++;

        this->fds[nfds].fd = this->w_in_fd;
        this->fds[nfds].events = POLLIN;
        this->nfds++;

        this->timeout = this->pi->server_poll_timeout;
        this->flag_compress_array = false;
    }

    ///
    /// \brief server_logic::run
    ///
    void server_logic::run(void) {
        auto connects_present = [&]() {
            this->fds[nfds].fd = this->c_out_fd;
            this->fds[nfds].events = POLLOUT;
            this->nfds++;

            this->fds[nfds].fd = this->w_out_fd;
            this->fds[nfds].events = POLLOUT;
            this->nfds++;
        };

        auto connects_absence = [&]() {
            nfds -= 2;
        };

        //bool have_connects = false;

        while(!this->pi->end_proxy) {
            this->c_read_enable = false;
            this->w_read_enable = false;
            this->c_write_enable = false;
            this->w_write_enable = false;

            constexpr size_t const data_size = sizeof(data);

            static int const min_descriptors_count_ro = 2;
            static int const min_descriptors_count_rw = 4;
#ifdef USE_FULL_DEBUG
    #ifdef USE_FULL_DEBUG_POLL_INTERVAL
            l(Ilog::LEVEL_DEBUG, "S: Waiting on poll (server)...");
    #endif // USE_FULL_DEBUG_POLL_INTERVAL
#endif // USE_FULL_DEBUG

            this->erase_old_wait_connect();

            int rc = ::poll(this->fds, this->nfds, this->timeout);
            if(rc < 0) {
                this->l.get()->error_poll_failed(__FILE__, __LINE__, errno);
                this->pi->s_last_err = RES_CODE_ERROR;
                throw Eserver_logic_fatal();
            }
            else if(rc) {
                if(min_descriptors_count_ro == this->nfds) {
                    connects_present();
                }
                else if(min_descriptors_count_rw == this->nfds) {
                    connects_absence();
                }

                int current_size = this->nfds;

                for(int i = 0; i < current_size; i++) {
                    if(0 == this->fds[i].revents) {
                        continue;
                    }
                    else if(this->fds[i].revents & POLLHUP) {
                        this->l.get()->debug_revent_includes_pollhup(
                                    __FILE__, __LINE__, this->fds[i].fd);

                        if(this->fds[i].fd != this->c_in_fd &&
                           this->fds[i].fd != this->w_in_fd) {
                            this->send_disconnect(this->db[this->fds[i].fd],
                                                  this->fds[i].fd);
                            this->calculate_count_lost(this->fds[i].fd);
                            this->l.get()->info_connect_close(
                                __FILE__, __LINE__, this->fds[i].fd,
                                this->counter_sent[this->fds[i].fd],
                                this->counter_recv[this->fds[i].fd],
                                this->counter_buffered[this->fds[i].fd],
                                this->counter_lost[this->fds[i].fd]);
                            this->close_connect_force(this->fds[i].fd);
                        }
                        else {
                            this->pi->s_last_err = RES_CODE_ERROR;
                            throw Eserver_logic_fatal();
                        }
                    }
                    else if(this->fds[i].revents & POLLERR) {
                        this->l.get()->debug_revent_includes_pollerr(
                                    __FILE__, __LINE__, this->fds[i].fd);

                        if(this->fds[i].fd != this->c_in_fd &&
                           this->fds[i].fd != this->w_in_fd) {
                            this->send_disconnect(this->db[this->fds[i].fd],
                                                  this->fds[i].fd);
                            this->calculate_count_lost(this->fds[i].fd);
                            this->l.get()->info_connect_close(
                                __FILE__, __LINE__, this->fds[i].fd,
                                this->counter_sent[this->fds[i].fd],
                                this->counter_recv[this->fds[i].fd],
                                this->counter_buffered[this->fds[i].fd],
                                this->counter_lost[this->fds[i].fd]);
                            this->close_connect_force(this->fds[i].fd);
                        }
                        else {
                            this->pi->s_last_err = RES_CODE_ERROR;
                            throw Eserver_logic_fatal();
                        }
                    }
                    else if(this->fds[i].revents & POLLNVAL) {
                        this->l.get()->debug_revent_includes_pollnval(
                                    __FILE__, __LINE__, this->fds[i].fd);
                        this->l.get()->error_inernal_error(__FILE__, __LINE__);
                        this->pi->s_last_err = RES_CODE_ERROR;
                        throw Eserver_logic_fatal();
                    }

                    this->cur_fd = this->fds[i].fd;
                    this->cur_events = this->fds[i].events;
                    this->cur_revents = this->fds[i].revents;

                    if(this->cur_fd == -1) {
                        this->flag_compress_array = true;
                        continue;
                    }

                    if(this->cur_fd == this->c_in_fd) {
                        this->c_read_enable = true;
                        this->from_client();
                    }
                    else if(this->cur_fd == this->w_in_fd) {
                        this->w_read_enable = true;
                        this->from_worker();
                    }
                    else if(this->cur_fd == this->c_out_fd) {
                        this->c_write_enable =
                                this->pi->can_write_to_pipe_data(
                                    this->cur_fd, data_size);
                    }
                    else if(this->cur_fd == this->w_out_fd) {
                        this->w_write_enable =
                                this->pi->can_write_to_pipe_data(
                                    this->cur_fd, data_size);
                    }
                    else {
                        if(this->c_write_enable && this->w_write_enable) {
                            this->from_server();
                        }
                    }
                }

                if(this->flag_compress_array) {
                    this->flag_compress_array = false;
                    this->compress_array();
                }
            }
        }
    }

    ///
    /// \brief server_logic::done
    ///
    void server_logic::done(void) noexcept {
        try {
            for(int i = 0; i < this->nfds; i++) {
                if(this->fds[i].fd >= 0) {
                    (void) ::close(this->fds[i].fd);
                }
            }

            this->pi->end_proxy = true;
        }
        catch(...) {
            this->l.get()->error_unknown_exception(__FILE__, __LINE__);
        }
    }

    /* ***************************************************************** */
    /* ********************** CLASS: server_logic ********************** */
    /* *************************** PROTECTED *************************** */
    /* ***************************************************************** */

    ///
    /// \brief server_logic::from_worker
    ///
    void server_logic::from_worker(void) {
        data d;

        this->read_data(this->cur_fd, d);

        this->pi->debug_log_info(d, "S");
    }

    ///
    /// \brief server_logic::from_client
    ///
    void server_logic::from_client(void) {
        data d;

        if(this->read_data(this->cur_fd, d)) {
            this->pi->debug_log_info(d, "S");

            // RU: Проверить, а данные точно от клиента?
            if(DIRECTION_CLIENT_TO_SERVER == d.direction) {
                switch(d.tod) {
                case TOD_NEW_CONNECT:
                    this->from_client_new_connect(d);
                    break;
                case TOD_DATA:
                    this->from_client_data(d);
                    break;
                case TOD_CONNECT_NOT_FOUND:
                    this->from_client_connect_not_found(d);
                    break;
                case TOD_DISCONNECT:
                    this->from_client_disconnect(d);
                    break;
                default:
                    // RU: По-идее, в данную секцию попадать не должны. Однако,
                    //     если мы тут оказались - это вовсе не означает что
                    //     произошла ошибка. Но обращать внимание на это стоит.
                    this->l.get()->debug_unsupported_data_client(
                                __FILE__, __LINE__, d.tod);
                    break;
                }
            }
            else {
                this->l.get()->error_inernal_error(__FILE__, __LINE__);
            }
        }
    }

    ///
    /// \brief server_logic::from_server
    ///
    void server_logic::from_server(void) {
        bool close_conn = false;
        bool cont = true;
        bool for_close = false;

#ifdef USE_FULL_DEBUG
        log_ns::log::inst()(Ilog::LEVEL_DEBUG, [&](auto file, auto line)
        ->std::string {
              std::stringstream ss;
              ss << "S: Descriptor is readable or writable (socket="
                 << this->cur_fd << ")."
                 << " FILE:" << file << ": " << line << ".";
              return ss.str();
        }(__FILE__, __LINE__));
#endif // USE_FULL_DEBUG

        if(this->cur_revents & POLLOUT) {
            // RU: Сокет доступен для записи
            auto search_wait = this->db_con_wait.find(this->cur_fd);
            if(search_wait != this->db_con_wait.end()) {
                // RU: Текущий сокет ожидает подключения
                socklen_t err_len = 0;
                int error = 0;
                int rc = 0;

                err_len = sizeof(error);
                rc = ::getsockopt(this->cur_fd, SOL_SOCKET, SO_ERROR,
                                  &error, &err_len);
                if(rc < 0 || error) {
                    // RU: Произошла ошибка - соединение не удалось
                    this->l.get()->info_server_not_respond(
                                __FILE__, __LINE__, rc, errno, this->cur_fd);

                    this->send_not_connect(this->db[this->cur_fd], -1);
                    this->close_connect_force(this->cur_fd);

                    cont = false;
                }
                else {
                    // RU: Сокет, ожидающий подключения, подключился
                    this->l.get()->info_connect_successful(
                                __FILE__, __LINE__, this->cur_fd);

                    this->send_new_connect(this->db[this->cur_fd], this->cur_fd);

                    cont = true;
                }

                // RU: В любом случае, данный дескриптор более не
                //     находится среди ожидающих окончания соединения
                db_con_wait.erase(this->cur_fd);
            }
            else {
                auto search_close = std::find(
                            this->db_for_close.begin(),
                            this->db_for_close.end(),
                            this->cur_fd);
                if(search_close != this->db_for_close.end()) {
                    // RU: Данный сокет ожидает завершения
                    for_close = true;
                }

                if(!this->empty_data_storage(this->cur_fd)) {
                    // RU: Есть неотправленные данные
                    unsigned char* buf = nullptr;
                    unsigned int buf_size = 0;
                    int index = 0;

                    buf = const_cast<unsigned char*>(this->get_data_storage(
                                                     this->cur_fd, buf_size));

                    int rc = ::send(this->cur_fd, buf + index, buf_size, 0);
                    if(rc < 0) {
                        if(errno != EWOULDBLOCK) {
                            this->l.get()->error_send_failed(
                                        __FILE__, __LINE__,
                                        errno,
                                        this->cur_fd);
                        }
                    }
                    else {
                        // Отправка данных удалась
                        this->counter_sent[this->cur_fd] += rc;
                        if(static_cast<unsigned int>(rc) != buf_size) {
                            // RU: не все данные отправлены
                            buf_size = buf_size - rc;
                            index += rc;

                            boost::shared_ptr<std::vector<unsigned char>> v =
                                    boost::make_shared<
                                        std::vector<unsigned char>>();

                            std::copy(buf + index, buf + index + buf_size,
                                      std::back_inserter(*v));

                            this->delete_data_storage(this->cur_fd);

                            this->save_unset_data_storage(this->cur_fd,
                                                          v.get()->data(),
                                                          v.get()->size());
                        }
                        else {
                            this->delete_data_storage(this->cur_fd);
                        }
                    }
                }

                if(for_close && this->empty_data_storage(this->cur_fd)) {
                    // RU: сокет ожидает завершения и все данные отправлены
                    //     (нет неотправленных данных)
                    this->calculate_count_lost(this->cur_fd);
                    this->l.get()->info_connect_close(
                        __FILE__, __LINE__, this->cur_fd,
                        this->counter_sent[this->cur_fd],
                        this->counter_recv[this->cur_fd],
                        this->counter_buffered[this->cur_fd],
                        this->counter_lost[this->cur_fd]);
                    this->close_connect_force(this->cur_fd);

                    cont = false;
                    close_conn = false;
                }
            }
        }

        if(this->cur_revents & POLLIN) {
            // RU: сокет доступен для чтения
            if(cont) {
                unsigned char buffer[DATA_BUFFER_SIZE] = { 0 };
                size_t buf_size = sizeof(buffer);

                std::fill_n(reinterpret_cast<char*>(buffer),
                            buf_size, '\0');

                this->read_data_socket(this->cur_fd, buffer, buf_size,
                    [this, &close_conn, &cont](int err) -> void {
                        // rc < 0
                        if((err != EWOULDBLOCK) && (err != EAGAIN)) {
                            this->l.get()->error_recv_failed(
                                __FILE__, __LINE__, err, this->cur_fd);
                            close_conn = true;
                        }
                    },
                    [this, &close_conn, &cont](void) -> void {
                        // rc == 0
                        close_conn = true;
                    },
                    [this, &cont, &for_close](int rc, unsigned char* buf,
                                              size_t size) -> void {
                        // rc > 0
                        boost::ignore_unused(size);
                        if(!for_close) {
                            size_t len = rc;
                            this->send_data(this->db[this->cur_fd],
                                            this->cur_fd,
                                            len, buf);
                        }
                });
            }
        }

        if(close_conn) {
            this->send_disconnect(this->db[this->cur_fd], this->cur_fd);

            this->calculate_count_lost(this->cur_fd);

            this->l.get()->info_connect_close(
                __FILE__, __LINE__, this->cur_fd,
                this->counter_sent[this->cur_fd],
                this->counter_recv[this->cur_fd],
                this->counter_buffered[this->cur_fd],
                this->counter_lost[this->cur_fd]);

            this->close_connect_force(this->cur_fd);
        }
    }

    ///
    /// \brief server_logic::from_client_new_connect
    /// \param d
    ///
    void server_logic::from_client_new_connect(data const& d) {
        int rc_ = RES_CODE_OK;
        int rc = 0;
        int new_server_sd = 0;
        struct sockaddr_in server_addr;
        int val = 0;

        new_server_sd = ::socket(AF_INET, SOCK_STREAM, 0);
        if(new_server_sd < 0) {
            this->l.get()->error_socket_failed(
                        __FILE__, __LINE__, errno, new_server_sd);
            this->pi->s_last_err = RES_CODE_ERROR;
            throw Eserver_logic_fatal();
        }

        // Nonblock
        rc_ = this->pi->set_nonblock(new_server_sd,
            this->pi->fok_placeholder,
            [this, &new_server_sd](int rc, int err) {
                boost::ignore_unused(rc);
                this->l.get()->error_ioctl_or_fcntl_failed(
                        __FILE__, __LINE__, err, new_server_sd);
            });

        if(RES_CODE_OK != rc_) {
            (void) ::close(new_server_sd);
            this->pi->s_last_err = RES_CODE_ERROR;
            throw Eserver_logic_fatal();
        }

        // Keep alive
        rc_ = this->pi->set_keep_alive(new_server_sd,
            this->pi->server_keep_alive,
            this->pi->fok_placeholder,
            [this, &new_server_sd](int rc, int err) {
                boost::ignore_unused(rc);
                this->l.get()->error_setsockopt_failed(
                        __FILE__, __LINE__, err, new_server_sd);
            });

        if(RES_CODE_OK != rc_) {
            (void) ::close(new_server_sd);
            this->pi->s_last_err = RES_CODE_ERROR;
            throw Eserver_logic_fatal();
        }

        rc_ = this->pi->get_keep_alive(new_server_sd, val,
            this->pi->fok_placeholder,
            [this, &new_server_sd](int rc, int err) {
                boost::ignore_unused(rc);
                this->l.get()->error_getsockopt_failed(
                        __FILE__, __LINE__, err, new_server_sd);
            });

        if(RES_CODE_OK != rc_) {
            (void) ::close(new_server_sd);
            this->pi->s_last_err = RES_CODE_ERROR;
            throw Eserver_logic_fatal();
        }
        else {
            this->l.get()->debug_keep_alive_onoff(
                        __FILE__, __LINE__, val, new_server_sd);
        }

        // TCP no delay (Nagle's algorithm)
        rc_ = this->pi->set_tcp_no_delay(new_server_sd,
            this->pi->server_tcp_no_delay,
            this->pi->fok_placeholder,
            [this, &new_server_sd](int rc, int err) {
                boost::ignore_unused(rc);
                this->l.get()->error_setsockopt_failed(
                        __FILE__, __LINE__, err, new_server_sd);
            });

        if(RES_CODE_OK != rc_) {
            (void) ::close(new_server_sd);
            this->pi->s_last_err = RES_CODE_ERROR;
            throw Eserver_logic_fatal();
        }

        rc_ = this->pi->get_tcp_no_delay(new_server_sd, val,
            this->pi->fok_placeholder,
            [this, &new_server_sd](int rc, int err) {
                boost::ignore_unused(rc);
                this->l.get()->error_getsockopt_failed(
                        __FILE__, __LINE__, err, new_server_sd);
            });

        if(RES_CODE_OK != rc_) {
            (void) ::close(new_server_sd);
            this->pi->s_last_err = RES_CODE_ERROR;
            throw Eserver_logic_fatal();
        }
        else {
            this->l.get()->debug_tcp_no_delay_onoff(
                        __FILE__, __LINE__, val, new_server_sd);
        }

        server_addr.sin_family =
                AF_INET;

        server_addr.sin_port =
                htons(this->pi->server_port);

        server_addr.sin_addr.s_addr =
                inet_addr(this->pi->server_ip.c_str());

        rc = ::connect(new_server_sd,
                       reinterpret_cast<struct sockaddr*>(
                           &server_addr),
                       sizeof(server_addr));
        if(rc < 0) {
            if(EINPROGRESS == errno) {
                // RU: Для установки соединения требуется время
                this->l.get()->debug_connect_take_time(
                            __FILE__, __LINE__, new_server_sd);

                this->db_con_wait[new_server_sd] =
                        std::chrono::system_clock::now();

                this->new_connect(new_server_sd, d.c_sd);
            }
            else {
                // RU: Соединение не удалось! Это не факт что
                //     ошибка. Вероятно, сервер недоступен.
                this->l.get()->error_connect_failed(
                            __FILE__, __LINE__, errno, new_server_sd);

                (void) ::close(new_server_sd);

                this->send_not_connect(d.c_sd, -1,
                                       0, nullptr,
                                       nullptr, nullptr, &server_addr);
            }
        }
        else {
            // RU: Соединение удалось сразу
            this->l.get()->info_connect_immediately(
                        __FILE__, __LINE__, new_server_sd);

            this->send_new_connect(d.c_sd, new_server_sd,
                                   0, nullptr,
                                   nullptr, nullptr, &server_addr);

            this->new_connect(new_server_sd, d.c_sd);
        }
    }

    ///
    /// \brief server_logic::from_client_data
    /// \param d
    ///
    void server_logic::from_client_data(data const& d) {
        if(d.s_sd < 0) {
            this->l.get()->error_inernal_error(__FILE__, __LINE__);
        }
        else {
            unsigned char* buf = nullptr;
            unsigned int buf_size = 0;
            int index = 0;
            bool direct = true;
            int server_index = this->find_pollfd(d.s_sd);

            if(server_index >= 0) {
                if(this->fds[server_index].revents & POLLOUT) {
                    // RU: серверный сокет найден в базе сервера и он
                    //     готов принимать данные
                    if(this->empty_data_storage(d.s_sd)) {
                        // No unsent data are present
                        buf = const_cast<unsigned char*>(&d.buffer[index]);
                        buf_size = d.buffer_len;

                        direct = true;
                    }
                    else {
                        this->save_new_data_storage(d.s_sd, d.buffer,
                                                    d.buffer_len);
                        buf = const_cast<unsigned char*>(this->get_data_storage(
                                                             d.s_sd, buf_size));

                        direct = false;
                    //}

                    int rc = ::send(d.s_sd, buf, buf_size, 0);
                    if(rc < 0) {
                        if(errno != EWOULDBLOCK) {
                            this->l.get()->error_send_failed(
                                        __FILE__, __LINE__, errno, d.s_sd);
                        }
                        else {
                            if(direct) {
                                this->save_new_data_storage(d.s_sd,
                                                            d.buffer,
                                                            d.buffer_len);

                                direct = false;
                            }
                            else {
                                // Ничего делать не надо - данные и так
                                // находятся в деке. Просто не надо
                                // их удалять оттуда.
                            }
                        }
                    }
                    else {
                        // Отправка данных удалась
                        this->counter_sent[d.s_sd] += rc;
                        if(static_cast<unsigned int>(rc) != buf_size) {
                            // RU: не все данные отправлены
                            buf_size = buf_size - rc;
                            index += rc;

                            boost::shared_ptr<std::vector<unsigned char>> v =
                                    boost::make_shared<
                                        std::vector<unsigned char>>();

                            std::copy(buf + index, buf + index + buf_size,
                                      std::back_inserter(*v));

                            if(!direct) {
                                this->delete_data_storage(d.s_sd);
                            }

                            this->save_unset_data_storage(d.s_sd,
                                                          v.get()->data(),
                                                          v.get()->size());
                        }
                        else {
                            // RU: отправлены все данные
                            if(!direct) {
                                this->delete_data_storage(d.s_sd);
                            }
                        }

                        direct = false;
                    }
                }
            }
            else {
                this->l.get()->error_inernal_error(__FILE__, __LINE__);
            }
        }
    }

    ///
    /// \brief server_logic::from_client_connect_not_found
    /// \param d
    ///
    void server_logic::from_client_connect_not_found(data const& d) {
        this->l.get()->debug_signal_client_connect_not_found(
                    __FILE__, __LINE__, d.c_sd, d.s_sd);
        if(d.s_sd >= 0) {
            this->close_connect(d.s_sd);
        }
    }

    ///
    /// \brief server_logic::from_client_disconnect
    /// \param d
    ///
    void server_logic::from_client_disconnect(data const& d) {
        this->l.get()->debug_signal_client_disconnect(
                    __FILE__, __LINE__, d.c_sd, d.s_sd);

        this->close_connect(d.s_sd);
    }

    ///
    /// \brief server_logic::compress_array
    ///
    void server_logic::compress_array(void) {
        for(int i = 0; i < this->nfds;) {
            if(this->fds[i].fd == -1) {
                for(int j = i; j < this->nfds; j++) {
                    this->fds[j].fd = this->fds[j + 1].fd;
                }
                this->nfds--;
            }
            else {
                i++;
            }
        }
    }

    ///
    /// \brief server_logic::erase_old_wait_connect
    ///
    void server_logic::erase_old_wait_connect(void) {
        if(this->db_con_wait.empty()) {
            // RU: Нет сокетов, которые ожидают соединения
            //     (ожидание завершения ::connect)
            return;
        }

        // RU: Поиск просроченных дескрипторов, у которых
        //     истёк интервал ожидания подключения
        bool compress = false;

        auto cur_time = std::chrono::system_clock::now();
        std::map<int, std::chrono::system_clock::time_point>::iterator it =
                this->db_con_wait.begin();

        for(it = this->db_con_wait.begin(); it != this->db_con_wait.end();) {
            auto start_time = it->second;

            auto dur = std::chrono::duration_cast<
                    std::chrono::milliseconds>(
                        cur_time - start_time).count();

            if(dur > this->pi->connect_timeout) {
                // RU: Удаляем просроченные дескрипторы и оповещаем
                //     клиента и воркера
                int s_sd = it->first;
                int c_sd = db[s_sd];

                this->send_not_connect(c_sd, -1, 0, nullptr);

                (void) ::close(s_sd);

                this->db.erase(s_sd);

                int count = nfds;
                for(int i = 0; i < count; i++) {
                    if(fds[i].fd == s_sd) {
                        fds[i].fd = -1;
                        break;
                    }
                }

                compress = true;

                it = this->db_con_wait.erase(it);
            }
            else {
                it++;
            }
        }

        if(compress) {
           compress = false;
           this->compress_array();
        }
    }

    /* ***************************************************************** */
    /* ********************** CLASS: server_logic ********************** */
    /* **************************** PRIVATE **************************** */
    /* ***************************************************************** */

    void server_logic::new_connect(int new_sd, int client_sd) {
        this->counter_sent[new_sd] = 0;
        this->counter_recv[new_sd] = 0;
        this->counter_buffered[new_sd] = 0;
        this->counter_lost[new_sd] = 0;

        std::deque<boost::shared_ptr<std::vector<unsigned char>>> q;
        this->storage[new_sd] = q;

        this->db[new_sd] = client_sd;
        this->fds[this->nfds].fd = new_sd;
        this->fds[this->nfds].events = POLLIN | POLLOUT;
        this->nfds++;
    }

    void server_logic::close_connect(int d) {
        if(!this->empty_data_storage(d)) {
            // RU: Ещё есть неотправленные данные
            this->db_for_close.push_front(d);

            this->db.erase(d);
            this->db_con_wait.erase(d);
        }
        else {
            this->calculate_count_lost(d);
            this->l.get()->info_connect_close(
                __FILE__, __LINE__, d,
                this->counter_sent[d],
                this->counter_recv[d],
                this->counter_buffered[d],
                this->counter_lost[d]);
            this->close_connect_force(d);
        }
    }

    void server_logic::close_connect_force(int d) {
        int count = this->nfds;
        for(int i = 0; i < count; i++) {
            if(this->fds[i].fd == d) {
               this->fds[i].fd = -1;
               this->flag_compress_array = true;
               break;
            }
        }

        (void) ::close(d);

        this->db.erase(d);
        this->db_con_wait.erase(d);
        this->db_for_close.remove(d);
        this->clear_data_storage(d);
        this->storage.erase(d);

        this->counter_sent.erase(d);
        this->counter_recv.erase(d);
        this->counter_buffered.erase(d);
        this->counter_lost.erase(d);
    }

    bool server_logic::save_new_data_storage(int d, unsigned char const* buf,
                                             unsigned int size) {
        auto search = storage.find(d);
        if(search != storage.end()) {
            boost::shared_ptr<std::vector<unsigned char>> v =
                    boost::make_shared<
                        std::vector<unsigned char>>();

            std::copy(buf, buf + size, std::back_inserter(*v));

            search->second.push_back(v);

            this->counter_buffered[d] += size;

            return true;
        }

        return false;
    }

    bool server_logic::save_unset_data_storage(int d, unsigned char const* buf,
                                               unsigned int size) {
        auto search = storage.find(d);
        if(search != storage.end()) {
            boost::shared_ptr<std::vector<unsigned char>> v =
                    boost::make_shared<
                        std::vector<unsigned char>>();

            std::copy(buf, buf + size, std::back_inserter(*v));

            search->second.push_front(v);

            this->counter_buffered[d] += size;

            return true;
        }

        return false;
    }

    unsigned char const* server_logic::get_data_storage(int d,
                                                        unsigned int& size)
    const {
        size = 0;
        auto search = storage.find(d);
        if(search != storage.end() && !search->second.empty()) {
            auto x = search->second.front().get();
            size = x->size();
            return x->data();
        }

        return nullptr;
    }

    bool server_logic::delete_data_storage(int d) {
        auto search = storage.find(d);
        if(search != storage.end()) {
            search->second.pop_front();
            return true;
        }

        return false;
    }

    bool server_logic::clear_data_storage(int d) {
        auto search = storage.find(d);
        if(search != storage.end()) {
            search->second.clear();
            return true;
        }

        return false;
    }

    void server_logic::clear_all_data_storage(void) {
        storage.clear();
    }

    bool server_logic::empty_data_storage(int d) {
        bool ret = true;

        auto search = storage.find(d);
        if(search == storage.end()) {
            ret = true;
        }
        else {
            ret = search->second.empty();
        }

        return ret;
    }

    int server_logic::find_pollfd(int d) const {
        int res_index = -1;

        int count = this->nfds;
        for(int i = 0; i < count; i++) {
            if(this->fds[i].fd == d) {
               res_index = i;
               break;
            }
        }

        return res_index;
    }

    void server_logic::calculate_count_lost(int d) {
        auto search = this->storage.find(d);
        if(search == this->storage.end() || search->second.empty()) {
            this->counter_lost[d] = 0;
        }
        else {
            boost::uint32_t count = 0;
            std::for_each(search->second.begin(), search->second.end(),
                          [&count](auto const v) {
                count += v.get()->size();
            });
            this->counter_lost[d] = count;
        }
    }

    template<class TF_NEG, class TF_ZERO, class TF_POS>
    int server_logic::read_data_socket(int sd, unsigned char* buf, size_t size,
                                       TF_NEG n_f, TF_ZERO z_f, TF_POS p_f) {
        // TF_NEG = void n_f(int err)
        // TF_ZERO = void z_f(void)
        // TF_POS = void p_f(rc, int buf, size_t size)
        int rc = 0;

        rc = ::recv(sd, buf, size, 0);
        if(rc < 0) {
            n_f(errno);
        }
        else if(0 == rc) {
            z_f();
        }
        else {
            this->counter_recv[sd] += rc;
            p_f(rc, buf, size);
        }

        return rc;
    }

    ///
    ///
    ///
    template<class TF_OK, class TF_ERR>
    bool server_logic::read_data(int fd, data& d, TF_OK ok_f, TF_ERR err_f) {
        // TF_OK = bool ok_f(int rc)
        // TF_ERR = bool err_f(int rc, int err)
        int rc = 0;
        size_t dl = sizeof(d);

        rc = ::read(fd, &d, dl);

        return ((rc < 0) ? err_f(rc, errno) : ok_f(rc));
    }

    ///
    /// \brief server_logic::read_data
    /// \param fd
    /// \param d
    /// \return
    ///
    bool server_logic::read_data(int fd, data& d) {
        return this->read_data(fd, d,
            [this, &d](int rc) -> bool {
                assert(rc == sizeof(d));
                return true;
            },
            [this, &fd](int rc, int err) -> bool {
                boost::ignore_unused(rc);
                this->l.get()->error_read_failed(__FILE__, __LINE__, err, fd);
                return false;
            });
    }

    ///
    ///
    ///
    template<class TF_OK, class TF_ERR>
    bool server_logic::send_data(int fd, direction_t direction, data& d,
                                 TF_OK ok_f, TF_ERR err_f) {
        // TF_OK = bool ok_f(int rc)
        // TF_ERR = bool err_f(int rc, int err)

        int rc = 0;
        size_t dl = sizeof(d);

        d.direction = direction;
        rc = ::write(fd, &d, dl);

        return ((rc < 0) ? err_f(rc, errno) : ok_f(rc));
    }

    ///
    /// \brief server_logic::send_data
    /// \param fd
    /// \param direction
    /// \param d
    /// \return
    ///
    bool server_logic::send_data(int fd, direction_t direction, data& d) {
        assert(DIRECTION_SERVER_TO_CLIENT == direction ||
               DIRECTION_SERVER_TO_WORKER == direction);
        return this->send_data(fd, direction, d,
            [this, &d](int rc) -> bool {
                assert(rc == sizeof(d));
                return true;
            },
            [this, &fd](int rc, int err) -> bool {
                boost::ignore_unused(rc);
                this->l.get()->error_write_failed(__FILE__, __LINE__, err, fd);
                return false;
            });
    }

    ///
    /// \brief server_logic::send_data
    /// \param tod
    /// \param c
    /// \param s
    /// \param len
    /// \param buf
    /// \param ca
    /// \param pa
    /// \param sa
    /// \return
    ///
    bool server_logic::send_data(type_of_data_t tod, int c, int s,
                                 unsigned int len,
                                 unsigned char const* buf,
                                 struct sockaddr_in const* ca,
                                 struct sockaddr_in const* pa,
                                 struct sockaddr_in const* sa) {
        bool retc = true;
        bool retw = true;

        data d(DIRECTION_UNKNOWN, tod, c, s, len, buf, ca, pa, sa);

        retc = this->send_data(this->c_out_fd, DIRECTION_SERVER_TO_CLIENT, d);
        retw = this->send_data(this->w_out_fd, DIRECTION_SERVER_TO_WORKER, d);

        return (retc && retw);
    }

    ///
    /// \brief server_logic::send_new_connect
    /// \param c
    /// \param s
    /// \param len
    /// \param buf
    /// \param ca
    /// \param pa
    /// \param sa
    /// \return
    ///
    bool server_logic::send_new_connect(int c, int s,
                                        unsigned int len,
                                        unsigned char const* buf,
                                        struct sockaddr_in const* ca,
                                        struct sockaddr_in const* pa,
                                        struct sockaddr_in const* sa) {
        return this->send_data(TOD_NEW_CONNECT, c, s, len, buf, ca, pa, sa);
    }

    ///
    /// \brief server_logic::send_disconnect
    /// \param c
    /// \param s
    /// \param len
    /// \param buf
    /// \param ca
    /// \param pa
    /// \param sa
    /// \return
    ///
    bool server_logic::send_disconnect(int c, int s,
                                       unsigned int len,
                                       unsigned char const* buf,
                                       struct sockaddr_in const* ca,
                                       struct sockaddr_in const* pa,
                                       struct sockaddr_in const* sa) {
        return this->send_data(TOD_DISCONNECT, c, s, len, buf, ca, pa, sa);
    }

    bool server_logic::send_data(int c, int s,
                                 unsigned int len,
                                 unsigned char const* buf,
                                 struct sockaddr_in const* ca,
                                 struct sockaddr_in const* pa,
                                 struct sockaddr_in const* sa) {
        return this->send_data(TOD_DATA, c, s, len, buf, ca, pa, sa);
    }

    ///
    /// \brief server_logic::send_not_connect
    /// \param c
    /// \param s
    /// \param len
    /// \param buf
    /// \param ca
    /// \param pa
    /// \param sa
    /// \return
    ///
    bool server_logic::send_not_connect(int c, int s,
                                        unsigned int len,
                                        unsigned char const* buf,
                                        struct sockaddr_in const* ca,
                                        struct sockaddr_in const* pa,
                                        struct sockaddr_in const* sa) {
        return this->send_data(TOD_NOT_CONNECT, c, s, len, buf, ca, pa, sa);
    }

    ///
    /// \brief server_logic::send_connect_not_found
    /// \param c
    /// \param s
    /// \param len
    /// \param buf
    /// \param ca
    /// \param pa
    /// \param sa
    /// \return
    ///
    bool server_logic::send_connect_not_found(int c, int s,
                                              unsigned int len,
                                              unsigned char const* buf,
                                              struct sockaddr_in const* ca,
                                              struct sockaddr_in const* pa,
                                              struct sockaddr_in const* sa) {
        return this->send_data(TOD_CONNECT_NOT_FOUND,
                               c, s, len, buf, ca, pa, sa);
    }

    ///
    /// \brief server_logic::send_other
    /// \param c
    /// \param s
    /// \param len
    /// \param buf
    /// \param ca
    /// \param pa
    /// \param sa
    /// \return
    ///
    bool server_logic::send_other(int c, int s,
                                  unsigned int len,
                                  unsigned char const* buf,
                                  struct sockaddr_in const* ca,
                                  struct sockaddr_in const* pa,
                                  struct sockaddr_in const* sa) {
        return this->send_data(TOD_OTHER, c, s, len, buf, ca, pa, sa);
    }
} // namespace proxy_ns

/* *****************************************************************************
 * End of file
 * ************************************************************************** */
