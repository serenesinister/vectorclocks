
FILE = pth_pool

all: clean compile run

compile:
	gcc -g -Wall -o $(FILE) $(FILE).c -lpthread -lrt

clean:
	rm -f $(FILE)

run:
	./$(FILE)
