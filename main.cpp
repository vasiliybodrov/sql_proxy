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

/* *****************************************************************************
 * RU: Замечу. Для того, чтобы программа работала корректно и практически во
 *     всех случаях подчищала за собой ресурсы, используется механизм SJLJ.
 *     Обратите внимание, что для завершения работы программы лучше пользоваться
 *     функциями program_exit(int) и _program_exit(int), вместо соответствующих
 *     exit(int) и _exit(int) /_Exit(int)/. Однако, применение exit и _exit
 *     так же допустимо. Без необходимости не рекомендуется использовать функции
 *     _program_exit(int) и _exit(int) /_Exit(int)/. Они не приводят к
 *     корректному завершению программы с подчисткой всех ресурсов. Исключением
 *     может являться процесс демонизации.
 *
 *     Пример:
 *
 *     extern void program_exit(int retcode) noexcept;
 *     extern void _program_exit(int retcode) noexcept;
 *     ...
 *     ::program_exit(0);
 *     // ::_program_exit(0);
* ************************************************************************** */

#include <iostream>
#include <ios>
#include <iomanip>
#include <memory>
#include <new>
#include <array>
#include <list>
#include <map>
#include <iterator>
#include <algorithm>
#include <exception>
#include <functional>
#include <numeric>
#include <mutex>
#include <atomic>

#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <cassert>
#include <csetjmp>

#include <boost/make_shared.hpp>
#include <boost/cstdint.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>

#include <getopt.h>
#include <unistd.h>

#include "daemon.hpp"
#include "log.hpp"
#include "proxy.hpp"

#ifndef USER_CONFIG_DEFAULT_PROXY_PORT
    #define USER_CONFIG_DEFAULT_PROXY_PORT 4880
#endif // USER_CONFIG_DEFAULT_PROXY_PORT

#ifndef USER_CONFIG_DEFAULT_SERVER_PORT
    #define USER_CONFIG_DEFAULT_SERVER_PORT 3306
#endif // USER_CONFIG_DEFAULT_SERVER_PORT

#ifndef USER_CONFIG_DEFAULT_SERVER_ADDR
    #define USER_CONFIG_DEFAULT_SERVER_ADDR "127.0.0.1"
#endif // USER_CONFIG_DEFAULT_SERVER_ADDR

#ifndef USER_CONFIG_DEFAULT_TIMEOUT
    #define USER_CONFIG_DEFAULT_TIMEOUT 1000
#endif // USER_CONFIG_DEFAULT_TIMEOUT

#ifndef USER_CONFIG_DEFAULT_CONNECT_TIMEOUT
    #define USER_CONFIG_DEFAULT_CONNECT_TIMEOUT 3000
#endif // USER_CONFIG_DEFAULT_CONNECT_TIMEOUT

#ifndef USER_CONFIG_DEFAULT_LOG_LEVEL
    #define USER_CONFIG_DEFAULT_LOG_LEVEL "INFO"
#endif // USER_CONFIG_DEFAULT_LOG_LEVEL

int main(int argc, char** argv);

void atexit1(void);

int get_jump_value_from_retcode(int retcode) noexcept;
int get_retcode_from_jump_value(int jump_value) noexcept;
int get_jump_value_from_retcode_hard(int retcode) noexcept;
int get_retcode_from_jump_value_hard(int jump_value) noexcept;

void program_exit(int retcode) noexcept;
void _program_exit(int retcode) noexcept;

extern char** environ;

std::jmp_buf jump_exit_buf;
int const jump_val_offset = 1000;
int const jump_val_offset_hard = 2000;
std::atomic<bool> atexit1_once = false;
//std::mutex exit_mutex; // ???

