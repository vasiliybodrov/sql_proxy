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
#include <cstring>
#include <cerrno>
#include <cassert>

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

    ///
    /// \brief worker_worker
    /// \param arg
    /// \return
    ///
    void* worker_worker(void* arg) {
        log& l = log::inst();

        worker_routine_arg* worker_arg =
                reinterpret_cast<worker_routine_arg*>(arg);

        proxy_impl* _this =
                worker_arg->_proxy;

        int s_in_fd  = worker_arg->_sw_in_pd;
        //int s_out_fd = worker_arg->_ws_out_pd;
        int c_in_fd  = worker_arg->_cw_in_pd;
        //int c_out_fd = worker_arg->_wc_out_pd;

        int rc = 0;
        struct pollfd fds[2];
        int timeout = 0;

        fds[0].fd = c_in_fd;
        fds[0].events = POLLIN;

        fds[1].fd = s_in_fd;
        fds[1].events = POLLIN;

        timeout = 1000; // TODO: сделать таймаут управляемым снаружи, а не выставляя значение тут (магические числа).

        do {
            rc = ::poll(fds, 2, timeout);

            if(rc < 0) {
                l(Ilog::LEVEL_ERROR, "'poll' failed");
                _this->c_last_err = RES_CODE_ERROR;
                break;
            }
            else if(0 == rc) {
                continue;
            }
            else {
                for(int i = 0; i < 2; i++) {
                    if(0 == fds[i].revents) {
                        continue;
                    }
                    else if(POLLIN != fds[i].revents) {
                        l(Ilog::LEVEL_ERROR, "Revents is not POLLIN");
                        _this->end_proxy = true;
                        _this->w_last_err = RES_CODE_ERROR;
                        break;
                    }
                    else {
                        data d;

                        ::memset(&d, 0, sizeof(d));

                        rc = ::read(fds[i].fd, &d, sizeof(d));
                        if(rc < 0) {
                            // TODO: проверить код ошибки!
                            continue;
                        }
                        else {
                            assert(rc == sizeof(d));
                        }

                        _this->debug_log_info(d, "W");
                    }
                }
            }
        }
        while(!_this->end_proxy);

        return reinterpret_cast<void*>(&(_this->w_last_err));
    }
} // namespace proxy_ns

/* *****************************************************************************
 * End of file
 * ************************************************************************** */
