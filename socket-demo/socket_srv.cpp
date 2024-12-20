//***************************************************************************
//
// Program example for labs in subject Operating Systems
//
// Petr Olivka, Dept. of Computer Science, petr.olivka@vsb.cz, 2017
//
// Example of socket server.
//
// This program is example of socket server and it allows to connect and serve
// the only one client.
// The mandatory argument of program is port number for listening.
//
//***************************************************************************

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdarg.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <mqueue.h>

#define STR_CLOSE   "close"
#define STR_QUIT    "quit"

//***************************************************************************
// log messages

#define LOG_ERROR               0       // errors
#define LOG_INFO                1       // information and notifications
#define LOG_DEBUG               2       // debug messages

// debug flag
int g_debug = LOG_INFO;

void log_msg( int t_log_level, const char *t_form, ... )
{
    const char *out_fmt[] = {
            "ERR: (%d-%s) %s\n",
            "INF: %s\n",
            "DEB: %s\n" };

    if ( t_log_level && t_log_level > g_debug ) return;

    char l_buf[ 1024 ];
    va_list l_arg;
    va_start( l_arg, t_form );
    vsprintf( l_buf, t_form, l_arg );
    va_end( l_arg );

    switch ( t_log_level )
    {
    case LOG_INFO:
    case LOG_DEBUG:
        fprintf( stdout, out_fmt[ t_log_level ], l_buf );
        break;

    case LOG_ERROR:
        fprintf( stderr, out_fmt[ t_log_level ], errno, strerror( errno ), l_buf );
        break;
    }
}

//***************************************************************************
// help

void help( int t_narg, char **t_args )
{
    if ( t_narg <= 1 || !strcmp( t_args[ 1 ], "-h" ) )
    {
        printf(
            "\n"
            "  Socket server example.\n"
            "\n"
            "  Use: %s [-h -d] port_number\n"
            "\n"
            "    -d  debug mode \n"
            "    -h  this help\n"
            "\n", t_args[ 0 ] );

        exit( 0 );
    }

    if ( !strcmp( t_args[ 1 ], "-d" ) )
        g_debug = LOG_DEBUG;
}

//***************************************************************************

typedef struct {
    int pid;
    int socket;
} Client;

typedef struct {
    Client clients[10];
    int client_count;
} ServerInfo;

ServerInfo* serverInfo = nullptr;

int message_queue_fd = -1;

void handle_client(int l_sock_client) {
    char buf[256];

    while (1) {
        int l_mq_read = mq_receive(message_queue_fd, buf, sizeof(buf), nullptr);
        if (l_mq_read > 0) { // data from parent process??
            write(l_sock_client, buf, l_mq_read);
        }

        int l_sock_read = read(l_sock_client, buf, sizeof(buf));
        if (l_sock_read <= 0) { // data from client??
            log_msg(LOG_ERROR, "Unable to read data from client.");
            close(l_sock_client);
            break;
        }

        if (strncmp(buf, "quit", strlen("close")) == 0) { // request to quit??
            close(l_sock_client);
            char pid_to_send[256];
            int written = snprintf(pid_to_send, sizeof(pid_to_send), "%d", getpid());

            mq_send(message_queue_fd, pid_to_send, written, 1);
            break;
        }

        mq_send(message_queue_fd, buf, strlen(buf), 0); // send to parent process
    }


}