namespace {
    std::string const PROGRAM_NAME = "sqlproxy";
    std::string const PROGRAM_NAME_FULL = "MySQL proxy and logger";
    std::string const PROGRAM_VERSION = "0.1";
    std::string const PROGRAM_AUTHORS = "Vasiliy V. Bodrov aka Bodro";
    std::string const PROGRAM_LICENSE = "" \
"The MIT License (MIT)\n" \
"\n" \
"Copyright (c) 2016 Vasiliy V. Bodrov aka Bodro, Ryazan, Russia\n" \
"\n" \
"Permission is hereby granted, free of charge, to any person obtaining a\n" \
"copy of this software and associated documentation files " \
"(the \"Software\"),\n" \
"Software is furnished to do so, subject to the following conditions:\n" \
"\n" \
"The above copyright notice and this permission notice shall be included\n" \
"in all copies or substantial portions of the Software.\n" \
"\n" \
"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS\n" \
"OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF\n" \
"MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.\n" \
"IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY\n" \
"CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT\n" \
"OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR\n" \
"THE USE OR OTHER DEALINGS IN THE SOFTWARE.\n";

    std::string const LOG_LEVEL_DEBUG = "DEBUG";
    std::string const LOG_LEVEL_INFO  = "INFO";
    std::string const LOG_LEVEL_ERROR = "ERROR";

    void usage(void) noexcept;
    void help(void) noexcept;
    void license(void) noexcept;
    void authors(void) noexcept;
    void version(void) noexcept;

    struct configuration {
    public:
        /* Fields (direct access) */
        int global_argc;
        char const* const* global_argv;
        int flag_show_help;
        int flag_show_version;
        int flag_show_authors;
        int flag_show_license;
        int flag_show_variable;
        int flag_show_variable_stop;
        int flag_no_daemon;
        int flag_force;
        int flag_client_keep_alive;
        int flag_server_keep_alive;
        boost::uint16_t proxy_port;
        std::string server_addr;
        boost::uint16_t server_port;
        std::string log_level;
        boost::int32_t timeout;
        boost::int32_t connect_timeout;
        std::list<std::string> operands;

        /* Methods */
        inline void set_flag_show_help(char const* value) {
            this->flag_show_help = boost::lexical_cast<int>(value);
        }
        inline void set_flag_show_version(char const* value) {
            this->flag_show_version = boost::lexical_cast<int>(value);
        }
        inline void set_flag_show_authors(char const* value) {
            this->flag_show_authors = boost::lexical_cast<int>(value);
        }
        inline void set_flag_show_license(char const* value) {
            this->flag_show_license = boost::lexical_cast<int>(value);
        }
        inline void set_flag_show_variable(char const* value) {
            this->flag_show_variable = boost::lexical_cast<int>(value);
        }
        inline void set_flag_show_variable_stop(char const* value) {
            this->flag_show_variable_stop = boost::lexical_cast<int>(value);
        }
        inline void set_flag_no_daemon(char const* value) {
            this->flag_no_daemon = boost::lexical_cast<int>(value);
        }
        inline void set_flag_force(char const* value) {
            this->flag_force = boost::lexical_cast<int>(value);
        }
        inline void set_flag_client_keep_alive(char const* value) {
            this->flag_client_keep_alive = boost::lexical_cast<int>(value);
        }
        inline void set_flag_server_keep_alive(char const* value) {
            this->flag_server_keep_alive = boost::lexical_cast<int>(value);
        }
        inline void set_proxy_port(char const* value) {
            this->proxy_port = boost::lexical_cast<boost::uint16_t>(value);
        }
        inline void set_server_addr(char const* value) {
            this->server_addr = boost::lexical_cast<std::string>(value);
        }
        inline void set_server_port(char const* value) {
            this->server_port = boost::lexical_cast<boost::uint16_t>(value);
        }
        inline void set_log_level(char const* value) {
            this->log_level = boost::lexical_cast<std::string>(value);
        }
        inline void set_timeout(char const* value) {
            this->timeout = boost::lexical_cast<boost::int32_t>(value);
        }
        inline void set_connect_timeout(char const* value) {
            this->connect_timeout = boost::lexical_cast<boost::int32_t>(value);
        }

