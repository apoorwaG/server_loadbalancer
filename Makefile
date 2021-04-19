all: loadbalancer.o
	gcc -Wall -Wextra -Wpedantic -Wshadow -o loadbalancer loadbalancer.o -lpthread
loadbalancer.o: loadbalancer.c
	gcc -Wall -Wextra -Wpedantic -Wshadow -c loadbalancer.c -lpthread
clean:
	rm -f loadbalancer.o
spotless:
	rm -f *.o loadbalancer

