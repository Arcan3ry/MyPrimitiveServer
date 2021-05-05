all: myhttp

myhttp: httpd.o http_conn.o
	g++ -W -Wall -o myhttp httpd.o http_conn.o -lpthread

http_conn.o: ./http/http_conn.cpp
	g++ -c -o http_conn.o ./http/http_conn.cpp -g


httpd.o: httpd.cpp
	g++ -c -o httpd.o httpd.cpp -g



clean:
	rm myhttp
	rm *.o