        inline void set_operands(char const* value) {
            std::istringstream iss(value);
            std::copy(std::istream_iterator<std::string>(iss),
                      std::istream_iterator<std::string>(),
                      std::back_inserter(this->operands));
        }

        inline configuration(void) :
            global_argc(0),
            global_argv(nullptr),
            flag_show_help(0),
            flag_show_version(0),
            flag_show_authors(0),
            flag_show_license(0),
            flag_show_variable(0),
            flag_show_variable_stop(0),
            flag_no_daemon(0),
            flag_force(0),
            flag_client_keep_alive(0),
            flag_server_keep_alive(0),
            proxy_port(USER_CONFIG_DEFAULT_PROXY_PORT),
            server_addr(USER_CONFIG_DEFAULT_SERVER_ADDR),
            server_port(USER_CONFIG_DEFAULT_SERVER_PORT),
            log_level(USER_CONFIG_DEFAULT_LOG_LEVEL),
            timeout(USER_CONFIG_DEFAULT_TIMEOUT),
            connect_timeout(USER_CONFIG_DEFAULT_CONNECT_TIMEOUT),
            operands() {
        }

        inline ~configuration(void) {
            this->global_argc = 0;
            this->global_argv = nullptr;
            this->flag_show_help = 0;
            this->flag_show_version = 0;
            this->flag_show_authors = 0;
            this->flag_show_license = 0;
            this->flag_show_variable = 0;
            this->flag_show_variable_stop = 0;
            this->flag_no_daemon = 0;
            this->flag_force = 0;
            this->flag_client_keep_alive = 0;
            this->flag_server_keep_alive = 0;
            this->proxy_port = 0;
            this->server_addr.clear();
            this->server_port = 0;
            this->log_level.clear();
            this->timeout = 0;
            this->connect_timeout = 0;
            this->operands.clear();
        }
    };

    typedef struct environment_names_s {
        const char* name;
        std::function<void(char const*)> setter;
    } environment_names_t;

    extern char** environ;

    configuration config;

    option longopts[] = {
        {"help",                no_argument,
            &config.flag_show_help,          0x01}, // 'h'
        {"version",             no_argument,
            &config.flag_show_version,       0x01}, // 'v'
        {"authors",             no_argument,
            &config.flag_show_authors,       0x01}, // 'a'
        {"license",             no_argument,
            &config.flag_show_license,       0x01}, // 'l'
        {"show-variable",       no_argument,
            &config.flag_show_variable,      0x01}, // 's'
        {"show-variable-stop",  no_argument,
            &config.flag_show_variable_stop, 0x01}, // none
        {"no-daemon",           no_argument,
            &config.flag_no_daemon,          0x01}, // none
        {"force",               no_argument,
            &config.flag_force,              0x01}, // 'f'
        {"client-keep-alive",   no_argument,
            &config.flag_client_keep_alive,  0x01}, // none
        {"server-keep-alive",   no_argument,
            &config.flag_server_keep_alive,  0x01}, // none
        {"port",                required_argument,
            0,                               'p' }, // 'p'
        {"server-port",         required_argument,
            0,                               'd' }, // 'd'
        {"server-addr",         required_argument,
            0,                               'i' }, // 'i'
        {"log-level",           required_argument,
            0,                               'o' }, // 'o'
        {"timeout",             required_argument,
            0,                               't' }, // 't'
        {"connect-timeout",     required_argument,
            0,                               'c' }, // 'c'
        {0,                     0,
            0,                               0x00}  // end
    };

