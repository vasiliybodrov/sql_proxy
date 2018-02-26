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

#ifndef __LOG_HPP__
#define __LOG_HPP__

#include <array>
#include <exception>
#include <stdexcept>

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/core/ignore_unused.hpp>

namespace log_ns {
	class Ilog {
	public:
		typedef enum {
			LEVEL_DEFAULT = 0,  // Don't recommend to use it
			LEVEL_DEBUG,        // It's a debug level
			LEVEL_INFO,         // It's an info level
			LEVEL_ERROR,        // It's an error level
			LEVEL_END           // Don't use it!
		} level_t;

        virtual void set_level(Ilog::level_t l) = 0;
        virtual Ilog::level_t get_level(void) const = 0;
		virtual void write(Ilog::level_t l, std::string const& msg) = 0;
		virtual void write(Ilog::level_t l, char const* msg) = 0;
		
		virtual ~Ilog(void) {}
	};

    class baselog : public Ilog {
        typedef baselog self;
    public:
        baselog(void);

        virtual void set_level(Ilog::level_t l);
        virtual Ilog::level_t get_level(void) const;

        virtual ~baselog(void);
    private:
        Ilog::level_t level;
    };

    class log : public baselog {
		typedef log self;
	public:
		static log& inst(void);
		static log& inst(boost::shared_ptr<Ilog> newlog);
		
		virtual void write(Ilog::level_t l, std::string const& msg);
		virtual void write(Ilog::level_t l, char const* msg);

		virtual boost::shared_ptr<Ilog> get_internal_log(void);

		void operator()(Ilog::level_t l, std::string const& msg);
		void operator()(Ilog::level_t l, char const* msg);

		virtual ~log(void);
	private:
		log(void);
		log(log const&) = delete;
		log& operator=(log const&) = delete;

		static log instance;
		
		boost::shared_ptr<Ilog> il;
	};
	
    class syslog : public baselog {
		typedef syslog self;
	public:
		explicit syslog(std::string const& name);
		
		virtual void write(Ilog::level_t l, std::string const& msg);
		virtual void write(Ilog::level_t l, char const* msg);

		virtual ~syslog(void);
	protected:
        virtual void native_write(Ilog::level_t l,
                                  int priority,
                                  char const* msg);
	private:
		static bool used;
		
		std::string name;
		std::array<int, Ilog::LEVEL_END> lvl;

		bool used_exception;
	};

    class stdoutlog : public baselog {
		typedef stdoutlog self;
	public:
		stdoutlog(void);
		
		virtual void write(Ilog::level_t l, std::string const& msg);
		virtual void write(Ilog::level_t l, char const* msg);

		virtual ~stdoutlog(void);
	};

    class mocklog : public baselog {
	public:
        mocklog() : level(Ilog::LEVEL_DEFAULT) {}
		
		virtual void write(Ilog::level_t l, std::string const& msg) {
			boost::ignore_unused(l, msg);
		}
		
		virtual void write(Ilog::level_t l, char const* msg) {
			boost::ignore_unused(l, msg);
		}

		virtual ~mocklog(void) {}
    private:
        Ilog::level_t level;
	};

    class loghelper {
        typedef loghelper self;
    public:
        loghelper(void);
        ~loghelper(void);

        static std::string const& lvl_to_string(Ilog::level_t lvl);
    private:
        static std::array<std::string, Ilog::LEVEL_END> const lvlstr;
    };

    class IElog : public std::exception {
    protected:
        IElog(void) noexcept {}
    public:
        virtual ~IElog() noexcept {}
        virtual char const* what(void) const noexcept {
            static std::string const msg("IElog");
            return msg.c_str();
        }
    };

    class Elog_exist : public IElog {
    public:
        Elog_exist(void) noexcept {}
        virtual ~Elog_exist() noexcept {}
        virtual char const* what(void) const noexcept {
            static std::string const msg("Log already exist");
            return msg.c_str();
        }
    };
} // namespace log_ns

#endif // __LOG_HPP__

/* *****************************************************************************
 * End of file
 * ************************************************************************** */
