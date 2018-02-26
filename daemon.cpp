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
 * NOTE: This file includes the code in pure C-style!
 */

#include <iostream>
#include <sstream>
#include <ios>
#include <iomanip>
#include <exception>
#include <stdexcept>

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <csetjmp>

#include <boost/cstdint.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/lexical_cast.hpp>

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "daemon.hpp"
#include "log.hpp"

extern void program_exit(int retcode) noexcept;
extern void _program_exit(int retcode) noexcept;

namespace daemon_ns {
	using namespace log_ns;

	void sig_handler_SIGUSR1(int sig, siginfo_t* sinfo, void* buf);
	void sig_handler_SIGUSR2(int sig, siginfo_t* sinfo, void* buf);
    void sig_handler_SIGPIPE(int sig, siginfo_t* sinfo, void* buf);
    void sig_handler_SIGINT(int sig, siginfo_t* sinfo, void* buf);
	
	bool daemon::used = false;
	
    mode_t const daemon::DEFAULT_UMASK =
            DAEMON_DEFAULT_UMASK;

    char const*  daemon::DEFAULT_LOCK_PID_PATH =
            DAEMON_DEFAULT_LOCK_PID_PATH;

    mode_t const daemon::DEFAULT_LOCK_PID_FILE_MODE =
            DAEMON_DEFAULT_LOCK_PID_FILE_MODE;
	
	daemon::daemon(void) :
		current_pid(0),
        lock_fd(0),
        owner(false) {
        // RU:
        // Не используется синглтон, потому что я считаю его,
        // в большинстве случаев, антипаттерном. В данном
        // контексте лучше бросить исключение и тем самым показать,
        // что класс используется повторно.

		if(self::used) {
            throw Edaemon_exist();
		}

		self::used = true;
	}
	
