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

#ifndef __CLIENT_LOGIC_HPP__
#define __CLIENT_LOGIC_HPP__

#include <map>
#include <vector>
#include <list>
#include <deque>
#include <exception>
#include <stdexcept>

#include <cerrno>
#include <cstring>

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/make_shared.hpp>

#include "log.hpp"
#include "proxy_result.hpp"
#include "proxy.hpp"
//#include "proxy_impl.hpp" // Instead of "proxy_impl.hpp" we use "proxy.hpp".

namespace proxy_ns {
    using namespace log_ns;

    class client_logic {
    public:
        ///
        /// \brief client_logic
        /// \param _s_arg
        /// \param _pi
        ///
        client_logic(client_routine_arg* _c_arg, proxy_impl* _pi);

        ///
        /// \brief ~client_logic
        ///
        virtual ~client_logic(void) noexcept;

        ///
        /// \brief prepare
        ///
        void prepare(void);

        ///
        /// \brief run
        ///
        void run(void);

        ///
        /// \brief done
        ///
        void done(void) noexcept;
    protected:
        ///
        /// \brief new_connect
        ///
        void new_connect(void);

        ///
        /// \brief from_worker
        ///
        void from_worker(void);

        ///
        /// \brief from_server
        ///
        void from_server(void);

        ///
        /// \brief from_clients
        ///
        void from_clients(void);

        ///
        /// \brief from_server_new_connect
        /// \param d
        ///
        void from_server_new_connect(data const& d);

        ///
        /// \brief from_server_data
        /// \param d
        ///
        void from_server_data(data const& d);

        ///
        /// \brief from_server_not_connect
        /// \param d
        ///
        void from_server_not_connect(data const& d);

        ///
        /// \brief from_server_disconnect
        /// \param d
        ///
        void from_server_disconnect(data const& d);

        ///
        /// \brief from_server_connect_not_found
        /// \param d
        ///
        void from_server_connect_not_found(data const& d);

        ///
        /// \brief compress_array
        ///
        void compress_array(void);
    private:
        client_routine_arg* c_arg;
        proxy_impl* pi;
        boost::scoped_ptr<proxy_ns::common_logic_log> l;

        int s_in_fd;
        int s_out_fd;
        int w_in_fd;
        int w_out_fd;

        struct sockaddr_in proxy_addr;

        struct pollfd fds[POLLING_REQUESTS_SIZE];
        int nfds;
        int timeout;
        bool flag_compress_array;

        int listen_sd;
        int cur_fd;
        short int cur_events;
        short int cur_revents;

        bool s_read_enable;
        bool w_read_enable;
        bool s_write_enable;
        bool w_write_enable;

        // key: client socket descriptor
        // value: server socket descriptor
        std::map<int, int> db;

        // key: client socket descriptor
        // value deque with data for send
        std::map<int, std::deque<boost::shared_ptr<
                std::vector<unsigned char>>>> storage;

        // Value: client socket descriptor
        std::list<int> db_for_close;

        std::map<int, boost::uint32_t> counter_sent;
        std::map<int, boost::uint32_t> counter_recv;
        std::map<int, boost::uint32_t> counter_buffered;
        std::map<int, boost::uint32_t> counter_lost;

        void new_connect(int d);
        void close_connect(int d);
        void close_connect_force(int d);
        bool save_new_data_storage(int d, unsigned char const* buf,
                                   unsigned int size);
        bool save_unset_data_storage(int d, unsigned char const* buf,
                                     unsigned int size);
        unsigned char const* get_data_storage(int d, unsigned int& size) const;
        bool delete_data_storage(int d);
        bool clear_data_storage(int d);
        void clear_all_data_storage(void);
        bool empty_data_storage(int d);
        int find_pollfd(int d) const;
        void calculate_count_lost(int d);

        template<class TF_NEG, class TF_ZERO, class TF_POS>
        int read_data_socket(int sd, unsigned char* buf, size_t size,
                             TF_NEG n_f, TF_ZERO z_f, TF_POS p_f);

        //bool send_data_socket();

        ///
        ///
        ///
        template<class TF_OK, class TF_ERR>
        bool read_data(int fd, data& d, TF_OK ok_f, TF_ERR err_f);

        ///
        /// \brief read_data
        /// \param fd
        /// \param d
        /// \return
        ///
        bool read_data(int fd, data& d);

