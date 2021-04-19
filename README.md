Loadbalancer: balances load between the provided servers.

Usage: ./loadbalancer -R r -N n clientport httpserverport1 httpserverport2 ...

-R and -N are optional arguments, which define how often to perform a healthcheck 
and how many parallel connections the program will support respectively.

clientport -> the port number that the clients will use to connect to httpserver

httpserverport -> the port number that the loadalancer will use to connect to the httpserver


Example of usage:
	./ loadbalancer 1234 8080 8081
	./ loadbalancer 1234 8080 8081 -R 5 -N 6