    daemon::result_t daemon::go(bool demonization, bool force) {
		daemon::result_t result = self::RES_NO_ERROR;
		
		int stdio_fd = 0;
		pid_t pid = 0;
		pid_t sid = 0;
		long count_files = 0;

        log_ns::log& l = log_ns::log::inst();
		
		try {
			do {
				l(Ilog::LEVEL_DEBUG, "Set umask");
				
				::umask(self::DEFAULT_UMASK);

				l(Ilog::LEVEL_DEBUG, "Create the lock file");
				
				this->lock_fd = ::open(self::DEFAULT_LOCK_PID_PATH,
									   O_RDWR | O_CREAT | O_EXCL,
									   self::DEFAULT_LOCK_PID_FILE_MODE);
			
				if(this->lock_fd < 0) {
                    l(Ilog::LEVEL_INFO,
                      "Can't open the lock file for writing. Analyzing...");
                    l(Ilog::LEVEL_DEBUG,
                      "Open the lock file for reading...");
					
					lock_fd = ::open(self::DEFAULT_LOCK_PID_PATH,
									 O_RDONLY,
									 self::DEFAULT_LOCK_PID_FILE_MODE);

					if(this->lock_fd < 0) {
                        l(Ilog::LEVEL_ERROR,
                          "Can't open the lock file for reading");
						result = self::RES_PID_BLOCK_ERROR;
						break;
					}
                    else {
                        size_t const BUF_SIZE = 10;
                        int rc = 0;
                        bool try_create_again = false;

                        void* buf = ::calloc(BUF_SIZE + 1, sizeof(char));

                        if(!buf) {
                            throw std::bad_alloc();
                        }

                        rc = ::read(this->lock_fd, buf, BUF_SIZE);

                        (void) ::close(this->lock_fd);

                        if(rc < 0) {
                            l(Ilog::LEVEL_ERROR,
                              "Can't read data from the lock file");
                            result = self::RES_READ_ERROR;
                            ::free(buf);
                            break;
                        }
                        else if(!rc) {
                            if(::unlink(self::DEFAULT_LOCK_PID_PATH) < 0) {
                                l(Ilog::LEVEL_ERROR,
                                  "Can't delete the lock file");
                                result = self::RES_UNLINK_ERROR;
                                ::free(buf);
                                break;
                            }

                            try_create_again = true;
                        }
                        else {
                            pid_t pid_from_file = 0;

                            try {
                                pid_from_file = boost::lexical_cast<pid_t>(
                                            reinterpret_cast<char const*>(buf));
                            }
                            catch(boost::bad_lexical_cast const&) {
                                pid_from_file = -1;
                            }

                            if(0 >= pid_from_file) {
                                if(::unlink(self::DEFAULT_LOCK_PID_PATH) < 0) {
                                    l(Ilog::LEVEL_ERROR,
                                      "Can't delete the lock file");
                                    result = self::RES_UNLINK_ERROR;
                                    ::free(buf);
                                    break;
                                }
                            }
                            else {
                                if(::kill(pid_from_file, 0) < 0) {
                                    if(ESRCH == errno) {
                                        if(::unlink(
                                            self::DEFAULT_LOCK_PID_PATH) < 0) {
                                            l(Ilog::LEVEL_ERROR,
                                              "Can't delete the lock file");
                                            result = self::RES_UNLINK_ERROR;
                                            ::free(buf);
                                            break;
                                        }
                                    }
                                    else {
                                        l(Ilog::LEVEL_ERROR, "'kill' error");
                                        break;
                                    }
                                }
                                else {
                                    if(force) {
                                        l(Ilog::LEVEL_INFO,
                     "A copy of the program is already running. Restarting...");
                                        (void) ::kill(pid_from_file, SIGUSR1);
                                        (void) ::sleep(1);
                                        if(!::kill(pid_from_file, 0)) {
                                            (void) ::sleep(1);
                                            if(!::kill(pid_from_file, 0)) {
                                                (void) ::kill(pid_from_file,
                                                              SIGKILL);
                                            }
                                        }
                                    }
                                    else {
                                        result = self::RES_ALREADY_RUNNING;
                                        l(Ilog::LEVEL_INFO,
                                   "A copy of the program is already running!");
                                        ::free(buf);
                                        break;
                                    }
                                }
                            }

                            try_create_again = true;
                        }

                        ::free(buf);

                        if(try_create_again) {
                            this->lock_fd = ::open(self::DEFAULT_LOCK_PID_PATH,
                                   O_RDWR | O_CREAT | O_EXCL,
                                   self::DEFAULT_LOCK_PID_FILE_MODE);
                            if(this->lock_fd < 0) {
                                l(Ilog::LEVEL_ERROR,
                                  "Can't open the lock file for writing.");
                                result = self::RES_PID_BLOCK_ERROR;
                                break;
                            }
                        }
                    }
				}

                this->owner = true;

                if(demonization) {
                    pid = ::fork();
                    if(pid < 0) {
                        l(Ilog::LEVEL_ERROR, "fork error (#1)");
                        result = self::RES_FORK1_ERROR;
                        break;
                    }
                    else if (pid > 0){
                        this->owner = false;
                        ::_exit(EXIT_SUCCESS);
                    }

                    sid = ::setsid();
                    if(sid < 0) {
                        l(Ilog::LEVEL_ERROR, "sid error");
                        result = self::RES_SID_ERROR;
                        break;
                    }

                    pid = ::fork();
                    if(pid < 0) {
                        l(Ilog::LEVEL_ERROR, "fork error (#2)");
                        result = self::RES_FORK2_ERROR;
                        break;
                    }
                    else if(pid > 0) {
                        this->owner = false;
                        ::_exit(EXIT_SUCCESS);
                    }

                    if(::chdir("/") < 0) {
                        l(Ilog::LEVEL_ERROR, "chdir error");
                        result = self::RES_CHDIR_TO_ROOT_ERROR;
                        break;
                    }

                    (void) ::close(STDIN_FILENO);
                    (void) ::close(STDOUT_FILENO);
                    (void) ::close(STDERR_FILENO);

                    count_files = ::sysconf(_SC_OPEN_MAX);
                    for(long i = count_files - 1; i >= 0; --i) {
                        if(this->lock_fd != i) {
                            (void) ::close(i);
                        }
                    }

                    stdio_fd = ::open("/dev/null", O_RDWR);
                    (void) ::dup(stdio_fd);
                    (void) ::dup(stdio_fd);

                    if(::setpgrp() < 0) {
                        l(Ilog::LEVEL_ERROR, "setpgrp error");
                        result = self::RES_PGRP_ERROR;
                        break;
                    }
                }

                this->current_pid = ::getpid();

                do {
                    char* buf = nullptr;

                    (void) ::asprintf(&buf, "%d", this->current_pid);

                    (void) ::write(this->lock_fd,
                                   static_cast<void*>(buf),
                                   ::strlen(buf));

                    (void) ::syncfs(this->lock_fd);

                    ::free(buf);
                }
                while(0);

				this->setsignal(SIGUSR1, sig_handler_SIGUSR1);
				this->setsignal(SIGUSR2, sig_handler_SIGUSR2);
                this->setsignal(SIGPIPE, sig_handler_SIGPIPE);
                this->setsignal(SIGINT, sig_handler_SIGINT);
			}
			while(0);
		}
        catch(std::bad_alloc&) {
            result = self::RES_MEMORY_ERROR;
        }
		catch(...) {
			result = self::RES_UNKNOWN_ERROR;
		}
		
		return result;
	}