    environment_names_t env_names[] = {
        {"SQLPROXY_FLAG_SHOW_HELP",
            boost::bind(&configuration::set_flag_show_help,
                &config, _1)},
        {"SQLPROXY_FLAG_SHOW_VERSION",
            boost::bind(&configuration::set_flag_show_version,
                &config, _1)},
        {"SQLPROXY_FLAG_SHOW_AUTHORS",
            boost::bind(&configuration::set_flag_show_authors,
                &config, _1)},
        {"SQLPROXY_FLAG_SHOW_LICENSE",
            boost::bind(&configuration::set_flag_show_variable,
                &config, _1)},
        {"SQLPROXY_FLAG_SHOW_VARIABLE",
            boost::bind(&configuration::set_flag_show_variable,
                &config, _1)},
        {"SQLPROXY_FLAG_SHOW_VARIABLE_STOP",
            boost::bind(&configuration::set_flag_show_variable_stop,
                &config, _1)},
        {"SQLPROXY_FLAG_NO_DAEMON",
            boost::bind(&configuration::set_flag_no_daemon,
                &config, _1)},
        {"SQLPROXY_FLAG_FORCE",
            boost::bind(&configuration::set_flag_force,
                &config, _1)},
        {"SQLPROXY_FLAG_CLIENT_KEEP_ALIVE",
            boost::bind(&configuration::set_flag_client_keep_alive,
                &config, _1)},
        {"SQLPROXY_FLAG_SERVER_KEEP_ALIVE",
            boost::bind(&configuration::set_flag_server_keep_alive,
                &config, _1)},
        {"SQLPROXY_PORT",
            boost::bind(&configuration::set_proxy_port,
                &config, _1)},
        {"SQLPROXY_SERVER_ADDR",
            boost::bind(&configuration::set_server_addr,
                &config, _1)},
        {"SQLPROXY_SERVER_PORT",
            boost::bind(&configuration::set_server_port,
                &config, _1)},
        {"SQLPROXY_LOG_LEVEL",
            boost::bind(&configuration::set_log_level,
                &config, _1)},
        {"SQLPROXY_TIMEOUT",
            boost::bind(&configuration::set_timeout,
                &config, _1)},
        {"SQLPROXY_CONNECT_TIMEOUT",
            boost::bind(&configuration::set_connect_timeout,
                &config, _1)},
        {"BRAINLOLLER_OPERANDS",
            boost::bind(&configuration::set_operands,
                &config, _1)},
    };

    void usage(void) noexcept {
        std::cout << "Program: " << PROGRAM_NAME
                  << " (" << PROGRAM_NAME_FULL << ")" << std::endl;
        std::cout << "Use --help or -h for help" << std::endl;
    }

