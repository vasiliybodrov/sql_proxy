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
    using namespace log_ns;

    class c_go_to_finish {};

    ///
    /// \brief client_worker
    /// \param arg
    /// \return
    ///
    void* client_worker(void* arg) {
        client_routine_arg* client_arg =
            reinterpret_cast<client_routine_arg*>(arg);

        proxy_impl* _this = client_arg->_proxy;

        log& l = log::inst();

        try {
            boost::scoped_ptr<client_logic> cl(nullptr);

            try {
                boost::scoped_ptr<client_logic> cl_tmp(
                            new client_logic(client_arg, _this));
                cl.swap(cl_tmp);
            }
            catch(std::bad_alloc const&) {
                _this->c_last_err = RES_CODE_ERROR;
                throw c_go_to_finish();
            }

            try {
                cl.get()->prepare();
                cl.get()->run();

                throw c_go_to_finish();
            }
            catch(...) {
                cl.get()->done();
                throw;
            }
        }
        catch(c_go_to_finish const&) {
            l(Ilog::LEVEL_DEBUG, "C: 'c_go_to_finish' exception");
        }
        catch(std::exception const& e) {
                    l(Ilog::LEVEL_DEBUG, std::string("C: ") +
                                         std::string("exception: ") +
                                         std::string(e.what()));
        }
        catch(...) {
            l(Ilog::LEVEL_DEBUG, "C: unknown exception");
        }

        return &(_this->c_last_err);
    }
} // namespace proxy_ns

/* *****************************************************************************
 * End of file
 * ************************************************************************** */