	void daemon::finish(void) {
		long count_files = 0;
		
		(void) ::close(this->lock_fd);

        if(this->owner) {
            (void) ::unlink(self::DEFAULT_LOCK_PID_PATH);
        }
		
		count_files = ::sysconf(_SC_OPEN_MAX);
		for(long i = count_files - 1; i >= 0; --i) {
			(void) ::close(i);
		}		
	}
	
	daemon::~daemon(void) {
		this->finish();

		self::used = false;
	}

	void daemon::setsignal(int sig, sighandler hndlr) {
		struct sigaction sa_new;
		struct sigaction sa_old;
		sigset_t mask;

		(void) ::memset(&sa_new, 0, sizeof(sa_new));
		(void) ::memset(&sa_old, 0, sizeof(sa_old));
		
        (void) ::sigemptyset(&mask);
		
		sa_new.sa_handler = nullptr;
		sa_new.sa_sigaction = hndlr;
		sa_new.sa_mask = mask;
		sa_new.sa_flags = SA_SIGINFO;
		sa_new.sa_restorer = nullptr;
		
        if(::sigaction(sig, &sa_new, &sa_old) < 0) {
            std::stringstream ss;

            ss << "sigaction error (signal number " << sig << ")";

            log::inst()(Ilog::LEVEL_ERROR, ss.str());
		}
		else {
            std::stringstream ss;

            ss << "sigaction ok (signal number " << sig << ")";

            log::inst()(Ilog::LEVEL_DEBUG, ss.str());
		}
	}
	
	void sig_handler_SIGUSR1(int sig, siginfo_t* sinfo, void* buf) {
        boost::ignore_unused(sig, sinfo, buf);

		log::inst()(Ilog::LEVEL_DEBUG, "Signal received: SIGUSR1");
		
        ::program_exit(EXIT_SUCCESS);
	}

	void sig_handler_SIGUSR2(int sig, siginfo_t* sinfo, void* buf) {
        boost::ignore_unused(sig, sinfo, buf);

		log::inst()(Ilog::LEVEL_DEBUG, "Signal received: SIGUSR2");
	}

    void sig_handler_SIGPIPE(int sig, siginfo_t* sinfo, void* buf) {
        boost::ignore_unused(sig, sinfo, buf);

        log::inst()(Ilog::LEVEL_DEBUG, "Signal received: SIGPIPE");

        ::program_exit(EXIT_FAILURE);
    }

    void sig_handler_SIGINT(int sig, siginfo_t* sinfo, void* buf) {
        boost::ignore_unused(sig, sinfo, buf);

        log::inst()(Ilog::LEVEL_DEBUG, "Signal received: SIGINT");

        ::program_exit(EXIT_SUCCESS);
    }
	
} // namespace daemon

/* *****************************************************************************
 * End of file
 * ************************************************************************** */
