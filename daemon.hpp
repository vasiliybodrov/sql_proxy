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

#ifndef __DAEMON_HPP__
#define __DAEMON_HPP__

#include <csignal>

#include <boost/cstdint.hpp>

#include <sys/types.h>

#define DAEMON_DEFAULT_UMASK 0x00
#define DAEMON_DEFAULT_LOCK_PID_PATH "/var/run/sqlproxy.pid"
#define DAEMON_DEFAULT_LOCK_PID_FILE_MODE 0644

namespace daemon_ns {
	typedef void (*sighandler)(int, siginfo_t*, void*);
	
	class daemon {
		typedef daemon self;
	public:
		typedef enum {
			RES_NO_ERROR = 0,
			RES_UNKNOWN_ERROR,
            RES_ALREADY_RUNNING,
			RES_PID_BLOCK_ERROR,
			RES_FORK1_ERROR,
			RES_FORK2_ERROR,
			RES_SID_ERROR,
			RES_CHDIR_TO_ROOT_ERROR,
			RES_PGRP_ERROR,
            RES_MEMORY_ERROR,
            RES_READ_ERROR,
            RES_UNLINK_ERROR,
			RES_END
		} result_t;
		
		daemon(void);

        virtual daemon::result_t go(bool demonization = true,
                                    bool force = false);
		virtual void finish(void);
		
		virtual ~daemon(void);
	protected:
		void setsignal(int sig, sighandler hndlr);
	private:
		static bool used;
		
		static mode_t const DEFAULT_UMASK;
		static char const*  DEFAULT_LOCK_PID_PATH;
		static mode_t const DEFAULT_LOCK_PID_FILE_MODE;

		pid_t current_pid;
		int lock_fd;
        bool owner;
	};

    class IEdaemon : public std::exception {
    protected:
        IEdaemon(void) noexcept {}
    public:
        virtual ~IEdaemon() noexcept {}
        virtual char const* what(void) const noexcept {
            static std::string const msg("IEdaemon");
            return msg.c_str();
        }
    };

    class Edaemon_exist : public IEdaemon {
    public:
        Edaemon_exist(void) noexcept {}
        virtual ~Edaemon_exist() noexcept {}
        virtual char const* what(void) const noexcept {
            static std::string const msg("Object 'daemon' already exist");
            return msg.c_str();
        }
    };
} // namespace daemon

#endif // __DAEMON_HPP__

/* *****************************************************************************
 * End of file
 * ************************************************************************** */