        ///
        ///
        ///
        template<class TF_OK, class TF_ERR>
        bool send_data(int fd, direction_t direction, data& d,
                       TF_OK ok_f, TF_ERR err_f);

        ///
        /// \brief send_data
        /// \param fd
        /// \param direction
        /// \param d
        /// \return
        ///
        bool send_data(int fd, direction_t direction, data& d);

        ///
        /// \brief send_data
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
        bool send_data(type_of_data_t tod, int c, int s = -1,
                       unsigned int len = 0,
                       unsigned char const* buf = nullptr,
                       struct sockaddr_in const* ca = nullptr,
                       struct sockaddr_in const* pa = nullptr,
                       struct sockaddr_in const* sa = nullptr);

        ///
        /// \brief send_new_connect
        /// \param c
        /// \param s
        /// \param len
        /// \param buf
        /// \param ca
        /// \param pa
        /// \param sa
        /// \return
        ///
        bool send_new_connect(int c, int s,
                              unsigned int len = 0,
                              unsigned char const* buf = nullptr,
                              struct sockaddr_in const* ca = nullptr,
                              struct sockaddr_in const* pa = nullptr,
                              struct sockaddr_in const* sa = nullptr);

        ///
        /// \brief send_disconnect
        /// \param c
        /// \param s
        /// \param len
        /// \param buf
        /// \param ca
        /// \param pa
        /// \param sa
        /// \return
        ///
        bool send_disconnect(int c, int s,
                             unsigned int len = 0,
                             unsigned char const* buf = nullptr,
                             struct sockaddr_in const* ca = nullptr,
                             struct sockaddr_in const* pa = nullptr,
                             struct sockaddr_in const* sa = nullptr);

        ///
        /// \brief send_data
        /// \param c
        /// \param s
        /// \param len
        /// \param buf
        /// \param ca
        /// \param pa
        /// \param sa
        /// \return
        ///
        bool send_data(int c, int s,
                       unsigned int len,
                       unsigned char const* buf,
                       struct sockaddr_in const* ca = nullptr,
                       struct sockaddr_in const* pa = nullptr,
                       struct sockaddr_in const* sa = nullptr);

        ///
        /// \brief send_not_connect
        /// \param c
        /// \param s
        /// \param len
        /// \param buf
        /// \param ca
        /// \param pa
        /// \param sa
        /// \return
        ///
        bool send_not_connect(int c, int s = -1,
                              unsigned int len = 0,
                              unsigned char const* buf = nullptr,
                              struct sockaddr_in const* ca = nullptr,
                              struct sockaddr_in const* pa = nullptr,
                              struct sockaddr_in const* sa = nullptr);

        ///
        /// \brief send_connect_not_found
        /// \param c
        /// \param s
        /// \param len
        /// \param buf
        /// \param ca
        /// \param pa
        /// \param sa
        /// \return
        ///
        bool send_connect_not_found(int c, int s,
                                    unsigned int len = 0,
                                    unsigned char const* buf = nullptr,
                                    struct sockaddr_in const* ca = nullptr,
                                    struct sockaddr_in const* pa = nullptr,
                                    struct sockaddr_in const* sa = nullptr);

        ///
        /// \brief send_other
        /// \param c
        /// \param s
        /// \param len
        /// \param buf
        /// \param ca
        /// \param pa
        /// \param sa
        /// \return
        ///
        bool send_other(int c, int s,
                        unsigned int len,
                        unsigned char const* buf,
                        struct sockaddr_in const* ca = nullptr,
                        struct sockaddr_in const* pa = nullptr,
                        struct sockaddr_in const* sa = nullptr);
    };

    ///
    /// \brief The IEclient_logic class
    ///
    class IEclient_logic : public std::exception {
    protected:
        IEclient_logic(void) noexcept {}
    public:
        virtual ~IEclient_logic() noexcept {}
        virtual char const* what(void) const noexcept {
            static std::string const msg("IEclient_logic");
            return msg.c_str();
        }
    };

    ///
    /// \brief The Eclient_logic_fatal class
    ///
    class Eclient_logic_fatal : public IEclient_logic {
    public:
        Eclient_logic_fatal(void) noexcept {}
        virtual ~Eclient_logic_fatal() noexcept {}
        virtual char const* what(void) const noexcept {
            static std::string const msg("client_logic: fatal error");
            return msg.c_str();
        }
    };
} // namespace proxy_ns

#endif // __CLIENT_LOGIC_HPP__

/* *****************************************************************************
 * End of file
 * ************************************************************************** */