    void help(void) noexcept {
        version();
        authors();

        std::cout << std::endl
                  << "This program uses getopts' rules (with long opts)."
                  << std::endl;
        std::cout << "You can see \"man 3 getopt_long\" or "
                  << "\"info getopt_long\" for more help." << std::endl;
        std::cout << std::endl
                  << "Use " << config.global_argv[0]
                  << " [OPTIONS]" << std::endl;
        std::cout << std::endl << "Options:" << std::endl;
        std::cout <<"-h\t--help\t\t\t\t"
                  << "- show this help and exit" << std::endl;
        std::cout <<"-v\t--version\t\t\t"
                  << "- show program version and exit" << std::endl;
        std::cout <<"-a\t--authors\t\t\t"
                  << "- show authors and exit" << std::endl;
        std::cout <<"-l\t--license\t\t\t"
                  << "- show license and exit" << std::endl;
        std::cout <<"-s\t--show-variable\t\t\t"
                  << "- show internal variables (for debug)" << std::endl;
        std::cout <<"\t--show-variable-stop\t\t"
                  << "- show internal variables and stop (for debug)"
                  << std::endl;
        std::cout <<"\t--no-daemon\t\t\t"
                  << "- run as no daemon" << std::endl;
        std::cout <<"\t--force\t\t\t\t"
                  << "- close another instance of the program if it is running"
                  << std::endl;
        std::cout <<"\t--client-keep-alive\t\t"
                  << "- enable keep-alive for clients"
                  << std::endl;
        std::cout <<"\t--server-keep-alive\t\t"
                  << "- enable keep-alive for server"
                  << std::endl;
        std::cout <<"-p\t--port=[PORT]\t\t\t"
                  << "- set proxy port" << std::endl;
        std::cout <<"-d\t--server-port=[PORT]\t\t"
                  << "- set sql server port" << std::endl;
        std::cout <<"-i\t--server-addr=[IPADDRESS]\t"
                  << "- set sql server ip-address" << std::endl;
        std::cout <<"-o\t--log-level=[LOGLEVEL]\t\t"
                  << "- set log level" << std::endl;
        std::cout <<"-t\t--timeout=[NUMBER]\t\t"
                  << "- set timeout (for poll)" << std::endl;
        std::cout <<"-с\t--connect-timeout=[NUMBER]\t"
                  << "- set timeout for connect to sql-server" << std::endl;

        std::cout << std::endl << "Environment:" << std::endl;
        std::cout << "\tSQLPROXY_FLAG_SHOW_HELP\t\t\t"
                  << "- same as '-h|--help': {0,1}" << std::endl;
        std::cout << "\tSQLPROXY_FLAG_SHOW_VERSION\t\t"
                  << "- same as '-v|--version': {0,1}" << std::endl;
        std::cout << "\tSQLPROXY_FLAG_SHOW_AUTHORS\t\t"
                  << "- same as '-a|--authors': {0,1}" << std::endl;
        std::cout << "\tSQLPROXY_FLAG_SHOW_LICENSE\t\t"
                  << "- same as '-l|--license': {0,1}" << std::endl;
        std::cout << "\tSQLPROXY_FLAG_SHOW_VARIABLE\t\t"
                  << "- same as '-s|--show-variable': {0,1}" << std::endl;
        std::cout << "\tSQLPROXY_FLAG_SHOW_VARIABLE_STOP\t"
                  << "- same as '--show-variable-stop': {0,1}" << std::endl;
        std::cout << "\tSQLPROXY_FLAG_NO_DAEMON\t\t\t"
                  << "- same as '--no-daemon': {0,1}" << std::endl;
        std::cout << "\tSQLPROXY_FLAG_FORCE\t\t\t"
                  << "- same as '-f|--force': {0,1}" << std::endl;
        std::cout << "\tSQLPROXY_FLAG_CLIENT_KEEP_ALIVE\t\t"
                  << "- same as '--client-keep-alive': {0,1}" << std::endl;
        std::cout << "\tSQLPROXY_FLAG_SERVER_KEEP_ALIVE\t\t"
                  << "- same as '--server-keep-alive': {0,1}" << std::endl;
        std::cout << "\tSQLPROXY_PORT\t\t\t\t"
                  << "- same as '-p|--port'" << std::endl;
        std::cout << "\tSQLPROXY_SERVER_ADDR\t\t\t"
                  << "- same as '-i|--server-addr'" << std::endl;
        std::cout << "\tSQLPROXY_SERVER_PORT\t\t\t"
                  << "- same as '-d|--server-port'" << std::endl;
        std::cout << "\tSQLPROXY_LOG_LEVEL\t\t\t"
                  << "- same as '-o|--log-level'" << std::endl;
        std::cout << "\tSQLPROXY_TIMEOUT\t\t\t"
                  << "- same as '-t|--timeout'" << std::endl;
        std::cout << "\tSQLPROXY_CONNECT_TIMEOUT\t\t"
                  << "- same as '-c|--connect-timeout'" << std::endl;

        std::cout << std::endl << "Log levels:" << std::endl;
        std::cout << "\t" << LOG_LEVEL_DEBUG << "\t"
                  << "- debug, info, errors" << std::endl;
        std::cout << "\t" << LOG_LEVEL_INFO << "\t"
                  << "- info, errors" << std::endl;
        std::cout << "\t" << LOG_LEVEL_ERROR << "\t"
                  << "- for only errors" << std::endl;

        std::cout << std::endl << "Example:" << std::endl;
        std::cout << "\t" << config.global_argv[0] << " --help" << std::endl;
        std::cout << "\t" << config.global_argv[0] << " -l" << std::endl;
        std::cout << "\t" << config.global_argv[0] << " --port 4880 "
                  << "--server-addr='127.0.0.1' "
                  << "--server-port=3306 "
                  << "--log-level=DEBUG"
                  << std::endl;
        std::cout << "\t" << config.global_argv[0] << " -p 4880 "
                  << "-i '127.0.0.1' "
                  << "-d 3306 "
                  << "-o DEBUG "
                  << "-t 1000 "
                  << "-c 5000"
                  << std::endl;
        std::cout << "\t" << config.global_argv[0] << " -p 4881 "
                  << "-i '192.168.254.1' "
                  << "-d 3307 "
                  << "-o ERROR "
                  << "-t 1050 "
                  << "-c 5001 "
                  << "--show-variable-stop"
                  << std::endl;
        std::cout << "\tSQLPROXY_FLAG_SHOW_VARIABLE_STOP=1 "
                  << config.global_argv[0] << " "
                  << "-p 4881 "
                  << "-i '192.168.254.1' "
                  << "-d 3307 "
                  << "-o ERROR "
                  << "-t 1050 "
                  << "-c 5001 "
                  << std::endl;
        std::cout << "\tSQLPROXY_FLAG_SHOW_AUTHORS=1 "
                  << config.global_argv[0]
                  << std::endl;
    }

