# Program:            ftclient.py
# Programmer:         Jordan Sturtz
# Course Name:        CS372 Introduction to Computer Networks
# Description:        My client to implement the ftp protocol. The -l command will display the directory of
#                     the server. The -g command will get the file specified in the command line argument
#                     Usage: python ftclient.py [hostname] [hostport] [command] [argument (optional] [dataport]
# Last Modified:      07 - 24 - 2019

import socket
import sys
import signal
import threading
import ctypes
import time
import os
    

# description:        creates socket, binds to provided port number, and then
#                     listens on that port
# pre-conditions:     port is an int representing a valid port number (1 - 66535)
# post-conditions:    returns socket object

def setupSocket(port):

    # create socket
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    
    # bind to server port
    s.bind(('', port))

    # listen on port number
    s.listen(5)
    return s

# description:        Will return the number prefixed in the socket buffer. 
#                     In other words, will scan buffer until the first whitespace character
#                     and return a string of all characters before it
# pre-conditions:     connection represents connected socket
# post-conditions:    returns string representation of integer
def getPrefixedInt(connection):
    length_string = ""
    while(True):
        next_char = connection.recv(1).decode()
        if next_char != " ":
            length_string += next_char
        else:
            break
    return length_string

# description:        Receives message according to my communication protocol
#                     Will first extract prefix for number of bytes sent and
#                     use bytes to read all data sent in message. Will also
#                     detect if first prefix is -1, which indicates failure
# pre-conditions:     connection must be connected socket
# post-conditions:    Returns any sent message and status value as tuple. Status
#                     will either be -1 or the number of bytes in message

def recvMessage(connection):

    # first, extract length prefixed message stream
    length_string = getPrefixedInt(connection)
    status = int(length_string)

    # if -1, then this is error message and need to extract next prefixed int for length
    if status == -1:
        length_string = getPrefixedInt(connection)

    length = int(length_string)

    # then, read from socket until full length read
    data = []
    while length > 0:
        chunk = connection.recv(length).decode()
        if not chunk: break
        data.append(chunk)
        length -= len(chunk)

    return ("".join(data), status)

# description:        validates args provided by command line
# pre-conditions:     args must be 4 or 5 in length
# post-conditions:    returns true if and only if args are valid
def valid_args(args):
    
    # check correct number of args
    if len(args) < 4 or len(args) > 5:
        print("ERROR: Incorrect number of arguments")
        return False

    # check second and last args can be cast to integer
    try:
        ports = [int(args[1]), int(args[-1])]
    except:
        print("ERROR: Invalid value for port")
        return False
    
    # check ranges of port numbers
    for p in ports:
        if p < 1025 or p > 65535:
            print("ERROR: Invalid range for port number (1025 - 65535)")
            return False
    
    # check commands are valid
    if args[2] != "-l" and args[2] != "-g":
        print("ERROR: Invalid command")
        return False
    
    # check filename arg exists if command is -g
    if args[2] == "-g" and len(args) != 5:
        print("ERROR: -g usage: [hostname] [hostport] -g [filename] [clientport]")
        return False

    # check length of -l is correct
    if args[2] == "-l" and len(args) != 4:
        print("ERROR: -l usage: [hostname] [hostport] -l [clientport]")
        return False

    return True

def main(args):
    
    # validate args
    if not valid_args(args):
        print("Error: Invalid arguments")
        return
    
    # for ease of use, unpack args
    if len(args) == 4:
        (server_name, server_port, cmd, client_port) = args
        filename = ""
    else:
        (server_name, server_port, cmd, filename, client_port) = args
    
    # check that file doesn't already exist
    if filename and os.path.exists(filename):
        print("Error: File \"" + filename + "\" already exists")
        return

    # create socket
    try:
        ctrl_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    except:
        print("ERROR: Failed to create socket")
        return
    
    # get ip from hostname
    try: 
        ip = socket.gethostbyname(server_name)
    except:
        print("ERROR: Hostname lookup failed")
        return
    
    # # create data connection
    data_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    data_sock.bind(("", 0))
    data_sock.listen(1)

    # connect to server
    try:
        ctrl_sock.connect((ip, int(server_port)))
    except:
        print("ERROR: Failed to connect to ip at " + str(ip) + ":" + server_port)
        return

    # send request
    ctrl_sock.send(" ".join([socket.gethostname(), str(data_sock.getsockname()[1]), cmd, filename]).encode())

    # get response back from server before opening up data socket
    response = ctrl_sock.recv(1024).decode()
    if response == "OK":
        
        try:
            # waiting for data into data socket
            (connection, addr) = data_sock.accept()
            if cmd == "-g":
                (message, status) = recvMessage(connection)
                if status == -1:
                    print(message)
                else:
                    with open(filename, "w+") as output:
                        output.write(message)
                    print("Transfer Complete");

            elif cmd == "-l":

                (message, status) = recvMessage(connection)
                if status != -1:
                    print("Retrieving server directory...");
                    print(message)
                    print("Transfer Complete");
                else:
                    print(message)

        except:
            print(response)
    else:
        print(response)

    ctrl_sock.close()
    data_sock.close()
    return

if __name__ == '__main__':
    main(sys.argv[1:])
