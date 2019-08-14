Compilation and Execution

To run ftclient.py:
    $ python3 ftclient.py [hostname] [hostport] [command] [filename (optional)] [dataport]

    Examples:
    $ python3 ftclient.py flip1.engr.oregonstate.edu 3191 -g somefile.txt 3291
    $ python3 ftclient.py flip1.engr.oregonstate.edu 3191 -l 3291
    

To compile ftserver.c:
    $ gcc ftserver.c

To execute chatclient.c:
    $ ./a.out [port number]

    Example:
    $ ./a.out 3191