    void license(void) noexcept {
        std::cout << PROGRAM_LICENSE << std::endl;
    }

    void authors(void) noexcept {
        std::cout << "Authors: " << PROGRAM_AUTHORS << std::endl;
    }

    void version(void) noexcept {
        std::cout << "Program: "
                  << PROGRAM_NAME
                  << " (" << PROGRAM_NAME_FULL << ")"
                  << std::endl;

        std::cout << "Version: "
                  << PROGRAM_VERSION
                  << std::endl;
    }

} // namespace

int main(int argc, char** argv) {
    [&]()->void {
        config.global_argc = argc;
        config.global_argv = argv;

        // EN: Read environment variable and set them value
        // RU: Чтение переменных окружения и установка их значений
        std::for_each(&env_names[0],
                      &env_names[sizeof(env_names) /
                            sizeof(environment_names_t)],
                      [](auto x)->void {
                            auto current_env = ::getenv(x.name);
                            if(current_env) {
                                x.setter(current_env);
                            }
                      });

        // EN: Read options and set them value
        // RU: Чтение опций и установка их значений
        [&argc, &argv]()->void{
            int optc = 0;
            while((optc = getopt_long(argc, argv, ":hvalsfp:d:i:o:t:c:",
                                      longopts, 0)) != -1) {
                switch(optc) {
                case 'h':
                    config.flag_show_help = 1;
                    break;
                case 'v':
                    config.flag_show_version = 1;
                    break;
                case 'a':
                    config.flag_show_authors = 1;
                    break;
                case 'l':
                    config.flag_show_license = 1;
                    break;
                case 's':
                    config.flag_show_variable = 1;
                    break;
                case 'f':
                    config.flag_force = 1;
                    break;
                case 'p':
                    if(optarg != nullptr) {
                        config.set_proxy_port(optarg);
                    }
                    break;
                case 'd':
                    if(optarg != nullptr) {
                        config.set_server_port(optarg);
                    }
                    break;
                case 'i':
                    if(optarg != nullptr) {
                        config.set_server_addr(optarg);
                    }
                    break;
                case 'o':
                    if(optarg != nullptr) {
                        config.set_log_level(optarg);
                    }
                    break;
                case 't':
                    if(optarg != nullptr) {
                        config.set_timeout(optarg);
                    }
                    break;
                case 'c':
                    if(optarg != nullptr) {
                        config.set_connect_timeout(optarg);
                    }
                    break;
                case 0:
                    break;
                case ':':
                    std::cerr << argv[0]
                              << ": option '-"
                              << optopt
                              << "' requires an argument: break!"
                              << std::endl;
                    usage();
                    ::exit(EXIT_FAILURE);
                    break;
                case '?':
                default:
                    std::cerr << argv[0]
                              << ": option '"
                              << optopt
                              << "' is invalid: break!"
                              << std::endl;
                    usage();
                    ::exit(EXIT_FAILURE);
                    break;
                }
            }
        }();

        // Read operands and set them
        std::for_each(&argv[optind], &argv[argc], [](auto x)->void {
                config.operands.push_back(std::string(x));
            });

        if(config.flag_show_variable || config.flag_show_variable_stop) {
            std::cout << "Program variables (internal flags): " << std::endl;
            std::cout << "\tflag_show_help = "
                      << config.flag_show_help << std::endl;
            std::cout << "\tflag_show_version = "
                      << config.flag_show_version << std::endl;
            std::cout << "\tflag_show_authors = "
                      << config.flag_show_authors<< std::endl;
            std::cout << "\tflag_show_license = "
                      << config.flag_show_license << std::endl;
            std::cout << "\tflag_show_variable = "
                      << config.flag_show_variable << std::endl;
            std::cout << "\tflag_show_variable_stop = "
                      << config.flag_show_variable_stop << std::endl;
            std::cout << "\tflag_no_daemon = "
                      << config.flag_no_daemon << std::endl;
            std::cout << "\tflag_force = "
                      << config.flag_force << std::endl;
            std::cout << "\tflag_client_keep_alive = "
                      << config.flag_client_keep_alive << std::endl;
            std::cout << "\tflag_server_keep_alive = "
                      << config.flag_server_keep_alive << std::endl;
            std::cout << "\tproxy_port = "
                      << config.proxy_port << std::endl;
            std::cout << "\tserver_addr = "
                      << config.server_addr << std::endl;
            std::cout << "\tserver_port = "
                      << config.server_port << std::endl;
            std::cout << "\tlog_level = "
                      << config.log_level << std::endl;
            std::cout << "\ttimeout = "
                      << config.timeout << std::endl;
            std::cout << "\tconnect_timeout = "
                      << config.connect_timeout << std::endl;
            std::cout << "\toperands = "
                      << ((config.operands.empty()) ? "(absense)" : "")
                      << std::endl;

            []()->void {
                int counter = 0;
                std::for_each(config.operands.begin(), config.operands.end(),
                              [&counter](auto x)->void {
                                    std::cout << "\t\t"
                                              << counter++
                                              << " = "
                                              << x
                                              << std::endl;
                              });
            }();

            if(config.flag_show_variable_stop) {
                ::exit(EXIT_SUCCESS);
            }
        }

        if(config.flag_show_help) {
            help();
            ::exit(EXIT_SUCCESS);
        }

        if(config.flag_show_version) {
            version();
            ::exit(EXIT_SUCCESS);
        }

        if(config.flag_show_authors) {
            authors();
            ::exit(EXIT_SUCCESS);
        }

        if(config.flag_show_license) {
            license();
            ::exit(EXIT_SUCCESS);
        }
    }();

    // *************************************************************************

    boost::shared_ptr<daemon_ns::daemon> d =
            boost::make_shared<daemon_ns::daemon>();

    daemon_ns::daemon::result_t daemon_result =
            daemon_ns::daemon::RES_UNKNOWN_ERROR;

    boost::shared_ptr<proxy_ns::Iproxy> p =
            boost::make_shared<proxy_ns::proxy>();

    proxy_ns::result_t proxy_result = proxy_ns::RES_CODE_UNKNOWN;

    // RU: Конфигурирование лога (работа будет вестись с syslog'ом)
    boost::ignore_unused(
                log_ns::log::inst(
                    boost::make_shared<log_ns::syslog>(PROGRAM_NAME)));

    log_ns::log::inst().set_level(log_ns::Ilog::LEVEL_DEFAULT);

    p.get()->set_proxy_port(config.proxy_port);
    p.get()->set_server_ip(config.server_addr);
    p.get()->set_server_port(config.server_port);
    p.get()->set_timeout(config.timeout);
    p.get()->set_connect_timeout(config.connect_timeout);
    p.get()->set_client_keep_alive(config.flag_client_keep_alive);
    p.get()->set_server_keep_alive(config.flag_server_keep_alive);

    []()->void {
        std::map<std::string, log_ns::Ilog::level_t> lvl {
            {LOG_LEVEL_DEBUG, log_ns::Ilog::LEVEL_DEBUG},
            {LOG_LEVEL_INFO,  log_ns::Ilog::LEVEL_INFO},
            {LOG_LEVEL_ERROR, log_ns::Ilog::LEVEL_ERROR},
        };

        log_ns::log::inst().set_level(lvl[config.log_level]);
    }();

    if(::atexit(::atexit1)) {
        log_ns::log::inst().write(log_ns::Ilog::LEVEL_ERROR,
                                  "Can't set exit function");
        ::exit(EXIT_FAILURE);
    }

    int jmp_rc = ::setjmp(::jump_exit_buf);
    int retcode = EXIT_SUCCESS;

    if(jmp_rc) {
        if(jmp_rc >= ::jump_val_offset && jmp_rc < ::jump_val_offset_hard) {
            retcode = ::get_retcode_from_jump_value(jmp_rc);
            goto end_program;
        }
        else if(jmp_rc >= ::jump_val_offset_hard) {
            retcode = ::get_retcode_from_jump_value_hard(jmp_rc);
            goto end_program_hard;
        }
        else {
            log_ns::log::inst().write(log_ns::Ilog::LEVEL_ERROR,
                "Internal error! Unknown return code. Terminate!");
            ::_exit(EXIT_FAILURE);
        }
    }

    daemon_result = d.get()->go(!config.flag_no_daemon, config.flag_force);

    if(daemon_ns::daemon::RES_NO_ERROR != daemon_result) {
        std::cout << "Startup failed!" << std::endl;

        log_ns::log::inst().write(log_ns::Ilog::LEVEL_ERROR, "Startup failed!");
        log_ns::log::inst().write(log_ns::Ilog::LEVEL_INFO, "exit (0)");

        ::exit(EXIT_SUCCESS);
    }

    proxy_result = p.get()->run();

    if(proxy_ns::RES_CODE_OK != proxy_result) {
        log_ns::log::inst().write(log_ns::Ilog::LEVEL_ERROR, "Proxy failed!");
    }

    ::atexit1_once = true;

end_program:
    log_ns::log::inst().write(log_ns::Ilog::LEVEL_DEBUG,
                              "Label: end_program");
    return retcode;

end_program_hard:
    log_ns::log::inst().write(log_ns::Ilog::LEVEL_DEBUG,
                              "Label: end_program_hard");
    ::_exit(retcode);
}

void atexit1(void) {
    if(!::atexit1_once) {
        ::atexit1_once = true;

        log_ns::log::inst().write(log_ns::Ilog::LEVEL_DEBUG,
                                  "Calling a function atexit1");

        ::program_exit(EXIT_SUCCESS);
    }
}

int get_jump_value_from_retcode(int retcode) noexcept {
    return ::jump_val_offset + retcode;
}

int get_retcode_from_jump_value(int jump_value) noexcept {
    return jump_value - ::jump_val_offset;
}

int get_jump_value_from_retcode_hard(int retcode) noexcept {
    return ::jump_val_offset_hard + retcode;
}

int get_retcode_from_jump_value_hard(int jump_value) noexcept {
    return jump_value - ::jump_val_offset_hard;
}

void program_exit(int retcode) noexcept {
    ::atexit1_once = true;
    std::longjmp(::jump_exit_buf, ::get_jump_value_from_retcode(retcode));
}

void _program_exit(int retcode) noexcept {
    std::longjmp(::jump_exit_buf, ::get_jump_value_from_retcode_hard(retcode));
}

/* ****************************************************************************
 * End of file
 * ************************************************************************** */
