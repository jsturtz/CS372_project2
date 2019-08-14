// Program:            ftserve.c
// Programmer:         Jordan Sturtz
// Course Name:        CS372 Introduction to Computer Networks
// Description:        Implements a partial ftp server by providing clients with the
//                     functionality of retrieving files (with -g) and of displaying
//                     the directory of the server (with -l)
// Last Modified:      08 - 13 - 2019

#include <stdio.h> 
#include <stdlib.h>
#include <sys/socket.h> 
#include <arpa/inet.h> 
#include <unistd.h> 
#include <string.h> 
#include <netdb.h>
#include <signal.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>

// these values are global so can be accessed by sigint signal handler 
int server_sock = -1;
int ctrl_connection = -1;
int data_connection = -1;

// ensures socket is closed
void exit_program()
{
    if (server_sock != -1)     close(server_sock);
    if (ctrl_connection != -1) close(ctrl_connection);
    if (data_connection != -1) close(data_connection);
    exit(0);
}

/* SOURCE: Adapted heavily from this website: */
/* https://www.binarytides.com/hostname-to-ip-address-c-sockets-linux/ */ 

/* description:         will attempt to convert name of hostname to ip address and fill ip buffer*/ 
/* preconditions:       ip must be long enough to hold ip address*/
/* postconditions:      ip data will change, will return -1 for invalid conversion or 0 for success*/
int hostname_to_ip(char * hostname , char* ip)
{
    struct hostent *he;
    struct in_addr **addr_list;
    int i;
            
    if ((he = gethostbyname(hostname )) == NULL) 
    {
        return 0;
    }

    addr_list = (struct in_addr **) he->h_addr_list;
    
    for(i = 0; addr_list[i] != NULL; i++) 
    {
        //Return the first one to be found
        strcpy(ip, inet_ntoa(*addr_list[i]));
        return 1;
    }
    
    return 0;
}	

/* 
 * Description:         Verifies that str character array represents valid port number between 1025 and 66535
 * Preconditions:       str must be a character array with only valid digits 0 - 9.
 *                      Whitespace, newlines, or carriage returns will return -1
 * Postconditions:      On success, returns port number. On failure, returns -1
*/
int valid_port(char* str)
{
    if (str == NULL) return 0;

    // validate all chars are numbers
    int i = 0;
    while (str[i] != '\0')
    {
        if (!isdigit(str[i])) 
        {
            return 0;
        }
        i++;
    }

    // convert string to portber
    int port;
    if ((port = atoi(str)) > 0)
    {
        // check for valid port range
        if ((port > 65535) || (port < 1025))
        {
            return 0;
        }
        else return port;
    }
    else return 0;
}

/* 
 * Description:         Will return 1 if file exists (and is readable), 0 if otherwise
 * Preconditions:       none, will return 0 if pointer is NULL
 * Postconditions:      Returns 1 or 0
*/
int file_exists(char* filename)
{
    if (!filename) return 0;

    int fd = open(filename, O_RDONLY);
    if (fd < 0) 
    {
        return 0;
    }
    else
    {
        close(fd);
        return 1;
    }
}

/* 
 * Description:         Will return 1 if command is valid, 0 otherwise
 * Preconditions:       none, will verify whether cmd and filename are not NULL
 * Postconditions:      Returns 1 or 0
*/

