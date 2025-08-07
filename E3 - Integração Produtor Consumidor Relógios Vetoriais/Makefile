
FILE = rvet_pth

all: clean compile run

compile:
	mpicc -o $(FILE) $(FILE).c -lpthread

clean:
	rm -f $(FILE)

run:
	mpiexec -n 3 ./$(FILE)
