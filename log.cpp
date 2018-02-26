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

#include <array>
#include <exception>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <ios>
#include <iomanip>

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/make_shared.hpp>

#include <syslog.h>

#include "log.hpp"

namespace log_ns {
    /* ***** CLASS: baselog ***** */
    baselog::baselog(void) : level(Ilog::LEVEL_DEFAULT) {
    }

    void baselog::set_level(Ilog::level_t l) {
        this->level = l;
    }

    Ilog::level_t baselog::get_level(void) const {
        return this->level;
    }

    baselog::~baselog(void) {
    }

    /* ***** CLASS: log ***** */
	log log::instance;
	
    log::log(void) : baselog(),
        il(boost::make_shared<mocklog>()) {
	}
	
	log& log::inst(void) {
		return self::instance;
	}

	log& log::inst(boost::shared_ptr<Ilog> newlog) {
		self::instance.il = newlog;
			
		return self::inst();
	}
	
	void log::write(Ilog::level_t l, std::string const& msg) {
        if(this->get_level() <= l) {
            this->il.get()->write(l, msg);
        }
	}
	
	void log::write(Ilog::level_t l, char const* msg) {
        if(this->get_level() <= l) {
            this->il.get()->write(l, msg);
        }
	}

	boost::shared_ptr<Ilog> log::get_internal_log(void) {
		return this->il;
	}

	void log::operator()(Ilog::level_t l, std::string const& msg) {
		this->write(l, msg);
	}
	
	void log::operator()(Ilog::level_t l, char const* msg) {
		this->write(l, msg);
	}
	
	log::~log(void) {
		this->il.reset();
	}
	
	/* ***** CLASS: syslog ***** */
	bool syslog::used = false;
	
	syslog::syslog(std::string const& name) :
        baselog(),
		name(name),
		lvl({LOG_INFO, LOG_DEBUG, LOG_INFO, LOG_ERR}),
		used_exception(false) {
		
		if(self::used) {
			this->used_exception = true;
            throw Elog_exist();
		}

		self::used = true;
		
		::openlog(name.c_str(),
				  LOG_NDELAY | LOG_NOWAIT | LOG_PID,
				  LOG_SYSLOG);
	}
		
	void syslog::write(Ilog::level_t l, std::string const& msg) {
        this->native_write(l, this->lvl[l], msg.c_str());
	}
	
	void syslog::write(Ilog::level_t l, char const* msg) {
        this->native_write(l, this->lvl[l], msg);
	}

    void syslog::native_write(Ilog::level_t l,
                              int priority,
                              char const* msg) {
        if(this->get_level() <= l) {
            std::stringstream log_msg;

            log_msg << loghelper::lvl_to_string(l)
                    << ": "
                    << msg;

            ::syslog(priority, log_msg.str().c_str());
        }
	}
	
	syslog::~syslog(void) {
		::closelog();

		if(!this->used_exception) {
			self::used = false;
		}
	}

	/* ***** CLASS: stdoutlog ***** */
    stdoutlog::stdoutlog(void) : baselog() {
	}
	
	void stdoutlog::write(Ilog::level_t l, std::string const& msg) {
        if(this->get_level() <= l) {
            std::cout << loghelper::lvl_to_string(l)
                      << ": "
                      << msg
                      << std::endl;
        }
	}
	
	void stdoutlog::write(Ilog::level_t l, char const* msg) {
        this->write(l, std::string(msg));
	}
	
	stdoutlog::~stdoutlog(void) {
	}

    /* ***** CLASS: loghelper ***** */
    std::array<std::string, Ilog::LEVEL_END> const
        loghelper::lvlstr{"DEFAULT", "DEBUG", "INFO", "ERROR"};

    loghelper::loghelper(void) {
    }

    loghelper::~loghelper(void) {
    }

    std::string const& loghelper::lvl_to_string(Ilog::level_t lvl) {
        return self::lvlstr[lvl];
    }
} // namespace log_ns

/* *****************************************************************************
 * End of file
 * ************************************************************************** */
