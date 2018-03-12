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

#include "client_logic.hpp"

namespace proxy_ns {
    /* ***************************************************************** */
    /* ********************** CLASS: client_logic ********************** */
    /* **************************** PUBLIC ***************************** */
    /* ***************************************************************** */

    ///
    /// \brief client_logic::client_logic
    ///
    client_logic::client_logic(client_routine_arg* _c_arg, proxy_impl* _pi) :
        c_arg(_c_arg),
        pi(_pi),
        l(new proxy_ns::common_logic_log("C")),
        s_in_fd(_c_arg->_sc_in_pd),
        s_out_fd(_c_arg->_cs_out_pd),
        w_in_fd(_c_arg->_wc_in_pd),
        w_out_fd(_c_arg->_cw_out_pd),
        nfds(0),
        timeout(0),
        flag_compress_array(false),
        listen_sd(-1),
        cur_fd(-1),
        cur_events(0),
        s_read_enable(false),
        w_read_enable(false),
        s_write_enable(false),
        w_write_enable(false){

        std::fill_n(reinterpret_cast<char*>(&this->proxy_addr),
                    sizeof(this->proxy_addr), '\0');

        std::fill_n(reinterpret_cast<char*>(this->fds),
                    sizeof(this->fds), '\0');

        this->db.clear();
    }

    ///
    /// \brief client_logic::~client_logic
    ///
    client_logic::~client_logic(void) noexcept {
        this->done();
    }

    ///
    /// \brief client_logic::prepare
    ///
    void client_logic::prepare(void) {
        int rc_ = RES_CODE_OK;
        int rc = 0;

        this->listen_sd = ::socket(AF_INET, SOCK_STREAM, 0);
        if(this->listen_sd < 0) {
            this->l.get()->error_socket_failed(
                        __FILE__, __LINE__, errno, this->listen_sd);
            this->pi->c_last_err = RES_CODE_ERROR;
            throw Eclient_logic_fatal();
        }

        // Re-use addr
        rc_ = this->pi->set_reuseaddr(this->listen_sd, true,
            this->pi->fok_placeholder,
            [this](int rc, int err) {
                boost::ignore_unused(rc);
                this->l.get()->error_setsockopt_failed(
                        __FILE__, __LINE__, err, this->listen_sd);
            });

        if(RES_CODE_OK != rc_) {
            (void) ::close(this->listen_sd);
            this->pi->c_last_err = RES_CODE_ERROR;
            throw Eclient_logic_fatal();
        }

        // Nonblock
        rc_ = this->pi->set_nonblock(this->listen_sd,
            this->pi->fok_placeholder,
            [this](int rc, int err) {
                boost::ignore_unused(rc);
                this->l.get()->error_ioctl_or_fcntl_failed(
                        __FILE__, __LINE__, err, this->listen_sd);
            });

        if(RES_CODE_OK != rc_) {
            (void) ::close(this->listen_sd);
            this->pi->c_last_err = RES_CODE_ERROR;
            throw Eclient_logic_fatal();
        }

        std::fill_n(reinterpret_cast<char*>(&this->proxy_addr),
                    sizeof(this->proxy_addr), '\0');

        this->proxy_addr.sin_family = AF_INET;
        this->proxy_addr.sin_port = htons(this->pi->proxy_port);
        this->proxy_addr.sin_addr.s_addr = htonl(INADDR_ANY);

        rc = ::bind(this->listen_sd,
                    reinterpret_cast<struct sockaddr*>(&this->proxy_addr),
                    sizeof(this->proxy_addr));
        if(rc < 0) {
            this->l.get()->error_bind_failed(
                        __FILE__, __LINE__, errno, this->listen_sd);
            (void) ::close(this->listen_sd);
            this->pi->c_last_err = RES_CODE_ERROR;
            throw Eclient_logic_fatal();
        }

        rc = ::listen(listen_sd, 1);
        if(rc < 0) {
            this->l.get()->error_listen_failed(
                        __FILE__, __LINE__, errno, this->listen_sd);
            (void) ::close(this->listen_sd);
            this->pi->c_last_err = RES_CODE_ERROR;
            throw Eclient_logic_fatal();
        }

        std::fill_n(reinterpret_cast<char*>(&this->fds[0]),
                    sizeof(this->fds), '\0');

        this->nfds = 0;

        this->fds[this->nfds].fd = this->s_in_fd;
        this->fds[this->nfds].events = POLLIN;
        this->nfds++;

        this->fds[nfds].fd = this->w_in_fd;
        this->fds[nfds].events = POLLIN;
        this->nfds++;

        this->fds[nfds].fd = this->listen_sd;
        this->fds[nfds].events = POLLIN;
        this->nfds++;

        this->timeout = this->pi->client_poll_timeout;

        this->cur_fd = -1;
    }