int main( int t_narg, char **t_args )
{
    if ( t_narg <= 1 ) help( t_narg, t_args );

    int l_port = 0;

    // parsing arguments
    for ( int i = 1; i < t_narg; i++ )
    {
        if ( !strcmp( t_args[ i ], "-d" ) )
            g_debug = LOG_DEBUG;

        if ( !strcmp( t_args[ i ], "-h" ) )
            help( t_narg, t_args );

        if ( *t_args[ i ] != '-' && !l_port )
        {
            l_port = atoi( t_args[ i ] );
            break;
        }
    }

    if ( l_port <= 0 )
    {
        log_msg( LOG_INFO, "Bad or missing port number %d!", l_port );
        help( t_narg, t_args );
    }

    log_msg( LOG_INFO, "Server will listen on port: %d.", l_port );

    // socket creation
    int l_sock_listen = socket( AF_INET, SOCK_STREAM, 0 );
    if ( l_sock_listen == -1 )
    {
        log_msg( LOG_ERROR, "Unable to create socket.");
        exit( 1 );
    }

    in_addr l_addr_any = { INADDR_ANY };
    sockaddr_in l_srv_addr;
    l_srv_addr.sin_family = AF_INET;
    l_srv_addr.sin_port = htons( l_port );
    l_srv_addr.sin_addr = l_addr_any;

    // Enable the port number reusing
    int l_opt = 1;
    if ( setsockopt( l_sock_listen, SOL_SOCKET, SO_REUSEADDR, &l_opt, sizeof( l_opt ) ) < 0 )
      log_msg( LOG_ERROR, "Unable to set socket option!" );

    // assign port number to socket
    if ( bind( l_sock_listen, (const sockaddr * ) &l_srv_addr, sizeof( l_srv_addr ) ) < 0 )
    {
        log_msg( LOG_ERROR, "Bind failed!" );
        close( l_sock_listen );
        exit( 1 );
    }

    // listenig on set port
    if ( listen( l_sock_listen, 1 ) < 0 )
    {
        log_msg( LOG_ERROR, "Unable to listen on given port!" );
        close( l_sock_listen );
        exit( 1 );
    }

    log_msg( LOG_INFO, "Enter 'quit' to quit server." );

    int is_mem_first = 0;

    int server_mem_fd = shm_open("/server_info", O_RDWR, 0660);

    if (server_mem_fd < 0) {
        server_mem_fd = shm_open("/server_info", O_RDWR | O_CREAT, 0660);
        ftruncate(server_mem_fd, sizeof(ServerInfo));
        is_mem_first = 1;
    }

    serverInfo->client_count = 0;
    for (int i = 0; i < 10; i++) {
        serverInfo->clients[i].pid = -1;
        serverInfo->clients[i].socket = -1;
    }

    serverInfo = (ServerInfo*) mmap(nullptr, sizeof(ServerInfo), PROT_READ | PROT_WRITE, MAP_SHARED, server_mem_fd, 0);

    message_queue_fd = mq_open("/queue", O_RDWR);

    if (message_queue_fd < 0) {
        mq_attr attrs;
        bzero(&attrs, sizeof(attrs));

        attrs.mq_flags = 0;
        attrs.mq_maxmsg = 10;
        attrs.mq_msgsize = sizeof(int);
        message_queue_fd = mq_open("/queue", O_RDWR | O_CREAT, 0660, &attrs);
    }

    // go!
    while ( 1 )
    {
        int l_sock_client = -1;

        // list of fd sources
        pollfd l_read_poll[ 3 ];

        l_read_poll[ 0 ].fd = STDIN_FILENO;
        l_read_poll[ 0 ].events = POLLIN;
        l_read_poll[ 1 ].fd = l_sock_listen;
        l_read_poll[ 1 ].events = POLLIN;
        l_read_poll[ 2 ].fd = message_queue_fd;
        l_read_poll[ 2 ].events = POLLIN;

        while ( 1 ) // wait for new client
        {
            // select from fds
            int l_poll = poll( l_read_poll, 2, -1 );

            if ( l_poll < 0 )
            {
                log_msg( LOG_ERROR, "Function poll failed!" );
                exit( 1 );
            }

            if ( l_read_poll[ 0 ].revents & POLLIN )
            { // data on stdin
                char buf[ 128 ];
                int len = read( STDIN_FILENO, buf, sizeof( buf) );
                if ( len < 0 )
                {
                    log_msg( LOG_DEBUG, "Unable to read from stdin!" );
                    exit( 1 );
                }

                log_msg( LOG_DEBUG, "Read %d bytes from stdin" );
                // request to quit?
                if ( !strncmp( buf, STR_QUIT, strlen( STR_QUIT ) ) )
                {
                    log_msg( LOG_INFO, "Request to 'quit' entered.");
                    close( l_sock_listen );
                    exit( 0 );
                }
            }

            if ( l_read_poll[ 1 ].revents & POLLIN )
            { // new client?
                sockaddr_in l_rsa;
                int l_rsa_size = sizeof( l_rsa );
                // new connection
                l_sock_client = accept( l_sock_listen, ( sockaddr * ) &l_rsa, ( socklen_t * ) &l_rsa_size );
                if ( l_sock_client == -1 )
                {
                    log_msg( LOG_ERROR, "Unable to accept new client." );
                    close( l_sock_listen );
                    exit( 1 );
                }

                if (fork() == 0) {
                    for (int i = 0; i < 10; i++) {
                        if (serverInfo->clients[i].pid != -1) {
                            close(serverInfo->clients[i].socket);   
                            break;
                        }
                        
                        serverInfo->clients[i].pid = getpid();
                        serverInfo->clients[i].socket = l_sock_client;
                        serverInfo->client_count++;
                        log_msg(LOG_INFO, "New client connected. Total clients: %d", serverInfo->client_count);
                    }

                    handle_client(l_sock_client);
                    close(l_sock_client);
                    exit(0);
                }

                wait(NULL);

                uint l_lsa = sizeof( l_srv_addr );
                // my IP
                getsockname( l_sock_client, ( sockaddr * ) &l_srv_addr, &l_lsa );
                log_msg( LOG_INFO, "My IP: '%s'  port: %d",
                                 inet_ntoa( l_srv_addr.sin_addr ), ntohs( l_srv_addr.sin_port ) );
                // client IP
                getpeername( l_sock_client, ( sockaddr * ) &l_srv_addr, &l_lsa );
                log_msg( LOG_INFO, "Client IP: '%s'  port: %d",
                                 inet_ntoa( l_srv_addr.sin_addr ), ntohs( l_srv_addr.sin_port ) );

                break;
            }

        } // while wait for client

        // change source from sock_listen to sock_client
        l_read_poll[ 1 ].fd = l_sock_client;

        char received_data[256];

        if (l_read_poll[2].revents & POLLIN) {
            int l = mq_receive(message_queue_fd, received_data, sizeof(received_data), nullptr);

            mq_send(message_queue_fd, received_data, l, 0);
        }
    } // while ( 1 )

    return 0;
}
