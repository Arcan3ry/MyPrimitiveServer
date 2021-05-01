all: myhttp

myhttp: httpd.o http_conn.o
	g++ -W -Wall -o myhttp httpd.o http_conn.o -lpthread -g

http_conn.o: ./http/http_conn.cpp
	g++ -c -o http_conn.o ./http/http_conn.cpp

httpd.o: httpd.o
	g++ -c -o httpd.o httpd.cpp

clean:
	rm myhttp
	rm *.o