    ///
    /// \brief client_logic::run
    ///
    void client_logic::run(void) {
        auto connects_present = [&]() {
            this->fds[nfds].fd = this->s_out_fd;
            this->fds[nfds].events = POLLOUT;
            this->nfds++;

            this->fds[nfds].fd = this->w_out_fd;
            this->fds[nfds].events = POLLOUT;
            this->nfds++;
        };

        auto connects_absence = [&]() {
            nfds -= 2;
        };

        while(!this->pi->end_proxy) {
            this->s_read_enable = false;
            this->w_read_enable = false;
            this->s_write_enable = false;
            this->w_write_enable = false;

            constexpr static size_t const data_size = sizeof(data);

            static int const min_descriptors_count_ro = 3;
            static int const min_descriptors_count_rw = 5;
#ifdef USE_FULL_DEBUG
    #ifdef USE_FULL_DEBUG_POLL_INTERVAL
            l(Ilog::LEVEL_DEBUG, "C: Waiting on poll (client)...");
    #endif // USE_FULL_DEBUG_POLL_INTERVAL
#endif // USE_FULL_DEBUG

            int rc = ::poll(this->fds, this->nfds, this->timeout);
            if(rc < 0) {
                this->l.get()->error_poll_failed(__FILE__, __LINE__, errno);
                this->pi->c_last_err = RES_CODE_ERROR;
                throw Eclient_logic_fatal();
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

                        if(this->fds[i].fd != this->listen_sd &&
                           this->fds[i].fd != this->s_in_fd &&
                           this->fds[i].fd != this->w_in_fd) {
                            this->send_disconnect(this->fds[i].fd,
                                                  this->db[this->fds[i].fd]);
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
                            this->pi->c_last_err = RES_CODE_ERROR;
                            throw Eclient_logic_fatal();
                        }
                    }
                    else if(this->fds[i].revents & POLLERR) {
                        this->l.get()->debug_revent_includes_pollerr(
                                    __FILE__, __LINE__, this->fds[i].fd);
                        if(this->fds[i].fd != this->listen_sd &&
                           this->fds[i].fd != this->s_in_fd &&
                           this->fds[i].fd != this->w_in_fd) {
                            this->send_disconnect(this->fds[i].fd,
                                                  this->db[this->fds[i].fd]);
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
                            this->pi->c_last_err = RES_CODE_ERROR;
                            throw Eclient_logic_fatal();
                        }
                    }
                    else if(this->fds[i].revents & POLLNVAL) {
                        this->l.get()->debug_revent_includes_pollnval(
                                    __FILE__, __LINE__, this->fds[i].fd);
                        this->l.get()->error_inernal_error(__FILE__, __LINE__);
                        this->pi->c_last_err = RES_CODE_ERROR;
                        throw Eclient_logic_fatal();
                    }

                    this->cur_fd = this->fds[i].fd;
                    this->cur_events = this->fds[i].events;
                    this->cur_revents = this->fds[i].revents;

                    if(this->cur_fd == -1) {
                        this->flag_compress_array = true;
                        continue;
                    }

                    if(this->cur_fd == this->listen_sd) {
                        this->new_connect();
                    }
                    else if(this->cur_fd == this->s_in_fd) {
                        this->from_server();
                    }
                    else if(this->cur_fd == this->w_in_fd) {
                        this->from_worker();
                    }
                    else if(this->cur_fd == this->s_out_fd) {
                        this->s_write_enable =
                                this->pi->can_write_to_pipe_data(
                                    this->cur_fd, data_size);
                    }
                    else if(this->cur_fd == this->w_out_fd) {
                        this->w_write_enable =
                                this->pi->can_write_to_pipe_data(
                                    this->cur_fd, data_size);
                    }
                    else {
                        if(this->s_write_enable && this->w_write_enable) {
                            this->from_clients();
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
    /// \brief client_logic::done
    ///
    void client_logic::done(void) noexcept {
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
    /* ********************** CLASS: client_logic ********************** */
    /* *************************** PROTECTED *************************** */
    /* ***************************************************************** */

    ///
    /// \brief client_logic::new_connect
    ///
    void client_logic::new_connect(void) {
        int new_sd = -1;
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = 0;
        int val = 0;
        int rc_ = RES_CODE_OK;

        this->l.get()->debug_listen_socket_readable(
                    __FILE__, __LINE__, this->listen_sd);

        do {
            std::fill_n(reinterpret_cast<char*>(&client_addr),
                        sizeof(client_addr), '\0');

            client_addr_len = 0;

            new_sd = ::accept(this->listen_sd,
                              reinterpret_cast<struct sockaddr*>(
                                  &client_addr),
                              &client_addr_len);
            if(new_sd < 0) {
                if(errno != EWOULDBLOCK && errno != EAGAIN) {
                    this->l.get()->error_accept_failed(
                                __FILE__, __LINE__, errno, new_sd);
                }

                break;
            }
            else {
                // Nonblock
                rc_ = this->pi->set_nonblock(new_sd,
                    this->pi->fok_placeholder,
                    [this, &new_sd](int rc, int err) {
                        boost::ignore_unused(rc);
                        this->l.get()->error_ioctl_or_fcntl_failed(
                                __FILE__, __LINE__, err, new_sd);
                    });

                if(RES_CODE_OK != rc_) {
                    (void) ::close(new_sd);
                    this->pi->c_last_err = RES_CODE_ERROR;
                    throw Eclient_logic_fatal();
                }

                // Keep alive
                rc_ = this->pi->set_keep_alive(new_sd,
                    this->pi->client_keep_alive,
                    this->pi->fok_placeholder,
                    [this, &new_sd](int rc, int err) {
                        boost::ignore_unused(rc);
                        this->l.get()->error_setsockopt_failed(
                                __FILE__, __LINE__, err, new_sd);
                    });

                if(RES_CODE_OK != rc_) {
                    (void) ::close(new_sd);
                    this->pi->c_last_err = RES_CODE_ERROR;
                    throw Eclient_logic_fatal();
                }

                rc_ = this->pi->get_keep_alive(new_sd, val,
                    this->pi->fok_placeholder,
                    [this, &new_sd](int rc, int err) {
                        boost::ignore_unused(rc);
                        this->l.get()->error_getsockopt_failed(
                                __FILE__, __LINE__, err, new_sd);
                    });

                if(RES_CODE_OK != rc_) {
                    (void) ::close(new_sd);
                    this->pi->c_last_err = RES_CODE_ERROR;
                    throw Eclient_logic_fatal();
                }
                else {
                    this->l.get()->debug_keep_alive_onoff(
                                __FILE__, __LINE__, val, new_sd);
                }

                // TCP no delay (Nagle's algorithm)
                rc_ = this->pi->set_tcp_no_delay(new_sd,
                    this->pi->client_tcp_no_delay,
                    this->pi->fok_placeholder,
                    [this, &new_sd](int rc, int err) {
                        boost::ignore_unused(rc);
                        this->l.get()->error_setsockopt_failed(
                                __FILE__, __LINE__, err, new_sd);
                    });

                if(RES_CODE_OK != rc_) {
                    (void) ::close(new_sd);
                    this->pi->c_last_err = RES_CODE_ERROR;
                    throw Eclient_logic_fatal();
                }

                rc_ = this->pi->get_tcp_no_delay(new_sd, val,
                    this->pi->fok_placeholder,
                    [this, &new_sd](int rc, int err) {
                        boost::ignore_unused(rc);
                        this->l.get()->error_getsockopt_failed(
                                __FILE__, __LINE__, err, new_sd);
                    });

                if(RES_CODE_OK != rc_) {
                    (void) ::close(new_sd);
                    this->pi->c_last_err = RES_CODE_ERROR;
                    throw Eclient_logic_fatal();
                }
                else {
                    this->l.get()->debug_tcp_no_delay_onoff(
                                __FILE__, __LINE__, val, new_sd);
                }

                this->send_new_connect(new_sd, -1,
                                       0, nullptr,
                                       &client_addr,
                                       &this->proxy_addr,
                                       nullptr);

                this->l.get()->info_new_incoming_connection(
                            __FILE__, __LINE__, new_sd,
                            inet_ntoa(client_addr.sin_addr),
                            ntohs(client_addr.sin_port));

                this->new_connect(new_sd);
            }
        }
        while(new_sd != -1);
    }

    ///
    /// \brief client_logic::from_worker
    ///
    void client_logic::from_worker(void) {
        data d;

        this->read_data(this->cur_fd, d);

        this->pi->debug_log_info(d, "C");
    }

    ///
    /// \brief client_logic::from_server
    ///
    void client_logic::from_server(void) {
        data d;

        if(this->read_data(cur_fd, d)) {
            this->pi->debug_log_info(d, "C");

            // RU: Проверить, а данные точно от сервера?
            if(DIRECTION_SERVER_TO_CLIENT == d.direction) {
                switch(d.tod) {
                case TOD_NEW_CONNECT:
                    this->from_server_new_connect(d);
                    break;
                case TOD_DATA:
                    this->from_server_data(d);
                    break;
                case TOD_NOT_CONNECT:
                    this->from_server_not_connect(d);
                    break;
                case TOD_DISCONNECT:
                    this->from_server_disconnect(d);
                    break;
                case TOD_CONNECT_NOT_FOUND:
                    this->from_server_connect_not_found(d);
                    break;
                default:
                    // RU: По-идее, в данную секцию попадать не должны. Однако,
                    //     если мы тут оказались - это вовсе не означает что
                    //     произошла ошибка. Но обращать внимание на это стоит.
                    this->l.get()->debug_unsupported_data_server(
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
    /// \brief client_logic::from_clients
    ///
    void client_logic::from_clients(void) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        bool close_conn = false;
        bool cont = true;
        bool for_close = false;
        int rc = 0;

#ifdef USE_FULL_DEBUG
        log_ns::log::inst()(Ilog::LEVEL_DEBUG, [&](auto file, auto line)
        ->std::string {
              std::stringstream ss;
              ss << "C: Descriptor is readable or writable (socket="
                 << this->cur_fd << ")."
                 << " FILE:" << file
                 << ": " << line << ".";
              return ss.str();
        }(__FILE__, __LINE__));
#endif // USE_FULL_DEBUG

        close_conn = false;

        if(this->cur_revents & POLLOUT) {
            // RU: Сокет доступен для записи
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

                rc = ::send(this->cur_fd, buf + index, buf_size, 0);
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

        if(this->cur_revents & POLLIN) {
            // RU: сокет доступен для чтения
            if(cont) {
                int count_bytes = 0;
                int srv_cur_fd = -1;

                std::fill_n(reinterpret_cast<char*>(&client_addr),
                            sizeof(client_addr), '\0');

                rc = ::getsockname(this->cur_fd,
                                   reinterpret_cast<struct sockaddr*>(
                                       &client_addr),
                                   &client_addr_len);
                if(rc < 0) {
                    this->l.get()->error_getsockname_failed(
                                __FILE__, __LINE__, errno, this->cur_fd);

                    this->pi->c_last_err = RES_CODE_ERROR;
                    this->pi->end_proxy = true;
                    throw Eclient_logic_fatal();
                }

                rc = ::ioctl(this->cur_fd, FIONREAD, &count_bytes);
                if(rc < 0) {
                    this->l.get()->error_ioctl_fionread_failed(
                                __FILE__, __LINE__, errno, this->cur_fd);

                    close_conn = true;
                }

        #ifdef USE_FULL_DEBUG
                log_ns::log::inst()(Ilog::LEVEL_DEBUG, [&](auto file, auto line)
                ->std::string {
                    std::stringstream ss;
                    ss << "C: Bytes available for reading (socket="
                       << this->cur_fd << "): " << count_bytes << "; rc = "
                       << rc << ". FILE:" << file << ":" << line << ".";
                    return ss.str();
                }(__FILE__, __LINE__));
        #endif // USE_FULL_DEBUG

                // RU: Если мы здесь оказались, значит на сокете точно
                //     произошло какое-то событие. Если данных в сокете нет,
                //     то это событие - закрытие соединения клиентом.
                //     Если данные есть, то необходимо убедиться, готов ли
                //     сервер принять их?
                if(!count_bytes) {
                    this->l.get()->info_connection_closed(
                                __FILE__, __LINE__, this->cur_fd);

                    close_conn = true;
                }
                else {
                    srv_cur_fd = this->db[cur_fd];

                    if(srv_cur_fd < 0) {
                        // RU: Сервер не готов принять данные
        #ifdef USE_FULL_DEBUG
                        log_ns::log::inst()(Ilog::LEVEL_DEBUG,
                                        "C: Server is not ready. Waiting...");
        #endif // USE_FULL_DEBUG
                    }
                    else {
                        if(cont) {
                            unsigned char buffer[DATA_BUFFER_SIZE] = { 0 };
                            size_t buf_size = sizeof(buffer);

                            std::fill_n(reinterpret_cast<char*>(buffer),
                                        buf_size, '\0');

                            this->read_data_socket(this->cur_fd, buffer,
                                                   buf_size,
                                [this, &close_conn, &cont](int err) -> void {
                                    // rc < 0
                                    if(err != EWOULDBLOCK) {
                                        this->l.get()->error_recv_failed(
                                            __FILE__, __LINE__, err,
                                            this->cur_fd);
                                        close_conn = true;
                                    }
                                },
                                [this, &close_conn, &cont](void) -> void {
                                    // rc == 0
                                    close_conn = true;
                                },
                                [this, &cont, &srv_cur_fd, &client_addr](
                                    int rc, unsigned char* buf,
                                    size_t size) -> void {
                                    // rc > 0
                                    boost::ignore_unused(size);
                                        size_t len = rc;
                                    this->send_data(this->cur_fd, srv_cur_fd,
                                                    len, buf,
                                                    &client_addr,
                                                    &this->proxy_addr,
                                                    nullptr);
                            });
                        }
                    }
                }
            }
        }

        if(close_conn) {
            this->send_disconnect(this->cur_fd, this->db[this->cur_fd],
                    0, nullptr,
                    &client_addr, &this->proxy_addr, nullptr);

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
    /// \brief client_logic::from_server_new_connect
    /// \param d
    ///
    void client_logic::from_server_new_connect(data const& d) {
        // RU: Сервер подтвердил установку соединения
        auto search = this->db.find(d.c_sd);
        if(search != this->db.end()) {
            // RU: Соединение найдено
            this->db[d.c_sd] = d.s_sd;
        }
        else {
            // RU: Найти подобное соединение не удалось
            this->l.get()->error_unknown_socket_descriptor(
                        __FILE__, __LINE__, d.c_sd);

            this->send_connect_not_found(d.c_sd, d.s_sd,
                                         0, nullptr,
                                         nullptr,
                                         &this->proxy_addr,
                                         nullptr);
        }
    }

    ///
    /// \brief client_logic::from_server_data
    /// \param d
    ///
    void client_logic::from_server_data(data const& d) {
        if(d.c_sd < 0) {
            this->l.get()->error_inernal_error(__FILE__, __LINE__);
        }
        else {
            unsigned char* buf = nullptr;
            unsigned int buf_size = 0;
            int index = 0;
            bool direct = true;
            int client_index = this->find_pollfd(d.c_sd);

            if(client_index >= 0) {
                if(this->fds[client_index].revents & POLLOUT) {
                    auto search = db.find(d.c_sd);
                    if(search != db.end()) {
                        // RU: Соединение найдено.
                        //     Отправить данные клиенту
                        if(this->empty_data_storage(d.c_sd)) {
                            // No unsent data are present
                            buf = const_cast<unsigned char*>(&d.buffer[index]);
                            buf_size = d.buffer_len;

                            direct = true;
                        }
                        else {
                            this->save_new_data_storage(d.c_sd, d.buffer,
                                                        d.buffer_len);
                            buf = const_cast<unsigned char*>(
                                        this->get_data_storage(
                                            d.c_sd, buf_size));

                            direct = false;
                        //}

                        int rc = ::send(d.c_sd, buf, buf_size, 0);
                        if(rc < 0) {
                            if(errno != EWOULDBLOCK) {
                                this->l.get()->error_send_failed(
                                            __FILE__, __LINE__, errno, d.c_sd);
                            }
                            else {
                                if(direct) {
                                    this->save_new_data_storage(d.c_sd,
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
                            this->counter_sent[d.c_sd] += rc;
                            if(static_cast<unsigned int>(rc) != buf_size) {
                                // RU: не все данные отправлены
                                buf_size = buf_size - rc;
                                index += rc;

                                boost::shared_ptr<
                                        std::vector<unsigned char>> v =
                                            boost::make_shared<
                                                std::vector<unsigned char>>();

                                std::copy(buf + index, buf + index + buf_size,
                                          std::back_inserter(*v));

                                if(!direct) {
                                    this->delete_data_storage(d.c_sd);
                                }

                                this->save_unset_data_storage(d.c_sd,
                                                              v.get()->data(),
                                                              v.get()->size());
                            }
                            else {
                                // RU: отправлены все данные
                                if(!direct) {
                                    this->delete_data_storage(d.c_sd);
                                }
                            }

                            direct = false;
                        }
                    }
                    else {
                        this->l.get()->error_unknown_socket_descriptor(
                                    __FILE__, __LINE__, d.c_sd);

                        this->send_connect_not_found(d.c_sd, d.s_sd,
                                                     0, nullptr,
                                                     nullptr,
                                                     &this->proxy_addr,
                                                     nullptr);
                    }
                }
            }
            else {
                this->l.get()->error_inernal_error(__FILE__, __LINE__);
            }
        }
    }

    ///
    /// \brief client_logic::from_server_not_connect
    /// \param d
    ///
    void client_logic::from_server_not_connect(data const& d) {
        this->l.get()->debug_signal_server_not_connect(
                    __FILE__, __LINE__, d.c_sd, d.s_sd);
        if(d.c_sd >= 0) {
            this->close_connect(d.c_sd);
        }
    }

    ///
    /// \brief client_logic::from_server_disconnect
    /// \param d
    ///
    void client_logic::from_server_disconnect(data const& d) {
        // RU: Разрыв соединения по инициативе сервера
        this->l.get()->debug_signal_server_disconnect(
                    __FILE__, __LINE__, d.c_sd, d.s_sd);

        this->close_connect(d.c_sd);
    }

    ///
    /// \brief client_logic::from_server_connect_not_found
    /// \param d
    ///
    void client_logic::from_server_connect_not_found(data const& d) {
        // RU: Игнорируем данное сообщение. Скорее всего, сервер
        //     получил сообщение о соединении, которое уже
        //     закрыто (т.е. он не может обработать данные).
        this->l.get()->debug_signal_server_connect_not_found(
                    __FILE__, __LINE__, d.c_sd, d.s_sd);
    }

    ///
    /// \brief client_logic::compress_array
    ///
    void client_logic::compress_array(void) {
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

    /* ***************************************************************** */
    /* ********************** CLASS: client_logic ********************** */
    /* **************************** PRIVATE **************************** */
    /* ***************************************************************** */

    void client_logic::new_connect(int d) {
        this->counter_sent[d] = 0;
        this->counter_recv[d] = 0;
        this->counter_buffered[d] = 0;
        this->counter_lost[d] = 0;

        std::deque<boost::shared_ptr<std::vector<unsigned char>>> q;
        this->storage[d] = q;

        this->db[d] = -1;
        this->fds[this->nfds].fd = d;
        this->fds[this->nfds].events = POLLIN | POLLOUT;
        this->nfds++;
    }

    void client_logic::close_connect(int d) {
        if(!this->empty_data_storage(d)) {
            // RU: Ещё есть неотправленные данные
            this->db_for_close.push_front(d);

            this->db.erase(d);
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

    void client_logic::close_connect_force(int d) {
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
        this->db_for_close.remove(d);
        this->clear_data_storage(d);
        this->storage.erase(d);

        this->counter_sent.erase(d);
        this->counter_recv.erase(d);
        this->counter_buffered.erase(d);
        this->counter_lost.erase(d);
    }

    bool client_logic::save_new_data_storage(int d, unsigned char const* buf,
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

    bool client_logic::save_unset_data_storage(int d, unsigned char const* buf,
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

    unsigned char const* client_logic::get_data_storage(int d,
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

    bool client_logic::delete_data_storage(int d) {
        auto search = storage.find(d);
        if(search != storage.end()) {
            search->second.pop_front();
            return true;
        }

        return false;
    }

    bool client_logic::clear_data_storage(int d) {
        auto search = storage.find(d);
        if(search != storage.end()) {
            search->second.clear();
            return true;
        }

        return false;
    }

    void client_logic::clear_all_data_storage(void) {
        storage.clear();
    }

    bool client_logic::empty_data_storage(int d) {
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

    int client_logic::find_pollfd(int d) const {
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

    void client_logic::calculate_count_lost(int d) {
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

    ///
    ///
    ///
    template<class TF_NEG, class TF_ZERO, class TF_POS>
    int client_logic::read_data_socket(int sd, unsigned char* buf, size_t size,
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
    bool client_logic::read_data(int fd, data& d, TF_OK ok_f, TF_ERR err_f) {
        // TF_OK = bool ok_f(int rc)
        // TF_ERR = bool err_f(int rc, int err)
        int rc = 0;
        size_t dl = sizeof(d);

        rc = ::read(fd, &d, dl);

        return ((rc < 0) ? err_f(rc, errno) : ok_f(rc));
    }

    ///
    /// \brief client_logic::read_data
    /// \param fd
    /// \param d
    /// \return
    ///
    bool client_logic::read_data(int fd, data& d) {
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
    bool client_logic::send_data(int fd, direction_t direction, data& d,
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
    /// \brief client_logic::send_data
    /// \param fd
    /// \param direction
    /// \param d
    /// \return
    ///
    bool client_logic::send_data(int fd, direction_t direction, data& d) {
        assert(DIRECTION_CLIENT_TO_SERVER == direction ||
               DIRECTION_CLIENT_TO_WORKER == direction);
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
    /// \brief client_logic::send_data
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
    bool client_logic::send_data(type_of_data_t tod, int c, int s,
                                 unsigned int len,
                                 unsigned char const* buf,
                                 struct sockaddr_in const* ca,
                                 struct sockaddr_in const* pa,
                                 struct sockaddr_in const* sa) {
        bool retc = true;
        bool retw = true;

        data d(DIRECTION_UNKNOWN, tod, c, s, len, buf, ca, pa, sa);

        retc = this->send_data(this->s_out_fd, DIRECTION_CLIENT_TO_SERVER, d);
        retw = this->send_data(this->w_out_fd, DIRECTION_CLIENT_TO_WORKER, d);

        return (retc && retw);
    }

    ///
    /// \brief client_logic::send_new_connect
    /// \param c
    /// \param s
    /// \param len
    /// \param buf
    /// \param ca
    /// \param pa
    /// \param sa
    /// \return
    ///
    bool client_logic::send_new_connect(int c, int s,
                                        unsigned int len,
                                        unsigned char const* buf,
                                        struct sockaddr_in const* ca,
                                        struct sockaddr_in const* pa,
                                        struct sockaddr_in const* sa) {
        return this->send_data(TOD_NEW_CONNECT, c, s, len, buf, ca, pa, sa);
    }

    ///
    /// \brief client_logic::send_disconnect
    /// \param c
    /// \param s
    /// \param len
    /// \param buf
    /// \param ca
    /// \param pa
    /// \param sa
    /// \return
    ///
    bool client_logic::send_disconnect(int c, int s,
                                       unsigned int len,
                                       unsigned char const* buf,
                                       struct sockaddr_in const* ca,
                                       struct sockaddr_in const* pa,
                                       struct sockaddr_in const* sa) {
        return this->send_data(TOD_DISCONNECT, c, s, len, buf, ca, pa, sa);
    }

    ///
    /// \brief client_logic::send_data
    /// \param c
    /// \param s
    /// \param len
    /// \param buf
    /// \param ca
    /// \param pa
    /// \param sa
    /// \return
    ///
    bool client_logic::send_data(int c, int s,
                                 unsigned int len,
                                 unsigned char const* buf,
                                 struct sockaddr_in const* ca,
                                 struct sockaddr_in const* pa,
                                 struct sockaddr_in const* sa) {
        return this->send_data(TOD_DATA, c, s, len, buf, ca, pa, sa);
    }

    ///
    /// \brief client_logic::send_not_connect
    /// \param c
    /// \param s
    /// \param len
    /// \param buf
    /// \param ca
    /// \param pa
    /// \param sa
    /// \return
    ///
    bool client_logic::send_not_connect(int c, int s,
                                        unsigned int len,
                                        unsigned char const* buf,
                                        struct sockaddr_in const* ca,
                                        struct sockaddr_in const* pa,
                                        struct sockaddr_in const* sa) {
        return this->send_data(TOD_NOT_CONNECT, c, s, len, buf, ca, pa, sa);
    }

    ///
    /// \brief client_logic::send_connect_not_found
    /// \param c
    /// \param s
    /// \param len
    /// \param buf
    /// \param ca
    /// \param pa
    /// \param sa
    /// \return
    ///
    bool client_logic::send_connect_not_found(int c, int s,
                                              unsigned int len,
                                              unsigned char const* buf,
                                              struct sockaddr_in const* ca,
                                              struct sockaddr_in const* pa,
                                              struct sockaddr_in const* sa) {
        return this->send_data(TOD_CONNECT_NOT_FOUND,
                               c, s, len, buf, ca, pa, sa);
    }

    ///
    /// \brief client_logic::send_other
    /// \param c
    /// \param s
    /// \param len
    /// \param buf
    /// \param ca
    /// \param pa
    /// \param sa
    /// \return
    ///
    bool client_logic::send_other(int c, int s,
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