int valid_command(char* cmd, char* filename)
{
    // if cmd doesn't exist and it's neither -g nor -l, then invalid
    if (!cmd && (strcmp(cmd, "-g") != 0 || strcmp(cmd, "-l") != 0))
    {
        return 0;
    }

    // if command is -g and yet no filename provided, then invalid
    if (strcmp(cmd, "-g") == 0 && !filename && filename[0] != '\0')
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

/* A valid message has the following format: */ 
/*     [hostname] [port number] [command] [arg1] ... [argn]\0 */

/* In other words, a connecting client is expected to send a hostname and port number for */
/* the data connection, followed by whitespace, followed by the command, */ 
/* followed by any necessary arguments for that command delimited by whitespace */

/* These messages must in total be less than 500 characters */

int valid_message(char* buffer, char* error)
{
    char* hostname;
    char* port;
    char* cmd = NULL;
    char* filename = NULL;
    char copyBuffer[strlen(buffer)];
    char ip[100];

    // save characters in buffer for later
    strcpy(copyBuffer, buffer);

    // arg1 should be hostname
    hostname = strtok(buffer, " ");
    if (!hostname_to_ip(hostname, ip))
    {
        strcpy(error, "Invalid hostname\0");
        return 0;
    }

    // arg2 should be port
    port = strtok(NULL, " ");

    // parse port, check validity
    if (!valid_port(port)) 
    {
        strcpy(error, "ERROR: Invalid port number. Port number must be between 1025 and 65535\0");
        return 0;
    }
    
    // arg3 should be command, arg4 if exists should be filename
    cmd = strtok(NULL, " ");
    filename = strtok(NULL, " ");

    if (!valid_command(cmd, filename))
    {
        strcpy(error, "Invalid command\0");
        return 0;
    }

    // restore character data
    strcpy(buffer, copyBuffer);
    return 1;
}

/* 
 * Description:         Will parse valid message in buffer and fill respective values of passed arguments
 *                      The valid message format can be found in the description for valid_message
 * Preconditions:       Buffer must contain valid message (validate with valid_message)
 *                      Also, values in buffer must not exceed length of arrays
 * Postconditions:      Fills arrays of arguments with their respectively parsed values
*/
void parse_message(char* buffer, char* hostname, int* port, char* cmd, char* filename) 
{
    strcpy(hostname, strtok(buffer, " "));  // arg1 = hostname
    *port = atoi(strtok(NULL, " "));        // arg2 = integer
    strcpy(cmd, strtok(NULL, " "));         // arg3 = command
    
    if (strcmp(cmd, "-g") == 0)             // arg4 = [filename]
    {
        strcpy(filename, strtok(NULL, " "));
    }
}

/* 
 * Description:         Used to setup server socket. Will create socket and listen on provided port
 * Preconditions:       addr must be valid, port number must be valid
 * Postconditions:      Returns sock file descriptor on success, 0 otherwise
*/

int setup_socket(struct sockaddr_in *addr, int port)
{
    int sock;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
    { 
        printf("Socket creation error\n");
        return 0; 
    }

    // set up addr struct 
    addr->sin_family = AF_INET;         // can communicate with IPv4 addresses
    addr->sin_port = htons(port);       // convert to network byte order
    addr->sin_addr.s_addr = INADDR_ANY;  // listen on any interface
       
    // bind socket to port
    if (bind(sock, (struct sockaddr*) addr, sizeof(*addr)) < 0)
    {
        printf("Bind error\n");
        return 0;
    }
    
    // listen on port number
    if (listen(sock, 5) < 0)
    {
        printf("Listening error\n");
        return 0;
    }

    return sock;
}

/* 
 * Description:         Used to connect to client socket at hostname:port
 * Preconditions:       hostname and port must have valid listening socket or else will return false
 * Postconditions:      Returns 1 for success, 0 otherwise
*/
int connect_to_sock(char* hostname, int port)
{
    int sock;
    struct sockaddr_in client_addr;
    char ip[100];

    if (!hostname_to_ip(hostname, ip))
    {
        return 0;
    }
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        return 0;
    }
    
    // set up struct for socket
    client_addr.sin_family = AF_INET; 
    client_addr.sin_port = htons(port); 
                       
    // Convert IPv4 address from text to binary form 
    if(inet_pton(AF_INET, ip, &client_addr.sin_addr)<=0)  
    { 
        return 0;
    } 
    
    // try to connect to socket
    if (connect(sock, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) 
    { 
        return 0; 
    } 
    
    // if success, return socket descriptor
    return sock;
}

/* 
 * Description:         Ensures all data in buffer is sent to socket
 * Preconditions:       Socket must still be connected, size must be size of buffer
 * Postconditions:      Returns 1 for success, 0 otherwise
*/

int sendall(int sock, char* buffer, int size)
{
    int total_sent = 0;
    int chunk_sent = 0;
    while (total_sent < size)
    {
        chunk_sent = write(sock, buffer + total_sent, size);
        total_sent += chunk_sent;
    }
    return 1;
}

/* 
 * Description:         Returns a long value representing size of file in bytes
 * Preconditions:       filename must not be NULL, filename must be read-accessible
 * Postconditions:      Returns length of file in bytes
*/
long get_file_len(char* filename)
{
    FILE* fd = fopen(filename, "r"); 
    if (fd == NULL) return -1;
    fseek(fd, 0, SEEK_END); 
    long size = ftell(fd);             
    rewind(fd);
    fclose(fd);
    return size;
}


/* 
 * Description:         Performs the task associated with -g command, 
 *                      i.e. reading and sending the file requested by client
 * Preconditions:       filename must not be NULL, filename must be read-accessible
 * Postconditions:      Returns 0 on failure, 1 on success. Will send contents of filename to sock
 *                      prefixed with the number of bytes in the message + one whitespace character
 *                      If file cannot be found, will send an error message prefixed with -1 in
 *                      addition to this message, to indicate a failing message
*/
int send_file(int sock, char* filename)
{
    FILE *sock_ptr = fdopen(sock, "w");

    if (file_exists(filename))
    {
        
        FILE *file_ptr = fopen(filename, "r");

        // for reading all bytes in file
        int bytes_read          = 0;
        int total_bytes_read    = 0;
        
        // for sending all bytes in file
        int total_sent          = 0; 
        int chunk_sent          = 0;
        
        // for reading and sending in chunks
        int  buff_size = 16384;
        char buffer[buff_size];
        memset(buffer, '\0', buff_size);
        
        // get length of file in bytes
        long filelen = get_file_len(filename);
        char filelen_string[256];
        sprintf(filelen_string, "%ld ", filelen);
        
        // send size first
        write(sock, filelen_string, strlen(filelen_string));
        
        // enter loop to ensure all bytes from file are sent
        while(total_bytes_read < filelen)
        {
            bytes_read = fread(buffer, 1, buff_size-1, file_ptr);
            total_bytes_read += bytes_read;
            sendall(sock, buffer, bytes_read);        
            memset(buffer, '\0', buff_size);
        }
        fclose(file_ptr);
        fclose(sock_ptr);
        return 1;
    }
    else
    {
        // prefix the -1 to indicate failure to host
        char *error = "Error: File not found";
        fprintf(sock_ptr, "-1 %d %s", strlen(error), error);
        fflush(sock_ptr);
        fclose(sock_ptr);
        return 0;
    }
}

// description:         performs the task associated with the -l command
// preconditions:       sock must be a valid connected socket
// postconditions:      as with all messages for my protocol, the message must be prefixed
//                      with a number representing the bytes in the message + a whitespace character
int send_list(int sock)
{
    
    // use direct structs
    struct dirent *dir;
    DIR *dr = opendir("."); 
    if (dr == NULL) return 0; 
    
    // read all directories into same char buffer
    char buffer[10000];
    char prefix[100];
    memset(buffer, '\0', 10000);
    memset(prefix, '\0', 100);
    
    int bytes_read = 0;
    while ((dir = readdir(dr)) != NULL) 
    {
        sprintf(buffer+bytes_read, "%s\n", dir->d_name); 
        bytes_read += strlen(dir->d_name) + 1;
    }

    // prefix message with length of message, send
    sprintf(prefix, "%d ", bytes_read);
    write(sock, prefix, strlen(prefix));

    // send message to client
    sendall(sock, buffer, strlen(buffer));
    closedir(dr);     
    return 1; 
}

/* description:         takes all relevant arguments parsed from message and calls appropriate function */
/* preconditions:       arguments must be valid */
/* postconditions:      calls other functions */
int do_cmd(char*hostname, int port, char* cmd, char* filename)
{
    // check that connection succeeds
    int sock;
    if (!(sock = connect_to_sock(hostname, port))) return 0;
    
    // decide which function to call
    if (strcmp(cmd, "-g") == 0)
    {
        if (send_file(sock, filename))
        {
            printf("Sending file \"%s\" to %s on port %d\n", filename, hostname, port);
        }
        else
        {
            printf("ERROR: File \"%s\" request by %s on port %d does not exist\n", filename, hostname, port);
        }
    }
    else if (strcmp(cmd, "-l") == 0)    
    {
        if (send_list(sock))
        {
            printf("Sending directory to %s on port %d\n", hostname, port);
        }
        else
        {
            printf("ERROR: Failed to send directory to %s on port %d\n", hostname, port);
        }
    }
    else return 0;
    close(sock);
    return 1;
}

int main(int argc, char const *argv[]) 
{ 
    // to handle user sigint
    signal(SIGINT, exit_program);
    
    // variables needed
    char client_host[1000] = {'\0'};     // holds name of host address
    char cmd[3]            = {'\0'};     // holds either "-l" or "-g"
    char filename[1000]    = {'\0'};     // holds filename for -g command
    char buffer[1000]      = {'\0'};     // buffer for input
    char error_msg[1000]   = {'\0'};     // buffer for error message
    int server_port, client_port;        // port numbers for connection
    int ctrl_connection;                 // ctrl socket file descriptor
    struct sockaddr_in addr;             // struct to hold server info
    int addr_len = sizeof(addr);

    // validate number of args
    if (argc != 2)
    {
        printf("ftserver takes only one command line argument\n");
        printf("USAGE: ftserver <SERVER_PORT>\n");
        return -1;
    }

    // validate port number from command line
    if ((server_port = valid_port(strtok((char*)argv[1], " \n\r"))) == -1)
    {
        printf("ERROR: Invalid port number (1025 - 65535)\n");
        return -1;
    }
    
    // create socket, bind to port, listen
    if (!(server_sock = setup_socket(&addr, server_port)))
    {
        return -1;
    }
    
    while (1)
    {
        // reset memory of character arrays
        memset(client_host, '\0', 1000);
        memset(cmd, '\0', 3);
        memset(filename, '\0', 1000);
        memset(buffer, '\0', 1000);
        memset(error_msg, '\0', 1000); 

        // accept new connection
        printf("Waiting to accept connection...\n");
        ctrl_connection = accept (
            server_sock, 
            (struct sockaddr *) &addr, 
            (socklen_t*) &addr_len
        );
        
        // read from client
        if ((read(ctrl_connection, buffer, 500)) <= 0) break;

        if (valid_message(buffer, error_msg))
        {
            // send OK message to control connection
            write(ctrl_connection, "OK", 2);

            // fill variables with information from buffer
            parse_message(buffer, client_host, &client_port, cmd, filename);  
            do_cmd(client_host, client_port, cmd, filename);

        }
        else
        {
            printf("ERROR: %s\n", error_msg);
            write(ctrl_connection, error_msg, 100);
        }
    } 
    return 0; 
} 
